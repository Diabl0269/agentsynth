// TimelineRoutingPaneAudioInputTests.cpp: the routing pane's audio Input section
// (docs/timeline/tracks.md#the-input-row)
// -- which input an audio track records from, its level meter and the Monitor switch. Driven against a stub host
// whose made-up input device can be resized, have inputs turned off, or be swapped for the host's inputs; every edit
// is one undo step, as MainComponent makes it.
#include "../Accessibility/AccessibilityAudit.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Layout/PaneKeysWalk.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {

using synth::MonitorMode;
using synth::TrackKind;
using synth::ui::AudioInputMenuIds;
using synth::ui::TimelinePanelComponent;
using synth::ui::TimelineRoutingPane;
using synth::ui::TrackHeaderHost;

struct FakeInputs : synth::ui::TrackAudioInputSurface {
    synth::AudioInputStatus status;
    float peak = 0.5f;
    int enableCalls = 0;
    int settingsCalls = 0;

    FakeInputs() { setDevice("Scarlett 4i4", 4, 0b1111); }
    void setDevice(const juce::String& name, int inputs, std::uint32_t mask) {
        status.deviceName = name;
        status.deviceInputs = inputs;
        status.activeMask = mask;
        status.activeInputs = 0;
        for (int c = 0; c < 32; ++c)
            status.activeInputs += status.isChannelActive(c) ? 1 : 0;
    }
    synth::AudioInputStatus getAudioInputStatus() override { return status; }
    float readInputPeak(int) override { return peak; }
    void enableDeviceInputs() override { ++enableCalls; }
    void openAudioSettings() override { ++settingsCalls; }
};

class AudioStubHost : public TrackHeaderHost {
public:
    AudioStubHost(synth::TimelineDoc& doc, AppUndoManager& undo)
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
    synth::ui::TrackAudioInputSurface* getAudioInputSurface() override { return &inputs; }

    FakeInputs inputs;

private:
    synth::TimelineDoc& doc_;
    AppUndoManager& undo_;
};

struct AudioPanel {
    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    AudioStubHost host{doc, undo};
    TimelinePanelComponent panel;

    AudioPanel() {
        panel.setSize(1200, 600);
        panel.setTimelineDoc(&doc);
        panel.setTrackHeaderHost(&host);
        panel.getSidePane().setOpen(true);
    }
    ~AudioPanel() { panel.setTrackHeaderHost(nullptr); }
    TimelineRoutingPane& pane() { return panel.getRoutingPane(); }
    void select(int index) {
        auto* row = panel.getTrackHeaderAt(index);
        ASSERT_NE(row, nullptr);
        row->mouseDown(makeClickEvent(*row, {110.0f, 3.0f}));
    }
};

struct MenuItem {
    int id;
    juce::String text;
    bool ticked;
    bool enabled;
    bool header;
};

std::vector<MenuItem> itemsOf(juce::PopupMenu& menu) {
    std::vector<MenuItem> items;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (!it.getItem().isSeparator)
            items.push_back({it.getItem().itemID, it.getItem().text, it.getItem().isTicked, it.getItem().isEnabled,
                             it.getItem().isSectionHeader});
    return items;
}

std::vector<MenuItem> openMenu(TimelineRoutingPane& pane) {
    std::vector<MenuItem> items;
    pane.setShowInputMenuHookForTest([&items](juce::PopupMenu& menu) { items = itemsOf(menu); });
    auto& button = pane.getAudioInputButtonForTest();
    const auto centre = button.getLocalBounds().getCentre().toFloat();
    auto& component = static_cast<juce::Component&>(button);
    component.mouseDown(makeClickEvent(button, centre));
    component.mouseUp(makeClickEvent(button, centre));
    pane.setShowInputMenuHookForTest(nullptr);
    return items;
}

std::vector<juce::String> textsOf(const std::vector<MenuItem>& items) {
    std::vector<juce::String> texts;
    for (const auto& item : items)
        texts.push_back(item.text);
    return texts;
}

const MenuItem* find(const std::vector<MenuItem>& items, const juce::String& text) {
    for (const auto& item : items)
        if (item.text == text)
            return &item;
    return nullptr;
}

