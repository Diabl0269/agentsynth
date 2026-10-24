// The audio Input menu's "Another track" submenu and the row words for a track source, as pure functions of the
// choice and the listed tracks (docs/timeline/tracks.md#the-input-row).
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelineRoutingPane/AudioInputMenu.h"
#include <gtest/gtest.h>

namespace {

using synth::TrackKind;
using synth::ui::AudioInputMenuAction;
using synth::ui::AudioInputMenuIds;
using synth::ui::AudioInputTrackSource;

struct Row {
    int id;
    juce::String text;
    bool enabled;
    bool ticked;
};

// The "Another track" submenu's rows, or nullopt-like empty plus found=false when the menu has no such entry.
struct SubmenuView {
    bool found = false;
    bool enabled = false;
    bool ticked = false;
    std::vector<Row> rows;
};

SubmenuView anotherTrackSubmenu(juce::PopupMenu& menu) {
    SubmenuView view;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.text != "Another track" || item.subMenu == nullptr)
            continue;
        view.found = true;
        view.enabled = item.isEnabled;
        view.ticked = item.isTicked;
        juce::PopupMenu::MenuItemIterator sub(*item.subMenu);
        while (sub.next())
            view.rows.push_back(
                {sub.getItem().itemID, sub.getItem().text, sub.getItem().isEnabled, sub.getItem().isTicked});
    }
    return view;
}

synth::AudioInputChoice trackChoice(synth::TrackId source) {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::Track;
    choice.sourceTrack = source;
    return choice;
}

} // namespace

TEST(AudioInputMenuTrackSourceTest, ListsEveryOtherNonAutomationTrackInTimelineOrderAndNotTheTarget) {
    synth::TimelineDoc doc;
    const auto lead = doc.addTrack(TrackKind::Midi, "Lead synth");
    const auto vox = doc.addTrack(TrackKind::Audio, "Vox");
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");
    doc.addTrack(TrackKind::Automation, "Auto");

    const auto sources = synth::ui::listAudioInputTrackSources(doc, vox, [&](synth::TrackId id) { return id != bass; });
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_EQ(sources[0].id, lead);
    EXPECT_EQ(sources[0].name, "Lead synth");
    EXPECT_TRUE(sources[0].hasChannel);
    EXPECT_EQ(sources[1].id, bass);
    EXPECT_FALSE(sources[1].hasChannel);

    auto menu = synth::ui::buildAudioInputMenu({}, synth::AudioInputStatus{}, sources);
    const auto sub = anotherTrackSubmenu(menu);
    ASSERT_TRUE(sub.found);
    EXPECT_TRUE(sub.enabled);
    EXPECT_FALSE(sub.ticked);
    ASSERT_EQ(sub.rows.size(), 2u);
    EXPECT_EQ(sub.rows[0].id, AudioInputMenuIds::kFirstTrack);
    EXPECT_EQ(sub.rows[0].text, "Lead synth");
    EXPECT_TRUE(sub.rows[0].enabled);
    EXPECT_EQ(sub.rows[1].text, "Bass (no mixer channel)");
    EXPECT_FALSE(sub.rows[1].enabled) << "a track with no channel cannot be recorded";
}

TEST(AudioInputMenuTrackSourceTest, TheChosenSourceIsTickedAndAnEmptyListDisablesTheSubmenu) {
    const std::vector<AudioInputTrackSource> sources = {{synth::TrackId{1}, "Lead", true},
                                                        {synth::TrackId{3}, "Bass", true}};
    auto menu = synth::ui::buildAudioInputMenu(trackChoice(synth::TrackId{3}), synth::AudioInputStatus{}, sources);
    const auto sub = anotherTrackSubmenu(menu);
    ASSERT_TRUE(sub.found);
    EXPECT_TRUE(sub.ticked);
    EXPECT_FALSE(sub.rows[0].ticked);
    EXPECT_TRUE(sub.rows[1].ticked);

    auto empty = synth::ui::buildAudioInputMenu({}, synth::AudioInputStatus{}, {});
    const auto none = anotherTrackSubmenu(empty);
    ASSERT_TRUE(none.found);
    EXPECT_FALSE(none.enabled) << "no other track to record";
}

TEST(AudioInputMenuTrackSourceTest, APickedIdResolvesToThatTrackAndANoChannelOrUnknownIdPicksNothing) {
    const std::vector<AudioInputTrackSource> sources = {{synth::TrackId{4}, "Lead", true},
                                                        {synth::TrackId{9}, "Bass", false}};
    const synth::AudioInputStatus status;
    const auto lead = synth::ui::resolveAudioInputMenuChoice(AudioInputMenuIds::kFirstTrack, status, sources);
    ASSERT_EQ(lead.kind, AudioInputMenuAction::Kind::Pick);
    EXPECT_EQ(lead.choice, trackChoice(synth::TrackId{4}));
    EXPECT_EQ(synth::ui::resolveAudioInputMenuChoice(AudioInputMenuIds::kFirstTrack + 1, status, sources).kind,
              AudioInputMenuAction::Kind::Nothing);
    EXPECT_EQ(synth::ui::resolveAudioInputMenuChoice(AudioInputMenuIds::kFirstTrack + 2, status, sources).kind,
              AudioInputMenuAction::Kind::Nothing);
    EXPECT_EQ(synth::ui::resolveAudioInputMenuChoice(AudioInputMenuIds::kWholeMix, status, sources).choice.kind,
              synth::AudioInputChoice::Kind::WholeMix)
        << "the other entries still resolve";
}

TEST(AudioInputMenuTrackSourceTest, TheRowNamesTheSourceAndWarnsWhenItIsRemovedOrHasNoChannel) {
    const std::vector<AudioInputTrackSource> sources = {{synth::TrackId{1}, "Lead synth", true},
                                                        {synth::TrackId{2}, "Bass", false}};
    const synth::AudioInputStatus status;
    auto row = synth::ui::describeAudioInput(trackChoice(synth::TrackId{1}), status, sources);
    EXPECT_EQ(row.text, "Lead synth");
    EXPECT_FALSE(row.warning);

    row = synth::ui::describeAudioInput(trackChoice(synth::TrackId{2}), status, sources);
    EXPECT_EQ(row.text, "Bass (no mixer channel)");
    EXPECT_TRUE(row.warning);

    row = synth::ui::describeAudioInput(trackChoice(synth::TrackId{7}), status, sources);
    EXPECT_EQ(row.text, "(track removed)");
    EXPECT_TRUE(row.warning) << "the same warning colour a device that is not connected gets";
    EXPECT_TRUE(row.note.isNotEmpty());

    auto menu = synth::ui::buildAudioInputMenu(trackChoice(synth::TrackId{7}), status, sources);
    bool ticked = false;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().itemID == AudioInputMenuIds::kTrackRemoved)
            ticked = it.getItem().isTicked && it.getItem().text == "(track removed)";
    EXPECT_TRUE(ticked) << "a removed source stays on the menu, ticked";
    EXPECT_EQ(synth::ui::resolveAudioInputMenuChoice(AudioInputMenuIds::kTrackRemoved, status, sources).kind,
              AudioInputMenuAction::Kind::Nothing);
}
