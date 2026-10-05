// CardGlideDeleteTests.cpp
//
// Deleting a canvas card shrinks it away and Cmd+Z grows it back (docs/layout/animation.md "Delete and undo
// animation"): driven through the real Delete key and AppUndoManager::undo()/redo(), with no VBlank, through the
// animator's timeline seam. The model change is final and synchronous; a headless (not showing) canvas makes no ghosts.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b;

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 400);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1400, 900);
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    bool hasCard(NodeID id) { return findComponent(editor, id) != nullptr; }

    void deleteWithKey(NodeID id) {
        editor.setSelectedNodes({id});
        ASSERT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    }
};

} // namespace

TEST(CardGlideDelete, HeadlessDeleteAndUndoLandAtOnceWithNoGhosts) {
    Canvas c;
    c.deleteWithKey(c.a);
    EXPECT_FALSE(c.hasCard(c.a));
    EXPECT_FALSE(c.glide().isLive());
    ASSERT_TRUE(c.undo.undo());
    ASSERT_TRUE(c.hasCard(c.a));
    EXPECT_FALSE(c.glide().isLive());
    EXPECT_FLOAT_EQ(findComponent(c.editor, c.a)->getAlpha(), 1.0f);
}

TEST(CardGlideDelete, DeleteShrinksTheCardAboutItsCentreWhileTheModelIsAlreadyGone) {
    Canvas c;
    c.glide().setForceAnimateForTest(true);
    const auto before = findComponent(c.editor, c.a)->getBounds();
    c.deleteWithKey(c.a);

    EXPECT_FALSE(c.hasCard(c.a)); // model + card are final immediately
    EXPECT_EQ(c.glide().exitGhostCount(), 1);
    EXPECT_TRUE(c.glide().timeline().hasExit);

    c.glide().applyTimelineAtMs(0.0);
    EXPECT_EQ(c.glide().ghostRectFor(c.a.uid).toNearestInt(), before);
    c.glide().applyTimelineAtMs(90.0);
    const auto mid = c.glide().ghostRectFor(c.a.uid);
    EXPECT_LT(mid.getWidth(), static_cast<float>(before.getWidth()));
    EXPECT_NEAR(mid.getCentreX(), before.toFloat().getCentreX(), 0.5f);
    EXPECT_NEAR(mid.getCentreY(), before.toFloat().getCentreY(), 0.5f);

    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.glide().exitGhostCount(), 0);
}

TEST(CardGlideDelete, ReducedMotionFadesInPlaceInsteadOfShrinking) {
    Canvas c;
    synth::ui::setReducedMotionForTest(true);
    c.glide().setForceAnimateForTest(true);
    const auto before = findComponent(c.editor, c.a)->getBounds();
    c.deleteWithKey(c.a);
    c.glide().applyTimelineAtMs(90.0);
    EXPECT_EQ(c.glide().ghostRectFor(c.a.uid).toNearestInt(), before);
    synth::ui::setReducedMotionForTest(std::nullopt);
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideDelete, UndoGrowsTheCardBackThenOutlinesItAndIsOneStep) {
    Canvas c;
    c.glide().setForceAnimateForTest(true);
    c.deleteWithKey(c.a);
    c.editor.finishCardGlideForTest();

    ASSERT_TRUE(c.undo.undo()); // one undo restores it
    auto* card = findComponent(c.editor, c.a);
    ASSERT_NE(card, nullptr);
    const auto bounds = card->getBounds();
    EXPECT_EQ(c.glide().enterGhostCount(), 1);
    EXPECT_FLOAT_EQ(card->getAlpha(), 0.0f); // hidden while the ghost grows

    c.glide().applyTimelineAtMs(100.0); // nothing else moved, so the grow starts at once
    EXPECT_GT(c.glide().ghostRectFor(c.a.uid).getWidth(), 0.0f);
    EXPECT_LT(c.glide().ghostRectFor(c.a.uid).getWidth(), static_cast<float>(bounds.getWidth()));

    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kGrowMs + 100.0); // outline phase
    EXPECT_FLOAT_EQ(card->getAlpha(), 1.0f);
    EXPECT_EQ(c.glide().ghostRectFor(c.a.uid).toNearestInt(), bounds);

    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.glide().enterGhostCount(), 0);
    EXPECT_FALSE(c.undo.canUndo() && !c.hasCard(c.a)); // the one undo brought it back
}

TEST(CardGlideDelete, RedoOfTheDeleteShrinksTheCardAgain) {
    Canvas c;
    c.glide().setForceAnimateForTest(true);
    c.deleteWithKey(c.a);
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.redo());
    EXPECT_FALSE(c.hasCard(c.a));
    EXPECT_EQ(c.glide().exitGhostCount(), 1);
    c.editor.finishCardGlideForTest();
}
