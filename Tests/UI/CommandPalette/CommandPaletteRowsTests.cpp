// CommandPaletteRowsTests.cpp -- the palette's ranking: the top match alone first, then every other hit under its
// kind's header; recents then views for an empty query; "+" narrows to the Add list's rows.
#include "UI/AddList/AddList.h"
#include "UI/CommandPalette/CommandPaletteRows.h"
#include <gtest/gtest.h>

using synth::ui::buildCommandPaletteRows;
using synth::ui::ModMatrixPicker;
using synth::ui::PaletteEntry;
using synth::ui::PaletteKind;

namespace {

PaletteEntry entry(PaletteKind kind, const juce::String& key, const juce::String& text, bool enabled = true) {
    PaletteEntry e;
    e.kind = kind;
    e.key = key;
    e.text = text;
    e.enabled = enabled;
    return e;
}

std::vector<PaletteEntry> sampleEntries() {
    return {entry(PaletteKind::Place, "place:timeline", "Timeline"),
            entry(PaletteKind::Place, "place:mixer", "Mixer"),
            entry(PaletteKind::Module, "module:a", "LFO 1"),
            entry(PaletteKind::Module, "module:b", "LFO 6"),
            entry(PaletteKind::Action, "action:toggleTimelinePanel", "Toggle Timeline Panel"),
            entry(PaletteKind::Action, "action:focusTimeline", "Focus Timeline"),
            entry(PaletteKind::Setting, "setting:x", "Natural scrolling")};
}

// Each item's category ("Top match", "Views") and text, in order.
std::vector<std::pair<juce::String, juce::String>> layout(const ModMatrixPicker::QueryRows& rows) {
    std::vector<std::pair<juce::String, juce::String>> out;
    for (const auto& item : rows.items)
        out.emplace_back(item.category, item.text);
    return out;
}

ModMatrixPicker::Item addRow(int id, const juce::String& category, const juce::String& text, bool enabled = true) {
    ModMatrixPicker::Item item;
    item.id = id;
    item.category = category;
    item.text = text;
    item.enabled = enabled;
    return item;
}

} // namespace

TEST(CommandPaletteRowsTest, TheTopMatchLeadsAloneThenEveryOtherHitUnderItsGroupInGroupOrder) {
    const auto rows = buildCommandPaletteRows(sampleEntries(), {}, {}, "timeline");
    const std::vector<std::pair<juce::String, juce::String>> expected{
        {"Top match", "Timeline"},
        {"Actions", "Focus Timeline"},
        {"Actions", "Toggle Timeline Panel"},
    };
    EXPECT_EQ(layout(rows), expected);
    EXPECT_EQ(rows.highlight, "timeline");
}

TEST(CommandPaletteRowsTest, LettersAndDigitsTypedTogetherFindTheNumberedModule) {
    const auto rows = buildCommandPaletteRows(sampleEntries(), {}, {}, "lfo6");
    ASSERT_FALSE(rows.items.empty());
    EXPECT_EQ(rows.items.front().category, "Top match");
    EXPECT_EQ(rows.items.front().text, "LFO 6");
    EXPECT_EQ(rows.items.size(), 1u) << "LFO 1 has no 6";
}

TEST(CommandPaletteRowsTest, ADisabledRowNeverLeads) {
    auto entries = sampleEntries();
    entries[0].enabled = false; // the Timeline view
    const auto rows = buildCommandPaletteRows(entries, {}, {}, "timeline");
    ASSERT_FALSE(rows.items.empty());
    EXPECT_EQ(rows.items.front().text, "Focus Timeline");
    EXPECT_EQ(rows.items.front().category, "Top match");
    EXPECT_EQ(rows.items[1].text, "Timeline") << "still listed under Views, greyed out";
    EXPECT_FALSE(rows.items[1].enabled);
}

TEST(CommandPaletteRowsTest, AnEmptyQueryListsRecentPicksFirstThenTheViews) {
    const juce::StringArray recent{"module:b", "gone:key", "action:focusTimeline"};
    const auto rows = buildCommandPaletteRows(sampleEntries(), {}, recent, "");
    const std::vector<std::pair<juce::String, juce::String>> expected{
        {"Recent", "LFO 6"},
        {"Recent", "Focus Timeline"},
        {"Views", "Timeline"},
        {"Views", "Mixer"},
    };
    EXPECT_EQ(layout(rows), expected);
}

TEST(CommandPaletteRowsTest, APlusNarrowsToTheAddListRowsWithTheBestFirst) {
    const std::vector<ModMatrixPicker::Item> add{addRow(1, "Tracks", "MIDI track"), addRow(2, "Sources", "Noise"),
                                                 addRow(3, "Sources", "Oscillator"),
                                                 addRow(4, "Sources", "Wavetable Oscillator")};
    const auto rows = buildCommandPaletteRows(sampleEntries(), add, {}, "+ osc");
    const std::vector<std::pair<juce::String, juce::String>> expected{
        {"Top match", "Oscillator"},
        {"Sources", "Wavetable Oscillator"},
    };
    EXPECT_EQ(layout(rows), expected);
    EXPECT_EQ(rows.highlight, "osc");
    EXPECT_EQ(rows.items.front().id, 3) << "the Add list's own id, so the pick runs the Add list's code";
    EXPECT_TRUE(synth::ui::isAddListQuery(" +"));
    EXPECT_FALSE(synth::ui::isAddListQuery("osc+"));
}

TEST(CommandPaletteRowsTest, ABarePlusListsEveryAddRowInItsOwnOrder) {
    const std::vector<ModMatrixPicker::Item> add{addRow(1, "Tracks", "MIDI track"), addRow(2, "Sources", "Noise")};
    const auto rows = buildCommandPaletteRows(sampleEntries(), add, {}, "+");
    ASSERT_EQ(rows.items.size(), 2u);
    EXPECT_EQ(rows.items[0].text, "MIDI track");
    EXPECT_EQ(rows.items[0].category, "Tracks");
}

TEST(CommandPaletteRowsTest, EntryIdsRoundTripAndStayClearOfTheAddListIds) {
    const auto rows = buildCommandPaletteRows(sampleEntries(), {}, {}, "mixer");
    ASSERT_FALSE(rows.items.empty());
    EXPECT_EQ(synth::ui::paletteEntryIndexForId(rows.items.front().id), std::optional<int>(1));
    EXPECT_FALSE(synth::ui::paletteEntryIndexForId(synth::ui::kAddListLibraryIdBase + 5).has_value());
    EXPECT_EQ(rows.items.front().tag, "View");
}

TEST(CommandPaletteRowsTest, ARecentPickMovesToTheFrontAndTheListIsCapped) {
    juce::StringArray recent{"a", "b", "c"};
    recent = synth::ui::pushRecentPaletteKey(recent, "c", 3);
    EXPECT_EQ(recent, (juce::StringArray{"c", "a", "b"}));
    recent = synth::ui::pushRecentPaletteKey(recent, "d", 3);
    EXPECT_EQ(recent, (juce::StringArray{"d", "c", "a"}));
}
