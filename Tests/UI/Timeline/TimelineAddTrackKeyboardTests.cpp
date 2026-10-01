// TimelineAddTrackKeyboardTests.cpp
//
// "+ Track" as the last keyboard stop of the track-header column: Down from the last row (or from
// the panel root of an empty timeline) lands on it, Up goes back, Return/Space press it. Real focus
// needs a native peer, so the panel's record-focus test mode stands in for it while the keys go
// through the real handlers (a header's keyPressed, the panel's keyPressed).

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TrackHeaderHost;

namespace {

juce::KeyPress key(int code, int mods = juce::ModifierKeys::noModifiers) {
    return juce::KeyPress(code, juce::ModifierKeys(mods), 0);
}
juce::KeyPress upKey() { return key(juce::KeyPress::upKey); }
juce::KeyPress downKey() { return key(juce::KeyPress::downKey); }

struct StubHost : TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

struct AddTrackFixture {
    TimelineDoc doc;
    StubHost host;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;
    int clicks = 0;

    explicit AddTrackFixture(int trackCount) {
        panel.setSize(1200, 400);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setShortcutManager(nullptr);
        panel.setRecordFocusForTest(true);
        for (int i = 0; i < trackCount; ++i)
            doc.addTrack(TrackKind::Midi, "Track " + juce::String(i + 1));
        // Counts presses of the real button; the real handler opens an async menu a test never sees.
        panel.getAddTrackButton().onClick = [this] { ++clicks; };
    }

    // Down on the panel root lands on "+ Track", Down again seeds row 0; each further Down from a header
    // steps one row.
    void focusRow(int index) {
        ASSERT_TRUE(panel.handleRootFocusKey(downKey()));
        ASSERT_TRUE(panel.keyPressed(downKey()));
        for (int i = 0; i < index; ++i)
            ASSERT_TRUE(panel.getTrackHeaderAt(i)->keyPressed(downKey()));
    }
};

} // namespace

TEST(TimelineAddTrackKeyboard, DownFromTheLastRowFocusesAddTrack) {
    AddTrackFixture f(3);
    f.focusRow(2);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2);
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());

    ASSERT_TRUE(f.panel.getTrackHeaderAt(2)->keyPressed(downKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
}

TEST(TimelineAddTrackKeyboard, DownBetweenRowsStillStepsRows) {
    AddTrackFixture f(3);
    f.focusRow(1);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());
}

TEST(TimelineAddTrackKeyboard, UpFromAddTrackReturnsToTheLastRow) {
    AddTrackFixture f(3);
    f.focusRow(2);
    ASSERT_TRUE(f.panel.getTrackHeaderAt(2)->keyPressed(downKey()));
    ASSERT_TRUE(f.panel.isAddTrackButtonFocused());

    EXPECT_TRUE(f.panel.keyPressed(upKey()));
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2);
}

TEST(TimelineAddTrackKeyboard, DownFromTheRootLandsOnAddTrackFirstThenTheFirstTrack) {
    AddTrackFixture f(3);
    EXPECT_TRUE(f.panel.handleRootFocusKey(downKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), -1);

    EXPECT_TRUE(f.panel.keyPressed(downKey()));
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);
}

TEST(TimelineAddTrackKeyboard, FromTheTopUpReturnsToTheRootAndFromTheFirstRowUpReachesAddTrack) {
    AddTrackFixture f(3);
    ASSERT_TRUE(f.panel.handleRootFocusKey(downKey()));
    ASSERT_TRUE(f.panel.isAddTrackButtonFocused());
    EXPECT_TRUE(f.panel.keyPressed(upKey())) << "entered from the top, Up goes back to the panel root";
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());

    f.focusRow(0);
    ASSERT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);
    ASSERT_TRUE(f.panel.getTrackHeaderAt(0)->keyPressed(upKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused()) << "Up off the first row lands on + Track";
    EXPECT_TRUE(f.panel.keyPressed(downKey()));
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0) << "and Down from it is the first row again";
}

