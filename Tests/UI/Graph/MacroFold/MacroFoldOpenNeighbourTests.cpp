// MacroFoldOpenNeighbourTests.cpp
//
// Every frame of a macro fold, unfold and undo on a real patch with an open neighbour pushed aside and cables crossing
// the border (the fro830 test project: Macro opens into Oscillator 2's open border and pushes it, Master and Audio
// Output aside; Macro2 sits below). Earlier guards read the first, a middle and the last frame with one macro; this
// steps the fold, the card glide and the canvas through the whole motion and checks what is painted:
//  - every card on screen has one owner per frame (its real card, or the glide's picture of it), never both, never
//    neither, and a card the glide draws is as bright as the card at rest: the glide's picture was drawn at the alpha
//    the fold's last box or cable left on the Graphics, so pushed cards faded to a shadow and snapped back;
//  - every cable drawn before and after the change is drawn in every frame, by the canvas or by the fold, never both.
// Full motion steps the frames; Reduce Motion and Off check the frames they have (Reduce Motion has no fold pictures
// and no glide, so the brightness check there only confirms nothing is hidden).
// docs/layout/macro-fold-motion.md "One owner per drawn thing per frame".

#include "../../../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AppUndoManager.h"
#include "TestPaths.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

using CableId = graph_editor_types::CableId;
enum class Motion { full, reduced, off };
enum class Change { unfoldMacro, unfoldMacro2, undoUnfold };

const char* motionName(Motion m) { return m == Motion::full ? "Full" : m == Motion::reduced ? "Reduce Motion" : "Off"; }

const char* changeName(Change c) {
    return c == Change::unfoldMacro ? "open Macro" : c == Change::unfoldMacro2 ? "open Macro2" : "undo opening Macro";
}

