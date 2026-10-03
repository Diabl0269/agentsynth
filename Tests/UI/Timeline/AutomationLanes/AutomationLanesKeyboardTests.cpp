// AutomationLanesKeyboardTests.cpp -- Up/Down through a track's open lanes: track row -> lane headers (each followed by
// its modulator rows) -> next track row, and back; folded lanes are skipped; the header's own combo keeps its arrows
// and Cmd+Alt+Up/Down still moves the lane; screen readers get each lane's name. Key events go through the focused
// component's own keyPressed, as the key dispatcher does (a headless test has no native focus, so the panel's model,
// getKeyboardLaneStopForTest(), says where focus went).

#include "AutomationLanesMenuFixture.h"
#include "ShortcutManager/ShortcutManager.h"

using namespace lane_menu_test;
using namespace automation_lanes_test;
using synth::ui::AutomationLaneHeaderComponent;
using synth::ui::ModulatorInfo;

namespace {

juce::KeyPress plainKey(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress downKey() { return plainKey(juce::KeyPress::downKey); }
juce::KeyPress upKey() { return plainKey(juce::KeyPress::upKey); }

// One LFO on every lane that is not itself a modulator's amount lane.
struct ModulatingHost : PickHost {
    std::vector<ModulatorInfo> modulators;
    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String&) override {
        return uuid.startsWith("atten-") ? std::vector<ModulatorInfo>{} : modulators;
    }
    bool canModulate(const juce::String&, const juce::String&) override { return true; }
};

// "Bass" with lanes cutoff and res, "Lead" with one lane (pitch), "Pad" with none; all expanded.
struct KeyboardPanel : LanesPanel {
    ModulatingHost host;
    synth::TrackId bass, lead, pad;
    synth::LaneId cutoff, res, pitch;

    KeyboardPanel() {
        bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lead = doc.addTrack(synth::TrackKind::Midi, "Lead");
        pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
        cutoff = addLane(bass, "cutoff");
        res = addLane(bass, "res");
        pitch = addLane(lead, "pitch");
        panel.setTrackHeaderHost(&host);
        panel.setRecordFocusForTest(true);
        panel.setTrackAutomationExpanded(bass, true);
        panel.setTrackAutomationExpanded(lead, true);
    }
    ~KeyboardPanel() { panel.setTrackHeaderHost(nullptr); }

    AutomationLaneHeaderComponent& header(synth::LaneId id) { return *panel.laneHeaderForTest(id); }
    synth::ui::TimelineTrackHeaderComponent& row(int index) { return *panel.getTrackHeaderAt(index); }
    juce::Component* stop() const { return panel.getKeyboardLaneStopForTest(); }

    // Selects a track row the way a click does, then presses `key` on it.
    bool pressOnTrack(int index, const juce::KeyPress& key) {
        row(index).mouseDown(makeClickEvent(row(index), {110.0f, 3.0f}, leftButton()));
        return row(index).keyPressed(key);
    }
};

} // namespace

TEST(AutomationLanesKeyboardTest, DownFromATrackWalksItsOpenLanesThenTheNextTrackAndUpWalksBack) {
    KeyboardPanel f;

    EXPECT_TRUE(f.pressOnTrack(0, downKey()));
    EXPECT_EQ(f.stop(), &f.header(f.cutoff)) << "Bass's first lane";
    EXPECT_TRUE(f.header(f.cutoff).keyPressed(downKey()));
    EXPECT_EQ(f.stop(), &f.header(f.res));
    EXPECT_TRUE(f.header(f.res).keyPressed(downKey()));
    EXPECT_EQ(f.stop(), nullptr) << "off the last lane lands on the next track's row";
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1);

    EXPECT_TRUE(f.row(1).keyPressed(downKey()));
    EXPECT_EQ(f.stop(), &f.header(f.pitch)) << "Lead's lane";
    EXPECT_TRUE(f.header(f.pitch).keyPressed(downKey()));
    EXPECT_EQ(f.stop(), nullptr);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2) << "Pad has no lanes: its row comes straight after";

    // And back up the same way.
    EXPECT_TRUE(f.row(2).keyPressed(upKey()));
    EXPECT_EQ(f.stop(), &f.header(f.pitch)) << "Up from the next track's row lands on the last lane above it";
    EXPECT_TRUE(f.header(f.pitch).keyPressed(upKey()));
    EXPECT_EQ(f.stop(), nullptr);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1) << "up from the first lane lands on its track row";
    EXPECT_TRUE(f.row(1).keyPressed(upKey()));
    EXPECT_EQ(f.stop(), &f.header(f.res));
    EXPECT_TRUE(f.header(f.res).keyPressed(upKey()));
    EXPECT_EQ(f.stop(), &f.header(f.cutoff));
    EXPECT_TRUE(f.header(f.cutoff).keyPressed(upKey()));
    EXPECT_EQ(f.stop(), nullptr);
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 0);
}