TEST(TimelineAddTrackKeyboard, AddTrackStaysReachableFromTheBottomAndUpThereReturnsToTheLastRow) {
    AddTrackFixture f(3);
    f.focusRow(2);
    ASSERT_TRUE(f.panel.getTrackHeaderAt(2)->keyPressed(downKey()));
    ASSERT_TRUE(f.panel.isAddTrackButtonFocused());
    EXPECT_TRUE(f.panel.keyPressed(downKey())) << "entered from the bottom, Down stays on the last stop";
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
    EXPECT_TRUE(f.panel.keyPressed(upKey()));
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2);
}

TEST(TimelineAddTrackKeyboard, EmptyTimelineRootDownLandsOnAddTrack) {
    AddTrackFixture f(0);
    EXPECT_TRUE(f.panel.handleRootFocusKey(downKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());

    EXPECT_TRUE(f.panel.keyPressed(upKey())) << "Up has nowhere to go but the panel root";
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());
}

TEST(TimelineAddTrackKeyboard, ReturnAndSpacePressTheButtonAndDownStaysPut) {
    AddTrackFixture f(2);
    f.focusRow(1);
    ASSERT_TRUE(f.panel.getTrackHeaderAt(1)->keyPressed(downKey()));

    // Button::triggerClick posts the click, so the loop is pumped before counting it.
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); };
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::returnKey)));
    pump();
    EXPECT_EQ(f.clicks, 1);
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::spaceKey))) << "Space is not left to toggle playback";
    pump();
    EXPECT_EQ(f.clicks, 2);
    EXPECT_TRUE(f.panel.keyPressed(downKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
}

TEST(TimelineAddTrackKeyboard, KeysAreIgnoredWhileAddTrackIsNotFocused) {
    AddTrackFixture f(2);
    EXPECT_FALSE(f.panel.handleAddTrackButtonKey(key(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.clicks, 0);
}

TEST(TimelineAddTrackKeyboard, ModifiedKeysFallThroughToTheAppShortcuts) {
    AddTrackFixture f(2);
    f.panel.focusAddTrackButton();
    EXPECT_FALSE(f.panel.handleAddTrackButtonKey(key(juce::KeyPress::spaceKey, juce::ModifierKeys::commandModifier)));
    EXPECT_FALSE(f.panel.handleAddTrackButtonKey(
        key('t', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)));
    EXPECT_EQ(f.clicks, 0);
}

// The menu's own close callback (showMenuAsync's result can't be driven headlessly): opened from the
// keyboard, focus comes back to "+ Track" whether the menu was dismissed or a pick was made, so the
// next Tab does not restart at the toolbar. Opened by mouse, focus is left where it was.
TEST(TimelineAddTrackKeyboard, ClosingTheMenuOpenedFromTheKeyboardRefocusesAddTrack) {
    AddTrackFixture f(2);
    f.panel.focusAddTrackButton();
    ASSERT_TRUE(f.panel.handleAddTrackButtonKey(upKey()));
    ASSERT_FALSE(f.panel.isAddTrackButtonFocused());

    f.panel.finishAddTrackMenu(0, false);
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused()) << "a mouse-opened menu leaves focus alone";

    f.panel.finishAddTrackMenu(0, true);
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused()) << "dismissed with Esc: back on + Track";
}

// Up/Down land on the rows themselves, so each row carries its track's name for a screen reader (it
// used to read as an unnamed group).
TEST(TimelineAddTrackKeyboard, EveryTrackRowIsNamedAfterItsTrack) {
    AddTrackFixture f(2);
    ASSERT_EQ(f.panel.getTrackHeaderCount(), 2);
    EXPECT_EQ(f.panel.getTrackHeaderAt(0)->getTitle(), "Track 1");
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getTitle(), "Track 2");
}
