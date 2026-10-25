// MacroFoldTests.cpp
//
// A macro folds its modules into the closed card and unfolds them out of it (docs/layout/animation.md "Macro fold"),
// driven through the real MacroGroupController::setMacroCollapsed and AppUndoManager::undo()/redo(), with no VBlank:
// the fold is stepped by hand through MacroFoldAnimator::applyAtMs. The model is final the moment the toggle returns.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"
#include <gtest/gtest.h>

namespace {

namespace mf = synth::ui::macro_fold;

struct AnimationModeGuard {
    explicit AnimationModeGuard(synth::ui::AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~AnimationModeGuard() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

// An expanded macro of `count` modules laid out on a grid, one cable between its first two modules and one into the
// second from a module outside it.
struct FoldCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> members;
    NodeID outside, sink;
    juce::String macroId;

    // The machine's own reduce-motion setting (a CI runner may have it on) must not decide which fold a test sees:
    // pin "system prefers full motion" unless the test pins an animation mode itself.
    struct SystemMotionPin {
        SystemMotionPin() { synth::ui::setReducedMotionForTest(false); }
        ~SystemMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } systemMotionPin;

    explicit FoldCanvas(int count, bool autoPorts = false) {
        undo.setGraphEditor(&editor);
        editor.setSize(6000, 4000);
        for (int i = 0; i < count; ++i) {
            std::unique_ptr<juce::AudioProcessor> processor =
                i % 2 == 0 ? std::unique_ptr<juce::AudioProcessor>(new OscillatorModule())
                           : std::unique_ptr<juce::AudioProcessor>(new FilterModule());
            members.push_back(
                addModuleAt(editor, engine, std::move(processor), 100 + (i % 4) * 500, 100 + (i / 4) * 600));
        }
        outside = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 2600);
        if (count >= 2) {
            editor.connectPorts(members[0], 0, members[1], 1, /*isMidi=*/false);
            editor.connectPorts(outside, 0, members[1], 0, /*isMidi=*/false);
        }
        sink = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1100, 2600);
        if (count >= 3) {
            editor.connectPorts(members[2], 0, sink, 0, /*isMidi=*/false);
        }
        editor.setSelectedNodes(members);
        macroId = editor.getMacroController().groupSelectionIntoMacro(autoPorts);
        editor.getMacroController().setMacroCollapsed(macroId, false);
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    MacroFoldAnimator& fold() { return glide().fold(); }
    MacroCardComponent* card() { return editor.getMacroController().getMacroCardForTest(macroId); }
    ModuleComponent* comp(NodeID id) { return findComponent(editor, id); }
    void animate() { glide().setForceAnimateForTest(true); }
    void toggle(bool collapsed) { editor.getMacroController().setMacroCollapsed(macroId, collapsed); }
    void land() { editor.finishCardGlideForTest(); }

    // Where each module's box sits on the closed card, from the card's own layout, in canvas coordinates.
    std::vector<juce::Rectangle<float>> boxesOnCard() {
        const auto previews = editor.getMacroController().macroMemberPreviews(macroId);
        std::vector<juce::Rectangle<int>> bounds;
        for (const auto& p : previews)
            bounds.push_back(p.bounds);
        auto boxes = macro_preview::boxes(bounds, card()->getPreviewArea());
        for (auto& b : boxes)
            b.translate(static_cast<float>(card()->getX()), static_cast<float>(card()->getY()));
        return boxes;
    }
    std::vector<MacroGroupController::MacroMemberPreview> previews() {
        return editor.getMacroController().macroMemberPreviews(macroId);
    }
};

void expectBorderHoldsEveryModule(FoldCanvas& c, bool collapsing, double stepMs) {
    auto& fold = c.fold();
    for (double ms = 0.0; ms <= fold.totalMs(); ms += stepMs) {
        fold.applyAtMs(ms);
        const auto rects = fold.moduleRects(c.macroId);
        ASSERT_FALSE(rects.empty());
        const auto outline = fold.outlineFor(c.macroId);
        ASSERT_TRUE(outline.has_value());
        const auto painted =
            collapsing ? outline->getSmallestIntegerContainer() : c.editor.paintedMacroHullBounds(c.macroId);
        for (const auto& r : rects)
            EXPECT_TRUE(painted.toFloat().contains(r)) << "t=" << ms << (collapsing ? " collapse" : " expand");
    }
}

} // namespace

