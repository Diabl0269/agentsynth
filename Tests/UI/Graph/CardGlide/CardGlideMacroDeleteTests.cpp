// CardGlideMacroDeleteTests.cpp
//
// Deleting a macro shrinks it away and Cmd+Z grows it back, like a module card (docs/layout/animation.md "Delete and
// undo animation"): a collapsed macro's card is its own ghost, and an open macro's dashed border (with its name chip
// and port strips) leaves and returns with the module cards. Driven through the real delete and AppUndoManager, with no
// VBlank, through the animator's timeline seam.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

struct MacroCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b, loose;
    juce::String macroId;

    // The macOS runner can report Reduce Motion as on; these tests assert the full shrink and grow.
    struct FullMotion {
        FullMotion() { synth::ui::setReducedMotionForTest(false); }
        ~FullMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } fullMotion;

    explicit MacroCanvas(bool collapsed) {
        undo.setGraphEditor(&editor);
        editor.setSize(4000, 3000);
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 400);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1000, 400);
        loose = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400, 1800);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/false);
        editor.getMacroController().setMacroCollapsed(macroId, collapsed);
        editor.finishCardGlideForTest();
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    uint32_t cardKey() const { return CardGlideAnimator::macroKey(macroId); }
    uint32_t borderKey() const { return CardGlideAnimator::borderKey(cardKey()); }
    MacroCardComponent* card() { return editor.getMacroController().getMacroCardForTest(macroId); }
    bool macroExists() { return editor.getMacros().find(macroId) != nullptr; }
    void deleteMacro() { editor.getMacroController().deleteMacroAndMembers(macroId); }
};

} // namespace

TEST(CardGlideMacroDelete, HeadlessDeleteAndUndoMakeNoGhosts) {
    MacroCanvas c(/*collapsed=*/true);
    c.deleteMacro();
    EXPECT_FALSE(c.macroExists());
    EXPECT_FALSE(c.glide().isLive());
    ASSERT_TRUE(c.undo.undo());
    EXPECT_TRUE(c.macroExists());
    EXPECT_FALSE(c.glide().isLive());
    EXPECT_FLOAT_EQ(c.card()->getAlpha(), 1.0f);
}

TEST(CardGlideMacroDelete, CollapsedMacroCardShrinksAboutItsCentre) {
    MacroCanvas c(true);
    c.glide().setForceAnimateForTest(true);
    ASSERT_NE(c.card(), nullptr);
    const auto before = c.card()->getBounds();
    c.deleteMacro();

    EXPECT_FALSE(c.macroExists()); // the model and the card are final at once
    EXPECT_EQ(c.card(), nullptr);
    EXPECT_EQ(c.glide().exitGhostCount(), 1) << "only the card: the hidden members have nothing to show";
    EXPECT_EQ(c.glide().borderGhostCount(), 0);
    EXPECT_TRUE(c.glide().timeline().hasExit);

    c.glide().applyTimelineAtMs(0.0);
    EXPECT_EQ(c.glide().ghostRectFor(c.cardKey()).toNearestInt(), before);
    c.glide().applyTimelineAtMs(90.0);
    const auto mid = c.glide().ghostRectFor(c.cardKey());
    EXPECT_LT(mid.getWidth(), static_cast<float>(before.getWidth()));
    EXPECT_NEAR(mid.getCentreX(), before.toFloat().getCentreX(), 0.5f);
    EXPECT_NEAR(mid.getCentreY(), before.toFloat().getCentreY(), 0.5f);

    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.glide().exitGhostCount(), 0);
}

TEST(CardGlideMacroDelete, UndoGrowsTheCollapsedCardBackThenOutlinesIt) {
    MacroCanvas c(true);
    c.glide().setForceAnimateForTest(true);
    c.deleteMacro();
    c.editor.finishCardGlideForTest();

    ASSERT_TRUE(c.undo.undo());
    auto* card = c.card();
    ASSERT_NE(card, nullptr);
    const auto bounds = card->getBounds();
    EXPECT_EQ(c.glide().enterGhostCount(), 1);
    EXPECT_FLOAT_EQ(card->getAlpha(), 0.0f); // hidden while the ghost grows

    c.glide().applyTimelineAtMs(100.0);
    EXPECT_GT(c.glide().ghostRectFor(c.cardKey()).getWidth(), 0.0f);
    EXPECT_LT(c.glide().ghostRectFor(c.cardKey()).getWidth(), static_cast<float>(bounds.getWidth()));

    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kGrowMs + 100.0); // outline phase
    EXPECT_FLOAT_EQ(card->getAlpha(), 1.0f);
    EXPECT_EQ(c.glide().ghostRectFor(c.cardKey()).toNearestInt(), bounds);

    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.glide().enterGhostCount(), 0);
    EXPECT_TRUE(c.macroExists());
}

