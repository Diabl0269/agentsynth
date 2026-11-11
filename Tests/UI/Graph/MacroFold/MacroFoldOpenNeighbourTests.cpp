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
#include <iostream>
#include <map>

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

    // What draws a module card this frame and where: the fold's ghost, the glide's picture or the real card. More
    // than one drawer is a doubled card; none, while the card is meant to show, a missing one.
    struct Drawn {
        int owners = 0;
        int owner = 0; // 1 the fold, 2 the glide, 3 the real card
        juce::Rectangle<float> rect;
        float alpha = 1.0f;
        bool onScreen = false;
        float restWidth = 0.0f;
    };
    Drawn drawnModule(juce::Component& card, uint32_t uid) {
        Drawn d;
        const auto foldRect = fold().moduleRectFor(uid);
        const bool real = card.isVisible() && card.getAlpha() > 0.0f;
        if (!fold().isReduced() && !foldRect.isEmpty() && (fold().isHeld(uid) || !card.isVisible())) {
            ++d.owners;
            d.owner = 1;
            d.rect = foldRect;
        }
        // The glide draws a picture only of a card it hid (alpha 0); one whose path stays off screen is drawn real.
        if (const auto g = glide().currentRectFor(&card); !g.isEmpty() && card.isVisible() && !real) {
            ++d.owners;
            d.owner = 2;
            d.rect = g.toFloat();
        }
        if (real) {
            ++d.owners;
            d.owner = 3;
            d.rect = card.getBounds().toFloat();
            d.alpha = card.getAlpha();
        }
        return d;
    }
    std::map<juce::String, Drawn> drawnAll() {
        std::map<juce::String, Drawn> out;
        const auto& canvasComp = *editor().getModuleComponents().getFirst()->getParentComponent();
        const auto view = canvasComp.getLocalArea(&editor(), editor().getLocalBounds()).toFloat();
        for (auto* c : editor().getModuleComponents())
            if (c != nullptr) {
                auto d = drawnModule(*c, c->getNodeId().uid);
                d.onScreen = d.rect.intersects(view);
                d.restWidth = static_cast<float>(c->getWidth());
                out[c->getComponentID()] = d;
            }
        for (const auto& m : editor().getMacros().getAll())
            if (auto* card = editor().getMacroController().getMacroCardForTest(m.id)) {
                // Every drawer of a macro card counts: the real card, the glide's picture of it (drawn whether or not
                // the card still shows), the border an opening fold grows out of it, or its box in a parent's fold.
                Drawn d;
                d.onScreen = true;
                const auto add = [&d](int owner, juce::Rectangle<float> r) {
                    ++d.owners;
                    d.owner = owner;
                    d.rect = r;
                };
                const bool real = card->isVisible() && card->getAlpha() > 0.0f;
                if (const auto g = glide().currentRectFor(card); !g.isEmpty() && !real)
                    add(2, g.toFloat());
                if (const auto o = fold().expandOutlineFor(m.id); o && !o->isEmpty())
                    add(1, o->toFloat());
                else if (const auto box = fold().nestedRectFor(m.id); !box.isEmpty())
                    add(1, box);
                if (real) {
                    const auto g = glide().currentRectFor(card);
                    add(3, (g.isEmpty() ? card->getBounds() : g).toFloat());
                    d.alpha = card->getAlpha();
                }
                out["macro " + m.name + " " + m.id] = d;
            }
        return out;
    }

    // Opens Macro then Macro2 (settled), then presses Cmd+Z twice, the second `gapMs` after the first, or (redo) undoes
    // both, settles, and presses Cmd+Shift+Z twice. Every 20 ms frame: no card drawn twice or missing; at the second
    // press nothing moves, fades or vanishes (the running fold and glide go on from where they are drawn); between
    // frames no card jumps further than `maxStepPx`. Returns the largest step seen.
    float checkTwoStepsMidMotion(Motion motion, bool redo, double gapMs = 40.0, float maxStepPx = 1.0e9f) {
        setMotion(Motion::off);
        apply(Change::unfoldMacro);
        land();
        apply(Change::unfoldMacro2);
        land();
        auto& undo = mc_->getUndoManager();
        if (redo) {
            EXPECT_TRUE(undo.undo());
            EXPECT_TRUE(undo.undo());
            land();
        }
        const auto step = [&] { EXPECT_TRUE(redo ? undo.redo() : undo.undo()); };
        setMotion(motion);
        const juce::String what = juce::String(redo ? "redo twice" : "undo twice") + ", " + motionName(motion);

        double glideArmedAt = 0.0;
        int glideArms = glide().armCount();
        double lastFoldMs = 0.0;
        const auto advance = [&](double ms, double sinceLast) {
            if (glide().armCount() != glideArms) {
                glideArms = glide().armCount();
                glideArmedAt = ms;
            }
            if (glide().isLive() && ms - glideArmedAt >= 160.0)
                glide().finish(); // what the driver's last frame does
            else if (glide().isLive())
                editor().advanceCardGlideForTest(static_cast<float>((ms - glideArmedAt) / 160.0));
            if (fold().isLive()) {
                lastFoldMs = fold().elapsedMs() + sinceLast;
                if (lastFoldMs >= fold().totalMs())
                    fold().land();
                else
                    fold().applyAtMs(lastFoldMs);
            }
        };
        const auto checkOwners = [&](const std::map<juce::String, Drawn>& now, const juce::String& where) {
            for (const auto& [name, d] : now)
                if (d.owners > 1)
                    ADD_FAILURE() << name << " is drawn " << d.owners << " times, " << where;
            if (fold().isReduced() && fold().isLive())
                return; // Reduce Motion fades cards in from alpha 0
            for (auto* c : editor().getModuleComponents())
                if (c != nullptr && c->isVisible() && c->getAlpha() <= 0.0f && glide().currentRectFor(c).isEmpty() &&
                    fold().moduleRectFor(c->getNodeId().uid).isEmpty())
                    ADD_FAILURE() << c->getComponentID() << " is hidden with nothing drawing it, " << where;
        };

        step();
        advance(0.0, 0.0);
        bool second = false;
        if (gapMs <= 0.0) { // the second press before any frame is drawn
            second = true;
            step();
            advance(0.0, 0.0);
        }
        auto prev = drawnAll();
        float largest = 0.0f;
        std::map<juce::String, float> lastStep;
        for (double ms = 20.0; ms <= 1500.0; ms += 20.0) {
            const auto where = what + " at " + juce::String(ms) + " ms";
            advance(ms, 20.0);
            auto now = drawnAll();
            checkOwners(now, where);
            for (const auto& [name, d] : now) {
                const auto it = prev.find(name);
                if (it == prev.end() || d.owners == 0 || it->second.owners == 0 || !(d.onScreen || it->second.onScreen))
                    continue; // off screen nothing shows, so nothing can pop
                const auto a = it->second.rect, b = d.rect;
                const float jump =
                    std::max({std::abs(a.getX() - b.getX()), std::abs(a.getY() - b.getY()),
                              std::abs(a.getRight() - b.getRight()), std::abs(a.getBottom() - b.getBottom())});
                // A handover (a glide's picture to the landed card) may move as far as the tween's last step did.
                if (d.owner != it->second.owner && jump > lastStep[name] + 3.0f)
                    ADD_FAILURE() << name << " jumps " << jump << " px as its drawer changes (" << it->second.owner
                                  << " to " << d.owner << "), " << where;
                lastStep[name] = jump;
                largest = std::max(largest, jump);
                if (jump > maxStepPx)
                    ADD_FAILURE() << name << " jumps " << jump << " px in one frame, " << where;
            }
            prev = std::move(now);
            if (!second && ms + 0.5 >= gapMs) {
                second = true;
                const auto before = drawnAll();
                step();
                advance(ms, 0.0);
                const auto after = drawnAll();
                checkOwners(after, where + " (second press)");
                for (const auto& [name, b] : before) {
                    const auto it = after.find(name);
                    const auto a = it == after.end() ? Drawn{} : it->second;
                    // A closing macro's port widget goes at once, after one step as after two (the fold draws no
                    // ports); any other card must never vanish.
                    const bool port = name.startsWith("card:Macro In") || name.startsWith("card:Macro Out");
                    // Under Reduce Motion and Off nothing travels: each step puts the cards in place at once (and
                    // fades what comes and goes), so only a fade that falls back counts there.
                    const bool travels = motion == Motion::full;
                    if (travels && b.owners > 0 && a.owners == 0 && !port)
                        ADD_FAILURE() << name << " vanishes at the second press, " << where;
                    else if (a.owners == 0 || !(a.onScreen || b.onScreen))
                        continue;
                    else if (travels && b.owners > 0 &&
                             (b.rect.getPosition().getDistanceFrom(a.rect.getPosition()) > 2.0f ||
                              std::abs(b.rect.getWidth() - a.rect.getWidth()) > 2.0f))
                        ADD_FAILURE() << name << " jumps at the second press from " << b.rect.toString() << " (drawer "
                                      << b.owner << ") to " << a.rect.toString() << " (drawer " << a.owner << "), "
                                      << where;
                    else if (b.owners > 0 && std::abs(b.alpha - a.alpha) > 0.05f)
                        ADD_FAILURE() << name << " fades from " << b.alpha << " to " << a.alpha
                                      << " at the second press, " << where;
                }
                for (const auto& [name, a] : after) {
                    const auto it = before.find(name);
                    // An opening macro's module shows first as a small box on its card, growing out of it.
                    const bool grows = a.owner == 1 && a.rect.getWidth() < a.restWidth * 0.5f;
                    if (a.owners > 0 && (it == before.end() || it->second.owners == 0) && motion == Motion::full &&
                        !grows)
                        ADD_FAILURE() << name << " pops in at the second press (drawer " << a.owner << " at "
                                      << a.rect.toString() << "), " << where;
                }
                prev = after;
            }
            if (second && !glide().isLive() && !fold().isLive())
                break;
        }
        EXPECT_TRUE(second);
        EXPECT_FALSE(fold().isLive()) << what << ": the folds settle";
        for (auto* c : editor().getModuleComponents())
            if (c != nullptr && c->isVisible())
                EXPECT_FLOAT_EQ(c->getAlpha(), 1.0f) << c->getComponentID() << " settles whole, " << what;
        const auto* m1 = editor().getMacros().find(macro_);
        const auto* m2 = editor().getMacros().find(macro2_);
        EXPECT_TRUE(m1 != nullptr && m2 != nullptr && m1->collapsed == !redo && m2->collapsed == !redo) << what;
        land();
        return largest;
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

// Regression test for FRO1664: a second Cmd+Z while the first fold ran landed that fold and restarted the glide.
// Cmd+Z twice quickly: the first fold used to land at once (its modules vanished and its card popped in) and the second
// started from where the cards stood at rest, not where they were drawn.
class MacroFoldSecondStepMidMotion : public Fro830Canvas {};

// The same two steps with a wait between them set the bar: pressed 40 ms apart, no frame moves a card much further
// than a frame of the slow pair does (a little more is the first fold bending towards where the second moved its card).
constexpr float kQuickStepAllowance = 1.25f;
TEST_F(MacroFoldSecondStepMidMotion, UndoTwiceQuicklyFoldsBothMacrosWithoutAPop) {
    const float slow = checkTwoStepsMidMotion(Motion::full, /*redo=*/false, /*gapMs=*/600.0);
    const float quick = checkTwoStepsMidMotion(Motion::full, /*redo=*/false, 40.0, slow * kQuickStepAllowance);
    std::cout << "[ undo twice ] largest step " << quick << " px (" << slow << " with a wait)\n";
    checkTwoStepsMidMotion(Motion::full, /*redo=*/false, /*gapMs=*/0.0, slow * kQuickStepAllowance);
}

TEST_F(MacroFoldSecondStepMidMotion, RedoTwiceQuicklyUnfoldsBothMacrosWithoutAPop) {
    const float slow = checkTwoStepsMidMotion(Motion::full, /*redo=*/true, /*gapMs=*/600.0);
    const float quick = checkTwoStepsMidMotion(Motion::full, /*redo=*/true, 40.0, slow * kQuickStepAllowance);
    std::cout << "[ redo twice ] largest step " << quick << " px (" << slow << " with a wait)\n";
    checkTwoStepsMidMotion(Motion::full, /*redo=*/true, /*gapMs=*/0.0, slow * kQuickStepAllowance);
}

TEST_F(MacroFoldSecondStepMidMotion, ReduceMotionAndOffTakeTwoQuickStepsWithoutAPop) {
    for (const auto motion : {Motion::reduced, Motion::off})
        for (const bool redo : {false, true}) {
            SCOPED_TRACE(motionName(motion));
            checkTwoStepsMidMotion(motion, redo);
        }
}