TEST(MacroFold, TheBorderHoldsEveryModuleInEveryFrameOfACollapse) {
    FoldCanvas c(5);
    c.animate();
    c.toggle(true);
    ASSERT_TRUE(c.fold().isLive());
    EXPECT_EQ(c.fold().ghostCount(), 5);
    expectBorderHoldsEveryModule(c, /*collapsing=*/true, 3.0);
}

TEST(MacroFold, TheBorderHoldsEveryModuleInEveryFrameOfAnExpand) {
    FoldCanvas c(5);
    c.toggle(true);
    c.animate();
    c.toggle(false);
    ASSERT_TRUE(c.fold().isLive());
    expectBorderHoldsEveryModule(c, /*collapsing=*/false, 3.0);
}

TEST(MacroFold, ABigCollapseIsDoneByTheCapAndATinyOneUsesTheFullStagger) {
    FoldCanvas big(12);
    big.animate();
    big.toggle(true);
    ASSERT_TRUE(big.fold().isLive());
    EXPECT_LE(big.fold().totalMs(), mf::kTotalMs + 1e-9);
    big.fold().applyAtMs(mf::kTotalMs);
    big.fold().stepFrameForTest(1.0f);
    EXPECT_FALSE(big.fold().isLive());
    EXPECT_EQ(big.fold().ghostCount(), 0) << "no ghost is left after the cap";
    ASSERT_NE(big.card(), nullptr);
    EXPECT_TRUE(big.card()->isVisible());
    EXPECT_EQ(big.card()->getAlpha(), 1.0f);

    FoldCanvas two(2);
    two.animate();
    two.toggle(true);
    ASSERT_TRUE(two.fold().isLive());
    const auto previews = two.previews();
    ASSERT_EQ(previews.size(), 2u);
    const auto start0 = two.fold().moduleRectFor(previews[0].nodeUid);
    const auto start1 = two.fold().moduleRectFor(previews[1].nodeUid);
    two.fold().applyAtMs(30.0);
    EXPECT_NE(two.fold().moduleRectFor(previews[0].nodeUid), start0) << "the first module is on its way";
    EXPECT_EQ(two.fold().moduleRectFor(previews[1].nodeUid), start1) << "the second waits for its 35 ms";
    two.fold().applyAtMs(60.0);
    EXPECT_NE(two.fold().moduleRectFor(previews[1].nodeUid), start1);
}

TEST(MacroFold, EveryCollapsingModuleLandsExactlyOnItsPreviewBox) {
    FoldCanvas c(6);
    c.animate();
    c.toggle(true);
    ASSERT_TRUE(c.fold().isLive());
    c.fold().applyAtMs(c.fold().totalMs());
    const auto boxes = c.boxesOnCard();
    const auto previews = c.previews();
    ASSERT_EQ(boxes.size(), previews.size());
    for (size_t i = 0; i < previews.size(); ++i) {
        const auto landed = c.fold().moduleRectFor(previews[i].nodeUid);
        EXPECT_NEAR(landed.getX(), boxes[i].getX(), 0.01f);
        EXPECT_NEAR(landed.getY(), boxes[i].getY(), 0.01f);
        EXPECT_NEAR(landed.getWidth(), boxes[i].getWidth(), 0.01f);
        EXPECT_NEAR(landed.getHeight(), boxes[i].getHeight(), 0.01f);
    }
}

TEST(MacroFold, TheClosedCardIsHeldBackUntilTheModulesHaveLanded) {
    FoldCanvas c(4);
    c.animate();
    c.toggle(true);
    ASSERT_NE(c.card(), nullptr);
    EXPECT_FALSE(c.card()->isVisible()) << "an invisible card cannot be clicked, focused or announced";
    c.fold().applyAtMs(c.fold().totalMs() - mf::kHandoverMs / 2);
    EXPECT_TRUE(c.card()->isVisible());
    EXPECT_GT(c.card()->getAlpha(), 0.0f);
    EXPECT_LT(c.card()->getAlpha(), 1.0f);
    c.land();
    EXPECT_TRUE(c.card()->isVisible());
    EXPECT_EQ(c.card()->getAlpha(), 1.0f);
}

