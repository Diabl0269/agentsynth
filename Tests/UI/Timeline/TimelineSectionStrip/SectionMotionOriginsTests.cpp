// SectionMotionOriginsTests.cpp -- the one origin rule for section blocks, pair by pair: a forward edit grows each
// block from the side it came from, and its undo shrinks it back toward that same side; a delete shrinks toward the
// middle and its undo and redo grow from and shrink to the middle again.
#include "UI/Timeline/TimelineSectionStrip/SectionMotionOrigins.h"
#include <gtest/gtest.h>

using synth::ui::MotionOrigin;
using synth::ui::PlacementSpan;
using synth::ui::SectionMotionOrigins;

namespace {

constexpr std::int64_t kSec1 = 1, kSec2 = 2;

// Section 1 at [0,16) (id 10) and Section 2 at [16,32) (id 20).
std::vector<PlacementSpan> twoSections() { return {{10, kSec1, 0.0, 16.0}, {20, kSec2, 16.0, 32.0}}; }

// Cmd+D on Section 1: the copy (id 11) right after it pushes Section 2 later.
std::vector<PlacementSpan> afterCopy() {
    return {{10, kSec1, 0.0, 16.0}, {11, kSec1, 16.0, 32.0}, {20, kSec2, 32.0, 48.0}};
}

MotionOrigin grewFrom(const SectionMotionOrigins::Moves& moves, std::int64_t id) { return moves.grows.at(id).origin; }
MotionOrigin shrankTo(const SectionMotionOrigins::Moves& moves, std::int64_t id) { return moves.exits.at(id).origin; }

} // namespace

TEST(SectionMotionOrigins, ACopyAfterItsOriginalGrowsFromTheEdgeTouchingItAndItsUndoShrinksBackIntoThatEdge) {
    SectionMotionOrigins origins;
    const auto copy = origins.diff(twoSections(), afterCopy(), false);
    ASSERT_EQ(copy.grows.size(), 1u);
    EXPECT_EQ(grewFrom(copy, 11), MotionOrigin::Left);
    EXPECT_DOUBLE_EQ(copy.grows.at(11).beat, 16.0) << "anchored on the original's right edge";

    const auto undo = origins.diff(afterCopy(), twoSections(), true);
    ASSERT_EQ(undo.exits.size(), 1u);
    EXPECT_EQ(shrankTo(undo, 11), MotionOrigin::Left);
    EXPECT_DOUBLE_EQ(undo.exits.at(11).beat, 16.0);

    const auto redo = origins.diff(twoSections(), afterCopy(), true);
    EXPECT_EQ(grewFrom(redo, 11), MotionOrigin::Left);
    EXPECT_DOUBLE_EQ(redo.grows.at(11).beat, 16.0);
}

TEST(SectionMotionOrigins, ACopyDroppedJustBeforeItsOriginalGrowsFromItsRightEdge) {
    SectionMotionOrigins origins;
    const std::vector<PlacementSpan> before{{10, kSec1, 16.0, 32.0}};
    const std::vector<PlacementSpan> after{{11, kSec1, 0.0, 16.0}, {10, kSec1, 16.0, 32.0}};
    const auto copy = origins.diff(before, after, false);
    EXPECT_EQ(grewFrom(copy, 11), MotionOrigin::Right);
    EXPECT_DOUBLE_EQ(copy.grows.at(11).beat, 16.0);
    EXPECT_EQ(shrankTo(origins.diff(after, before, true), 11), MotionOrigin::Right);
}

TEST(SectionMotionOrigins, ADeleteShrinksToTheMiddleAndItsUndoAndRedoGrowFromAndShrinkToTheMiddle) {
    SectionMotionOrigins origins;
    // Deleting the copy (not undoing it): even beside its original, a delete goes to the middle.
    const auto del = origins.diff(afterCopy(), twoSections(), false);
    EXPECT_EQ(shrankTo(del, 11), MotionOrigin::Centre);
    EXPECT_DOUBLE_EQ(del.exits.at(11).beat, 24.0) << "the block's own middle";

    const auto undo = origins.diff(twoSections(), afterCopy(), true);
    EXPECT_EQ(grewFrom(undo, 11), MotionOrigin::Centre) << "it comes back from where it went";
    EXPECT_DOUBLE_EQ(undo.grows.at(11).beat, 24.0);

    const auto redo = origins.diff(afterCopy(), twoSections(), true);
    EXPECT_EQ(shrankTo(redo, 11), MotionOrigin::Centre);
}

TEST(SectionMotionOrigins, AnOptionDragCopyAwayFromItsOriginalGrowsFromTheMiddleAndBesideItFromTheTouchingEdge) {
    SectionMotionOrigins origins;
    std::vector<PlacementSpan> away = twoSections();
    away.push_back({12, kSec1, 48.0, 64.0}); // after Section 2, not touching Section 1
    const auto copy = origins.diff(twoSections(), away, false);
    EXPECT_EQ(grewFrom(copy, 12), MotionOrigin::Centre);
    EXPECT_EQ(shrankTo(origins.diff(away, twoSections(), true), 12), MotionOrigin::Centre);

    const auto beside = origins.diff(twoSections(), afterCopy(), false);
    EXPECT_EQ(grewFrom(beside, 11), MotionOrigin::Left);
}

