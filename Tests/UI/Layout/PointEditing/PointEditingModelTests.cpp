// PointEditingModelTests.cpp -- the pure shared point model both point editors run on (UI/Layout/PointEditing/):
// the selection keys under each editor's rules, the rigid group move's limits, the clipboard entries and the one
// key decoder.

#include "UI/Layout/PointEditing/PointClipboard.h"
#include "UI/Layout/PointEditing/PointEditorKeys.h"
#include "UI/Layout/PointEditing/PointGroupMove.h"
#include "UI/Layout/PointEditing/PointSelectionModel.h"
#include <gtest/gtest.h>
#include <limits>

using namespace synth::ui;

namespace {
using Rules = PointSelectionRules;
constexpr Rules kCurveLike{Rules::FirstStep::firstPoint, false, Rules::Extend::rangeFromAnchor,
                           Rules::SelectAllCursor::keepSelectedElseLast};
constexpr Rules kLaneLike{Rules::FirstStep::nearestEnd, true, Rules::Extend::growPastEnd,
                          Rules::SelectAllCursor::keepElseFirst};
const std::vector<int> kDots{1, 2, 3};
} // namespace

TEST(PointSelectionModel, TheFirstStepLandsOnTheFirstDotOrTheNearestEnd) {
    PointSelectionModel<int> curve, lane;
    EXPECT_EQ(curve.step(-1, kDots, kCurveLike), std::optional<int>(1));
    EXPECT_EQ(lane.step(-1, kDots, kLaneLike), std::optional<int>(3));
    EXPECT_EQ(lane.selected(), (std::vector<int>{3}));
    EXPECT_FALSE(PointSelectionModel<int>().step(1, {}, kLaneLike).has_value()) << "no dot, no change";
}

TEST(PointSelectionModel, AStepAtTheEndCollapsesOrChangesNothing) {
    PointSelectionModel<int> curve, lane;
    curve.assign({2, 3}, 3, 2);
    lane.assign({2, 3}, 3, 2);
    EXPECT_FALSE(curve.step(1, kDots, kCurveLike).has_value());
    EXPECT_EQ(curve.selected(), (std::vector<int>{2, 3}));
    EXPECT_EQ(lane.step(1, kDots, kLaneLike), std::optional<int>(3));
    EXPECT_EQ(lane.selected(), (std::vector<int>{3}));
}

TEST(PointSelectionModel, ExtendShrinksTowardTheAnchorOrOnlyEverGrows) {
    PointSelectionModel<int> curve, lane;
    curve.assign({2}, 2, 2);
    lane.assign({2}, 2, 2);
    curve.extend(1, kDots, kCurveLike);
    lane.extend(1, kDots, kLaneLike);
    EXPECT_EQ(curve.selected(), (std::vector<int>{2, 3}));
    EXPECT_EQ(lane.selected(), (std::vector<int>{2, 3}));
    EXPECT_EQ(curve.extend(-1, kDots, kCurveLike), std::optional<int>(2));
    EXPECT_EQ(curve.selected(), (std::vector<int>{2})) << "reversing shrinks back to the anchor";
    EXPECT_EQ(lane.extend(-1, kDots, kLaneLike), std::optional<int>(1));
    EXPECT_EQ(lane.selected(), (std::vector<int>{1, 2, 3})) << "reaches past the selection's left end";
    EXPECT_EQ(lane.cursor(), std::optional<int>(1));
}

TEST(PointSelectionModel, SelectAllPlacesTheCursorByTheEditorsRule) {
    PointSelectionModel<int> curve, lane;
    curve.setCursor(2); // a cursor on an unselected dot
    EXPECT_TRUE(curve.selectAll(kDots, kCurveLike));
    EXPECT_EQ(curve.cursor(), std::optional<int>(3));
    EXPECT_EQ(curve.anchor(), std::optional<int>(1));
    EXPECT_TRUE(lane.selectAll(kDots, kLaneLike));
    EXPECT_EQ(lane.cursor(), std::optional<int>(1));
    EXPECT_FALSE(lane.selectAll({}, kLaneLike));
}

