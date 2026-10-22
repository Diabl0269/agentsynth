// The sound filter: what passes (kind, sound tag, tempo range, near song tempo with half and double, key, length,
// place), key-name parsing, filtered folder listings and search, and the model that turns switches into chips.
#include "SoundBrowser/SoundSearch.h"
#include "UI/Library/SoundBrowser/SoundFilterModel.h"
#include <gtest/gtest.h>

using namespace synth::soundbrowser;
using synth::ui::SoundFilterModel;

namespace {

SoundEntry entry(const juce::String& name, SoundKind kind, const juce::String& place, const juce::String& folder,
                 double seconds, double tempo = 0.0, const juce::String& key = {}, const juce::StringArray& tags = {}) {
    SoundEntry e;
    e.name = name;
    e.kind = kind;
    e.placeId = place;
    e.placeName = place;
    e.folder = folder;
    e.path = "/x/" + place + "/" + folder + "/" + name + ".wav";
    e.lengthSeconds = seconds;
    e.tempo = tempo;
    e.key = key;
    e.tags = tags;
    return e;
}

SoundSnapshot makeSnapshot() {
    SoundSnapshot s;
    s.places.push_back({"core", "Core", {}, true, 0});
    s.places.push_back({"user", "User", {}, false, 0});
    s.entries = {
        entry("kick", SoundKind::OneShot, "core", "Drums", 0.4, 0.0, {}, {"Drums"}),
        entry("snare", SoundKind::OneShot, "core", "Drums", 0.9, 0.0, {}, {"Drums"}),
        entry("bassline", SoundKind::Loop, "core", "Loops", 3.0, 90.0, "A minor", {"Bass"}),
        entry("keys_loop", SoundKind::Loop, "user", "Field", 6.0, 120.0, "C major", {"Keys"}),
        entry("half_loop", SoundKind::Loop, "user", "Field", 8.0, 60.0, "Am", {"Drums"}),
        entry("double_loop", SoundKind::Loop, "user", "Field", 2.0, 240.0, "D minor", {"FX"}),
        entry("piano", SoundKind::Instrument, "core", "Piano", 0.0, 0.0, {}, {"Keys"}),
    };
    return s;
}

juce::StringArray names(const std::vector<SoundEntry>& entries) {
    juce::StringArray out;
    for (const auto& e : entries)
        out.add(e.name);
    out.sort(false);
    return out;
}

juce::StringArray filtered(const SoundFilter& f) { return names(searchSnapshot(makeSnapshot(), "", f)); }

} // namespace

TEST(SoundFilterTest, AnEmptyFilterKeepsEverythingAndIsInactive) {
    SoundFilter f;
    EXPECT_FALSE(f.isActive());
    EXPECT_EQ(filtered(f).size(), 7);
}

TEST(SoundFilterTest, KindsAndTagsMatchAnyOneWithinTheirOwnGroup) {
    SoundFilter f;
    f.kinds = {SoundKind::Loop, SoundKind::Instrument};
    EXPECT_EQ(filtered(f).joinIntoString(","), "bassline,double_loop,half_loop,keys_loop,piano");
    f.tags = {"Keys", "Bass"};
    EXPECT_EQ(filtered(f).joinIntoString(","), "bassline,keys_loop,piano");
    f.kinds = {SoundKind::OneShot};
    EXPECT_EQ(filtered(f).size(), 0) << "kind and tag groups both have to match";
}

TEST(SoundFilterTest, TempoRangeIsInclusiveAndUnknownTempoFailsIt) {
    SoundFilter f;
    f.tempo = std::make_pair(90.0, 120.0);
    EXPECT_EQ(filtered(f).joinIntoString(","), "bassline,keys_loop");
    f.tempo = std::make_pair(91.0, 119.0);
    EXPECT_EQ(filtered(f).size(), 0);
}

TEST(SoundFilterTest, NearSongTempoCountsHalfAndDoubleAndTenBpm) {
    SoundFilter f;
    f.nearTempo = 120.0;
    // 120 itself, 60 (half), 240 (double); 90 is 30 away and stays out.
    EXPECT_EQ(filtered(f).joinIntoString(","), "double_loop,half_loop,keys_loop");
    f.nearTempo = 130.0; // 120 is within 10; half is 65, so 60 passes; double is 260, so 240 does not
    EXPECT_EQ(filtered(f).joinIntoString(","), "half_loop,keys_loop");
    f.nearTempo = 150.0; // 75 and 300 are too far from 60 and 240, and 120 and 90 are over 10 from 150
    EXPECT_EQ(filtered(f).size(), 0);
    f.nearTempo = 85.0;
    EXPECT_TRUE(filtered(f).contains("bassline"));
    f.nearTempo = 45.0; // double is 90
    EXPECT_TRUE(filtered(f).contains("bassline"));
}

