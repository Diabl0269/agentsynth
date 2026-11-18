// dropNotesOnSequencerTracks / fixSequencerInstrumentWiring / applyServerRepairs: the C++ port of the
// hosted service's sequencer repairs and post-processing order.
#include "AI/ProjectGenerate/SequencerRepair.h"
#include <gtest/gtest.h>

using synth::projectgenerate::applyServerRepairs;
using synth::projectgenerate::dropNotesOnSequencerTracks;
using synth::projectgenerate::fixSequencerInstrumentWiring;

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

juce::String text(const juce::var& v) { return juce::JSON::toString(v, true); }

// A pattern sequencer (id 5) feeding the new "Bass" track's instrument (id 1), plus clip notes on Bass and Lead.
juce::var doubledPart(const juce::String& connection = R"({"src": 5, "dst": 1, "srcPort": -1, "dstPort": -1})",
                      const juce::String& sequencerExtra = R"(, "params": {"run": 1})") {
    return parse(R"({"mode": "merge",
      "nodes": [{"id": 5, "type": "Sequencer", "state": {"steps": []})" +
                 sequencerExtra + R"(}],
      "connections": [)" +
                 connection + R"(],
      "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 1},
        {"op": "addInstrumentTrack", "name": "Lead", "instrument": "Oscillator", "instrumentId": 2},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4}]},
        {"op": "placeClips", "track": "Lead", "clips": [{"startBeat": 0, "lengthBeats": 4}]}]})");
}

juce::StringArray opNames(const juce::var& env) {
    juce::StringArray names;
    for (const juce::var& op : *env["timelineOps"].getArray())
        names.add(op["op"].toString() + ":" + op["track"].toString());
    return names;
}

} // namespace

TEST(SequencerRepair, DropsClipNotesOnATrackWhoseInstrumentASequencerFeeds) {
    const juce::var out = dropNotesOnSequencerTracks(doubledPart());
    EXPECT_EQ(opNames(out).joinIntoString(","), "addInstrumentTrack:,addInstrumentTrack:,placeClips:Lead");
}

TEST(SequencerRepair, LeavesTheAnswerAloneWhenNothingIsDoubled) {
    const juce::var noSequencer =
        parse(R"({"nodes": [{"id": 5, "type": "Oscillator"}], "connections": [{"src": 5, "dst": 1}],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrumentId": 1},
                        {"op": "placeClips", "track": "Bass", "clips": [{}]}]})");
    EXPECT_EQ(text(dropNotesOnSequencerTracks(noSequencer)), text(noSequencer));

    // A sequencer with no pattern ("state") does not play the part.
    const juce::var noState =
        parse(R"({"nodes": [{"id": 5, "type": "Sequencer"}], "connections": [{"src": 5, "dst": 1}],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrumentId": 1},
                        {"op": "placeClips", "track": "Bass", "clips": [{}]}]})");
    EXPECT_EQ(text(dropNotesOnSequencerTracks(noState)), text(noState));

    // A selector by index, or a track the answer did not create, is left alone.
    const juce::var byIndex =
        parse(R"({"nodes": [{"id": 5, "type": "Poly Sequencer", "state": {}}], "connections": [{"src": 5, "dst": 1}],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrumentId": 1},
                        {"op": "placeClips", "track": {"index": 0}, "clips": [{}]},
                        {"op": "placeClips", "track": "Existing", "clips": [{}]}]})");
    EXPECT_EQ(text(dropNotesOnSequencerTracks(byIndex)), text(byIndex));
}

TEST(SequencerRepair, RewritesTheCableToTheMidiSentinelAndTurnsTheSequencerOn) {
    const juce::var out =
        fixSequencerInstrumentWiring(doubledPart(R"({"src": 5, "dst": 1, "srcPort": 0, "dstPort": 0})", R"()"));
    const juce::var cable = (*out["connections"].getArray())[0];
    EXPECT_EQ(static_cast<int>(cable["srcPort"]), -1);
    EXPECT_EQ(static_cast<int>(cable["dstPort"]), -1);
    EXPECT_EQ(static_cast<int>((*out["nodes"].getArray())[0]["params"]["run"]), 1);
}

TEST(SequencerRepair, KeepsAStatedRunValueAndAnAlreadyCorrectCable) {
    const juce::var stopped =
        doubledPart(R"({"src": 5, "dst": 1, "srcPort": -1, "dstPort": -1})", R"(, "params": {"run": 0})");
    EXPECT_EQ(text(fixSequencerInstrumentWiring(stopped)), text(stopped));
    const juce::var cable = (*fixSequencerInstrumentWiring(doubledPart(
        R"({"src": 5, "dst": 1, "srcPort": 0, "dstPort": 0})", R"(, "params": {"run": 0})"))["connections"]
                                  .getArray())[0];
    EXPECT_EQ(static_cast<int>(cable["srcPort"]), -1);
}

TEST(SequencerRepair, WiringRepairIgnoresCablesThatDoNotFeedANewInstrument) {
    const juce::var other = parse(
        R"({"nodes": [{"id": 5, "type": "Sequencer"}], "connections": [{"src": 5, "dst": 9, "srcPort": 0, "dstPort": 0}],
        "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrumentId": 1}]})");
    EXPECT_EQ(text(fixSequencerInstrumentWiring(other)), text(other));
}

TEST(SequencerRepair, ApplyServerRepairsExpandsTheSongThenRepairsTheSequencerParts) {
    const juce::var env = parse(R"({"mode": "merge",
      "nodes": [{"id": 5, "type": "Sequencer", "state": {}}],
      "connections": [{"src": 5, "dst": 1, "srcPort": 0, "dstPort": 0}],
      "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 1}],
      "song": {"bpm": 120,
        "patterns": [{"id": "b", "track": "Bass", "bars": 1, "notes": "0:1:40"}, {"id": "l", "track": "Lead", "bars": 1, "notes": "0:1:60"}],
        "sections": [{"name": "A", "bars": 1, "play": [{"pattern": "b"}, {"pattern": "l"}]}]}})");
    juce::StringArray warnings;
    const juce::var out = applyServerRepairs(env, &warnings);
    EXPECT_TRUE(warnings.isEmpty());
    EXPECT_FALSE(out.hasProperty("song"));
    EXPECT_EQ(opNames(out).joinIntoString(","), "addInstrumentTrack:,setTempo:,addMarker:,placeClips:Lead");
    EXPECT_EQ(static_cast<int>((*out["connections"].getArray())[0]["srcPort"]), -1);
    EXPECT_EQ(static_cast<int>((*out["nodes"].getArray())[0]["params"]["run"]), 1);
}

TEST(SequencerRepair, AnEnvelopeNeedingNoRepairComesBackUnchanged) {
    const juce::var env = parse(R"({"mode": "merge", "nodes": [], "connections": []})");
    EXPECT_EQ(text(applyServerRepairs(env)), text(env));
}