synth::AudioInputChoice device(int first, int count, const juce::String& name) {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::Device;
    choice.firstChannel = first;
    choice.numChannels = count;
    choice.deviceName = name;
    return choice;
}

juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

} // namespace

TEST(TimelineRoutingPaneAudioInputTest, AnAudioTrackShowsInputAndMonitorAndAMidiTrackDoesNot) {
    AudioPanel f;
    f.doc.addTrack(TrackKind::Audio, "Vox");
    f.doc.addTrack(TrackKind::Midi, "Keys");
    f.select(0);
    auto& pane = f.pane();
    ASSERT_TRUE(pane.isAudioInputShownForTest());
    EXPECT_FALSE(pane.isMidiInputShownForTest());
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "No input");
    EXPECT_TRUE(pane.getAudioInputTextForTest().muted);
    EXPECT_EQ(pane.getAudioInputButtonForTest().getTitle(), "Audio input, No input");
    EXPECT_TRUE(pane.getAudioInputButtonForTest().getTooltip().isNotEmpty());
    EXPECT_TRUE(pane.getMonitorSwitchForTest().isVisible());
    EXPECT_EQ(pane.getMonitorSwitchForTest().getSelectedIndex(), 0) << "Auto";
    EXPECT_TRUE(pane.getMonitorSwitchForTest().getTooltip().isNotEmpty());
    EXPECT_FALSE(pane.getInputMeterForTest().isVisible()) << "no input, no meter";

    for (const auto& gap : synth::test::auditAccessibility(pane))
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name " : "missing tooltip ")
                      << gap.path;

    f.select(1);
    EXPECT_TRUE(pane.isMidiInputShownForTest());
    EXPECT_EQ(pane.getAudioInputButtonForTest().getButtonText(), "No input") << "a fading section keeps its words";
}

TEST(TimelineRoutingPaneAudioInputTest, TheMenuListsMonoAndStereoInputsOfTheOpenDevice) {
    AudioPanel f;
    f.doc.addTrack(TrackKind::Audio, "Vox");
    f.select(0);
    const auto items = openMenu(f.pane());
    const std::vector<juce::String> expected{"No input",
                                             "Mono" + dot() + "Scarlett 4i4",
                                             "In 1",
                                             "In 2",
                                             "In 3",
                                             "In 4",
                                             "Stereo" + dot() + "Scarlett 4i4",
                                             "In 1 + 2",
                                             "In 3 + 4",
                                             "Another track",
                                             "Whole mix",
                                             "Audio settings..."};
    EXPECT_EQ(textsOf(items), expected);
    EXPECT_TRUE(items[0].ticked);
    EXPECT_TRUE(find(items, "In 3")->enabled);
}

TEST(TimelineRoutingPaneAudioInputTest, PickingAPairIsOneUndoStepAndShowsTheMeter) {
    AudioPanel f;
    const auto id = f.doc.addTrack(TrackKind::Audio, "Vox");
    f.select(0);
    auto& pane = f.pane();
    pane.applyAudioInputChoice(AudioInputMenuIds::kFirstStereo + 2);
    EXPECT_EQ(f.doc.getTrack(id)->audioInput, device(2, 2, "Scarlett 4i4")) << "a pick names the open device";
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "In 3 + 4");
    EXPECT_EQ(pane.getAudioInputButtonForTest().getTitle(), "Audio input, In 3 + 4");
    EXPECT_TRUE(pane.getInputMeterForTest().isVisible());
    EXPECT_EQ(pane.getInputMeterForTest().getChannelCount(), 2);
    EXPECT_TRUE(find(openMenu(pane), "In 3 + 4")->ticked);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(id)->audioInput.kind, synth::AudioInputChoice::Kind::None);
    EXPECT_FALSE(f.undo.canUndo()) << "exactly one undo step";
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "No input");

    pane.applyAudioInputChoice(AudioInputMenuIds::kFirstMono + 1);
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "In 2 (mono)");
    pane.applyAudioInputChoice(AudioInputMenuIds::kWholeMix);
    EXPECT_EQ(f.doc.getTrack(id)->audioInput.kind, synth::AudioInputChoice::Kind::WholeMix);
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "Whole mix");
}