TEST(SoundFilterTest, KeysAreParsedInEveryWritingAndMatchedByPitchAndMode) {
    EXPECT_EQ(parseSoundKey("A minor").pitchClass, 9);
    EXPECT_EQ(parseSoundKey("A minor").mode, 2);
    EXPECT_EQ(parseSoundKey("Am").pitchClass, 9);
    EXPECT_EQ(parseSoundKey("Am").mode, 2);
    EXPECT_EQ(parseSoundKey("C#").pitchClass, 1);
    EXPECT_EQ(parseSoundKey("Db major").pitchClass, 1);
    EXPECT_EQ(parseSoundKey("Db major").mode, 1);
    EXPECT_EQ(parseSoundKey("Ebm").pitchClass, 3);
    EXPECT_EQ(parseSoundKey("Ebm").mode, 2);
    EXPECT_EQ(parseSoundKey("nonsense").pitchClass, -1);

    SoundFilter f;
    f.key = "A minor";
    EXPECT_EQ(filtered(f).joinIntoString(","), "bassline,half_loop") << "Am and A minor are one key";
    f.key = "C";
    EXPECT_EQ(filtered(f).joinIntoString(","), "keys_loop");
    f.key = "A major";
    EXPECT_EQ(filtered(f).size(), 0) << "mode matters when both sides know it";
}

TEST(SoundFilterTest, LengthBucketsAndPlace) {
    SoundFilter f;
    f.length = SoundLength::Under1;
    EXPECT_EQ(filtered(f).joinIntoString(","), "kick,snare") << "an instrument has no length and fails";
    f.length = SoundLength::OneToFour;
    EXPECT_EQ(filtered(f).joinIntoString(","), "bassline,double_loop");
    f.length = SoundLength::Over4;
    EXPECT_EQ(filtered(f).joinIntoString(","), "half_loop,keys_loop");
    f = {};
    f.placeId = "user";
    EXPECT_EQ(filtered(f).joinIntoString(","), "double_loop,half_loop,keys_loop");
}

TEST(SoundFilterTest, SearchAndFolderListingsBothUseTheFilter) {
    const auto snap = makeSnapshot();
    SoundFilter f;
    f.kinds = {SoundKind::Loop};
    EXPECT_EQ(names(searchSnapshot(snap, "loop", f)).joinIntoString(","), "bassline,double_loop,half_loop,keys_loop")
        << "the Loops folder name matches too";

    const auto all = listFolder(snap, "core", "");
    EXPECT_EQ(all.folders.size(), 3u);
    const auto loops = listFolder(snap, "core", "", f);
    ASSERT_EQ(loops.folders.size(), 1u) << "folders without a passing sound are left out";
    EXPECT_EQ(loops.folders[0].name, "Loops");
    EXPECT_EQ(loops.folders[0].soundCount, 1);

    f.kinds = {SoundKind::OneShot};
    const auto drums = listFolder(snap, "core", "Drums", f);
    EXPECT_EQ(drums.entries.size(), 2u);
    f.tags = {"Bass"};
    EXPECT_EQ(listFolder(snap, "core", "Drums", f).entries.size(), 0u);
}

TEST(SoundFilterModelTest, ChipsStandForEverySwitchAndRemoveSwitchesOneOff) {
    SoundFilterModel m;
    EXPECT_EQ(m.count(), 0);
    m.setKind(SoundKind::Loop, true);
    m.setTag("Drums", true);
    m.setTempoRange(std::make_pair(100, 90)); // reversed on purpose
    m.setNearSongTempo(true);
    m.setLength(SoundLength::OneToFour);
    m.setPlace("core", "Core instruments");
    m.setPickedKey("A minor");
    const auto chips = m.chips();
    ASSERT_EQ(chips.size(), 7u);
    EXPECT_EQ(chips[0].text, "Loops");
    EXPECT_EQ(chips[0].tooltip, "Remove Loops filter");
    EXPECT_EQ(chips[2].text, "90-100 BPM");
    EXPECT_EQ(m.toFilter().tempo->first, 90.0);

    m.remove("tempo");
    EXPECT_FALSE(m.tempoRange().has_value());
    m.remove("kind:" + juce::String((int)SoundKind::Loop));
    EXPECT_FALSE(m.hasKind(SoundKind::Loop));
    m.remove("tag:Drums");
    m.remove("nearTempo");
    m.remove("length");
    m.remove("place");
    m.remove("key");
    EXPECT_EQ(m.count(), 0);
    EXPECT_FALSE(m.toFilter().isActive());
}

TEST(SoundFilterModelTest, TempoClampsSongKeyNeedsASongAndClearAllEmpties) {
    SoundFilterModel m;
    m.setTempoRange(std::make_pair(10, 500));
    EXPECT_EQ(*m.tempoRange(), std::make_pair(60, 200));
    m.setUseSongKey(true);
    EXPECT_FALSE(m.useSongKey()) << "no song key, no Song key filter";
    m.setSongKey("D minor");
    m.setUseSongKey(true);
    EXPECT_TRUE(m.useSongKey());
    EXPECT_EQ(m.toFilter().key, "D minor");
    m.setPickedKey("C");
    EXPECT_FALSE(m.useSongKey()) << "picking a key switches Song key off";
    m.setSongTempo(100.0);
    m.setNearSongTempo(true);
    EXPECT_EQ(*m.toFilter().nearTempo, 100.0);
    m.clearAll();
    EXPECT_EQ(m.count(), 0);
}