TEST(MacroFold, AnExpandingMacroHoldsEachCardAndItsCablesUntilItsModuleLands) {
    FoldCanvas c(4);
    c.toggle(true);
    c.animate();
    c.toggle(false);
    ASSERT_TRUE(c.fold().isLive());
    const auto previews = c.previews();
    ASSERT_EQ(previews.size(), 4u);
    for (const auto& p : previews) {
        EXPECT_FALSE(p.comp->isVisible());
        EXPECT_TRUE(c.fold().isHeld(p.nodeUid));
    }

    // The cable from the outside module into the second member, and the one between the first two.
    const auto isMemberCable = [&](const GraphEditor::VisibleCable& cable) {
        for (const auto& p : previews)
            if (cable.id.srcUid == p.nodeUid || cable.id.dstUid == p.nodeUid)
                return true;
        return false;
    };
    for (const auto& cable : c.editor.buildVisibleCables())
        EXPECT_FALSE(isMemberCable(cable)) << "the canvas leaves them to the fold";

    mf::Timeline timeline{4, false, true};
    int second = -1, first = -1;
    for (size_t i = 0; i < previews.size(); ++i) {
        if (previews[i].nodeUid == c.members[1].uid)
            second = static_cast<int>(i);
        if (previews[i].nodeUid == c.members[0].uid)
            first = static_cast<int>(i);
    }
    ASSERT_GE(second, 0);
    ASSERT_GE(first, 0);
    const double landSecond = timeline.landMs(second), landFirst = timeline.landMs(first);
    const double later = std::max(landSecond, landFirst);

    const auto drawnBetween = [&](uint32_t src, uint32_t dst) {
        for (const auto& drawn : c.fold().drawnCables())
            if (drawn.cable.id.srcUid == src && drawn.cable.id.dstUid == dst)
                return true;
        return false;
    };
    // The cable between the first two modules waits for the later of them; the one from outside only for the second.
    c.fold().applyAtMs(landSecond - 1.0);
    EXPECT_FALSE(drawnBetween(c.outside.uid, c.members[1].uid)) << "not drawn before its module lands";
    EXPECT_FALSE(previews[static_cast<size_t>(second)].comp->isVisible());
    c.fold().applyAtMs(landSecond + 40.0);
    EXPECT_TRUE(drawnBetween(c.outside.uid, c.members[1].uid)) << "drawn once its module has landed";
    EXPECT_TRUE(previews[static_cast<size_t>(second)].comp->isVisible());
    c.fold().applyAtMs(later - 1.0);
    EXPECT_FALSE(drawnBetween(c.members[0].uid, c.members[1].uid)) << "the inner one waits for the later module";
    EXPECT_NE(landFirst, landSecond);

    c.fold().applyAtMs(later + 40.0);
    EXPECT_TRUE(previews[static_cast<size_t>(second)].comp->isVisible());
    EXPECT_TRUE(previews[static_cast<size_t>(first)].comp->isVisible());
    EXPECT_TRUE(drawnBetween(c.members[0].uid, c.members[1].uid))
        << "the cables draw out of their ports once their modules land";

    c.land();
    EXPECT_FALSE(c.fold().isLive());
    bool cableBack = false;
    for (const auto& cable : c.editor.buildVisibleCables())
        cableBack = cableBack || isMemberCable(cable);
    EXPECT_TRUE(cableBack) << "the canvas draws them again once the fold lands";
    for (const auto& p : previews)
        EXPECT_TRUE(p.comp->isVisible());
}

TEST(MacroFold, ACableBetweenTwoCollapsingModulesRidesThemAndFades) {
    FoldCanvas c(3);
    c.animate();
    c.toggle(true);
    ASSERT_TRUE(c.fold().isLive());
    c.fold().applyAtMs(10.0);
    EXPECT_EQ(c.fold().drawnCables().size(), 1u) << "the inner cable is drawn by the fold, between the flying modules";
    c.fold().applyAtMs(c.fold().totalMs());
    EXPECT_TRUE(c.fold().drawnCables().empty()) << "and has faded by the time the modules are in their boxes";
}

