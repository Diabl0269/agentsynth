// ListSelectionModelTests.cpp -- the Finder-rules selection the track list and the Sections strip share, exercised
// with section placement ids: plain click, Cmd toggle, Shift range from the anchor, re-Shift, and pruning.
#include "Timeline/TimelineDoc/TimelineTypes.h"
#include "UI/Timeline/ListSelectionModel.h"
#include <gtest/gtest.h>

using synth::PlacementId;
using synth::ui::ListSelectionModel;

namespace {
const std::vector<PlacementId> kOrder{PlacementId{1}, PlacementId{2}, PlacementId{3}, PlacementId{4}, PlacementId{5}};
std::vector<PlacementId> ids(std::initializer_list<std::int64_t> values) {
    std::vector<PlacementId> out;
    for (const auto v : values)
        out.push_back(PlacementId{v});
    return out;
}
} // namespace

TEST(ListSelectionModelTest, APlainClickSelectsOneAndAnInvalidIdClears) {
    ListSelectionModel<PlacementId> model;
    model.toggle(PlacementId{4});
    model.selectOnly(PlacementId{2});
    EXPECT_EQ(model.ordered(kOrder), ids({2}));
    EXPECT_EQ(model.anchor(), PlacementId{2});
    model.selectOnly({});
    EXPECT_TRUE(model.isEmpty());
}

TEST(ListSelectionModelTest, CmdTogglesOneAndMovesTheAnchorToIt) {
    ListSelectionModel<PlacementId> model;
    model.selectOnly(PlacementId{1});
    EXPECT_TRUE(model.toggle(PlacementId{3}));
    EXPECT_EQ(model.ordered(kOrder), ids({1, 3}));
    EXPECT_EQ(model.anchor(), PlacementId{3});
    EXPECT_FALSE(model.toggle(PlacementId{1}));
    EXPECT_EQ(model.ordered(kOrder), ids({3}));
    EXPECT_EQ(model.anchor(), PlacementId{1}) << "the anchor follows the click even when it took the item out";
}

TEST(ListSelectionModelTest, ShiftSelectsTheRunFromTheAnchorAndAgainMovesTheFarEnd) {
    ListSelectionModel<PlacementId> model;
    model.selectOnly(PlacementId{2});
    model.extendTo(PlacementId{4}, kOrder);
    EXPECT_EQ(model.ordered(kOrder), ids({2, 3, 4}));
    model.extendTo(PlacementId{1}, kOrder);
    EXPECT_EQ(model.ordered(kOrder), ids({1, 2})) << "a second Shift-click replaces the range, it does not add one";
    EXPECT_EQ(model.anchor(), PlacementId{2});
}

TEST(ListSelectionModelTest, ShiftAddsToWhatCmdClicksBuilt) {
    ListSelectionModel<PlacementId> model;
    model.selectOnly(PlacementId{1});
    model.toggle(PlacementId{4});
    model.extendTo(PlacementId{5}, kOrder);
    EXPECT_EQ(model.ordered(kOrder), ids({1, 4, 5}));
}

TEST(ListSelectionModelTest, RetainOnlyDropsGoneIdsAndForgetsAGoneAnchor) {
    ListSelectionModel<PlacementId> model;
    model.selectAll(ids({1, 3, 5}));
    EXPECT_EQ(model.anchor(), PlacementId{5});
    model.retainOnly(ids({1, 2, 3}));
    EXPECT_EQ(model.ordered(kOrder), ids({1, 3}));
    EXPECT_FALSE(model.anchor().isValid());
    model.extendTo(PlacementId{2}, ids({1, 2, 3}));
    EXPECT_EQ(model.ordered(kOrder), ids({2})) << "with no anchor a Shift-click acts as a plain click";
}