TEST(PointSelectionModel, RetainAndRekeyFollowTheDots) {
    PointSelectionModel<double> s;
    s.assign({1.0, 2.0, 4.0}, 4.0, 1.0);
    EXPECT_TRUE(s.retainOnly({1.0, 2.0}));
    EXPECT_EQ(s.selected(), (std::vector<double>{1.0, 2.0}));
    EXPECT_FALSE(s.cursor().has_value());
    EXPECT_FALSE(s.retainOnly({1.0, 2.0})) << "nothing went, no change";
    s.rekey([](double b) { return 10.0 - b; });
    EXPECT_EQ(s.selected(), (std::vector<double>{8.0, 9.0})) << "kept ascending";
    EXPECT_EQ(s.anchor(), std::optional<double>(9.0));
}

TEST(PointGroupMove, TheBlockStopsAtItsFirstLimitAndNeverBacksUp) {
    const std::vector<double> xs{0.0, 0.25, 0.5, 0.75, 1.0};
    const auto rooms = roomBetweenNeighbours(xs, {false, true, true, false, false}, 0.0, 1.0);
    ASSERT_EQ(rooms.size(), 2u);
    EXPECT_DOUBLE_EQ(clampGroupDelta(rooms, 1.0), 0.25) << "the leading dot stops at its unmoving neighbour";
    EXPECT_DOUBLE_EQ(clampGroupDelta(rooms, -1.0), -0.25);
    EXPECT_DOUBLE_EQ(clampGroupDelta({roomWithin(2.0, 0.0, std::numeric_limits<double>::infinity())}, -5.0), -2.0);
    EXPECT_DOUBLE_EQ(clampGroupDelta({roomWithin(1.5, 0.0, 1.0)}, -0.2), -0.2) << "outside its room: may still go in";
    EXPECT_DOUBLE_EQ(clampGroupDelta({roomWithin(1.5, 0.0, 1.0)}, 0.2), 0.0) << "but never further out";
    EXPECT_DOUBLE_EQ(clampGroupDelta({}, 3.0), 3.0);
}

TEST(PointClipboard, EntriesAreOffsetsFromTheEarliestDotAndLandFromAnAnchor) {
    const auto clip = makePointClip<int>({2.0, 3.5}, {0.1, 0.9}, {7, 8});
    ASSERT_EQ(clip.entries.size(), 2u);
    EXPECT_DOUBLE_EQ(clip.entries[1].offset, 1.5);
    EXPECT_EQ(clip.entries[1].extra, 8);
    EXPECT_EQ(clipPositions(clip, 10.0), (std::vector<double>{10.0, 11.5}));
    EXPECT_TRUE(makePointClip<int>({}, {}, {}).isEmpty());
}

TEST(PointEditorKeys, EveryKeyOfTheMapDecodesOnce) {
    using Kind = PointKeyAction::Kind;
    const auto decode = [](int code, int mods = 0) {
        return pointKeyAction(juce::KeyPress(code, juce::ModifierKeys(mods), 0));
    };
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;
    constexpr int cmd = juce::ModifierKeys::commandModifier;
    EXPECT_EQ(decode(juce::KeyPress::leftKey).kind, Kind::pick);
    EXPECT_EQ(decode(juce::KeyPress::leftKey).side, -1);
    EXPECT_EQ(decode(juce::KeyPress::rightKey, shift).kind, Kind::extend);
    EXPECT_EQ(decode(juce::KeyPress::upKey).kind, Kind::value);
    EXPECT_EQ(decode(juce::KeyPress::downKey, shift).size, PointKeyAction::Size::coarse);
    const auto fine = decode(juce::KeyPress::rightKey, alt | shift);
    EXPECT_EQ(fine.kind, Kind::move);
    EXPECT_TRUE(fine.time);
    EXPECT_EQ(fine.size, PointKeyAction::Size::fine);
    EXPECT_EQ(decode('A', cmd).kind, Kind::selectAll);
    EXPECT_EQ(decode('D', cmd).kind, Kind::duplicate);
    EXPECT_EQ(decode('D', cmd | shift).kind, Kind::none) << "a Shift chord stays with the app";
    EXPECT_EQ(decode('T', juce::ModifierKeys::ctrlModifier).kind, Kind::insert);
    EXPECT_EQ(decode(juce::KeyPress::backspaceKey).kind, Kind::remove);
    EXPECT_TRUE(decode(juce::KeyPress::escapeKey).plain);
    EXPECT_FALSE(decode(juce::KeyPress::escapeKey, shift).plain);
    EXPECT_EQ(decode(juce::KeyPress::leftKey, cmd).kind, Kind::none);
}
