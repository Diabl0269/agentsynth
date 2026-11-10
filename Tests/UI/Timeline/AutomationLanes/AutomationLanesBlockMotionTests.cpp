// AutomationLanesBlockMotionTests.cpp -- a lane block that arrives in or leaves an open track (Duplicate, Move to
// track, Delete lane, their undo) grows from nothing or shrinks away instead of jumping, and so does a modulator row
// that is added or removed: the track's extra height, and so the track below, pass through every height in between.
// Under Reduce Motion everything lands at once. Headless: the animated path is forced (FadeAnimateGuard) and the
// tweens are stepped by hand, never by frames.

#include "../../../TestWait.h"

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesMenuFixture.h"
#include "AutomationLanesModulatorHost.h"
#include <algorithm>
#include <vector>

using namespace lane_menu_test;
using namespace modulator_rows_test;
using synth::TrackKind;
using synth::ui::AutomationLaneHeaderComponent;

namespace {

// MenuPanel ("Bass" with an open cutoff lane) plus an open "Lead" track below it with one lane of its own.
struct BlockScene : MenuPanel {
    synth::TrackId lead;
    synth::LaneId leadLane;

    BlockScene() {
        lead = doc.addTrack(TrackKind::Midi, "Lead");
        leadLane = addLane(lead, "detune");
        panel.setTrackAutomationExpanded(lead, true);
    }

    synth::ui::TimelineAutomationLanes& lanes() { return panel.getAutomationLanes(); }
    int extra(synth::TrackId track) { return panel.getClipLaneArea().getRowLayout().trackExtraHeight(indexOf(track)); }
    int headerY(synth::TrackId track) { return panel.getTrackHeaderAt(indexOf(track))->getY(); }
    int laneHeight() { return lanes().laneRowHeight(lane); }

    void duplicate() {
        PickerCapture capture;
        header(lane)->applyMenuChoice(AutomationLaneHeaderComponent::kDuplicateMenuId);
        ASSERT_NE(capture.picker, nullptr);
        capture.picker->chooseVisibleItemForTest(0); // Resonance
    }
    // Opens the lane menu (which captures the move targets) and picks `trackName` under Move to track.
    void moveTo(synth::LaneId id, const juce::String& trackName) {
        MenuCapture capture;
        header(id)->showMenuAt(juce::PopupMenu::Options());
        const auto* item = findMenuItem(capture.menu, trackName);
        ASSERT_NE(item, nullptr);
        header(id)->applyMenuChoice(item->itemID);
    }
};

// Asserts `value` is strictly between `a` and `b` (either order).
void expectBetween(int value, int a, int b, const char* what) {
    EXPECT_GT(value, std::min(a, b)) << what;
    EXPECT_LT(value, std::max(a, b)) << what;
}

} // namespace

TEST(AutomationLanesBlockMotionTest, ADuplicatedLaneGrowsInAndTheTrackBelowGlidesDown) {
    BlockScene f;
    FadeAnimateGuard animate;
    const int one = f.extra(f.track);
    const int leadY = f.headerY(f.lead);
    ASSERT_GT(one, 0);

    f.duplicate();
    ASSERT_EQ(f.doc.getTrack(f.track)->lanes.size(), 2u);
    const auto copy = f.doc.getTrack(f.track)->lanes[1].id;
    EXPECT_TRUE(f.lanes().isBlockMotionAnimating());
    EXPECT_EQ(f.extra(f.track), one) << "the copy starts with no height";
    EXPECT_EQ(f.headerY(f.lead), leadY) << "nothing below has moved yet";

    f.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(f.extra(f.track), one, 2 * one, "the block is part way grown");
    expectBetween(f.headerY(f.lead), leadY, leadY + one, "the track below glides with it");
    EXPECT_LT(f.header(copy)->getHeight(), f.laneHeight()) << "the copy's header shows what fits so far";

    f.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_FALSE(f.lanes().isBlockMotionAnimating());
    EXPECT_EQ(f.extra(f.track), 2 * one);
    EXPECT_EQ(f.headerY(f.lead), leadY + one);
    EXPECT_EQ(f.header(copy)->getHeight(), f.laneHeight());
}

