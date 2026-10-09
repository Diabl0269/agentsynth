// MacroFoldNestedTests.cpp
//
// A macro nested in a folding macro (docs/layout/animation.md "Macro fold", nested macros): collapsing the parent folds
// an open child first and then flies its card into the parent's card as one box; expanding the parent brings the child
// back as it was left (an open child unfolds after its box lands, a folded one stays a card). Driven through the real
// MacroGroupController, AppUndoManager and Fold and Pack, stepped by hand through MacroFoldAnimator::applyAtMs.

#include "../../../Macros/MacroContainer/MacroNestedTestFixture.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"

namespace {

namespace mf = synth::ui::macro_fold;

struct AnimationModeGuard {
    explicit AnimationModeGuard(synth::ui::AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~AnimationModeGuard() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

// The machine's own reduce-motion setting (a CI runner may have it on) must not decide which fold a test sees.
struct SystemMotionPin {
    SystemMotionPin() { synth::ui::setReducedMotionForTest(false); }
    ~SystemMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct NestedFold {
    SystemMotionPin pin;
    NestedMacroFixture f;

    MacroFoldAnimator& fold() { return f.editor.getCardGlideForTest().fold(); }
    void animate() { f.editor.getCardGlideForTest().setForceAnimateForTest(true); }
    void land() { f.editor.finishCardGlideForTest(); }
    void toggleParent(bool collapsed) { f.ctl().setMacroCollapsed(f.parentId, collapsed); }
    juce::Rectangle<float> rectOf(NodeID id) { return findComponent(f.editor, id)->getBounds().toFloat(); }

    // The child's box on the parent's closed card, canvas coordinates, from the card's own layout.
    juce::Rectangle<float> childBoxOnParentCard() {
        const auto previews = f.ctl().macroMemberPreviews(f.parentId);
        std::vector<juce::Rectangle<int>> bounds;
        size_t childIndex = previews.size();
        for (size_t i = 0; i < previews.size(); ++i) {
            bounds.push_back(previews[i].bounds);
            if (previews[i].macroId == f.childId)
                childIndex = i;
        }
        auto* card = f.card(f.parentId);
        const auto boxes = macro_preview::boxes(bounds, card->getPreviewArea());
        if (childIndex >= boxes.size())
            return {};
        return boxes[childIndex].translated(static_cast<float>(card->getX()), static_cast<float>(card->getY()));
    }
};

void expectRectNear(juce::Rectangle<float> a, juce::Rectangle<float> b, const char* what) {
    EXPECT_NEAR(a.getX(), b.getX(), 0.5f) << what << ": " << a.toString() << " vs " << b.toString();
    EXPECT_NEAR(a.getY(), b.getY(), 0.5f) << what;
    EXPECT_NEAR(a.getWidth(), b.getWidth(), 0.5f) << what;
    EXPECT_NEAR(a.getHeight(), b.getHeight(), 0.5f) << what;
}

} // namespace

TEST(MacroFoldNested, TheParentsCardPreviewsTheNestedMacroAsOneBox) {
    NestedFold n;
    ASSERT_TRUE(n.f.linked);
    const auto previews = n.f.ctl().macroMemberPreviews(n.f.parentId);
    ASSERT_EQ(previews.size(), 3u) << "two modules of its own and one box for the nested macro";
    int macroBoxes = 0;
    for (const auto& p : previews)
        if (p.isMacro()) {
            ++macroBoxes;
            EXPECT_EQ(p.macroId, n.f.childId);
            EXPECT_EQ(p.nodeUid, 0u);
            EXPECT_EQ(p.colour, n.f.editor.getMacros().find(n.f.childId)->colour);
            EXPECT_EQ(p.bounds, n.f.ctl().macroHullBounds(n.f.childId)) << "an open child previews at its border";
        }
    EXPECT_EQ(macroBoxes, 1);

    // Still one box once the parent is folded (the child is hidden then), and at its card when the child is folded.
    n.toggleParent(true);
    EXPECT_EQ(n.f.ctl().macroMemberPreviews(n.f.parentId).size(), 3u);
    n.toggleParent(false);
    n.f.ctl().setMacroCollapsed(n.f.childId, true);
    for (const auto& p : n.f.ctl().macroMemberPreviews(n.f.parentId))
        if (p.isMacro())
            EXPECT_EQ(p.bounds, n.f.card(n.f.childId)->getBounds());
}

TEST(MacroFoldNested, CollapsingTheParentFoldsTheOpenChildFirstThenFliesItsCardIn) {
    NestedFold n;
    const auto c1Start = n.rectOf(n.f.c1), p1Start = n.rectOf(n.f.p1);
    n.animate();
    n.toggleParent(true);
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_FALSE(n.f.editor.getMacros().find(n.f.childId)->collapsed) << "the child keeps its own flag";

    const auto child = n.fold().spanFor(n.f.childId), parent = n.fold().spanFor(n.f.parentId);
    ASSERT_TRUE(child.has_value());
    ASSERT_TRUE(parent.has_value());
    EXPECT_DOUBLE_EQ(child->first, 0.0) << "the child folds first";
    EXPECT_DOUBLE_EQ(parent->first, child->second) << "the parent's flight starts when the child's modules have landed";
    EXPECT_LE(child->second - child->first, mf::kTotalMs + 1e-9);
    EXPECT_LE(parent->second - parent->first, mf::kTotalMs + 1e-9);
    EXPECT_DOUBLE_EQ(n.fold().totalMs(), parent->second);
    EXPECT_LE(n.fold().totalMs(), 2 * mf::kTotalMs + 1e-9);

    const auto childCard = n.fold().nestedRectFor(n.f.childId);
    ASSERT_FALSE(childCard.isEmpty());
    n.fold().applyAtMs(child->second * 0.5);
    EXPECT_NE(n.fold().moduleRectFor(n.f.c1.uid), c1Start) << "the child's modules are on their way";
    EXPECT_EQ(n.fold().moduleRectFor(n.f.p1.uid), p1Start) << "the parent's own modules wait";
    EXPECT_EQ(n.fold().nestedRectFor(n.f.childId), childCard) << "and so does the child's card";

    // When the child has folded, its border is its card, and that card is where the parent's flight takes it from.
    n.fold().applyAtMs(child->second);
    const auto childOutline = n.fold().outlineFor(n.f.childId);
    ASSERT_TRUE(childOutline.has_value());
    EXPECT_TRUE(childOutline->expanded(0.5f).contains(childCard));
    EXPECT_TRUE(childCard.expanded(0.5f).contains(*childOutline));

    n.fold().applyAtMs(n.fold().totalMs());
    expectRectNear(n.fold().nestedRectFor(n.f.childId), n.childBoxOnParentCard(), "the child lands on its box");
    n.fold().stepFrameForTest(1.0f);
    EXPECT_FALSE(n.fold().isLive());
    EXPECT_TRUE(n.f.cardVisible(n.f.parentId));
    EXPECT_FALSE(n.f.cardVisible(n.f.childId));
}

TEST(MacroFoldNested, ExpandingTheParentUnfoldsTheOpenChildAfterItsBoxLands) {
    NestedFold n;
    n.toggleParent(true);
    n.animate();
    n.toggleParent(false);
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_FALSE(n.f.editor.getMacros().find(n.f.childId)->collapsed);

    const auto child = n.fold().spanFor(n.f.childId), parent = n.fold().spanFor(n.f.parentId);
    ASSERT_TRUE(child.has_value()) << "the child was open, so it unfolds too";
    ASSERT_TRUE(parent.has_value());
    EXPECT_DOUBLE_EQ(parent->first, 0.0);
    EXPECT_GT(child->first, 0.0) << "after the parent's flight has brought its box out";
    EXPECT_LE(child->first, parent->second);

    const auto childCard = n.f.ctl().foldedCardBounds(n.f.childId).toFloat();
    n.fold().applyAtMs(child->first - 1.0);
    EXPECT_TRUE(n.fold().isHeld(n.f.c1.uid));
    EXPECT_FALSE(n.f.moduleVisible(n.f.c1));
    EXPECT_TRUE(n.f.editor.paintedMacroHullBounds(n.f.childId).isEmpty()) << "no child border before it unfolds";
    EXPECT_NEAR(n.fold().nestedRectFor(n.f.childId).getX(), childCard.getX(), 0.5f);
    EXPECT_NEAR(n.fold().nestedRectFor(n.f.childId).getY(), childCard.getY(), 0.5f);

    n.fold().applyAtMs(child->first + 10.0);
    EXPECT_FALSE(n.f.editor.paintedMacroHullBounds(n.f.childId).isEmpty()) << "the child's border grows from its card";
    n.fold().applyAtMs(child->second + 1.0);
    EXPECT_TRUE(n.f.moduleVisible(n.f.c1));
    EXPECT_TRUE(n.f.moduleVisible(n.f.c2));
    n.land();
    EXPECT_FALSE(n.fold().isLive());
    EXPECT_TRUE(n.f.moduleVisible(n.f.p1));
    EXPECT_FALSE(n.f.cardVisible(n.f.childId));
}

TEST(MacroFoldNested, AFoldedChildFliesOutAsItsCardAndStaysFolded) {
    NestedFold n;
    n.f.ctl().setMacroCollapsed(n.f.childId, true);
    n.animate();
    n.toggleParent(true);
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_FALSE(n.fold().spanFor(n.f.childId).has_value()) << "a folded child has nothing to fold first";
    EXPECT_DOUBLE_EQ(n.fold().spanFor(n.f.parentId)->first, 0.0);
    EXPECT_FALSE(n.fold().nestedRectFor(n.f.childId).isEmpty()) << "its card flies in as one box";
    n.land();

    n.toggleParent(false);
    ASSERT_TRUE(n.fold().isLive());
    EXPECT_FALSE(n.fold().spanFor(n.f.childId).has_value());
    EXPECT_TRUE(n.f.editor.getMacros().find(n.f.childId)->collapsed);
    EXPECT_FALSE(n.f.cardVisible(n.f.childId)) << "its real card is held until its box lands";
    n.fold().applyAtMs(n.fold().totalMs());
    EXPECT_TRUE(n.f.cardVisible(n.f.childId));
    expectRectNear(n.fold().nestedRectFor(n.f.childId), n.f.card(n.f.childId)->getBounds().toFloat(),
                   "it lands on its card");
    n.land();
    EXPECT_FALSE(n.f.moduleVisible(n.f.c1)) << "the child stays folded";
}

TEST(MacroFoldNested, UndoAndRedoPlayTheSameNestedFold) {
    NestedFold n;
    n.animate();
    n.toggleParent(true);
    n.land();

    ASSERT_TRUE(n.f.undo.undo());
    EXPECT_FALSE(n.f.editor.getMacros().find(n.f.parentId)->collapsed);
    ASSERT_TRUE(n.fold().isLive());
    const auto child = n.fold().spanFor(n.f.childId);
    ASSERT_TRUE(child.has_value()) << "undoing the collapse unfolds the child after the parent";
    EXPECT_GT(child->first, 0.0);
    n.land();
    EXPECT_TRUE(n.f.moduleVisible(n.f.c1));

    ASSERT_TRUE(n.f.undo.redo());
    ASSERT_TRUE(n.fold().isLive());
    const auto again = n.fold().spanFor(n.f.childId);
    ASSERT_TRUE(again.has_value());
    EXPECT_DOUBLE_EQ(again->first, 0.0) << "redoing it folds the child first again";
    EXPECT_DOUBLE_EQ(n.fold().spanFor(n.f.parentId)->first, again->second);
    n.land();
    EXPECT_TRUE(n.f.cardVisible(n.f.parentId));
}

TEST(MacroFoldNested, ReduceMotionIsOnePlainFadeAndOffScreenIsInstant) {
    {
        NestedFold n;
        AnimationModeGuard reduced(synth::ui::AnimationMode::reduced);
        n.animate();
        n.toggleParent(true);
        ASSERT_TRUE(n.fold().isLive());
        EXPECT_TRUE(n.fold().isReduced());
        EXPECT_DOUBLE_EQ(n.fold().totalMs(), mf::kFadeMs) << "no nested delay under Reduce Motion";
        EXPECT_EQ(n.f.card(n.f.parentId)->getAlpha(), 0.0f);
        n.land();
        n.toggleParent(false);
        ASSERT_TRUE(n.fold().isLive());
        EXPECT_DOUBLE_EQ(n.fold().totalMs(), mf::kFadeMs);
        EXPECT_TRUE(n.f.moduleVisible(n.f.c1));
        EXPECT_EQ(findComponent(n.f.editor, n.f.c1)->getAlpha(), 0.0f) << "the child's modules fade in with the rest";
        n.fold().applyAtMs(mf::kFadeMs / 2);
        EXPECT_NEAR(findComponent(n.f.editor, n.f.c1)->getAlpha(), 0.5f, 0.01f);
        n.land();
        EXPECT_EQ(findComponent(n.f.editor, n.f.c1)->getAlpha(), 1.0f);
    }
    {
        NestedFold n; // never shown, not forced: lands at once
        n.toggleParent(true);
        EXPECT_FALSE(n.fold().isLive());
        EXPECT_TRUE(n.f.cardVisible(n.f.parentId));
        n.toggleParent(false);
        EXPECT_FALSE(n.fold().isLive());
        EXPECT_TRUE(n.f.moduleVisible(n.f.c1));
    }
}

TEST(MacroFoldNested, FoldAndPackOverTheWholeParentFoldsTheChildInsideIt) {
    NestedFold n;
    n.animate();
    n.f.editor.setSelectedNodes({n.f.c1, n.f.c2, n.f.p1, n.f.p2});
    n.f.ctl().foldAndPackSelectionMacros();
    EXPECT_TRUE(n.f.editor.getMacros().find(n.f.parentId)->collapsed);
    ASSERT_TRUE(n.fold().isLive());
    const auto child = n.fold().spanFor(n.f.childId);
    ASSERT_TRUE(child.has_value()) << "the open child folds first inside its parent";
    EXPECT_DOUBLE_EQ(n.fold().spanFor(n.f.parentId)->first, child->second);
    n.fold().applyAtMs(n.fold().totalMs());
    expectRectNear(n.fold().nestedRectFor(n.f.childId), n.childBoxOnParentCard(), "on the packed card");
    n.land();
    EXPECT_TRUE(n.f.cardVisible(n.f.parentId));
    EXPECT_FALSE(n.f.cardVisible(n.f.childId)) << "a nested card never shows inside a folded parent";

    ASSERT_TRUE(n.f.undo.undo());
    n.land();
    EXPECT_TRUE(n.f.moduleVisible(n.f.c1));
    EXPECT_TRUE(n.f.moduleVisible(n.f.p1));
}
