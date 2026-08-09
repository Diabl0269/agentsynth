// SelectionModelTests.cpp
// Headless unit tests for the multi-select primitives behind issue #156:
//   • SelectionModel — add/remove/toggle/setSelection/retainOnly, invalid-id rejection, ordering
//   • marqueeRectFrom — normalises a drag in any direction into a positive rect
//   • hitTestMarquee  — intersection (not containment) semantics, degenerate marquee selects nothing
//   • unionSelection  — additive marquee keeps the base selection

#include "../Source/UI/SelectionModel.h"
#include <gtest/gtest.h>

using synth::ui::SelectionModel;
using NodeID = SelectionModel::NodeID;

static NodeID id(juce::uint32 uid) { return NodeID(uid); }

// ============================================================================
// SelectionModel
// ============================================================================

TEST(SelectionModel, StartsEmpty) {
    SelectionModel sel;
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_EQ(sel.size(), 0);
    EXPECT_FALSE(sel.contains(id(1)));
}

TEST(SelectionModel, AddIsIdempotent) {
    SelectionModel sel;
    EXPECT_TRUE(sel.add(id(7)));
    EXPECT_FALSE(sel.add(id(7))) << "adding an already-selected id must report no change";
    EXPECT_EQ(sel.size(), 1);
    EXPECT_TRUE(sel.contains(id(7)));
}

