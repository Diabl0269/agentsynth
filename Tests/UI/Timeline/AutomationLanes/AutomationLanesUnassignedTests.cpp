// AutomationLanesUnassignedTests.cpp -- the Automation track (lanes no single track owns) is always
// last and draws as the "Unassigned automation" section: the doc keeps it last on every path, the
// panel draws it as a 26 px section row, a reorder drag can neither move it nor drop below it, and
// taking its last lane away removes it in the same undo step.

#include "AutomationLanesTestFixture.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

using namespace automation_lanes_test;
using synth::TrackKind;

namespace {
std::vector<TrackKind> kindsOf(const synth::TimelineDoc& doc) {
    std::vector<TrackKind> kinds;
    for (const auto& t : doc.getTracks())
        kinds.push_back(t.kind);
    return kinds;
}
} // namespace

TEST(AutomationTrackOrderTest, AddMoveAndLoadAllKeepTheAutomationTrackLast) {
    synth::TimelineDoc doc;
    const auto a = doc.addTrack(TrackKind::Midi, "A");
    const auto automation = doc.addTrack(TrackKind::Automation, "Automation");
    const auto b = doc.addTrack(TrackKind::Audio, "B");
    EXPECT_TRUE(b.isValid()) << "addTrack still returns the NEW track's id";
    EXPECT_EQ(doc.getTrack(b)->name, "B");
    EXPECT_EQ(kindsOf(doc), (std::vector<TrackKind>{TrackKind::Midi, TrackKind::Audio, TrackKind::Automation}));

    const auto revision = doc.getRevision();
    EXPECT_TRUE(doc.moveTrack(automation, 0));
    EXPECT_EQ(doc.getRevision(), revision) << "the Automation track never moves: no change, no bump";
    EXPECT_TRUE(doc.moveTrack(a, 2));
    EXPECT_EQ(kindsOf(doc), (std::vector<TrackKind>{TrackKind::Audio, TrackKind::Midi, TrackKind::Automation}))
        << "a track dropped past the end lands just above the Automation track";

    // A file written with the Automation track first still loads with it last.
    auto state = doc.toVar();
    auto* tracks = state.getProperty("tracks", {}).getArray();
    ASSERT_NE(tracks, nullptr);
    tracks->move(2, 0);
    synth::TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(state));
    EXPECT_EQ(kindsOf(loaded), (std::vector<TrackKind>{TrackKind::Audio, TrackKind::Midi, TrackKind::Automation}));
}

TEST(AutomationLanesUnassignedTest, TheAutomationTrackIsALastShortSectionRowThatStartsOpen) {
    LanesPanel f;
    f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto automation = f.doc.addTrack(TrackKind::Automation, "Automation");
    f.doc.addTrack(TrackKind::Midi, "Lead");
    const auto lane = f.addLane(automation, "cutoff");

    ASSERT_EQ(f.panel.getTrackHeaderCount(), 3);
    auto* section = f.panel.getTrackHeaderAt(2);
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->getTrackId(), automation);
    EXPECT_TRUE(section->isSectionHeader());
    EXPECT_EQ(section->getHeight(), synth::ui::TimelineAutomationLanes::kSectionRowHeight);
    EXPECT_EQ(section->getNameLabel().getText(), "Unassigned automation");
    EXPECT_FALSE(section->getMuteButton().isVisible());
    EXPECT_FALSE(section->getColourSwatch().isVisible());
    EXPECT_FALSE(section->getBindingChip().isVisible());
    EXPECT_FALSE(section->getChannelChip().isVisible());
    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(automation)) << "Unassigned starts open";
    EXPECT_NE(f.panel.laneEditorForTest(lane), nullptr);

    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    EXPECT_EQ(layout.trackRowHeight(2), 26);
    // The section row is no clip row: a click there is not the clip lanes' to answer.
    const int y = f.panel.getClipLaneArea().getY() + layout.trackTop(2) + 13;
    EXPECT_NE(f.componentAt({f.panel.getClipLaneArea().getX() + 50, y}), &f.panel.getClipLaneArea());

    clickButton(section->getFoldArrow());
    EXPECT_FALSE(f.panel.isTrackAutomationExpandedForTest(automation));
    EXPECT_EQ(f.panel.laneEditorForTest(lane), nullptr);
}