TEST(AutomationLanesKeyboardTest, FoldedLanesAreSkippedAndStepsFromTheEndsGoToAddTrack) {
    KeyboardPanel f;
    f.panel.setTrackAutomationExpanded(f.bass, false);

    EXPECT_TRUE(f.pressOnTrack(0, downKey()));
    EXPECT_EQ(f.stop(), nullptr) << "folded lanes are not stops";
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 1) << "Down goes straight to the next track";

    // The last track's row with no lanes: Down reaches "+ Track".
    EXPECT_TRUE(f.pressOnTrack(2, downKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
}

TEST(AutomationLanesKeyboardTest, DownOffTheLastLaneOfTheLastTrackReachesAddTrack) {
    KeyboardPanel f;
    const auto padLane = f.addLane(f.pad, "gain");
    f.panel.setTrackAutomationExpanded(f.pad, true);
    EXPECT_TRUE(f.header(padLane).keyPressed(downKey()));
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
}

TEST(AutomationLanesKeyboardTest, ModulatorRowsAreStopsDirectlyUnderTheirLane) {
    KeyboardPanel f;
    ModulatorInfo info;
    info.sourceUuid = "lfo-1";
    info.sourceTitle = "LFO 1";
    info.isLfo = true;
    info.attenuverterUuid = "atten-1";
    info.paramId = "cutoff";
    info.targetChannel = 2;
    f.host.modulators = {info};
    f.panel.setTrackAutomationExpanded(f.bass, false);
    f.panel.setTrackAutomationExpanded(f.bass, true);
    auto* modRow = f.panel.modulatorRowForTest(f.cutoff, 0);
    ASSERT_NE(modRow, nullptr);

    EXPECT_TRUE(f.header(f.cutoff).keyPressed(downKey()));
    EXPECT_EQ(f.stop(), modRow) << "the lane's modulator row follows the lane header";
    EXPECT_TRUE(modRow->keyPressed(downKey()));
    EXPECT_EQ(f.stop(), &f.header(f.res)) << "then the next lane";
    EXPECT_TRUE(f.header(f.res).keyPressed(upKey()));
    EXPECT_EQ(f.stop(), modRow) << "Up walks back through it";
    EXPECT_TRUE(modRow->keyPressed(upKey()));
    EXPECT_EQ(f.stop(), &f.header(f.cutoff));
    EXPECT_FALSE(modRow->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)))
        << "a modified arrow is not a step";
}

TEST(AutomationLanesKeyboardTest, MoveLaneStillMovesTheLaneAndModifiedArrowsAreNotSteps) {
    KeyboardPanel f;
    auto& header = f.header(f.cutoff);
    auto& combo = header.getRecordModeCombo();
    const juce::ModifierKeys commandAlt(juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier);
    ASSERT_EQ(f.doc.getTrack(f.bass)->lanes.front().id, f.cutoff);

    EXPECT_TRUE(header.keyPressed(juce::KeyPress(juce::KeyPress::downKey, commandAlt, 0), &combo))
        << "Cmd+Alt+Down is the move-lane key, even from the record-mode combo";
    EXPECT_EQ(f.doc.getTrack(f.bass)->lanes.front().id, f.res) << "the lane moved down one slot";
    EXPECT_EQ(f.stop(), nullptr) << "and focus did not step";

    const juce::KeyPress shiftUp(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0);
    EXPECT_FALSE(header.keyPressed(shiftUp));
    EXPECT_EQ(f.stop(), nullptr);
}

TEST(AutomationLanesKeyboardTest, TheRecordModeComboKeepsItsOwnUpAndDown) {
    KeyboardPanel f;
    auto& header = f.header(f.cutoff);
    auto& combo = header.getRecordModeCombo();
    // Focus on the combo: the header's key listener must leave a bare Up/Down to the combo (it changes the mode with
    // them), so the lane stop does not move; only focus on the row itself steps.
    EXPECT_FALSE(header.keyPressed(downKey(), &combo));
    EXPECT_FALSE(header.keyPressed(upKey(), &combo));
    EXPECT_EQ(f.stop(), nullptr);
    EXPECT_TRUE(header.keyPressed(downKey())) << "on the row itself Down steps";
    EXPECT_EQ(f.stop(), &f.header(f.res));
}

TEST(AutomationLanesKeyboardTest, ScreenReadersGetEachLanesNameAndTheRowsAreFocusable) {
    KeyboardPanel f;
    f.host.offer("Filter 1", "Cutoff", "cutoff", {0.0f, 100.0f, 50.0f});
    f.panel.setTrackAutomationExpanded(f.bass, false);
    f.panel.setTrackAutomationExpanded(f.bass, true);
    auto& header = f.header(f.cutoff);
    EXPECT_TRUE(header.getWantsKeyboardFocus());
    EXPECT_TRUE(header.getTitle().endsWith(" automation lane")) << header.getTitle();
    EXPECT_NE(header.getTitle(), f.header(f.res).getTitle()) << "each lane is named for its own parameter";
    EXPECT_TRUE(f.header(f.res).getTitle().endsWith(" automation lane"));
    EXPECT_TRUE(header.getTooltip().isNotEmpty());
}
