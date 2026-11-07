// CardGlideMacroPushTests.cpp
//
// Opening a macro pushes the macro beside it out of the way (makeRoomFor). The pushed neighbour glides to its new
// place like every other push on the canvas: a folded neighbour's card slides, and an open neighbour's member cards
// AND its border slide together, so no frame shows the border at the new place around cards still at the old one
// (docs/layout/macro-fold-motion.md, docs/layout/animation.md "Adding a module between others"). Driven through the
// real expand with the animators' test seams, no VBlank.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

struct PushCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> leftRow, rightRow;
    juce::String left, right;

    struct FullMotion {
        FullMotion() { synth::ui::setReducedMotionForTest(false); }
        ~FullMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } fullMotion;

    // `left` is a folded macro whose open border is wide; `right` sits just past its folded card, inside the area the
    // left border grows into, so opening `left` pushes `right` to the right.
    explicit PushCanvas(bool rightOpen) {
        undo.setGraphEditor(&editor);
        editor.setSize(8000, 3000);
        editor.getCardGlideForTest().setForceAnimateForTest(true);
        auto& mc = editor.getMacroController();
        for (int i = 0; i < 3; ++i)
            leftRow.push_back(addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400 + i * 600, 400));
        editor.setSelectedNodes(leftRow);
        left = mc.groupSelectionIntoMacro(/*autoCreatePorts=*/false);
        mc.setMacroCollapsed(left, true);
        settle();

        auto* leftCard = mc.getMacroCardForTest(left); // clear of the folded card, inside the open border
        const int x0 = (leftCard != nullptr ? leftCard->getRight() : 700) + 300;
        for (int i = 0; i < 2; ++i)
            rightRow.push_back(addModuleAt(editor, engine, std::make_unique<FilterModule>(), x0 + i * 600, 400));
        editor.setSelectedNodes(rightRow);
        right = mc.groupSelectionIntoMacro(/*autoCreatePorts=*/false);
        mc.setMacroCollapsed(right, !rightOpen);
        editor.setSelectedNodes({});
        settle();
    }

    void settle() {
        editor.finishCardGlideForTest();
        editor.finishHullGlideForTest();
    }
    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    juce::Rectangle<int> rect(NodeID id) { return findComponent(editor, id)->getBounds(); }
};

} // namespace

TEST(CardGlideMacroPush, OpeningAMacroGlidesAnOpenNeighbourAndItsBorder) {
    PushCanvas c(/*rightOpen=*/true);
    const auto memberBefore = c.rect(c.rightRow[0]);
    const auto hullBefore = c.editor.paintedMacroHullBounds(c.right);

    c.editor.getMacroController().setMacroCollapsed(c.left, false);

    const auto memberAfter = c.rect(c.rightRow[0]);
    ASSERT_GT(memberAfter.getX(), memberBefore.getX()) << "sanity: the open left macro pushed the right one";
    ASSERT_TRUE(c.glide().isLive());
    EXPECT_EQ(c.glide().currentRectFor(findComponent(c.editor, c.rightRow[0])), memberBefore)
        << "the pushed member card is drawn where it was";
    EXPECT_TRUE(c.editor.isHullGlideLiveForTest()) << "the pushed border glides with its cards";
    EXPECT_EQ(c.editor.paintedMacroHullBounds(c.right), hullBefore) << "first frame: the border is where it was";

    c.settle();
    EXPECT_EQ(c.editor.paintedMacroHullBounds(c.right), c.editor.getMacroController().macroHullBounds(c.right))
        << "end: the border sits around its cards";
}

TEST(CardGlideMacroPush, OpeningAMacroGlidesAFoldedNeighbourCard) {
    PushCanvas c(/*rightOpen=*/false);
    auto* card = c.editor.getMacroController().getMacroCardForTest(c.right);
    ASSERT_NE(card, nullptr);
    const auto before = card->getBounds();

    c.editor.getMacroController().setMacroCollapsed(c.left, false);

    card = c.editor.getMacroController().getMacroCardForTest(c.right);
    ASSERT_NE(card, nullptr);
    ASSERT_GT(card->getX(), before.getX()) << "sanity: the folded right macro was pushed";
    ASSERT_TRUE(c.glide().isLive());
    EXPECT_EQ(c.glide().currentRectFor(card), before) << "first frame: the card is drawn where it was";
    c.settle();
    EXPECT_EQ(card->getAlpha(), 1.0f);
}

TEST(CardGlideMacroPush, ReduceMotionMovesTheNeighbourWithNoGlide) {
    PushCanvas c(/*rightOpen=*/true);
    synth::ui::setReducedMotionForTest(true);
    const auto memberBefore = c.rect(c.rightRow[0]);
    c.editor.getMacroController().setMacroCollapsed(c.left, false);
    EXPECT_GT(c.rect(c.rightRow[0]).getX(), memberBefore.getX());
    EXPECT_FALSE(c.editor.isHullGlideLiveForTest()) << "Reduce Motion: the border lands at once";
    EXPECT_EQ(c.editor.paintedMacroHullBounds(c.right), c.editor.getMacroController().macroHullBounds(c.right));
}