TEST(AutomationLanesBlockMotionTest, ADeletedLaneShrinksAwayInItsPlaceThenItsPictureGoes) {
    BlockScene f;
    FadeAnimateGuard animate;
    const auto second = f.addLane(f.track, "res");
    f.lanes().stepBlockMotionForTest(1.0f); // a lane added to an open track grows in too: land it first
    const int two = f.extra(f.track);
    const int one = two / 2;
    const int leadY = f.headerY(f.lead);

    f.header(f.lane)->applyMenuChoice(AutomationLaneHeaderComponent::kDeleteLaneMenuId);
    ASSERT_EQ(f.doc.getLane(f.lane), nullptr);
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 1);
    EXPECT_EQ(f.extra(f.track), two) << "the leaving block still holds its height";
    EXPECT_EQ(f.header(second)->getY(), f.headerY(f.track) + f.panel.getTrackHeaderAt(0)->getHeight() + one)
        << "the lane below stays under the leaving block, which kept its place";

    f.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(f.extra(f.track), one, two, "the gap is closing");
    expectBetween(f.headerY(f.lead), leadY - one, leadY, "the track below glides up");

    f.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 0) << "the picture is dropped once the gap has closed";
    EXPECT_EQ(f.extra(f.track), one);
    EXPECT_EQ(f.headerY(f.lead), leadY - one);
}

TEST(AutomationLanesBlockMotionTest, MovingALaneToAnotherTrackShrinksItHereAndGrowsItThere) {
    BlockScene f;
    FadeAnimateGuard animate;
    f.addLane(f.track, "res");
    f.lanes().stepBlockMotionForTest(1.0f);
    const int bassBefore = f.extra(f.track);
    const int leadBefore = f.extra(f.lead);
    const int one = f.laneHeight();

    f.moveTo(f.lane, "Lead");
    ASSERT_EQ(f.doc.getTrackForLane(f.lane)->id, f.lead);
    EXPECT_EQ(f.extra(f.track), bassBefore);
    EXPECT_EQ(f.extra(f.lead), leadBefore);

    f.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(f.extra(f.track), bassBefore - one, bassBefore, "the old track's gap is closing");
    expectBetween(f.extra(f.lead), leadBefore, leadBefore + one, "the new track's block is growing");

    f.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(f.extra(f.track), bassBefore - one);
    EXPECT_EQ(f.extra(f.lead), leadBefore + one);
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 0);
}

TEST(AutomationLanesBlockMotionTest, UndoOfADuplicateShrinksTheCopyAwayAndRedoGrowsItBack) {
    BlockScene f;
    FadeAnimateGuard animate;
    const int one = f.extra(f.track);
    f.duplicate();
    f.lanes().stepBlockMotionForTest(1.0f);
    ASSERT_EQ(f.extra(f.track), 2 * one);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 1) << "only the copy leaves; the source lane is untouched";
    f.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(f.extra(f.track), one, 2 * one, "the copy is shrinking");
    f.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(f.extra(f.track), one);

    ASSERT_TRUE(f.undo.redo());
    EXPECT_TRUE(f.lanes().isBlockMotionAnimating());
    f.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(f.extra(f.track), one, 2 * one, "the copy grows back");
    f.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(f.extra(f.track), 2 * one);
}

TEST(AutomationLanesBlockMotionTest, UnderReduceMotionALaneArrivesAndLeavesAtOnce) {
    BlockScene f;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    const int one = f.extra(f.track);

    f.duplicate();
    EXPECT_FALSE(f.lanes().isBlockMotionAnimating());
    EXPECT_EQ(f.extra(f.track), 2 * one);

    f.header(f.lane)->applyMenuChoice(AutomationLaneHeaderComponent::kDeleteLaneMenuId);
    EXPECT_FALSE(f.lanes().isBlockMotionAnimating());
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 0);
    EXPECT_EQ(f.extra(f.track), one);
}

TEST(AutomationLanesBlockMotionTest, OpeningOrFoldingATrackIsNotAnArrivalOrALeaving) {
    BlockScene f;
    FadeAnimateGuard animate;
    f.panel.setTrackAutomationExpanded(f.track, false);
    EXPECT_FALSE(f.lanes().isBlockMotionAnimating());
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 0);
    f.panel.setTrackAutomationExpanded(f.track, true);
    EXPECT_FALSE(f.lanes().isBlockMotionAnimating());
}

TEST(AutomationLanesBlockMotionTest, AProjectLoadReplacingTheDocLandsAtOnce) {
    BlockScene f;
    FadeAnimateGuard animate;
    const int one = f.extra(f.track);
    const auto saved = f.doc.toVar(); // a project with one lane on Bass
    f.addLane(f.track, "res");
    f.lanes().stepBlockMotionForTest(1.0f);
    ASSERT_EQ(f.extra(f.track), 2 * one);

    ASSERT_TRUE(f.doc.fromVar(saved)); // what a project load does to the live doc
    EXPECT_FALSE(f.lanes().isBlockMotionAnimating());
    EXPECT_EQ(f.lanes().leavingLaneCountForTest(), 0);
    EXPECT_EQ(f.extra(f.track), one);
}

