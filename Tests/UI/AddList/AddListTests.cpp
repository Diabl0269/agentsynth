// AddListTests.cpp -- the rows of the one searchable Add list (Source/UI/AddList/AddList.h): kind tags, ids, group
// order for each origin, search, greyed rows and the screen-reader name.
#include "UI/AddList/AddList.h"
#include "UI/Timeline/TimelinePanelComponent/AddTrackPicker.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace synth::ui;

namespace {

// The shape of the Timeline's add menu, with a preset submenu of each kind.
juce::PopupMenu sampleTrackMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Track");
    menu.addItem(2, "Audio Track");
    juce::PopupMenu instruments;
    instruments.addItem(6, "Sampler");
    instruments.addItem(4, "Oscillator");
    juce::PopupMenu plugins;
    plugins.addItem(100, "Diva (VST3)");
    instruments.addSubMenu("Plugin", plugins);
    menu.addSubMenu("Instrument Track", instruments);
    juce::PopupMenu audioPresets;
    audioPresets.addItem(5000, "Vocal Chain");
    menu.addSubMenu("Audio Track from Preset", audioPresets);
    juce::PopupMenu busPresets;
    busPresets.addItem(7000, "Drum Bus");
    menu.addSubMenu("Bus from Preset", busPresets);
    menu.addItem(11, "Insert Track Preset from File...");
    menu.addItem(3, "Add Marker");
    menu.addItem(9, "Create Channels", false);
    return menu;
}

std::vector<AddListLibraryRow> sampleLibrary() {
    std::vector<AddListLibraryRow> rows;
    auto add = [&rows](AddListLibraryKind kind, const char* text, const char* section) {
        AddListLibraryRow row;
        row.kind = kind;
        row.text = text;
        row.section = section;
        rows.push_back(row);
    };
    add(AddListLibraryKind::Snippet, "My Bass", "Snippets");
    add(AddListLibraryKind::Module, "Oscillator", "Sources");
    add(AddListLibraryKind::Module, "Sampler", "Sources");
    add(AddListLibraryKind::Module, "Audio Output", "I/O");
    rows.back().enabled = false;
    rows.back().disabledReason = "Already in this patch";
    add(AddListLibraryKind::Plugin, "Massive", "Plugins");
    rows.back().format = "VST3";
    return rows;
}

std::vector<ModMatrixPicker::Item> build(AddListOrigin origin) {
    return buildAddListItems(flattenAddTrackMenu(sampleTrackMenu()), sampleLibrary(), origin);
}

const ModMatrixPicker::Item& find(const std::vector<ModMatrixPicker::Item>& items, const juce::String& text,
                                  const juce::String& category = {}) {
    for (const auto& item : items)
        if (item.text == text && (category.isEmpty() || item.category == category))
            return item;
    ADD_FAILURE() << "no row " << text;
    static const ModMatrixPicker::Item none;
    return none;
}

std::vector<juce::String> headerOrder(const std::vector<ModMatrixPicker::Item>& items) {
    std::vector<juce::String> headers;
    for (const auto& item : items)
        if (headers.empty() || headers.back() != item.category)
            headers.push_back(item.category);
    return headers;
}

const juce::String kDot = juce::CharPointer_UTF8("\xc2\xb7");

} // namespace

TEST(AddList, EachRowCarriesItsKindWord) {
    const auto items = build(AddListOrigin::Timeline);
    EXPECT_EQ(find(items, "MIDI Track").tag, "Track");
    EXPECT_EQ(find(items, "Sampler", "Instrument Tracks").tag, "Track");
    EXPECT_EQ(find(items, "Diva (VST3)").tag, "Track");
    EXPECT_EQ(find(items, "Vocal Chain").tag, "Track preset");
    EXPECT_EQ(find(items, "Insert Track Preset from File...").tag, "Track preset");
    EXPECT_EQ(find(items, "Drum Bus").tag, "Bus preset");
    EXPECT_EQ(find(items, "Add Marker").tag, "Marker");
    EXPECT_TRUE(find(items, "Create Channels").tag.isEmpty());
    EXPECT_EQ(find(items, "Oscillator", "Modules " + kDot + " Sources").tag, "Module");
    EXPECT_EQ(find(items, "Massive (VST3)").tag, "Module");
    EXPECT_EQ(find(items, "Massive (VST3)").category, "Modules " + kDot + " Plugins");
    EXPECT_EQ(find(items, "My Bass").tag, "Snippet");
    EXPECT_EQ(find(items, "My Bass").category, "Snippets");
}