class Fro830Canvas : public ChannelFlowTest {
protected:
    void SetUp() override {
        ChannelFlowTest::SetUp();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProviderCFT>());
        const auto src = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("fro830-test.agsproj");
        copy_ = juce::File::createTempFile(".agsproj");
        ASSERT_TRUE(src.copyDirectoryTo(copy_));
        mc_->setSize(1600, 900);
        mc_->getAudioEngine().suspendDeviceCallback();
        ASSERT_TRUE(mc_->openProjectForTest(copy_));
        for (const auto& m : editor().getMacros().getAll()) {
            if (m.name == "Macro" && m.bounds.getX() > 3000)
                macro_ = m.id; // the one beside Oscillator 2 (another macro is also named Macro)
            if (m.name == "Macro2")
                macro2_ = m.id;
        }
        ASSERT_TRUE(macro_.isNotEmpty() && macro2_.isNotEmpty());
    }
    void TearDown() override {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
        mc_.reset();
        copy_.deleteRecursively();
        ChannelFlowTest::TearDown();
    }

    GraphEditor& editor() { return mc_->getGraphEditor(); }
    CardGlideAnimator& glide() { return editor().getCardGlideForTest(); }
    MacroFoldAnimator& fold() { return glide().fold(); }
    void land() {
        editor().finishCardGlideForTest();
        fold().land();
        editor().finishHullGlideForTest();
    }

    void setMotion(Motion m) {
        synth::ui::setAnimationMode(m == Motion::off ? synth::ui::AnimationMode::off
                                                     : synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(m == Motion::reduced);
        glide().setForceAnimateForTest(true);
        fold().setForceAnimateForTest(true);
    }

    void apply(Change change) {
        auto& macros = editor().getMacroController();
        if (change == Change::unfoldMacro)
            macros.setMacroCollapsed(macro_, false);
        else if (change == Change::unfoldMacro2)
            macros.setMacroCollapsed(macro2_, false);
        else
            ASSERT_TRUE(mc_->getUndoManager().undo());
    }

    // Every card on the canvas with its node id (macro cards by their card).
    std::vector<std::pair<juce::Component*, juce::String>> cards() {
        std::vector<std::pair<juce::Component*, juce::String>> out;
        for (auto* c : editor().getModuleComponents())
            if (c != nullptr)
                out.emplace_back(c, c->getComponentID());
        for (const auto& m : editor().getMacros().getAll())
            if (auto* card = editor().getMacroController().getMacroCardForTest(m.id))
                out.emplace_back(card, "macro " + m.name);
        return out;
    }

    // Mean brightness of a canvas-space rect in a picture of the editor.
    float brightness(const juce::Image& shot, juce::Component& card, juce::Rectangle<int> canvasRect) {
        // Only a card wholly in view: a card cut by the window edge shows a different part of itself as it moves.
        const auto r = editor().getLocalArea(card.getParentComponent(), canvasRect);
        if (r.isEmpty() || !shot.getBounds().contains(r))
            return -1.0f;
        double sum = 0.0;
        for (int y = r.getY(); y < r.getBottom(); y += 2)
            for (int x = r.getX(); x < r.getRight(); x += 2)
                sum += shot.getPixelAt(x, y).getBrightness();
        return static_cast<float>(sum / (((r.getWidth() + 1) / 2) * ((r.getHeight() + 1) / 2)));
    }
    juce::Image shoot() { return editor().createComponentSnapshot(editor().getLocalBounds(), true, 1.0f); }

    std::vector<CableId> canvasCables() {
        std::vector<CableId> out;
        for (const auto& c : editor().buildVisibleCables())
            out.push_back(c.id);
        return out;
    }

    // Runs `change` (after `setup`) under `motion` and checks every frame. Returns how many frames had the fold and a
    // gliding card drawn together (the case that faded).
    int checkEveryFrame(Motion motion, Change change) {
        setMotion(Motion::off);
        if (change == Change::undoUnfold) {
            apply(Change::unfoldMacro);
            land();
        }
        // At rest after the change: where and how bright every card is, and which cables the canvas draws.
        std::map<juce::Component*, float> restBrightness;
        std::vector<CableId> before = canvasCables(), after;
        {
            // Measure the end state on the same canvas, then undo back to the start.
            apply(change);
            land();
            const auto shot = shoot();
            for (auto& [card, name] : cards())
                if (card->isVisible())
                    restBrightness[card] = brightness(shot, *card, card->getBounds());
            after = canvasCables();
            if (change == Change::undoUnfold)
                EXPECT_TRUE(mc_->getUndoManager().redo());
            else
                EXPECT_TRUE(mc_->getUndoManager().undo());
            land();
        }
        std::vector<CableId> kept;
        for (const auto& id : before)
            if (std::find(after.begin(), after.end(), id) != after.end())
                kept.push_back(id);

        setMotion(motion);
        apply(change);
        const double foldMs = fold().isLive() ? fold().totalMs() : 0.0;
        const double endMs = std::max(foldMs, 200.0);
        int together = 0;
        for (double ms = 0.0; ms <= endMs + 0.5; ms += 20.0) {
            const auto where =
                juce::String(changeName(change)) + ", " + motionName(motion) + " at " + juce::String(ms) + " ms";
            if (glide().isLive())
                editor().advanceCardGlideForTest(static_cast<float>(std::min(1.0, ms / 160.0)));
            if (fold().isLive())
                fold().applyAtMs(std::min(ms, foldMs));
            const auto shot = shoot();
            const bool foldDraws = fold().isLive();
            const auto& canvasComp = *editor().getModuleComponents().getFirst()->getParentComponent();
            const auto view = canvasComp.getLocalArea(&editor(), editor().getLocalBounds());

            for (auto& [card, name] : cards()) {
                if (!card->isVisible())
                    continue; // held back by the fold, which draws it
                const auto glideRect = glide().currentRectFor(card);
                if (motion != Motion::full && !glideRect.isEmpty())
                    ADD_FAILURE() << name << " glides, but nothing travels under " << motionName(motion) << ", "
                                  << where;
                const bool onScreen = card->getBounds().getUnion(glideRect).intersects(view);
                const bool real = card->getAlpha() > 0.0f;
                const bool pictured = !glideRect.isEmpty() && !real;
                if (onScreen && real && !glideRect.isEmpty())
                    ADD_FAILURE() << name << " is drawn twice (real card and glide picture), " << where;
                if (onScreen && !real && !pictured && !fold().isLive())
                    ADD_FAILURE() << name << " is hidden with nothing drawing it, " << where;
                if (!pictured || restBrightness.count(card) == 0 || restBrightness[card] <= 0.0f)
                    continue;
                const float now = brightness(shot, *card, glideRect);
                if (now >= 0.0f && foldDraws)
                    ++together;
                if (now >= 0.0f && now < restBrightness[card] * 0.85f)
                    ADD_FAILURE() << name << " is drawn faded by the glide (" << now << " against "
                                  << restBrightness[card] << " at rest), " << where;
            }

            const auto canvas = canvasCables();
            std::vector<CableId> drawnByFold;
            for (const auto& d : fold().drawnCables())
                drawnByFold.push_back(d.cable.id);
            for (const auto& id : kept) {
                const bool byCanvas = std::find(canvas.begin(), canvas.end(), id) != canvas.end();
                const bool byFold = std::find(drawnByFold.begin(), drawnByFold.end(), id) != drawnByFold.end();
                if (byCanvas && byFold)
                    ADD_FAILURE() << "cable " << id.srcUid << "->" << id.dstUid << " is drawn twice, " << where;
                if (!byCanvas && !byFold)
                    ADD_FAILURE() << "cable " << id.srcUid << "->" << id.dstUid << " is missing, " << where;
            }
            if (!glide().isLive() && !fold().isLive() && ms > 0.0)
                break;
        }
        land();
        return together;
    }

    std::unique_ptr<MainComponent> mc_;
    juce::File copy_;
    juce::String macro_, macro2_;
};