//==============================================================================
// Modulator rows

namespace {

// "Bass" with an open cutoff lane routed from LFO 1, and an open "Lead" track below.
struct RowScene {
    HostedLanes h;
    synth::TrackId bass, lead;
    synth::LaneId cutoff;

    RowScene() {
        h.host.modulators.push_back(lfoInfo("1"));
        bass = h.f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = h.f.addLane(bass, "cutoff");
        lead = h.f.doc.addTrack(TrackKind::Midi, "Lead");
        h.f.panel.setTrackAutomationExpanded(bass, true);
    }
    LanesPanel& f() { return h.f; }
    synth::ui::TimelineAutomationLanes& lanes() { return f().panel.getAutomationLanes(); }
    int extra() { return f().panel.getClipLaneArea().getRowLayout().trackExtraHeight(0); }
    int leadY() { return f().panel.getTrackHeaderAt(1)->getY(); }
};

} // namespace

TEST(AutomationLanesBlockMotionTest, AnAddedModulatorRowGrowsInAndTheLaneBlockFollows) {
    RowScene s;
    FadeAnimateGuard animate;
    const int before = s.extra();
    const int leadY = s.leadY();
    const int full = s.lanes().modulatorRowHeight(s.cutoff, 0);

    s.h.host.modulators.push_back(lfoInfo("2"));
    s.f().panel.refreshModulators();
    ASSERT_EQ(s.lanes().modulatorCount(s.cutoff), 2);
    EXPECT_EQ(s.lanes().modulatorRowHeightFor(s.cutoff, 1), 0) << "the new row starts with no height";
    EXPECT_EQ(s.extra(), before);

    s.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(s.lanes().modulatorRowHeightFor(s.cutoff, 1), 0, full, "the row is growing");
    expectBetween(s.extra(), before, before + full, "the block grows with it");
    expectBetween(s.leadY(), leadY, leadY + full, "the track below glides");

    s.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(s.lanes().modulatorRowHeightFor(s.cutoff, 1), full);
    EXPECT_EQ(s.extra(), before + full);
    EXPECT_EQ(s.leadY(), leadY + full);
}

TEST(AutomationLanesBlockMotionTest, ARemovedModulatorRowFadesInAGapThatClosesThenIsDropped) {
    RowScene s;
    FadeAnimateGuard animate;
    s.h.host.modulators.push_back(lfoInfo("2"));
    s.f().panel.refreshModulators();
    s.lanes().stepBlockMotionForTest(1.0f);
    const int before = s.extra();
    const int full = s.lanes().modulatorRowHeight(s.cutoff, 0);
    const auto secondY = s.lanes().modulatorRowFor(s.cutoff, 1)->getY();

    s.h.host.modulators.erase(s.h.host.modulators.begin()); // LFO 1 goes; LFO 2 was below it
    s.f().panel.refreshModulators();
    ASSERT_EQ(s.lanes().modulatorCount(s.cutoff), 1);
    EXPECT_EQ(s.lanes().leavingModulatorCountForTest(), 1);
    EXPECT_EQ(s.extra(), before) << "the leaving row still holds its height";
    EXPECT_EQ(s.lanes().modulatorRowFor(s.cutoff, 0)->getY(), secondY) << "the row below has not jumped up";

    s.lanes().stepBlockMotionForTest(0.5f);
    expectBetween(s.extra(), before - full, before, "the gap is closing");
    expectBetween(s.lanes().modulatorRowFor(s.cutoff, 0)->getY(), secondY - full, secondY, "the row below slides up");

    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    s.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(s.lanes().leavingModulatorCountForTest(), 0);
    EXPECT_EQ(s.extra(), before - full);
    EXPECT_EQ(s.lanes().modulatorRowFor(s.cutoff, 0)->getY(), secondY - full);
}

TEST(AutomationLanesBlockMotionTest, UnderReduceMotionAModulatorRowArrivesAndLeavesAtOnce) {
    RowScene s;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    const int before = s.extra();
    const int full = s.lanes().modulatorRowHeight(s.cutoff, 0);

    s.h.host.modulators.push_back(lfoInfo("2"));
    s.f().panel.refreshModulators();
    EXPECT_FALSE(s.lanes().isBlockMotionAnimating());
    EXPECT_EQ(s.extra(), before + full);

    s.h.host.modulators.pop_back();
    s.f().panel.refreshModulators();
    EXPECT_FALSE(s.lanes().isBlockMotionAnimating());
    EXPECT_EQ(s.extra(), before);
}

