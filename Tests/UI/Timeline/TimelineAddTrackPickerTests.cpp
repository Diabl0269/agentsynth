// TimelineAddTrackPickerTests.cpp
//
// "+ Track" opens a searchable picker built from the add-track menu: the menu stays the one source of the
// entries, flattened into rows whose ids are the menu ids, so a pick lands exactly where a menu click did.

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/AddTrackPicker.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::TrackId;
using synth::ui::ModMatrixPicker;
using synth::ui::TrackHeaderHost;

namespace {

bool contains(const std::vector<juce::String>& names, const juce::String& text) {
    return std::find(names.begin(), names.end(), text) != names.end();
}

struct PickerHost : TrackHeaderHost {
    std::vector<synth::PluginIdentity> plugins;
    int midiTracks = 0;
    int audioTracks = 0;
    juce::String lastInstrument;
    bool poly = false;

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
    void addMidiTrack() override { ++midiTracks; }
    void addAudioTrack() override { ++audioTracks; }
    void addInstrumentTrack(const juce::String& type, bool isPoly) override {
        lastInstrument = type;
        poly = isPoly;
    }
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
    std::vector<synth::PluginIdentity> getInstrumentPluginOptions() const override { return plugins; }
};

struct PickerFixture {
    synth::TimelineDoc doc;
    PickerHost host;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;

    PickerFixture() {
        panel.setSize(1200, 400);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setShortcutManager(nullptr);
        panel.setRecordFocusForTest(true);
    }

    // Opens the picker the way the button does, through the launcher's hook.
    std::unique_ptr<ModMatrixPicker> openFromButton() {
        std::unique_ptr<ModMatrixPicker> picker;
        synth::ui::test_hooks::addTrackPickerHookForTest() = [&picker](std::unique_ptr<ModMatrixPicker> p) {
            picker = std::move(p);
        };
        if (panel.getAddTrackButton().onClick)
            panel.getAddTrackButton().onClick();
        synth::ui::test_hooks::addTrackPickerHookForTest() = nullptr;
        return picker;
    }
};

synth::PluginIdentity plugin(const juce::String& name, int uid) {
    synth::PluginIdentity identity;
    identity.format = "VST3";
    identity.name = name;
    identity.uid = uid;
    return identity;
}

} // namespace

