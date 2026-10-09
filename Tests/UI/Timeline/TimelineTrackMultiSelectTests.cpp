// TimelineTrackMultiSelectTests.cpp
//
// Cmd/Ctrl-click and Shift-click select several tracks in the timeline's header column, and
// Shift+Up/Down and Cmd/Ctrl+Space do the same from the keyboard (docs/timeline/tracks.md#selecting-several-tracks).
// Clicks are real juce::MouseEvents with modifiers, sent to the component the header list's
// getComponentAt returns, so the whole route (hit test -> mouseDown -> panel -> model -> header look) runs.
// Selection is view state: none of it may touch the document or the undo history.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

struct StubHost : TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        ++editCalls;
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    int editCalls = 0;
};

constexpr int kTracks = 8;

struct MultiSelectFixture {
    TimelineDoc doc;
    StubHost host;
    synth::ui::TimelinePanelComponent panel;
    std::vector<TrackId> ids; // ids[0] is "track 1"

    MultiSelectFixture() {
        panel.setSize(1200, 600);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        for (int i = 0; i < kTracks; ++i)
            ids.push_back(doc.addTrack(TrackKind::Midi, "Track " + juce::String(i + 1)));
    }

    // The component a click on the row's empty background hits, found through the real hit test.
    TimelineTrackHeaderComponent* rowAt(int index) {
        auto* header = panel.getTrackHeaderAt(index);
        auto& list = panel.getTrackHeaderListForTest();
        auto* hit = list.getComponentAt(header->getX() + 1, header->getY() + header->getHeight() / 2);
        return dynamic_cast<TimelineTrackHeaderComponent*>(hit);
    }

    void click(int trackNumber, juce::ModifierKeys mods = {}) {
        auto* row = rowAt(trackNumber - 1);
        ASSERT_NE(row, nullptr) << "getComponentAt did not return the header for track " << trackNumber;
        const juce::Point<float> pos(1.0f, (float)row->getHeight() * 0.5f);
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), pos,
                           mods.withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, row,
                           row, now, pos, now, 1, false);
        row->mouseDown(e);
        row->mouseUp(e);
    }

    // Track numbers (1-based) of the selection, in the order selectedTracks() returned them.
    std::vector<int> selected() {
        std::vector<int> out;
        for (const auto id : panel.selectedTracks())
            out.push_back((int)(std::find(ids.begin(), ids.end(), id) - ids.begin()) + 1);
        return out;
    }

    bool lookSelected(int trackNumber) { return panel.getTrackHeaderAt(trackNumber - 1)->isSelected(); }
};

const juce::ModifierKeys kCmd(juce::ModifierKeys::commandModifier);
const juce::ModifierKeys kShift(juce::ModifierKeys::shiftModifier);
using Tracks = std::vector<int>;

} // namespace

TEST(TimelineTrackMultiSelectTest, PlainClickSelectsOneTrackAndResetsTheRest) {
    MultiSelectFixture f;
    f.click(2);
    EXPECT_EQ(f.selected(), Tracks({2}));
    f.click(5, kCmd);
    ASSERT_EQ(f.selected(), Tracks({2, 5}));
    f.click(3);
    EXPECT_EQ(f.selected(), Tracks({3})) << "a plain click resets to one";
    EXPECT_FALSE(f.lookSelected(2));
    EXPECT_FALSE(f.lookSelected(5));
    EXPECT_TRUE(f.lookSelected(3));
}

TEST(TimelineTrackMultiSelectTest, CmdClickAddsAndRemovesATrack) {
    MultiSelectFixture f;
    f.click(1);
    f.click(4, kCmd);
    EXPECT_EQ(f.selected(), Tracks({1, 4}));
    EXPECT_TRUE(f.lookSelected(1));
    EXPECT_TRUE(f.lookSelected(4));
    EXPECT_FALSE(f.lookSelected(2));
    f.click(4, kCmd);
    EXPECT_EQ(f.selected(), Tracks({1})) << "Cmd-click on a selected track drops it";
    EXPECT_FALSE(f.lookSelected(4));
}

TEST(TimelineTrackMultiSelectTest, ShiftClickExtendsARangeFromTheAnchorAndCmdClickDropsOneOut) {
    MultiSelectFixture f;
    f.click(1);
    f.click(4, kCmd);
    f.click(7, kShift);
    EXPECT_EQ(f.selected(), Tracks({1, 4, 5, 6, 7})) << "tracks 4 to 7 join, track 1 stays";
    f.click(5, kCmd);
    EXPECT_EQ(f.selected(), Tracks({1, 4, 6, 7})) << "track 5 drops out";
    EXPECT_FALSE(f.lookSelected(5));
}

TEST(TimelineTrackMultiSelectTest, ShiftClickRangeMovesWithTheClickAndWorksUpwards) {
    MultiSelectFixture f;
    f.click(5);
    f.click(7, kShift);
    EXPECT_EQ(f.selected(), Tracks({5, 6, 7}));
    f.click(3, kShift);
    EXPECT_EQ(f.selected(), Tracks({3, 4, 5})) << "the anchor stays at 5; the far end moves to 3";
}