class CardGlideMacroPushOpenNeighbour : public Fro830Canvas {};
class MacroFoldCableSlideOpenNeighbour : public Fro830Canvas {};

} // namespace

TEST_F(CardGlideMacroPushOpenNeighbour, OpeningMacroBesideOscillator2DrawsEveryPushedCardWholeInEveryFrame) {
    EXPECT_GT(checkEveryFrame(Motion::full, Change::unfoldMacro), 0)
        << "sanity: some frames draw the fold and a pushed card's glide together";
}

TEST_F(CardGlideMacroPushOpenNeighbour, UndoingTheOpenDrawsEveryReturningCardWholeInEveryFrame) {
    EXPECT_GT(checkEveryFrame(Motion::full, Change::undoUnfold), 0)
        << "sanity: some frames draw the fold and a returning card's glide together";
}

TEST_F(CardGlideMacroPushOpenNeighbour, OpeningMacro2DrawsEveryCardWholeInEveryFrame) {
    checkEveryFrame(Motion::full, Change::unfoldMacro2);
}

TEST_F(CardGlideMacroPushOpenNeighbour, ReduceMotionAndOffDrawEveryCardWholeInEveryFrame) {
    for (const auto motion : {Motion::reduced, Motion::off})
        for (const auto change : {Change::unfoldMacro, Change::unfoldMacro2, Change::undoUnfold}) {
            SCOPED_TRACE(motionName(motion));
            checkEveryFrame(motion, change);
            land();
        }
}

TEST_F(MacroFoldCableSlideOpenNeighbour, EveryKeptCableIsDrawnOnceInEveryFrameOfOpenFoldAndUndo) {
    // The same frame walk; its cable half is the check here (each kept cable by the canvas or the fold, not both).
    for (const auto change : {Change::unfoldMacro, Change::unfoldMacro2, Change::undoUnfold}) {
        checkEveryFrame(Motion::full, change);
        land();
    }
}