TEST(TimelineRoutingPaneAudioInputTest, PickingAnotherTrackIsOneUndoStepAndADeletedSourceReadsAsRemoved) {
    AudioPanel f;
    const auto id = f.doc.addTrack(TrackKind::Audio, "Vox");
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead synth");
    f.select(0);
    auto& pane = f.pane();
    pane.applyAudioInputChoice(AudioInputMenuIds::kFirstTrack);
    EXPECT_EQ(f.doc.getTrack(id)->audioInput.kind, synth::AudioInputChoice::Kind::Track);
    EXPECT_EQ(f.doc.getTrack(id)->audioInput.sourceTrack, lead);
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "Lead synth");
    EXPECT_FALSE(pane.getAudioInputTextForTest().warning);
    EXPECT_FALSE(pane.getInputMeterForTest().isVisible()) << "no device input to meter";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.undo.canUndo()) << "exactly one undo step";
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "No input");

    pane.applyAudioInputChoice(AudioInputMenuIds::kFirstTrack);
    ASSERT_TRUE(f.doc.removeTrack(lead));
    pane.refresh();
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "(track removed)");
    EXPECT_TRUE(pane.getAudioInputTextForTest().warning);
}

TEST(TimelineRoutingPaneAudioInputTest, TurnedOffInputsAreDisabledAndCanBeTurnedOn) {
    AudioPanel f;
    const auto id = f.doc.addTrack(TrackKind::Audio, "Vox");
    ASSERT_TRUE(f.doc.setTrackAudioInput(id, device(2, 1, "Scarlett 4i4")));
    f.host.inputs.setDevice("Scarlett 4i4", 4, 0b0011);
    f.select(0);
    auto& pane = f.pane();
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "In 3 (turned off)");
    EXPECT_TRUE(pane.getAudioInputTextForTest().warning);
    EXPECT_TRUE(pane.getAudioInputButtonForTest().getTooltip().contains("turned off"));

    const auto items = openMenu(pane);
    EXPECT_FALSE(find(items, "In 3")->enabled);
    EXPECT_FALSE(find(items, "In 3 + 4")->enabled);
    EXPECT_TRUE(find(items, "In 1 + 2")->enabled);
    ASSERT_NE(find(items, "In 3 (turned off)"), nullptr) << "the track's own choice stays on the menu, ticked";
    EXPECT_TRUE(find(items, "In 3 (turned off)")->ticked);
    ASSERT_NE(find(items, "Turn on all its inputs"), nullptr);
    pane.applyAudioInputChoice(find(items, "Turn on all its inputs")->id);
    EXPECT_EQ(f.host.inputs.enableCalls, 1);
    pane.applyAudioInputChoice(AudioInputMenuIds::kNotConnected);
    EXPECT_FALSE(f.undo.canUndo()) << "picking the current choice again changes nothing";

    f.host.inputs.setDevice("Scarlett 4i4", 4, 0);
    const auto off = textsOf(openMenu(pane));
    EXPECT_NE(std::find(off.begin(), off.end(), "The inputs of Scarlett 4i4 are turned off."), off.end());
    EXPECT_NE(std::find(off.begin(), off.end(), "Turn on its inputs"), off.end());
    pane.applyAudioInputChoice(AudioInputMenuIds::kAudioSettings);
    EXPECT_EQ(f.host.inputs.settingsCalls, 1);
}

TEST(TimelineRoutingPaneAudioInputTest, AnInputOnAnotherDeviceIsNamedAsNotConnected) {
    AudioPanel f;
    const auto id = f.doc.addTrack(TrackKind::Audio, "Vox");
    ASSERT_TRUE(f.doc.setTrackAudioInput(id, device(0, 2, "MOTU M4")));
    f.select(0);
    auto& pane = f.pane();
    EXPECT_EQ(pane.getAudioInputTextForTest().text, "In 1 + 2 (MOTU M4 not connected)");
    EXPECT_TRUE(pane.getAudioInputTextForTest().warning);
    EXPECT_FALSE(pane.getInputMeterForTest().isVisible()) << "nothing to meter";
    EXPECT_FALSE(find(openMenu(pane), "In 1 + 2")->ticked) << "the open device's pair is not the track's";
}

