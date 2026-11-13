// CardGlideMacroGapTests.cpp
//
// Deleting a card inside an OPEN macro closes the hole (the cards after it move up and the border shrinks), the open
// canvas keeps its hole, the neighbours a deleted card had pushed aside come home, and a second delete or undo while
// the first is still moving carries on from what is drawn (docs/layout/animation.md "Delete and undo animation",
// docs/macros/macros.md "Deleting a card inside an open macro"). Driven through the real delete and AppUndoManager
// with the animator's timeline seam, no VBlank.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

struct GapCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> row;
    NodeID loose;
    juce::String macroId;

    struct FullMotion {
        FullMotion() { synth::ui::setReducedMotionForTest(false); }
        ~FullMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } fullMotion;

    // `count` filters in one row, grouped into an open macro, plus one loose filter far below.
    explicit GapCanvas(int count, bool grouped = true) {
        undo.setGraphEditor(&editor);
        editor.setSize(6000, 3000);
        for (int i = 0; i < count; ++i)
            row.push_back(addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400 + i * 600, 400));
        loose = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400, 2000);
        if (grouped) {
            editor.setSelectedNodes(row);
            macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/false);
            editor.getMacroController().setMacroCollapsed(macroId, false);
        }
        editor.finishCardGlideForTest();
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    juce::Rectangle<int> rect(NodeID id) { return findComponent(editor, id)->getBounds(); }
    juce::Rectangle<int> hull() { return editor.getMacroController().macroHullBounds(macroId); }
    void deleteCard(NodeID id) {
        editor.setSelectedNodes({id});
        editor.deleteSelection();
    }
};

constexpr double kMidGapMs = synth::ui::ExitEnterTimeline::kExitMs + 60.0;

} // namespace

TEST(CardGlideMacroGap, DeletingTheMiddleCardClosesTheGapAndShrinksTheBorder) {
    GapCanvas c(3);
    const auto first = c.rect(c.row[0]), second = c.rect(c.row[1]), third = c.rect(c.row[2]);
    const auto hullBefore = c.hull();

    c.deleteCard(c.row[1]);

    EXPECT_EQ(c.rect(c.row[0]), first) << "the cards before the hole stay";
    EXPECT_EQ(c.rect(c.row[2]).getPosition(), second.getPosition()) << "the third card lands in the second's place";
    EXPECT_EQ(c.rect(c.row[2]).getWidth(), third.getWidth());
    EXPECT_LT(c.hull().getWidth(), hullBefore.getWidth()) << "the border follows the cards in";
    EXPECT_EQ(c.hull().getX(), hullBefore.getX());
}

TEST(CardGlideMacroGap, UndoPutsEveryCardBackExactly) {
    GapCanvas c(3);
    std::vector<juce::Rectangle<int>> before;
    for (auto id : c.row)
        before.push_back(c.rect(id));
    const auto hullBefore = c.hull();

    c.deleteCard(c.row[1]);
    ASSERT_TRUE(c.undo.undo());

    ASSERT_NE(findComponent(c.editor, c.row[1]), nullptr);
    for (size_t i = 0; i < c.row.size(); ++i)
        EXPECT_EQ(c.rect(c.row[i]), before[i]) << "card " << i;
    EXPECT_EQ(c.hull(), hullBefore);
}

TEST(CardGlideMacroGap, DeletingTheLastCardInARowLeavesTheOthersAlone) {
    GapCanvas c(3);
    const auto first = c.rect(c.row[0]), second = c.rect(c.row[1]);
    c.deleteCard(c.row[2]);
    EXPECT_EQ(c.rect(c.row[0]), first);
    EXPECT_EQ(c.rect(c.row[1]), second);
}

TEST(CardGlideMacroGap, OpenCanvasKeepsItsHole) {
    GapCanvas c(3, /*grouped=*/false);
    const auto first = c.rect(c.row[0]), third = c.rect(c.row[2]);
    c.deleteCard(c.row[1]);
    EXPECT_EQ(c.rect(c.row[0]), first);
    EXPECT_EQ(c.rect(c.row[2]), third) << "no reflow on the top-level canvas";
}

TEST(CardGlideMacroGap, WithNothingAfterItInTheRowTheCardsBelowMoveUp) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(4000, 4000);
    const auto top = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400, 400);
    const auto below = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400, 1100);
    editor.setSelectedNodes({top, below});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(false);
    editor.getMacroController().setMacroCollapsed(macroId, false);
    editor.finishCardGlideForTest();
    const auto slot = findComponent(editor, top)->getBounds();

    editor.setSelectedNodes({top});
    editor.deleteSelection();

    EXPECT_EQ(findComponent(editor, below)->getBounds().getPosition(), slot.getPosition());
}