TEST(CardGlideMacroDelete, RedoOfTheDeleteShrinksTheCardAgain) {
    MacroCanvas c(true);
    c.glide().setForceAnimateForTest(true);
    c.deleteMacro();
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.redo());
    EXPECT_FALSE(c.macroExists());
    EXPECT_EQ(c.glide().exitGhostCount(), 1);
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroDelete, CollapsingByUndoIsNotAMacroEnterOrExit) {
    MacroCanvas c(false);
    c.editor.getMacroController().setMacroCollapsed(c.macroId, true);
    c.editor.finishCardGlideForTest();
    c.glide().setForceAnimateForTest(true);
    ASSERT_TRUE(c.undo.undo()); // expands again: the card is hidden, not removed
    EXPECT_EQ(c.glide().exitGhostCount(), 0);
    EXPECT_EQ(c.glide().borderGhostCount(), 0);
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.redo()); // collapses: the card shows, but it was there all along
    EXPECT_EQ(c.glide().enterGhostCount(), 0);
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroDelete, OpenMacroBorderShrinksAwayWithItsCards) {
    MacroCanvas c(false);
    c.glide().setForceAnimateForTest(true);
    const auto hull = c.editor.paintedMacroHullBounds(c.macroId);
    ASSERT_FALSE(hull.isEmpty());
    c.deleteMacro();

    EXPECT_FALSE(c.macroExists());
    EXPECT_EQ(c.glide().borderGhostCount(), 1);
    EXPECT_EQ(c.glide().exitGhostCount(), 3) << "both module cards and the border";

    c.glide().applyTimelineAtMs(0.0);
    EXPECT_GE(c.glide().ghostRectFor(c.borderKey()).getWidth(), static_cast<float>(hull.getWidth()));
    const auto atStart = c.glide().ghostRectFor(c.borderKey());
    c.glide().applyTimelineAtMs(90.0);
    const auto mid = c.glide().ghostRectFor(c.borderKey());
    EXPECT_LT(mid.getWidth(), atStart.getWidth());
    EXPECT_NEAR(mid.getCentreX(), atStart.getCentreX(), 0.5f);

    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.glide().borderGhostCount(), 0);
}

TEST(CardGlideMacroDelete, DeletingAModuleOutsideAMacroLeavesItsBorderAlone) {
    MacroCanvas c(false);
    c.glide().setForceAnimateForTest(true);
    c.editor.setSelectedNodes({c.loose});
    ASSERT_TRUE(c.editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(c.glide().borderGhostCount(), 0);
    EXPECT_EQ(c.glide().exitGhostCount(), 1);
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideMacroDelete, UndoOfAnOpenMacroDeleteHoldsTheBorderUntilItGrowsBack) {
    MacroCanvas c(false);
    c.glide().setForceAnimateForTest(true);
    c.deleteMacro();
    c.editor.finishCardGlideForTest();

    ASSERT_TRUE(c.undo.undo());
    ASSERT_TRUE(c.macroExists());
    EXPECT_EQ(c.glide().borderGhostCount(), 1);
    EXPECT_TRUE(c.glide().isBorderHeld(c.macroId)) << "the real border waits for the ghost";

    c.glide().applyTimelineAtMs(100.0);
    EXPECT_GT(c.glide().ghostRectFor(c.borderKey()).getWidth(), 0.0f);
    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kGrowMs + 100.0);
    EXPECT_FALSE(c.glide().isBorderHeld(c.macroId));

    c.editor.finishCardGlideForTest();
    EXPECT_EQ(c.glide().borderGhostCount(), 0);
    EXPECT_FALSE(c.glide().isBorderHeld(c.macroId));
}

TEST(CardGlideMacroDelete, ReducedMotionFadesInPlaceInsteadOfShrinking) {
    for (const bool collapsed : {true, false}) {
        MacroCanvas c(collapsed);
        synth::ui::setReducedMotionForTest(true);
        c.glide().setForceAnimateForTest(true);
        const auto key = collapsed ? c.cardKey() : c.borderKey();
        const auto before = collapsed ? c.card()->getBounds() : c.editor.paintedMacroHullBounds(c.macroId);
        c.deleteMacro();
        c.glide().applyTimelineAtMs(90.0);
        const auto rect = c.glide().ghostRectFor(key).toNearestInt();
        EXPECT_TRUE(rect.contains(before) || rect == before) << "a fade keeps the size";
        synth::ui::setReducedMotionForTest(std::nullopt);
        c.editor.finishCardGlideForTest();
    }
}