TEST(SectionMotionOrigins, AGroupCopyGrowsAsOneRunFromTheGroupsEdgeAndShrinksBackIntoIt) {
    SectionMotionOrigins origins;
    const auto before = twoSections();
    // Both selected, Cmd+D: copies of Section 1 and Section 2 after Section 2.
    const std::vector<PlacementSpan> after{
        {10, kSec1, 0.0, 16.0}, {20, kSec2, 16.0, 32.0}, {11, kSec1, 32.0, 48.0}, {21, kSec2, 48.0, 64.0}};
    const auto copy = origins.diff(before, after, false);
    for (const auto id : {11, 21}) {
        EXPECT_EQ(grewFrom(copy, id), MotionOrigin::Left) << id;
        EXPECT_DOUBLE_EQ(copy.grows.at(id).beat, 32.0) << "the whole run is anchored on the group's edge";
    }
    const auto undo = origins.diff(after, before, true);
    for (const auto id : {11, 21}) {
        EXPECT_EQ(shrankTo(undo, id), MotionOrigin::Left) << id;
        EXPECT_DOUBLE_EQ(undo.exits.at(id).beat, 32.0);
    }
}

TEST(SectionMotionOrigins, AMakeDragGrowsFromItsAnchorAndAMenuMakeFromTheMiddle) {
    SectionMotionOrigins origins;
    const std::vector<PlacementSpan> before = twoSections();
    std::vector<PlacementSpan> after = before;
    after.push_back({30, 3, 32.0, 40.0});
    EXPECT_EQ(grewFrom(origins.diff(before, after, false, MotionOrigin::Right), 30), MotionOrigin::Right);
    EXPECT_DOUBLE_EQ(origins.diff(before, after, false, MotionOrigin::Right).grows.at(30).beat, 40.0);
    EXPECT_EQ(grewFrom(origins.diff(before, after, false), 30), MotionOrigin::Centre) << "a new section has no source";
}

TEST(SectionMotionOrigins, EveryGrowOriginIsTheOriginItsReverseShrinksTo) {
    struct Pair {
        const char* name;
        std::vector<PlacementSpan> from, to;
        bool forwardAdds;
    };
    std::vector<PlacementSpan> away = twoSections();
    away.push_back({12, kSec1, 48.0, 64.0});
    const std::vector<Pair> pairs{
        {"copy / undo", twoSections(), afterCopy(), true},
        {"delete / undo", afterCopy(), twoSections(), false},
        {"drag copy away / undo", twoSections(), away, true},
        {"group copy / undo",
         twoSections(),
         {{10, kSec1, 0.0, 16.0}, {20, kSec2, 16.0, 32.0}, {11, kSec1, 32.0, 48.0}, {21, kSec2, 48.0, 64.0}},
         true},
    };
    for (const auto& pair : pairs) {
        SectionMotionOrigins origins;
        const auto forward = origins.diff(pair.from, pair.to, false);
        const auto undo = origins.diff(pair.to, pair.from, true);
        const auto redo = origins.diff(pair.from, pair.to, true);
        const auto& forwardSide = pair.forwardAdds ? forward.grows : forward.exits;
        const auto& undoSide = pair.forwardAdds ? undo.exits : undo.grows;
        const auto& redoSide = pair.forwardAdds ? redo.grows : redo.exits;
        ASSERT_FALSE(forwardSide.empty()) << pair.name;
        for (const auto& [id, anchor] : forwardSide) {
            ASSERT_EQ(undoSide.count(id), 1u) << pair.name;
            EXPECT_EQ(undoSide.at(id).origin, anchor.origin) << pair.name << ": the undo is the edit backwards";
            EXPECT_DOUBLE_EQ(undoSide.at(id).beat, anchor.beat) << pair.name;
            EXPECT_EQ(redoSide.at(id).origin, anchor.origin) << pair.name << ": the redo is the edit again";
        }
    }
}

TEST(SectionMotionOrigins, AnUndoWithNoRememberedEditFallsBackToTheCopyRule) {
    SectionMotionOrigins origins;
    const auto undo = origins.diff(afterCopy(), twoSections(), true);
    EXPECT_EQ(shrankTo(undo, 11), MotionOrigin::Left) << "it sits after a block of its section: it shrinks into it";
    origins.clear();
    EXPECT_EQ(shrankTo(origins.diff(afterCopy(), twoSections(), true), 11), MotionOrigin::Left);
}

TEST(SectionMotionOrigins, TheMovesFindTheBlockThatHoldsABeat) {
    SectionMotionOrigins origins;
    const auto copy = origins.diff(twoSections(), afterCopy(), false);
    ASSERT_NE(copy.growAt(20.0), nullptr);
    EXPECT_DOUBLE_EQ(copy.growAt(16.0)->beat, 16.0);
    EXPECT_EQ(copy.growAt(32.0), nullptr) << "Section 2 only moved";
    EXPECT_EQ(copy.exitAt(20.0), nullptr);
}