TEST(TimelineRoutingPaneAudioInputTest, HostedInputsCarryNoDeviceAndOfferNoSettings) {
    AudioPanel f;
    const auto id = f.doc.addTrack(TrackKind::Audio, "Vox");
    f.host.inputs.status = {};
    f.host.inputs.status.hosted = true;
    f.host.inputs.status.activeInputs = 2;
    f.host.inputs.status.activeMask = 0b11;
    f.select(0);
    const auto texts = textsOf(openMenu(f.pane()));
    const std::vector<juce::String> expected{"No input",
                                             "Mono" + dot() + "Host input",
                                             "In 1",
                                             "In 2",
                                             "Stereo" + dot() + "Host input",
                                             "In 1 + 2",
                                             "Another track",
                                             "Whole mix"};
    EXPECT_EQ(texts, expected);
    f.pane().applyAudioInputChoice(AudioInputMenuIds::kFirstStereo);
    EXPECT_EQ(f.doc.getTrack(id)->audioInput, device(0, 2, {}));
    f.pane().applyAudioInputChoice(AudioInputMenuIds::kAudioSettings);
    EXPECT_EQ(f.host.inputs.settingsCalls, 0);

    f.host.inputs.status.activeInputs = 0;
    f.host.inputs.status.activeMask = 0;
    const auto none = textsOf(openMenu(f.pane()));
    EXPECT_NE(std::find(none.begin(), none.end(), "The host passes no inputs to this plugin"), none.end());
}

TEST(TimelineRoutingPaneAudioInputTest, TheMonitorSwitchIsOneUndoStepAndFollowsTheDoc) {
    AudioPanel f;
    const auto id = f.doc.addTrack(TrackKind::Audio, "Vox");
    f.select(0);
    auto& pane = f.pane();
    auto& monitor = pane.getMonitorSwitchForTest();
    const auto plain = [](int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); };
    EXPECT_TRUE(monitor.keyPressed(plain(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.doc.getTrack(id)->monitorMode, MonitorMode::On);
    EXPECT_TRUE(monitor.keyPressed(plain(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.doc.getTrack(id)->monitorMode, MonitorMode::Off);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(id)->monitorMode, MonitorMode::On);
    EXPECT_EQ(monitor.getSelectedIndex(), 1) << "the switch follows undo";

    // Up and Down move between the pane's stops; they never change the mode.
    auto& keys = pane.getPaneKeysForTest();
    EXPECT_TRUE(keys.keyPressed(plain(juce::KeyPress::upKey), &monitor));
    EXPECT_EQ(f.doc.getTrack(id)->monitorMode, MonitorMode::On);
}

TEST(TimelineRoutingPaneAudioInputTest, EveryControlOfAnAudioTrackIsReachedByDown) {
    AudioPanel f;
    f.doc.addTrack(TrackKind::Audio, "Vox");
    f.select(0);
    pane_walk::expectEveryControlReachedByDownAndUp(f.pane().getPaneKeysForTest(), f.pane());
}

TEST(TimelineRoutingPaneAudioInputTest, InputAndMonitorAreKeyboardStopsInVisualOrder) {
    AudioPanel f;
    f.doc.addTrack(TrackKind::Audio, "Vox");
    f.select(0);
    auto& pane = f.pane();
    const auto stops = synth::ui::focusableDescendants(pane);
    const auto at = [&](juce::Component* c) { return std::find(stops.begin(), stops.end(), c) - stops.begin(); };
    const auto canvas = at(&pane.getCanvasNodeButtonForTest());
    const auto input = at(&pane.getAudioInputButtonForTest());
    const auto monitor = at(&pane.getMonitorSwitchForTest());
    ASSERT_LT(input, (long)stops.size());
    ASSERT_LT(monitor, (long)stops.size());
    EXPECT_EQ(input, canvas + 1);
    EXPECT_EQ(monitor, input + 1);
    EXPECT_GT(pane.getMonitorSwitchForTest().getY(), pane.getAudioInputButtonForTest().getBottom());
}