TEST(MacroFold, OffScreenAndAnimationsOffLandAtOnce) {
    {
        FoldCanvas c(3);
        c.toggle(true); // the canvas is not showing: no fold
        EXPECT_FALSE(c.fold().isLive());
        ASSERT_NE(c.card(), nullptr);
        EXPECT_TRUE(c.card()->isVisible());
        for (const auto& p : c.previews())
            EXPECT_FALSE(p.comp->isVisible());
        c.toggle(false);
        EXPECT_FALSE(c.fold().isLive());
        for (const auto& p : c.previews())
            EXPECT_TRUE(p.comp->isVisible());
    }
    {
        AnimationModeGuard off(synth::ui::AnimationMode::off);
        FoldCanvas c(3);
        c.animate();
        c.toggle(true);
        EXPECT_FALSE(c.fold().isLive()) << "Animations Off is instant even with the canvas forced to animate";
        EXPECT_TRUE(c.card()->isVisible());
    }
}

TEST(MacroFold, ReduceMotionFadesTheAppearingCardsInPlaceWithoutAFlight) {
    AnimationModeGuard reduced(synth::ui::AnimationMode::reduced);
    FoldCanvas c(3);
    c.animate();
    c.toggle(true);
    ASSERT_TRUE(c.fold().isLive());
    EXPECT_TRUE(c.fold().isReduced());
    EXPECT_DOUBLE_EQ(c.fold().totalMs(), mf::kFadeMs);
    EXPECT_TRUE(c.card()->isVisible());
    EXPECT_EQ(c.card()->getAlpha(), 0.0f);
    EXPECT_TRUE(c.fold().drawnCables().empty());
    c.fold().applyAtMs(mf::kFadeMs / 2);
    EXPECT_NEAR(c.card()->getAlpha(), 0.5f, 0.01f);
    c.fold().stepFrameForTest(1.0f);
    EXPECT_FALSE(c.fold().isLive());
    EXPECT_EQ(c.card()->getAlpha(), 1.0f);

    c.toggle(false);
    ASSERT_TRUE(c.fold().isLive());
    for (const auto& p : c.previews()) {
        EXPECT_TRUE(p.comp->isVisible());
        EXPECT_EQ(p.comp->getAlpha(), 0.0f);
    }
    c.land();
    for (const auto& p : c.previews())
        EXPECT_EQ(p.comp->getAlpha(), 1.0f);
}

TEST(MacroFold, UndoAndRedoOfAToggleFoldTheModulesToo) {
    FoldCanvas c(4);
    c.animate();
    c.toggle(true);
    c.land();
    ASSERT_NE(c.card(), nullptr);

    ASSERT_TRUE(c.undo.undo()); // back to expanded
    EXPECT_FALSE(c.editor.getMacros().find(c.macroId)->collapsed);
    ASSERT_TRUE(c.fold().isLive()) << "undoing a collapse unfolds the modules";
    EXPECT_EQ(c.fold().ghostCount(), 4);
    EXPECT_EQ(c.glide().exitGhostCount(), 0) << "the delete ghosts leave the folded cards alone";
    EXPECT_EQ(c.glide().enterGhostCount(), 0);
    for (const auto& p : c.previews())
        EXPECT_FALSE(p.comp->isVisible());
    expectBorderHoldsEveryModule(c, /*collapsing=*/false, 5.0);
    c.land();
    for (const auto& p : c.previews())
        EXPECT_TRUE(p.comp->isVisible());

    ASSERT_TRUE(c.undo.redo()); // collapsed again
    EXPECT_TRUE(c.editor.getMacros().find(c.macroId)->collapsed);
    ASSERT_TRUE(c.fold().isLive()) << "redoing the collapse folds them in again";
    EXPECT_EQ(c.glide().exitGhostCount(), 0);
    expectBorderHoldsEveryModule(c, /*collapsing=*/true, 5.0);
    c.land();
    EXPECT_TRUE(c.card()->isVisible());
}