TEST(SelectionModel, RejectsInvalidNodeIdZero) {
    // NodeID{0} is the graph's "no node" sentinel — selecting it would let a stale/absent node
    // reach snippet extraction and group drags.
    SelectionModel sel;
    EXPECT_FALSE(sel.add(id(0)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(SelectionModel, RemoveReportsWhetherAnythingWasRemoved) {
    SelectionModel sel;
    sel.add(id(3));
    EXPECT_TRUE(sel.remove(id(3)));
    EXPECT_FALSE(sel.remove(id(3)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(SelectionModel, ToggleReturnsStateAfterToggling) {
    SelectionModel sel;
    EXPECT_TRUE(sel.toggle(id(2))) << "toggling an unselected id selects it";
    EXPECT_TRUE(sel.contains(id(2)));
    EXPECT_FALSE(sel.toggle(id(2))) << "toggling a selected id deselects it";
    EXPECT_FALSE(sel.contains(id(2)));
}

TEST(SelectionModel, SetSelectionReplacesAndDeduplicates) {
    SelectionModel sel;
    sel.add(id(99));
    sel.setSelection({id(1), id(2), id(2), id(0)});

    EXPECT_EQ(sel.size(), 2) << "duplicates collapse and the invalid id is dropped";
    EXPECT_TRUE(sel.contains(id(1)));
    EXPECT_TRUE(sel.contains(id(2)));
    EXPECT_FALSE(sel.contains(id(99))) << "setSelection replaces rather than merges";
}

TEST(SelectionModel, GetSelectedIsOrderedByUidRegardlessOfInsertionOrder) {
    // Snippet extraction walks this order, so a snippet's node order must not depend on the order
    // the user happened to click.
    SelectionModel a;
    a.setSelection({id(30), id(10), id(20)});
    SelectionModel b;
    b.setSelection({id(10), id(20), id(30)});

    auto expected = std::vector<NodeID>{id(10), id(20), id(30)};
    EXPECT_EQ(a.getSelected(), expected);
    EXPECT_EQ(b.getSelected(), expected);
}

TEST(SelectionModel, ClearEmptiesEverything) {
    SelectionModel sel;
    sel.setSelection({id(1), id(2)});
    sel.clear();
    EXPECT_TRUE(sel.isEmpty());
}

TEST(SelectionModel, RetainOnlyDropsIdsThatNoLongerExist) {
    SelectionModel sel;
    sel.setSelection({id(1), id(2), id(3)});

    EXPECT_TRUE(sel.retainOnly({id(1), id(3)}));
    EXPECT_EQ(sel.size(), 2);
    EXPECT_TRUE(sel.contains(id(1)));
    EXPECT_FALSE(sel.contains(id(2)));
    EXPECT_TRUE(sel.contains(id(3)));
}

TEST(SelectionModel, RetainOnlyReportsNoChangeWhenEverythingSurvives) {
    SelectionModel sel;
    sel.setSelection({id(1), id(2)});
    EXPECT_FALSE(sel.retainOnly({id(1), id(2), id(5)}));
    EXPECT_EQ(sel.size(), 2);
}

TEST(SelectionModel, RetainOnlyWithNothingAliveClearsSelection) {
    SelectionModel sel;
    sel.setSelection({id(1), id(2)});
    EXPECT_TRUE(sel.retainOnly({}));
    EXPECT_TRUE(sel.isEmpty());
}

// ============================================================================
// marqueeRectFrom
// ============================================================================

TEST(MarqueeRect, NormalisesDragInEveryDirection) {
    const juce::Rectangle<int> expected(10, 20, 90, 80); // x 10..100, y 20..100

    EXPECT_EQ(synth::ui::marqueeRectFrom({10, 20}, {100, 100}), expected) << "down-right";
    EXPECT_EQ(synth::ui::marqueeRectFrom({100, 100}, {10, 20}), expected) << "up-left";
    EXPECT_EQ(synth::ui::marqueeRectFrom({100, 20}, {10, 100}), expected) << "down-left";
    EXPECT_EQ(synth::ui::marqueeRectFrom({10, 100}, {100, 20}), expected) << "up-right";
}

TEST(MarqueeRect, ZeroDragIsEmpty) { EXPECT_TRUE(synth::ui::marqueeRectFrom({50, 50}, {50, 50}).isEmpty()); }

// ============================================================================
// hitTestMarquee
// ============================================================================

static std::vector<synth::LayoutUtil::Box> threeBoxes() {
    return {
        {id(1), juce::Rectangle<int>(0, 0, 100, 100)},
        {id(2), juce::Rectangle<int>(200, 0, 100, 100)},
        {id(3), juce::Rectangle<int>(400, 400, 100, 100)},
    };
}

TEST(MarqueeHitTest, SelectsFullyEnclosedBoxes) {
    auto hits = synth::ui::hitTestMarquee(juce::Rectangle<int>(-10, -10, 330, 130), threeBoxes());
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], id(1));
    EXPECT_EQ(hits[1], id(2));
}

TEST(MarqueeHitTest, SelectsPartiallyTouchedBoxes) {
    // Intersection semantics: clipping one corner is enough. Requiring full enclosure would make a
    // 560px-wide Sequencer practically unselectable when zoomed out.
    auto hits = synth::ui::hitTestMarquee(juce::Rectangle<int>(90, 90, 20, 20), threeBoxes());
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], id(1));
}

TEST(MarqueeHitTest, MissesBoxesOutsideTheBand) {
    auto hits = synth::ui::hitTestMarquee(juce::Rectangle<int>(120, 0, 50, 50), threeBoxes());
    EXPECT_TRUE(hits.empty());
}

TEST(MarqueeHitTest, DegenerateMarqueeSelectsNothing) {
    // A Shift-click with no drag must deselect, not select whatever sits under the cursor.
    auto hits = synth::ui::hitTestMarquee(juce::Rectangle<int>(50, 50, 0, 0), threeBoxes());
    EXPECT_TRUE(hits.empty());
}

TEST(MarqueeHitTest, IgnoresInvalidBoxIds) {
    std::vector<synth::LayoutUtil::Box> boxes{{id(0), juce::Rectangle<int>(0, 0, 100, 100)}};
    auto hits = synth::ui::hitTestMarquee(juce::Rectangle<int>(0, 0, 200, 200), boxes);
    EXPECT_TRUE(hits.empty());
}

TEST(MarqueeHitTest, EmptyBoxListYieldsNoHits) {
    auto hits = synth::ui::hitTestMarquee(juce::Rectangle<int>(0, 0, 500, 500), {});
    EXPECT_TRUE(hits.empty());
}

// ============================================================================
// unionSelection
// ============================================================================

TEST(UnionSelection, KeepsBaseAndAddsHits) {
    auto merged = synth::ui::unionSelection({id(1), id(5)}, {id(2), id(5)});
    auto expected = std::vector<NodeID>{id(1), id(2), id(5)};
    EXPECT_EQ(merged, expected) << "overlap must not duplicate";
}

TEST(UnionSelection, EmptyHitsPreservesBase) {
    auto merged = synth::ui::unionSelection({id(4), id(9)}, {});
    auto expected = std::vector<NodeID>{id(4), id(9)};
    EXPECT_EQ(merged, expected);
}

TEST(UnionSelection, EmptyBaseYieldsHits) {
    auto merged = synth::ui::unionSelection({}, {id(3)});
    ASSERT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0], id(3));
}