TEST(AutomationLanesUnassignedTest, ReorderDragCanNotMoveTheSectionOrDropATrackBelowIt) {
    LanesPanel f;
    f.panel.setSize(1200, 700);
    const auto a = f.doc.addTrack(TrackKind::Midi, "A");
    f.doc.addTrack(TrackKind::Midi, "B");
    const auto automation = f.doc.addTrack(TrackKind::Automation, "Automation");
    f.addLane(automation, "cutoff");
    auto& list = f.panel.getTrackHeaderListForTest();

    // Drag A far past the bottom of the list.
    {
        auto* row = f.panel.getTrackHeaderAt(0);
        const int pressY = row->getY() + 10;
        auto local = [&](int listY) { return row->getLocalPoint(&list, juce::Point<float>(3.0f, (float)listY)); };
        row->mouseDown(makeClickEvent(*row, local(pressY)));
        for (int y = pressY; y <= pressY + 400; y += 20)
            row->mouseDrag(makeDragEvent(*row, local(y), local(pressY)));
        row->mouseUp(makeClickEvent(*row, local(pressY + 400)));
    }
    EXPECT_EQ(f.indexOf(a), 1) << "A lands last among the tracks, above the section";
    EXPECT_EQ(f.indexOf(automation), 2);

    // Drag the section itself upward: nothing happens.
    {
        auto* row = f.panel.getTrackHeaderAt(2);
        ASSERT_TRUE(row->isSectionHeader());
        const int pressY = row->getY() + 5;
        auto local = [&](int listY) { return row->getLocalPoint(&list, juce::Point<float>(3.0f, (float)listY)); };
        row->mouseDown(makeClickEvent(*row, local(pressY)));
        for (int y = pressY; y >= 0; y -= 20)
            row->mouseDrag(makeDragEvent(*row, local(y), local(pressY)));
        row->mouseUp(makeClickEvent(*row, local(0)));
    }
    EXPECT_EQ(f.indexOf(automation), 2);
    EXPECT_FALSE(f.panel.isTrackReorderActiveForTest());
}

TEST(AutomationLanesUnassignedTest, DeletingItsLastLaneRemovesTheSectionInTheSameUndoStep) {
    LanesPanel f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto automation = f.doc.addTrack(TrackKind::Automation, "Automation");
    const auto lane = f.addLane(automation, "cutoff");
    ASSERT_EQ(f.panel.getTrackHeaderCount(), 2);

    auto* header = f.panel.laneHeaderForTest(lane);
    ASSERT_NE(header, nullptr);
    auto menu = header->buildMenu();
    header->applyMenuChoice(findMenuItem(menu, "Delete lane")->itemID);

    EXPECT_EQ(f.doc.getTrack(automation), nullptr) << "an empty Unassigned section goes";
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 1);
    f.undo.undo();
    ASSERT_NE(f.doc.getTrack(automation), nullptr) << "one undo brings the track and its lane back";
    EXPECT_NE(f.doc.getLane(lane), nullptr);
    EXPECT_EQ(f.indexOf(automation), 1);

    // Moving the last lane onto a track empties it the same way.
    ASSERT_TRUE(synth::ui::moveLaneUndoable(f.doc, &f.undo, lane, bass));
    EXPECT_EQ(f.doc.getTrack(automation), nullptr);
    f.undo.undo();
    EXPECT_NE(f.doc.getTrack(automation), nullptr);
    EXPECT_EQ(f.doc.getTrackForLane(lane)->id, automation);
}