TEST(MacroFold, ASecondToggleMidFlightLandsTheRunningFoldFirst) {
    FoldCanvas c(3);
    c.animate();
    c.toggle(true);
    ASSERT_TRUE(c.fold().isLive());
    c.fold().applyAtMs(50.0);
    c.toggle(false);
    ASSERT_TRUE(c.fold().isLive());
    EXPECT_EQ(c.card()->isVisible(), false) << "the card from the first fold was landed, then hidden by the unfold";
    EXPECT_FALSE(c.fold().outlineFor(c.macroId)->isEmpty());
    EXPECT_FALSE(c.editor.getMacros().find(c.macroId)->collapsed);
}

TEST(MacroFold, EveryFramePaintsAndRepaintsOnlyTheFoldsOwnArea) {
    FoldCanvas c(6);
    c.animate();
    c.toggle(true);
    c.land();
    const auto cardBounds = c.card()->getBounds();
    c.toggle(false);
    c.land();
    // The fold's own area: the border, the card, and the cable out to the module outside.
    const auto reach = c.editor.getMacroController()
                           .macroHullBounds(c.macroId)
                           .getUnion(cardBounds)
                           .getUnion(c.comp(c.outside)->getBounds())
                           .expanded(100);
    for (bool collapsing : {true, false}) {
        c.toggle(collapsing);
        ASSERT_TRUE(c.fold().isLive());
        for (double ms = 0.0; ms < c.fold().totalMs(); ms += 40.0) {
            c.fold().applyAtMs(ms);
            juce::Image image(juce::Image::ARGB, 400, 300, true);
            juce::Graphics g(image);
            c.glide().paint(g); // ghosts, the dashed border and the cables it draws itself
            const auto area = c.fold().drawnArea();
            EXPECT_FALSE(area.isEmpty());
            EXPECT_TRUE(reach.contains(area))
                << "a frame repaints the fold's own area, not the canvas: " << area.toString() << " vs "
                << reach.toString() << " ms=" << ms << (collapsing ? " collapse" : " expand");
        }
        c.land();
        EXPECT_TRUE(c.fold().drawnArea().isEmpty()) << "nothing is repainted once it has settled";
    }
}

TEST(MacroFold, AnUnfoldingMacroKeepsItsPortWidgetsOnTheGrowingBorder) {
    FoldCanvas c(3, /*autoPorts=*/true);
    const auto* macro = c.editor.getMacros().find(c.macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_GE(macro->ports.size(), 2u) << "the cables to and from the outside minted an input and an output port";
    std::vector<ModuleComponent*> widgets;
    for (const auto& port : macro->ports)
        widgets.push_back(c.comp(c.editor.getMacroController().resolveMemberNodeId(port.nodeUuid)));
    for (auto* w : widgets)
        ASSERT_NE(w, nullptr);

    c.toggle(true);
    c.land();
    c.animate();
    c.toggle(false);
    ASSERT_TRUE(c.fold().isLive());
    for (auto* w : widgets)
        EXPECT_FALSE(w->isVisible()) << "a port widget is not left on the open border while the border is card-sized";
    c.fold().applyAtMs(c.fold().totalMs() * 0.7);
    for (auto* w : widgets)
        EXPECT_TRUE(w->isVisible()) << "it shows once the border has grown to it";
    c.fold().stepFrameForTest(1.0f); // the last frame lands the fold
    EXPECT_FALSE(c.fold().isLive());
}

// Grouping a selection into a macro folds its modules into the new closed card like a collapse of the same group
// does, through the real groupSelectionIntoMacro (what Cmd+G, the menu and the canvas menu call). Bug kind:
// motion-over-time.
namespace {

struct GroupCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> members;
    NodeID neighbour;

    struct SystemMotionPin {
        SystemMotionPin() { synth::ui::setReducedMotionForTest(false); }
        ~SystemMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } systemMotionPin;

    explicit GroupCanvas(bool animate) {
        undo.setGraphEditor(&editor);
        editor.setSize(6000, 4000);
        for (int i = 0; i < 4; ++i) {
            std::unique_ptr<juce::AudioProcessor> processor =
                i % 2 == 0 ? std::unique_ptr<juce::AudioProcessor>(new OscillatorModule())
                           : std::unique_ptr<juce::AudioProcessor>(new FilterModule());
            members.push_back(
                addModuleAt(editor, engine, std::move(processor), 100 + (i % 2) * 500, 100 + (i / 2) * 600));
        }
        neighbour = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1300, 100);
        editor.getCardGlideForTest().setForceAnimateForTest(animate);
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    MacroFoldAnimator& fold() { return glide().fold(); }
    ModuleComponent* comp(NodeID id) { return findComponent(editor, id); }

    juce::String group() {
        editor.setSelectedNodes(members);
        return editor.getMacroController().groupSelectionIntoMacro(false);
    }
    // Every card and the macro's, as laid out right now.
    std::vector<juce::Rectangle<int>> layout(const juce::String& macroId) {
        std::vector<juce::Rectangle<int>> out;
        for (auto id : members)
            out.push_back(comp(id)->getBounds());
        out.push_back(comp(neighbour)->getBounds());
        if (auto* card = editor.getMacroController().getMacroCardForTest(macroId))
            out.push_back(card->getBounds());
        return out;
    }
};

} // namespace