TEST(AddList, TrackRowsKeepTheirIdsAndLibraryRowsCountUpFromTheBase) {
    const auto items = build(AddListOrigin::Timeline);
    EXPECT_EQ(find(items, "MIDI Track").id, 1);
    EXPECT_EQ(find(items, "Vocal Chain").id, 5000);
    EXPECT_EQ(find(items, "My Bass").id, kAddListLibraryIdBase + 0);
    EXPECT_EQ(find(items, "Oscillator", "Modules " + kDot + " Sources").id, kAddListLibraryIdBase + 1);
    EXPECT_EQ(find(items, "Massive (VST3)").id, kAddListLibraryIdBase + 4);
    EXPECT_FALSE(libraryIndexForAddListId(7000).has_value());
    EXPECT_EQ(libraryIndexForAddListId(kAddListLibraryIdBase + 4), 4);
    for (const auto& item : items)
        EXPECT_NE(item.id, 0) << item.text;
}

TEST(AddList, TimelineOriginListsTracksFirstAndCanvasOriginModulesFirst) {
    const juce::String modules = "Modules " + kDot + " Sources";
    EXPECT_EQ(headerOrder(build(AddListOrigin::Timeline)),
              (std::vector<juce::String>{"Tracks", "Instrument Tracks", "Plugins", "Audio Track from Preset",
                                         "Bus from Preset", modules, "Modules " + kDot + " I/O",
                                         "Modules " + kDot + " Plugins", "Snippets", "More"}));
    EXPECT_EQ(headerOrder(build(AddListOrigin::Canvas)),
              (std::vector<juce::String>{modules, "Modules " + kDot + " I/O", "Modules " + kDot + " Plugins",
                                         "Snippets", "Tracks", "Instrument Tracks", "Plugins",
                                         "Audio Track from Preset", "Bus from Preset", "More"}));
}

TEST(AddList, MoreIsLastForBothOrigins) {
    for (const auto origin : {AddListOrigin::Timeline, AddListOrigin::Canvas}) {
        const auto items = build(origin);
        EXPECT_EQ(items.back().category, "More");
        EXPECT_EQ(items.back().text, "Create Channels");
    }
}

TEST(AddList, AGreyedSingletonKeepsItsReason) {
    const auto items = build(AddListOrigin::Canvas);
    const auto& row = find(items, "Audio Output");
    EXPECT_FALSE(row.enabled);
    EXPECT_EQ(row.detail, "Already in this patch");
}

TEST(AddList, TrackSamLeavesOnlyTheSamplerTrack) {
    int picked = 0;
    ModMatrixPicker picker("track", build(AddListOrigin::Timeline), 0, [&picked](int id) { picked = id; });
    picker.setSearchTextForTest("track sam");
    EXPECT_EQ(picker.getVisibleItemTextsForTest(), (std::vector<juce::String>{"Sampler"}));
    picker.chooseVisibleItemForTest(0);
    EXPECT_EQ(picked, 6) << "the Sampler instrument track, not the Sampler module";

    picker.setSearchTextForTest("module sam");
    EXPECT_EQ(picker.getVisibleItemTextsForTest(), (std::vector<juce::String>{"Sampler"}));
    picker.chooseVisibleItemForTest(0);
    EXPECT_EQ(picked, kAddListLibraryIdBase + 2);
}

TEST(AddList, TheScreenReaderNameEndsWithTheKindWord) {
    ModMatrixPicker picker("track", build(AddListOrigin::Timeline), 0, [](int) {});
    picker.setSize(360, 400);
    picker.setSearchTextForTest("sampler");
    std::vector<juce::String> titles;
    std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
        if (c.getTitle().isNotEmpty())
            titles.push_back(c.getTitle());
        for (auto* child : c.getChildren())
            walk(*child);
    };
    walk(picker);
    EXPECT_NE(std::find(titles.begin(), titles.end(), juce::String("Sampler, Track")), titles.end());
    EXPECT_NE(std::find(titles.begin(), titles.end(), juce::String("Sampler, Module")), titles.end());
}