TEST(TimelineAddTrackPicker, RowsCoverTheMenuAndKeepItsIds) {
    PickerFixture f;
    f.host.plugins = {plugin("Diva", 1)};
    auto menu = f.panel.buildAddTrackMenu();
    const auto rows = synth::ui::flattenAddTrackMenu(menu);

    auto find = [&](const juce::String& text) -> const ModMatrixPicker::Item* {
        for (const auto& row : rows)
            if (row.text == text)
                return &row;
        return nullptr;
    };
    ASSERT_NE(find("MIDI Track"), nullptr);
    EXPECT_EQ(find("MIDI Track")->id, synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    EXPECT_EQ(find("MIDI Track")->category, "Tracks");
    EXPECT_EQ(find("Audio Track")->id, synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    EXPECT_EQ(find("Oscillator")->category, "Instrument Tracks");
    ASSERT_NE(find("Diva (VST3)"), nullptr);
    EXPECT_EQ(find("Diva (VST3)")->category, "Plugins");
    EXPECT_EQ(find("Diva (VST3)")->id, synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase);
    EXPECT_EQ(find("Add Marker")->category, "More");
    for (const auto& row : rows)
        EXPECT_GT(row.id, 0) << row.text;
}

TEST(TimelineAddTrackPicker, NoPluginsLeavesADisabledPlaceholder) {
    PickerFixture f;
    const auto rows = synth::ui::flattenAddTrackMenu(f.panel.buildAddTrackMenu());
    const auto it = std::find_if(rows.begin(), rows.end(), [](const auto& r) { return r.category == "Plugins"; });
    ASSERT_NE(it, rows.end());
    EXPECT_FALSE(it->enabled);
    EXPECT_EQ(it->text, "No instrument plugins found");
    EXPECT_TRUE(it->detail.isNotEmpty());
}

TEST(TimelineAddTrackPicker, CreateChannelsIsGreyedWithAReasonWhenEveryTrackHasOne) {
    PickerFixture f;
    auto picker = f.openFromButton();
    ASSERT_NE(picker, nullptr);
    const auto texts = picker->getVisibleItemTextsForTest();
    const auto details = picker->getVisibleItemDetailsForTest();
    const auto at = std::find(texts.begin(), texts.end(), juce::String("Create Channels")) - texts.begin();
    ASSERT_LT((size_t)at, texts.size());
    EXPECT_FALSE(picker->isVisibleItemPickableForTest((int)at));
    EXPECT_EQ(details[(size_t)at], "Every track already has a channel");
}

TEST(TimelineAddTrackPicker, TypingFiltersTheRows) {
    PickerFixture f;
    auto picker = f.openFromButton();
    ASSERT_NE(picker, nullptr);
    EXPECT_TRUE(contains(picker->getVisibleItemTextsForTest(), "Wavetable"));

    picker->setSearchTextForTest("osc");
    const auto texts = picker->getVisibleItemTextsForTest();
    EXPECT_TRUE(contains(texts, "Oscillator"));
    EXPECT_TRUE(contains(texts, "Oscillator (Poly)"));
    EXPECT_FALSE(contains(texts, "Wavetable"));
    EXPECT_FALSE(contains(texts, "MIDI Track"));
}

TEST(TimelineAddTrackPicker, ChoosingARowDoesWhatTheMenuChoiceDid) {
    PickerFixture f;
    auto picker = f.openFromButton();
    ASSERT_NE(picker, nullptr);
    const auto texts = picker->getVisibleItemTextsForTest();
    picker->chooseVisibleItemForTest(
        (int)(std::find(texts.begin(), texts.end(), juce::String("MIDI Track")) - texts.begin()));
    EXPECT_EQ(f.host.midiTracks, 1);
    EXPECT_EQ(f.host.audioTracks, 0);

    picker->setSearchTextForTest("audio track");
    picker->chooseVisibleItemForTest(0);
    EXPECT_EQ(f.host.audioTracks, 1);
}

TEST(TimelineAddTrackPicker, TheShortcutFocusesTheButtonAndOpensAPickerWithItsSearchFocused) {
    PickerFixture f;
    ASSERT_FALSE(f.panel.isAddTrackButtonFocused());
    std::unique_ptr<ModMatrixPicker> picker;
    synth::ui::test_hooks::addTrackPickerHookForTest() = [&picker](std::unique_ptr<ModMatrixPicker> p) {
        picker = std::move(p);
    };
    f.panel.openAddTrackMenuFromShortcut();
    synth::ui::test_hooks::addTrackPickerHookForTest() = nullptr;

    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
    ASSERT_NE(picker, nullptr);
    EXPECT_FALSE(picker->getSearchEditorForTest().isReadOnly());
    EXPECT_EQ(picker->getSearchTextForTest(), "");
}

TEST(TimelineAddTrackPicker, ClosingAKeyboardOpenedPickerHandsFocusBackOnce) {
    PickerFixture f;
    std::unique_ptr<ModMatrixPicker> picker;
    synth::ui::test_hooks::addTrackPickerHookForTest() = [&picker](std::unique_ptr<ModMatrixPicker> p) {
        picker = std::move(p);
    };
    f.panel.openAddTrackMenuFromShortcut();
    synth::ui::test_hooks::addTrackPickerHookForTest() = nullptr;
    ASSERT_NE(picker, nullptr);

    // Closing without a pick (Escape / click away destroys the picker) keeps focus on "+ Track".
    picker.reset();
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused());
    EXPECT_EQ(f.host.midiTracks + f.host.audioTracks, 0) << "closing without a pick adds nothing";
}
