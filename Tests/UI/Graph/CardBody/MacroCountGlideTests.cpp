// MacroCountGlideTests.cpp
//
// Changing the Macros bank's Knobs count glides the card to its new height instead of snapping
// (docs/layout/animation.md, "Macros count"): through the real parameter gesture (the Knobs arrows) and its async
// card callbacks, with the animated path forced on (no VBlank reaches an off-screen card) and the glide stepped by
// hand. The layout is final at once (a card built at the new count is the reference); the DRAWN height starts where
// the card was, passes through heights between, and lands exactly on the final one; neighbours are made room for
// once; Reduce Motion shortens it, Animations Off lands at once; undo and redo glide back in one step (the canvas
// card glide); the jacks that stay do not move, so their cables never trail.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "../../../TestWait.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/MacroControlModule.h"
#include "UI/Graph/CardBody/CardHeightGlide.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>

using synth::test::flushMessageQueue;

namespace {

constexpr int kFrom = 8;
constexpr int kTo = 3;

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        editor.getCardGlideForTest().setForceAnimateForTest(true);
    }

    NodeID bank(int x, int y, int count) {
        auto module = std::make_unique<MacroControlModule>();
        setCount(*module, count);
        return addModuleAt(editor, engine, std::move(module), x, y);
    }
    ModuleComponent* card(NodeID id) { return findComponent(editor, id); }
    NodeID neighbour(int x, int y) {
        return addModuleAt(editor, engine, synth::AIStateMapper::createModule("Noise"), x, y);
    }

    static juce::AudioParameterInt* countParam(juce::AudioProcessor& module) {
        for (auto* p : module.getParameters())
            if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p); i != nullptr && i->paramID == "macroCount")
                return i;
        return nullptr;
    }
    static void setCount(juce::AudioProcessor& module, int count) {
        countParam(module)->setValueNotifyingHost(countParam(module)->convertTo0to1(count));
    }

    // What the Knobs arrows do: one parameter gesture (one undo step), then the card's async callbacks run.
    void arrowTo(NodeID id, int count) {
        auto* param = countParam(*engine.getGraph().getNodeForId(id)->getProcessor());
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(count));
        param->endChangeGesture();
        ASSERT_TRUE(flushMessageQueue());
    }

    // The height a card built at `count` measures, the one the glide has to land on.
    static int heightAt(int count) {
        Canvas other;
        return other.card(other.bank(400, 300, count))->getHeight();
    }
};

} // namespace

TEST(MacroCountGlide, ShrinkingTheCountDrawsHeightsBetweenAndLandsOnTheFinalOne) {
    FadeAnimateGuard animate;
    Canvas c;
    const auto id = c.bank(400, 300, kFrom);
    const int tall = c.card(id)->getHeight();
    const int small = Canvas::heightAt(kTo);
    ASSERT_GT(tall, small) << "premise: fewer knobs is a shorter card";

    c.arrowTo(id, kTo);

    auto& card = *c.card(id);
    EXPECT_TRUE(card.heightGlideForTest().isGliding());
    EXPECT_EQ(card.getHeight(), tall) << "the first frame is the old height, not a snap to the new one";
    int previous = tall;
    for (double ms : {30.0, 70.0, 110.0}) {
        card.heightGlideForTest().stepForTest(ms);
        EXPECT_LT(card.getHeight(), previous) << "frame at " << ms << " ms";
        EXPECT_GT(card.getHeight(), small) << "still on its way at " << ms << " ms";
        previous = card.getHeight();
    }
    card.heightGlideForTest().stepForTest(1000.0);
    EXPECT_FALSE(card.heightGlideForTest().isGliding());
    EXPECT_EQ(card.getHeight(), small) << "lands exactly where a card built at that count lands";
}

TEST(MacroCountGlide, GrowingTheCountGlidesUpAndPushesTheNeighbourOnceAtTheStart) {
    FadeAnimateGuard animate;
    Canvas c;
    const auto id = c.bank(400, 300, kTo);
    const int small = c.card(id)->getHeight();
    const int tall = Canvas::heightAt(kFrom);
    const auto neighbour = c.neighbour(920, 300 + small + synth::LayoutUtil::kCollisionGap);
    const auto home = c.card(neighbour)->getBounds();

    c.arrowTo(id, kFrom);

    auto& card = *c.card(id);
    EXPECT_EQ(card.getHeight(), small);
    EXPECT_NE(c.card(neighbour)->getBounds(), home) << "room is made for the final footprint when the glide starts";
    const auto pushed = c.card(neighbour)->getBounds();
    card.heightGlideForTest().stepForTest(60.0);
    EXPECT_GT(card.getHeight(), small);
    EXPECT_LT(card.getHeight(), tall);
    EXPECT_EQ(c.card(neighbour)->getBounds(), pushed) << "the neighbour does not move per frame";
    card.heightGlideForTest().stepForTest(1000.0);
    EXPECT_EQ(card.getHeight(), tall);
}

