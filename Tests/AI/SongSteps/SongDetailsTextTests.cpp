// The plain-language details a built song shows under "Show details": tracks with their sound modules and note counts,
// the sections with their bars, and the automation lanes.
#include "AI/SongSteps/SongDetailsText.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

using namespace synth::songsteps;

namespace {

std::vector<PlanSection> sections() {
    PlanSection intro, drop;
    intro.name = "Intro";
    intro.bars = 8;
    drop.name = "Drop";
    drop.bars = 16;
    return {intro, drop};
}

juce::String midiWithNotes(int count) {
    juce::MidiMessageSequence sequence;
    for (int i = 0; i < count; ++i) {
        sequence.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8)100), i * 480.0);
        sequence.addEvent(juce::MidiMessage::noteOff(1, 60 + i), i * 480.0 + 240.0);
    }
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(sequence);
    juce::MemoryOutputStream out;
    file.writeTo(out);
    return juce::Base64::toBase64(out.getData(), out.getDataSize());
}

} // namespace

TEST(SongDetailsText, OneLinePerTrackNamesItsModulesAndCountsItsNotes) {
    const auto envelope = juce::JSON::parse(R"({"nodes":[],"timelineOps":[
        {"op":"addInstrumentTrack","name":"Lead","instrument":"Oscillator","inserts":[{"type":"Filter"},{"type":"Filter"}]},
        {"op":"addInstrumentTrack","name":"Kit","instrument":"Drum Kit"},
        {"op":"placeClips","track":"Lead","clips":[{"notes":[{"pitch":60},{"pitch":62}]},{"notes":[{"pitch":64}]}]}]})");
    const auto lines = juce::StringArray::fromLines(buildSongDetailsText(envelope, {}));
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], juce::String("Lead: Oscillator, Filter, 3 notes"));
    EXPECT_EQ(lines[1], juce::String("Kit: Drum Kit, 0 notes"));
}

TEST(SongDetailsText, AMidiClipCountsItsNoteOns) {
    const auto json =
        juce::String(R"({"timelineOps":[{"op":"addInstrumentTrack","name":"Bass","instrument":"Oscillator"},
        {"op":"placeMidiClip","track":"Bass","startBeat":0,"midBase64":")") +
        midiWithNotes(5) + R"("}]})";
    EXPECT_EQ(buildSongDetailsText(juce::JSON::parse(json), {}), juce::String("Bass: Oscillator, 5 notes"));
}

TEST(SongDetailsText, SectionsAreListedWithTheirBarsAndTheTotal) {
    const auto text = buildSongDetailsText(juce::JSON::parse(R"({"timelineOps":[]})"), sections());
    EXPECT_EQ(text, juce::String("2 sections: Intro 8, Drop 16 (24 bars in all)"));
}

TEST(SongDetailsText, ALaneNamesItsParameterBarsAndSections) {
    const auto envelope = juce::JSON::parse(R"({"nodes":[{"id":3,"type":"Filter"}],"timelineOps":[
        {"op":"writeLane","nodeId":3,"paramId":"cutoff","points":[{"beat":0,"value":1},{"beat":36,"value":2}]},
        {"op":"writeLane","nodeUuid":"u","paramId":"gain","points":[{"beat":4,"value":1}]}]})");
    const auto lines = juce::StringArray::fromLines(buildSongDetailsText(envelope, sections()));
    ASSERT_EQ(lines.size(), 3); // the sections line, then the two lanes
    EXPECT_EQ(lines[1], juce::String("Automation: cutoff on Filter, bars 1 to 10 (Intro to Drop)"));
    EXPECT_EQ(lines[2], juce::String("Automation: gain, bar 2 (Intro)"));
}

TEST(SongDetailsText, AnEnvelopeWithNothingToDescribeGivesNoText) {
    EXPECT_TRUE(buildSongDetailsText(juce::JSON::parse(R"({"mode":"merge"})"), {}).isEmpty());
    EXPECT_TRUE(buildSongDetailsText({}, {}).isEmpty());
}

TEST(SongDetailsText, ALongListOfLanesIsCutWithACountOfWhatWasLeftOut) {
    juce::String ops;
    for (int i = 0; i < kMaxDetailLines + 3; ++i)
        ops << (i ? "," : "") << R"({"op":"writeLane","paramId":"p","points":[{"beat":0}]})";
    const auto lines =
        juce::StringArray::fromLines(buildSongDetailsText(juce::JSON::parse("{\"timelineOps\":[" + ops + "]}"), {}));
    EXPECT_EQ(lines.size(), kMaxDetailLines + 1);
    EXPECT_EQ(lines[lines.size() - 1], juce::String("... and 3 more automation lanes"));
}