// Regression test for FRO1579: a removed row used to stay a live ModulatorRow, shrunk with its gap, and every frame
// laid its controls out again (the combos squashed to nothing, the Sync toggle pushed below the row); on screen those
// frames were left behind under the lane. Now the live row goes at once and a still picture of it closes with the gap:
// cropped by it, never squashed, with nothing inside it laid out again.
TEST(AutomationLanesBlockMotionTest, ARemovedModulatorRowLeavesAsAStillPictureCroppedByItsGap) {
    RowScene s;
    FadeAnimateGuard animate;
    s.h.host.modulators.push_back(lfoInfo("2"));
    s.f().panel.refreshModulators();
    s.lanes().stepBlockMotionForTest(1.0f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    auto* liveRow = s.lanes().modulatorRowFor(s.cutoff, 1);
    ASSERT_NE(liveRow, nullptr);
    auto* column = liveRow->getParentComponent();
    ASSERT_NE(column, nullptr);
    const auto stood = liveRow->getBounds();
    const juce::Component::SafePointer<juce::Component> row(liveRow);
    std::vector<juce::Component*> others; // what the column held besides the row
    for (auto* child : column->getChildren())
        if (child != liveRow)
            others.push_back(child);

    s.h.host.modulators.pop_back(); // LFO 2, the bottom row, goes
    s.f().panel.refreshModulators();
    ASSERT_EQ(s.lanes().leavingModulatorCountForTest(), 1);
    EXPECT_EQ(row.getComponent(), nullptr) << "the live row and its controls go at once";
    const auto* picture = s.lanes().leavingModulatorForTest(0);
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->getParentComponent(), column);
    EXPECT_EQ(picture->getBounds(), stood) << "the picture stands where the row stood";
    EXPECT_EQ(picture->getNumChildComponents(), 0) << "a still picture: nothing in it is laid out again";

    s.lanes().stepBlockMotionForTest(0.5f);
    EXPECT_EQ(picture->getY(), stood.getY()) << "the gap closes from the bottom";
    expectBetween(picture->getHeight(), 0, stood.getHeight(), "the picture is cropped by the closing gap");

    s.lanes().stepBlockMotionForTest(1.0f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(s.lanes().leavingModulatorCountForTest(), 0);
    EXPECT_LE(column->getNumChildComponents(), (int)others.size()) << "nothing is added to the column";
    for (auto* child : column->getChildren())
        EXPECT_NE(std::find(others.begin(), others.end(), child), others.end()) << "nothing of the row is left";
}

// Regression test for FRO1472: under Reduce Motion a removed row only fades, in place. Once the fade ends the row is
// hidden and deleted on the next message-loop turn; it used to stay a hidden child of the header column until the
// next routing change.
TEST(AutomationLanesBlockMotionTest, UnderReduceMotionARemovedRowIsDeletedOnceItsFadeEnds) {
    RowScene s;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    s.h.host.modulators.push_back(lfoInfo("2"));
    s.f().panel.refreshModulators();
    auto* leavingRow = s.lanes().modulatorRowFor(s.cutoff, 1);
    ASSERT_NE(leavingRow, nullptr);
    auto* column = leavingRow->getParentComponent();
    ASSERT_NE(column, nullptr);
    const auto stood = leavingRow->getBounds();

    s.h.host.modulators.pop_back();
    s.f().panel.refreshModulators();
    ASSERT_EQ(s.lanes().leavingModulatorCountForTest(), 1);
    const juce::Component::SafePointer<juce::Component> picture(
        const_cast<juce::Component*>(s.lanes().leavingModulatorForTest(0)));
    ASSERT_NE(picture.getComponent(), nullptr);
    EXPECT_EQ(picture->getParentComponent(), column);
    EXPECT_TRUE(picture->isVisible()) << "the row's picture fades where it stood";
    EXPECT_EQ(picture->getBounds(), stood);

    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    ASSERT_NE(picture.getComponent(), nullptr) << "never deleted inside the fade's own callback";
    EXPECT_FALSE(picture->isVisible());
    ASSERT_TRUE(synth::test::flushMessageQueue());
    EXPECT_EQ(s.lanes().leavingModulatorCountForTest(), 0);
    EXPECT_EQ(picture.getComponent(), nullptr);
}