TEST(MacroCountGlide, AShrinkingBankGivesTheRoomBackOnlyWhenItHasLanded) {
    FadeAnimateGuard animate;
    Canvas c;
    const auto id = c.bank(400, 300, kTo);
    const int small = c.card(id)->getHeight();
    const auto neighbour = c.neighbour(920, 300 + small + synth::LayoutUtil::kCollisionGap);
    const auto home = c.card(neighbour)->getBounds();
    c.arrowTo(id, kFrom);
    c.card(id)->heightGlideForTest().stepForTest(1000.0);
    const auto pushed = c.card(neighbour)->getBounds();
    ASSERT_NE(pushed, home) << "premise: the grown bank pushed it";
    ASSERT_FALSE(pushed.expanded(synth::LayoutUtil::kCollisionGap).intersects(c.card(id)->getBounds()))
        << "premise: clear of the grown bank: bank " << c.card(id)->getBounds().toString() << " neighbour "
        << pushed.toString() << " home " << home.toString();

    c.arrowTo(id, kTo);

    EXPECT_EQ(c.card(neighbour)->getBounds(), pushed) << "still drawn tall: the neighbour has not come back yet";
    c.card(id)->heightGlideForTest().stepForTest(1000.0);
    EXPECT_EQ(c.card(neighbour)->getBounds(), home);
}

TEST(MacroCountGlide, TheJacksThatStayDoNotMoveWhileTheCardGlides) {
    FadeAnimateGuard animate;
    Canvas c;
    const auto id = c.bank(400, 300, kFrom);
    auto& card = *c.card(id);
    std::vector<juce::Point<int>> jacks;
    for (int i = 0; i < kTo; ++i)
        jacks.push_back(card.getPortCenter(i, false));

    c.arrowTo(id, kTo);

    for (double ms : {0.0, 40.0, 90.0, 1000.0}) {
        card.heightGlideForTest().stepForTest(ms);
        for (int i = 0; i < kTo; ++i) {
            EXPECT_EQ(card.getPortCenter(i, false), jacks[(size_t)i]) << "jack " << i << " at " << ms << " ms";
            EXPECT_LT(card.getPortCenter(i, false).y, card.getHeight()) << "jack " << i << " stays on the card";
        }
    }
}

TEST(MacroCountGlide, ReduceMotionShortensItAndOffLandsAtOnce) {
    const int small = Canvas::heightAt(kTo);
    {
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        Canvas c;
        const auto id = c.bank(400, 300, kFrom);
        c.arrowTo(id, kTo);
        auto& card = *c.card(id);
        ASSERT_TRUE(card.heightGlideForTest().isGliding());
        card.heightGlideForTest().stepForTest(synth::CardHeightGlide::kReducedMs + 5.0);
        EXPECT_EQ(card.getHeight(), small) << "a shorter glide: done within the reduced time";
        EXPECT_LT(synth::CardHeightGlide::kReducedMs, synth::CardHeightGlide::kMs);
    }
    {
        FadeAnimateGuard off(synth::ui::AnimationMode::off);
        Canvas c;
        const auto id = c.bank(400, 300, kFrom);
        c.arrowTo(id, kTo);
        EXPECT_FALSE(c.card(id)->heightGlideForTest().isGliding());
        EXPECT_EQ(c.card(id)->getHeight(), small) << "Animations Off: the final height at once";
    }
}

TEST(MacroCountGlide, UndoAndRedoGlideTheCardBackInOneStep) {
    FadeAnimateGuard animate;
    Canvas c;
    const auto id = c.bank(400, 300, kFrom);
    const int tall = c.card(id)->getHeight();
    c.undo.clearUndoHistory();
    c.arrowTo(id, kTo);
    c.card(id)->heightGlideForTest().stepForTest(1000.0);
    c.editor.finishCardGlideForTest();
    const int small = c.card(id)->getHeight();
    ASSERT_LT(small, tall);

    // The restore rebuilds the card and puts the count back, so the card measures the old height through the same
    // path as the arrows: it glides back from the short height it showed, in the one undo step.
    for (const bool undoing : {true, false}) {
        const int from = undoing ? small : tall;
        const int to = undoing ? tall : small;
        ASSERT_TRUE(undoing ? c.undo.undo() : c.undo.redo());
        ASSERT_TRUE(flushMessageQueue());

        auto* card = c.card(id);
        ASSERT_NE(card, nullptr);
        EXPECT_TRUE(card->heightGlideForTest().isGliding()) << (undoing ? "undo" : "redo");
        EXPECT_EQ(card->getHeight(), from) << "the first frame is where the card stood";
        card->heightGlideForTest().stepForTest(60.0);
        EXPECT_GT(card->getHeight(), std::min(from, to));
        EXPECT_LT(card->getHeight(), std::max(from, to));
        card->heightGlideForTest().stepForTest(1000.0);
        EXPECT_EQ(card->getHeight(), to);
        c.editor.finishCardGlideForTest();
        if (undoing)
            EXPECT_FALSE(c.undo.canUndo()) << "the count change was one undo step";
    }
    EXPECT_TRUE(c.undo.canUndo()) << "redo put the step back";
}