TEST(TimelineTrackMultiSelectTest, SelectedTracksComeBackInTimelineOrderNotClickOrder) {
    MultiSelectFixture f;
    f.click(6);
    f.click(2, kCmd);
    f.click(4, kCmd);
    EXPECT_EQ(f.selected(), Tracks({2, 4, 6}));
    EXPECT_EQ(f.panel.getSelectedTrackId(), f.ids[3]) << "the primary track is the one clicked last";
}

TEST(TimelineTrackMultiSelectTest, SelectionSurvivesARelayoutAndARebuild) {
    MultiSelectFixture f;
    f.click(2);
    f.click(5, kCmd);
    f.panel.setSize(900, 500);
    f.panel.resized();
    EXPECT_EQ(f.selected(), Tracks({2, 5}));
    f.doc.addTrack(TrackKind::Midi, "Track 9"); // the header rows are synced for a changed track set
    EXPECT_EQ(f.selected(), Tracks({2, 5}));
    EXPECT_TRUE(f.lookSelected(2));
    EXPECT_TRUE(f.lookSelected(5));
}

TEST(TimelineTrackMultiSelectTest, ADeletedTrackLeavesTheSelection) {
    MultiSelectFixture f;
    f.click(2);
    f.click(5, kCmd);
    f.doc.removeTrack(f.ids[4]);
    EXPECT_EQ(f.selected(), Tracks({2}));
}

TEST(TimelineTrackMultiSelectTest, SelectingIsNotAnEditAndLeavesUndoAlone) {
    MultiSelectFixture f;
    const auto revision = f.doc.getRevision();
    f.click(1);
    f.click(4, kCmd);
    f.click(7, kShift);
    f.click(5, kCmd);
    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_EQ(f.host.editCalls, 0);
}

TEST(TimelineTrackMultiSelectTest, SingleTrackCommandsStillActOnThePrimaryTrack) {
    MultiSelectFixture f;
    f.click(2);
    f.click(5, kCmd);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 4);
    EXPECT_TRUE(f.panel.getTrackHeaderAt(4)->keyPressed(juce::KeyPress('m')));
    EXPECT_TRUE(f.doc.getTrack(f.ids[4])->muted);
    EXPECT_FALSE(f.doc.getTrack(f.ids[1])->muted);
}

TEST(TimelineTrackMultiSelectTest, ShiftArrowsExtendAndShrinkFromTheAnchor) {
    MultiSelectFixture f;
    f.click(3);
    const juce::KeyPress shiftDown(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0);
    const juce::KeyPress shiftUp(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0);
    EXPECT_TRUE(f.panel.getTrackHeaderAt(2)->keyPressed(shiftDown));
    EXPECT_EQ(f.selected(), Tracks({3, 4}));
    EXPECT_TRUE(f.panel.getTrackHeaderAt(3)->keyPressed(shiftDown));
    EXPECT_EQ(f.selected(), Tracks({3, 4, 5}));
    EXPECT_TRUE(f.panel.getTrackHeaderAt(4)->keyPressed(shiftUp));
    EXPECT_EQ(f.selected(), Tracks({3, 4})) << "reversing direction shrinks the range";
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 3);
}

TEST(TimelineTrackMultiSelectTest, PlainArrowResetsToTheRowItMovesTo) {
    MultiSelectFixture f;
    f.click(3);
    f.click(5, kShift);
    ASSERT_EQ(f.selected(), Tracks({3, 4, 5}));
    EXPECT_TRUE(f.panel.getTrackHeaderAt(4)->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.selected(), Tracks({6}));
}

TEST(TimelineTrackMultiSelectTest, CmdSpaceTogglesTheFocusedTrack) {
    MultiSelectFixture f;
    f.click(2);
    f.click(5, kCmd);
    const juce::KeyPress cmdSpace(juce::KeyPress::spaceKey, juce::ModifierKeys::commandModifier, 0);
    EXPECT_TRUE(f.panel.getTrackHeaderAt(4)->keyPressed(cmdSpace));
    EXPECT_EQ(f.selected(), Tracks({2}));
    EXPECT_TRUE(f.panel.getTrackHeaderAt(4)->keyPressed(cmdSpace));
    EXPECT_EQ(f.selected(), Tracks({2, 5}));
}

TEST(TimelineTrackMultiSelectTest, EachRowReportsItsSelectedStateToAccessibility) {
    MultiSelectFixture f;
    f.click(2);
    f.click(4, kCmd);
    // No native peer headlessly, so the component-owned handler is never attached; the row's own factory builds the
    // same handler the platform would, and it reads the row's state live.
    auto stateOf = [&](int n) {
        const auto handler = f.panel.getTrackHeaderAt(n - 1)->createAccessibilityHandler();
        return handler->getCurrentState();
    };
    EXPECT_TRUE(stateOf(2).isSelected());
    EXPECT_TRUE(stateOf(4).isSelected());
    EXPECT_FALSE(stateOf(3).isSelected());
    EXPECT_TRUE(stateOf(3).isSelectable());
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getTitle(), "Track 2") << "the title is the track name, unchanged";
}
