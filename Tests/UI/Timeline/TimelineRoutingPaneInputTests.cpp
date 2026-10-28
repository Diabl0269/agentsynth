// TimelineRoutingPaneInputTests.cpp: the routing pane's Input row (docs/timeline/tracks.md#the-input-row) -- which MIDI
// input and channel record into a MIDI track. Driven through real mouse and key events on the two buttons, against a
// stub host that lists made-up devices and makes every edit one undo step, as MainComponent does.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Layout/PaneKeysWalk.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include <gtest/gtest.h>

namespace {

using synth::TrackKind;
using synth::ui::TimelinePanelComponent;
using synth::ui::TimelineRoutingPane;
using synth::ui::TrackHeaderHost;

class InputStubHost : public TrackHeaderHost {
public:
    InputStubHost(synth::TimelineDoc& doc, AppUndoManager& undo)
        : doc_(doc)
        , undo_(undo) {}
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        undo_.recordTimelineChange(doc_, mutation);
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
    std::vector<MidiInputDevice> getMidiInputDevices() override { return devices; }

    std::vector<MidiInputDevice> devices{{"dev-keys", "Keystation 49"}, {"dev-pads", "Pad Controller"}};

private:
    synth::TimelineDoc& doc_;
    AppUndoManager& undo_;
};

struct InputPanel {
    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    InputStubHost host{doc, undo};
    TimelinePanelComponent panel;

    InputPanel() {
        panel.setSize(1200, 600);
        panel.setTimelineDoc(&doc);
        panel.setTrackHeaderHost(&host);
        panel.getSidePane().setOpen(true);
    }
    ~InputPanel() { panel.setTrackHeaderHost(nullptr); }
    TimelineRoutingPane& pane() { return panel.getRoutingPane(); }

    void select(int index) {
        auto* row = panel.getTrackHeaderAt(index);
        ASSERT_NE(row, nullptr);
        row->mouseDown(makeClickEvent(*row, {110.0f, 3.0f}));
    }
};

// A real click: press and release over the button.
void click(juce::Button& button) {
    const juce::Point<float> centre = button.getLocalBounds().getCentre().toFloat();
    auto& component = static_cast<juce::Component&>(button); // the events a press and release deliver
    component.mouseDown(makeClickEvent(button, centre));
    component.mouseUp(makeClickEvent(button, centre));
}

struct MenuItem {
    int id;
    juce::String text;
    bool ticked;
    bool enabled;
};

std::vector<MenuItem> itemsOf(juce::PopupMenu& menu) {
    std::vector<MenuItem> items;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (!it.getItem().isSeparator)
            items.push_back({it.getItem().itemID, it.getItem().text, it.getItem().isTicked, it.getItem().isEnabled});
    return items;
}

// Clicks `button` and returns the menu it built.
std::vector<MenuItem> openMenu(TimelineRoutingPane& pane, juce::Button& button) {
    std::vector<MenuItem> items;
    pane.setShowInputMenuHookForTest([&items](juce::PopupMenu& menu) { items = itemsOf(menu); });
    click(button);
    pane.setShowInputMenuHookForTest(nullptr);
    return items;
}

int idOf(const std::vector<MenuItem>& items, const juce::String& text) {
    for (const auto& item : items)
        if (item.text == text)
            return item.id;
    return 0;
}

} // namespace

TEST(TimelineRoutingPaneInputTest, AMidiTrackShowsItsInputAndAnAudioTrackHasNone) {
    InputPanel f;
    f.doc.addTrack(TrackKind::Midi, "Keys");
    f.doc.addTrack(TrackKind::Audio, "Vox");
    f.select(0);
    auto& pane = f.pane();
    ASSERT_TRUE(pane.isMidiInputShownForTest());
    EXPECT_EQ(pane.getMidiInputDeviceTextForTest(), "All MIDI inputs");
    EXPECT_EQ(pane.getMidiInputChannelTextForTest(), "Any channel");
    EXPECT_FALSE(pane.isMidiInputDeviceWarningForTest());
    auto& device = pane.getMidiInputDeviceButtonForTest();
    auto& channel = pane.getMidiInputChannelButtonForTest();
    EXPECT_EQ(device.getTitle(), "MIDI input, All MIDI inputs") << "the screen-reader name carries the value";
    EXPECT_EQ(channel.getTitle(), "Input channel, Any channel");
    EXPECT_TRUE(device.getTooltip().isNotEmpty());
    EXPECT_TRUE(channel.getTooltip().isNotEmpty());
    EXPECT_EQ(device.getWidth(), channel.getWidth()) << "each gets the pane's width: a device name needs it";
    EXPECT_GT(channel.getY(), device.getBottom()) << "the channel sits under the device";

    f.select(1);
    EXPECT_EQ(pane.getKindBadgeForTest(), "Audio");
    EXPECT_FALSE(pane.isMidiInputShownForTest()) << "an audio track records no MIDI";
}

TEST(TimelineRoutingPaneInputTest, PickingAControllerRecordsFromItInOneUndoStep) {
    InputPanel f;
    const auto id = f.doc.addTrack(TrackKind::Midi, "Keys");
    f.select(0);
    auto& pane = f.pane();

    const auto items = openMenu(pane, pane.getMidiInputDeviceButtonForTest());
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].text, "All MIDI inputs");
    EXPECT_TRUE(items[0].ticked);
    EXPECT_EQ(items[1].text, "Keystation 49");
    EXPECT_EQ(items[2].text, "Pad Controller");

    pane.applyMidiInputDeviceChoice(idOf(items, "Pad Controller"));
    const auto* track = f.doc.getTrack(id);
    EXPECT_EQ(track->midiInputId, "dev-pads");
    EXPECT_EQ(track->midiInputName, "Pad Controller");
    EXPECT_EQ(track->midiInputChannel, 0);
    EXPECT_EQ(pane.getMidiInputDeviceTextForTest(), "Pad Controller");
    EXPECT_EQ(pane.getMidiInputDeviceButtonForTest().getTitle(), "MIDI input, Pad Controller");
    EXPECT_TRUE(idOf(openMenu(pane, pane.getMidiInputDeviceButtonForTest()), "Pad Controller") != 0);

    ASSERT_TRUE(f.undo.canUndo());
    ASSERT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.doc.getTrack(id)->recordsEveryMidiInput());
    EXPECT_FALSE(f.undo.canUndo()) << "the pick was exactly one undo step";
    EXPECT_EQ(pane.getMidiInputDeviceTextForTest(), "All MIDI inputs") << "the row follows undo";
}