TEST(MacroFoldGroup, GroupingFoldsTheMembersIntoTheNewCardInsteadOfJumping) {
    GroupCanvas c(/*animate=*/true);
    std::vector<juce::Rectangle<int>> start;
    for (auto id : c.members)
        start.push_back(c.comp(id)->getBounds());

    const auto macroId = c.group();
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(c.editor.getMacros().find(macroId)->collapsed);

    ASSERT_TRUE(c.fold().isLive()) << "grouping starts the fold";
    EXPECT_EQ(c.fold().ghostCount(), 4);
    auto* card = c.editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->isVisible(), false) << "the closed card is held back while the modules fly into it";
    for (auto id : c.members)
        EXPECT_FALSE(c.comp(id)->isVisible()) << "no member card is left standing on the canvas";

    // Frame 0: every module is still where it stood.
    c.fold().applyAtMs(0.0);
    auto rects = c.fold().moduleRects(macroId);
    ASSERT_EQ(rects.size(), 4u);
    for (size_t i = 0; i < rects.size(); ++i)
        EXPECT_EQ(rects[i].getSmallestIntegerContainer(), start[i]) << "module " << i;

    // Mid-flight the first module (the first to leave) is neither where it stood nor on its box yet.
    const auto previews = c.editor.getMacroController().macroMemberPreviews(macroId);
    ASSERT_EQ(previews.size(), 4u);
    c.fold().applyAtMs(150.0);
    rects = c.fold().moduleRects(macroId);
    EXPECT_NE(rects[0].getPosition(), start[0].getPosition().toFloat());
    EXPECT_GT(rects[0].getWidth(), 0.0f);
    EXPECT_LT(rects[0].getWidth(), start[0].getWidth() * 1.0f) << "it has shrunk toward its preview box";

    // The last frame lands the fold: nothing is left running, the card shows, no ghost remains.
    c.fold().applyAtMs(c.fold().totalMs());
    c.fold().stepFrameForTest(1.0f);
    c.editor.finishCardGlideForTest();
    EXPECT_FALSE(c.fold().isLive());
    EXPECT_EQ(c.fold().ghostCount(), 0);
    EXPECT_TRUE(card->isVisible());
    EXPECT_EQ(card->getAlpha(), 1.0f);
}

TEST(MacroFoldGroup, TheFinalLayoutIsTheSameWithAndWithoutTheMotion) {
    GroupCanvas still(/*animate=*/false);
    const auto stillId = still.group();
    EXPECT_FALSE(still.fold().isLive()) << "off screen, it lands at once";
    const auto stillLayout = still.layout(stillId);

    GroupCanvas moving(/*animate=*/true);
    const auto movingId = moving.group();
    moving.editor.finishCardGlideForTest();
    EXPECT_EQ(moving.layout(movingId), stillLayout);
}