TEST(CardGlideMacroGap, TheCardsGlideIntoTheHoleAfterTheExit) {
    GapCanvas c(3);
    c.glide().setForceAnimateForTest(true);
    const auto third = c.rect(c.row[2]);
    c.deleteCard(c.row[1]);

    EXPECT_TRUE(c.glide().timeline().hasExit);
    EXPECT_TRUE(c.glide().timeline().hasGap);
    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kExitMs * 0.5);
    EXPECT_EQ(c.glide().currentRectFor(findComponent(c.editor, c.row[2])).getX(), third.getX())
        << "still waiting for the exit";
    c.glide().applyTimelineAtMs(kMidGapMs);
    const auto mid = c.glide().currentRectFor(findComponent(c.editor, c.row[2]));
    EXPECT_LT(mid.getX(), third.getX());
    EXPECT_GT(mid.getX(), c.rect(c.row[2]).getX());
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroGap, DeletingAPusherBringsItsNeighboursHome) {
    GapCanvas c(2, /*grouped=*/false);
    const auto pusher = c.row[0], neighbour = c.row[1];
    const auto home = c.rect(neighbour);
    auto* card = findComponent(c.editor, pusher);
    card->setSize(card->getWidth() + 900, card->getHeight()); // grows over the neighbour
    c.editor.getMacroController().reflowForResizedModule(pusher);
    ASSERT_NE(c.rect(neighbour), home) << "the growing card pushed it aside";

    c.deleteCard(pusher);
    EXPECT_EQ(c.rect(neighbour), home);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_NE(c.rect(neighbour), home) << "undo restores the pushed state exactly";
}

TEST(CardGlideMacroGap, ASecondDeleteMidMotionContinuesFromTheDrawnRects) {
    GapCanvas c(4);
    c.glide().setForceAnimateForTest(true);
    c.deleteCard(c.row[1]);
    c.glide().applyTimelineAtMs(kMidGapMs);
    auto* fourth = findComponent(c.editor, c.row[3]);
    const auto drawn = c.glide().currentRectFor(fourth);
    ASSERT_FALSE(drawn.isEmpty());
    ASSERT_NE(drawn, fourth->getBounds());

    c.deleteCard(c.row[0]); // closes the gap again while the first is moving

    EXPECT_EQ(c.glide().currentRectFor(fourth), drawn) << "starts from where it is drawn, not from where it began";
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroGap, ASecondDeleteMidShrinkLetsTheFirstCardFinishShrinking) {
    GapCanvas c(4);
    c.glide().setForceAnimateForTest(true);
    c.deleteCard(c.row[1]);
    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kExitMs * 0.5);
    const auto key = c.row[1].uid;
    const auto halfway = c.glide().ghostRectFor(key);
    ASSERT_FALSE(halfway.isEmpty());

    c.deleteCard(c.row[0]);

    EXPECT_EQ(c.glide().exitGhostCount(), 2) << "the first card's shrink carries on beside the second's";
    c.glide().applyTimelineAtMs(0.0);
    EXPECT_NEAR(c.glide().ghostRectFor(key).getWidth(), halfway.getWidth(), 0.5f) << "resumes at the drawn size";
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroGap, ADeleteThatMovesNothingDoesNotSnapTheMotionUnderway) {
    GapCanvas c(3);
    c.glide().setForceAnimateForTest(true);
    c.deleteCard(c.row[1]);
    c.glide().applyTimelineAtMs(kMidGapMs);
    auto* third = findComponent(c.editor, c.row[2]);
    const auto drawn = c.glide().currentRectFor(third);
    ASSERT_NE(drawn, third->getBounds());

    c.deleteCard(c.loose); // no card moves for this one

    EXPECT_EQ(c.glide().currentRectFor(third), drawn);
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroGap, AnUndoMidExitGrowsTheCardBackWithoutALeftoverShrink) {
    GapCanvas c(3);
    c.glide().setForceAnimateForTest(true);
    c.deleteCard(c.row[1]);
    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kExitMs * 0.5);
    ASSERT_EQ(c.glide().exitGhostCount(), 1);

    ASSERT_TRUE(c.undo.undo());

    EXPECT_EQ(c.glide().exitGhostCount(), 0) << "the returning card is not drawn twice";
    EXPECT_EQ(c.glide().enterGhostCount(), 1);
    c.editor.finishCardGlideForTest();
}