TEST(TimelineRoutingPaneInputTest, PickingAChannelKeepsTheController) {
    InputPanel f;
    const auto id = f.doc.addTrack(TrackKind::Midi, "Drums");
    ASSERT_TRUE(f.doc.setTrackMidiInput(id, "dev-pads", "Pad Controller", 0));
    f.select(0);
    auto& pane = f.pane();

    const auto items = openMenu(pane, pane.getMidiInputChannelButtonForTest());
    ASSERT_EQ(items.size(), 17u) << "Any channel and 1 to 16";
    EXPECT_EQ(items[0].text, "Any channel");
    EXPECT_TRUE(items[0].ticked);
    EXPECT_EQ(items[16].text, "Channel 16");

    pane.applyMidiInputChannelChoice(idOf(items, "Channel 10"));
    EXPECT_EQ(f.doc.getTrack(id)->midiInputChannel, 10);
    EXPECT_EQ(f.doc.getTrack(id)->midiInputId, "dev-pads");
    EXPECT_EQ(pane.getMidiInputChannelTextForTest(), "Channel 10");
    EXPECT_EQ(pane.getMidiInputChannelButtonForTest().getTitle(), "Input channel, Channel 10");
    EXPECT_TRUE(openMenu(pane, pane.getMidiInputChannelButtonForTest())[10].ticked);
}

// A controller that is not plugged in stays the track's input, named as not connected in the warning colour; it is
// never quietly swapped for "All MIDI inputs", and picking it again changes nothing.
TEST(TimelineRoutingPaneInputTest, AnUnpluggedControllerIsNamedAsNotConnected) {
    InputPanel f;
    const auto id = f.doc.addTrack(TrackKind::Midi, "Keys");
    ASSERT_TRUE(f.doc.setTrackMidiInput(id, "dev-old", "Old Keyboard", 0));
    f.select(0);
    auto& pane = f.pane();
    EXPECT_EQ(pane.getMidiInputDeviceTextForTest(), "Old Keyboard (not connected)");
    EXPECT_TRUE(pane.isMidiInputDeviceWarningForTest());
    EXPECT_TRUE(pane.getMidiInputDeviceButtonForTest().getTooltip().contains("not connected"));

    const auto items = openMenu(pane, pane.getMidiInputDeviceButtonForTest());
    const int missing = idOf(items, "Old Keyboard (not connected)");
    ASSERT_EQ(missing, TimelineRoutingPane::kMissingInputDeviceMenuId);
    EXPECT_FALSE(items[0].ticked) << "All MIDI inputs is not what the track records from";
    pane.applyMidiInputDeviceChoice(missing);
    EXPECT_EQ(f.doc.getTrack(id)->midiInputId, "dev-old");
    EXPECT_FALSE(f.undo.canUndo());

    // It comes back: the row says so on the next refresh.
    f.host.devices.push_back({"dev-old", "Old Keyboard"});
    pane.refresh();
    EXPECT_EQ(pane.getMidiInputDeviceTextForTest(), "Old Keyboard");
    EXPECT_FALSE(pane.isMidiInputDeviceWarningForTest());
}

TEST(TimelineRoutingPaneInputTest, EveryControlWithTheInputRowsIsReachedByDown) {
    InputPanel f;
    f.doc.addTrack(TrackKind::Midi, "Keys");
    f.select(0);
    pane_walk::expectEveryControlReachedByDownAndUp(f.pane().getPaneKeysForTest(), f.pane());
}

// Keyboard path: the two Input buttons are stops in visual order between Canvas node and MIDI destinations, and
// Return on one opens its menu.
TEST(TimelineRoutingPaneInputTest, TheInputButtonsAreKeyboardStopsAndReturnOpensTheirMenus) {
    InputPanel f;
    f.doc.addTrack(TrackKind::Midi, "Keys");
    f.select(0);
    auto& pane = f.pane();
    const auto stops = synth::ui::focusableDescendants(pane);
    ASSERT_EQ(stops.size(), 4u);
    EXPECT_EQ(stops[0], &pane.getCanvasNodeButtonForTest());
    EXPECT_EQ(stops[1], &pane.getMidiInputDeviceButtonForTest());
    EXPECT_EQ(stops[2], &pane.getMidiInputChannelButtonForTest());
    EXPECT_EQ(stops[3], &pane.getMidiDestinationsButtonForTest());

    const auto plain = [](int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); };
    auto& keys = pane.getPaneKeysForTest();
    EXPECT_TRUE(keys.keyPressed(plain(juce::KeyPress::downKey), &pane.getMidiInputDeviceButtonForTest()));

    int opened = 0;
    pane.setShowInputMenuHookForTest([&opened](juce::PopupMenu&) { ++opened; });
    auto& channel = static_cast<juce::Component&>(pane.getMidiInputChannelButtonForTest());
    EXPECT_TRUE(channel.keyPressed(plain(juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // a Return click is delivered as a message
    EXPECT_EQ(opened, 1);
}