TEST(MacroFoldGroup, RedoOfTheGroupingFoldsTheMembersToo) {
    GroupCanvas c(/*animate=*/true);
    const auto macroId = c.group();
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    ASSERT_EQ(c.editor.getMacros().find(macroId), nullptr);

    ASSERT_TRUE(c.undo.redo());
    ASSERT_NE(c.editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(c.fold().isLive()) << "redoing the grouping folds the modules in again";
    EXPECT_EQ(c.fold().ghostCount(), 4);
    c.editor.finishCardGlideForTest();
    EXPECT_FALSE(c.fold().isLive());
}

TEST(MacroFoldGroup, UndoOfTheGroupingUnfoldsTheMembers) {
    GroupCanvas c(/*animate=*/true);
    std::vector<juce::Rectangle<int>> start;
    for (auto id : c.members)
        start.push_back(c.comp(id)->getBounds());
    const auto macroId = c.group();
    c.editor.finishCardGlideForTest();

    ASSERT_TRUE(c.undo.undo());
    ASSERT_EQ(c.editor.getMacros().find(macroId), nullptr);
    ASSERT_TRUE(c.fold().isLive()) << "undoing a grouping unfolds the modules out of the card";
    EXPECT_EQ(c.fold().ghostCount(), 4);
    for (auto id : c.members)
        EXPECT_FALSE(c.comp(id)->isVisible()) << "a member's card is held until its ghost lands";

    c.fold().applyAtMs(0.0);
    auto rects = c.fold().moduleRects(macroId);
    ASSERT_EQ(rects.size(), 4u);
    for (const auto& r : rects)
        EXPECT_LT(r.getWidth(), 100.0f) << "frame 0 sits on the small box on the closed card";
    c.fold().applyAtMs(c.fold().totalMs() * 0.5);
    const auto mid = c.fold().moduleRects(macroId);
    EXPECT_NE(mid[0], rects[0]) << "it moves between the box and its card";
    EXPECT_LT(mid[0].getWidth(), static_cast<float>(start[0].getWidth()));

    c.editor.finishCardGlideForTest();
    EXPECT_FALSE(c.fold().isLive());
    for (size_t i = 0; i < c.members.size(); ++i) {
        EXPECT_TRUE(c.comp(c.members[i])->isVisible());
        EXPECT_EQ(c.comp(c.members[i])->getBounds(), start[i]) << "final layout is the ungrouped one";
    }
}

TEST(MacroFoldGroup, UndoOfTheGroupingStartsTheNeighbourGlideWithTheUnfold) {
    GroupCanvas c(/*animate=*/true);
    c.group();
    c.editor.finishCardGlideForTest();

    ASSERT_TRUE(c.undo.undo());
    ASSERT_TRUE(c.fold().isLive());
    // The closed card the members unfold out of is drawn by the fold; no shrinking ghost of it may hold the
    // neighbours' glide back behind an exit phase.
    EXPECT_EQ(c.glide().exitGhostCount(), 0) << "the removed macro card does not also shrink away";
    EXPECT_EQ(c.glide().timeline().exitMs(), 0.0) << "the glide has no exit phase to wait behind";
}

TEST(MacroFoldGroup, UndoLandsOnTheSameLayoutWithAndWithoutTheMotion) {
    auto layoutAfterUndo = [](bool animate) {
        GroupCanvas c(animate);
        c.group();
        c.editor.finishCardGlideForTest();
        c.undo.undo();
        c.editor.finishCardGlideForTest();
        std::vector<juce::Rectangle<int>> out;
        for (auto id : c.members)
            out.push_back(c.comp(id)->getBounds());
        out.push_back(c.comp(c.neighbour)->getBounds());
        return out;
    };
    EXPECT_EQ(layoutAfterUndo(true), layoutAfterUndo(false));
}

TEST(MacroFoldGroup, UngroupingAClosedMacroUnfoldsItsMembers) {
    GroupCanvas c(/*animate=*/true);
    const auto macroId = c.group();
    c.editor.finishCardGlideForTest();
    c.editor.setSelectedNodes(c.members);
    c.editor.getMacroController().ungroupSelection();
    ASSERT_EQ(c.editor.getMacros().find(macroId), nullptr);
    ASSERT_TRUE(c.fold().isLive());
    EXPECT_EQ(c.fold().ghostCount(), 4);
    c.editor.finishCardGlideForTest();
    EXPECT_FALSE(c.fold().isLive());
    for (auto id : c.members)
        EXPECT_TRUE(c.comp(id)->isVisible());
}
