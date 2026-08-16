// TimelineOps, the app-side timeline tools: validate (untrusted) -> preview -> the user
// clicks Apply -> apply, as ONE undo step.
//
// The two properties every case here is really defending: the preview cannot describe an apply
// that then fails (validate and apply run the same code, one against a throwaway copy of the
// document and one against the real one), and a batch is all-or-nothing (one bad op leaves the
// document byte-identical). The per-op checks are validateTimeline's, reused rather than
// re-stated, so the cap/bounds cases below assert against ITS constants — a change there must move
// this file too, or the two gates have drifted.

#include "AI/AIIntegrationService.h"
#include "AI/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include "Timeline/TimelineValidator.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

#if SYNTH_ENABLE_TIMELINE
#include "Transport/TransportService.h"
#endif

using synth::TimelineDoc;
using synth::TimelineOps;
using synth::TimelineOpsResult;
using synth::TrackKind;

namespace {

// FilterModule's "cutoff", the live parameter every writeLane case here automates. Its real range
// is 20 .. 20000 Hz — restated because the value-bounds cases depend on those exact numbers, the
// same way TimelineValidatorTests.cpp restates them.
constexpr double kCutoffMin = 20.0;
constexpr double kCutoffMax = 20000.0;

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    // A typo in a test's own JSON must fail as a broken test, not as a passing rejection.
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

juce::var envelopeOf(const juce::String& opsArrayJson) { return parse("{\"timelineOps\": " + opsArrayJson + "}"); }

// Deep equality by serialised form, the convention TimelineDocTests/TimelineUndoTests use.
juce::String dump(const TimelineDoc& doc) { return juce::JSON::toString(doc.toVar()); }

juce::String notesJson(int count, int firstPitch = 36) {
    juce::StringArray notes;
    for (int i = 0; i < count; ++i)
        notes.add("{\"startBeat\": " + juce::String(i * 0.5) + ", \"lengthBeats\": 0.5, \"pitch\": " +
                  juce::String(firstPitch + i) + ", \"velocity\": 100, \"channel\": 1}");
    return "[" + notes.joinIntoString(", ") + "]";
}

juce::String pointsJson(int count, double value = 800.0) {
    juce::StringArray points;
    for (int i = 0; i < count; ++i)
        points.add("{\"beat\": " + juce::String(i) + ", \"value\": " + juce::String(value) +
                   ", \"tension\": 0, \"curve\": 1}");
    return "[" + points.joinIntoString(", ") + "]";
}

const synth::Track* findTrackByName(const TimelineDoc& doc, const juce::String& name) {
    for (const auto& track : doc.getTracks())
        if (track.name == name)
            return &track;
    return nullptr;
}

// A real base64-encoded Standard MIDI File, type 1, PPQ, two tracks of 17 notes each (34 total) -
// generated once with MidiClipFile::exportClip and embedded, the same bytes
// Tools/TimelineOpsHarness/Fixtures/02-valid-place-midi-clip.json pins.
constexpr const char* kValidMidBase64 =
    "TVRoZAAAAAYAAQACA8BNVHJrAAAAnQCQJGSDYIAkAACQJWSDYIAlAACQJmSDYIAmAACQJ2SDYIAnAACQKGSDYIAoAACQ"
    "KWSDYIApAACQKmSDYIAqAACQK2SDYIArAACQLGSDYIAsAACQLWSDYIAtAACQLmSDYIAuAACQL2SDYIAvAACQJGSDYIAk"
    "AACQJWSDYIAlAACQJmSDYIAmAACQJ2SDYIAnAACQKGSDYIAoAAD/LwBNVHJrAAAAnQCQPGSDYIA8AACQPWSDYIA9AACQ"
    "PmSDYIA+AACQP2SDYIA/AACQQGSDYIBAAACQQWSDYIBBAACQQmSDYIBCAACQQ2SDYIBDAACQRGSDYIBEAACQRWSDYIBF"
    "AACQRmSDYIBGAACQR2SDYIBHAACQPGSDYIA8AACQPWSDYIA9AACQPmSDYIA+AACQP2SDYIA/AACQQGSDYIBAAAD/LwA=";

// The same file with an SMPTE (25fps/40 subframes) time format instead of PPQ.
constexpr const char* kSmpteMidBase64 = "TVRoZAAAAAYAAQAB5yhNVHJrAAAADACQPGRkgDwAAP8vAA==";

} // namespace

class TimelineOpsTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    TimelineDoc doc;

    void SetUp() override {
        graph.clear();
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        osc->properties.set("uuid", "osc-uuid");
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        filter->properties.set("uuid", "filter-uuid");
    }

    TimelineOpsResult validate(const juce::var& envelope) { return TimelineOps::validate(envelope, doc, graph); }
    TimelineOpsResult apply(const juce::var& envelope) { return TimelineOps::apply(envelope, doc, graph, undoManager); }
};

// =============================================================================
// 1. Each op validates, then applies exactly what it said it would
// =============================================================================

TEST_F(TimelineOpsTest, AddTrackCreatesAnUnboundDocTrackAndNoGraphNode) {
    const int nodesBefore = graph.getNumNodes();
    const auto envelope = envelopeOf(R"([{"op": "addTrack", "kind": "midi", "name": "Bass"}])");

    const auto preview = validate(envelope);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(doc.isEmpty()) << "validate() must not touch the document";

    const auto applied = apply(envelope);
    ASSERT_TRUE(applied.ok) << applied.message;

    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks()[0];
    EXPECT_EQ(track.kind, TrackKind::Midi);
    EXPECT_EQ(track.name, "Bass");
    // v1 creates the DOC track only: wiring it to a Track In node stays a user/host gesture, and
    // the preview says so rather than leaving the user to discover a track that plays nowhere.
    EXPECT_TRUE(track.bindingUuid.isEmpty());
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_TRUE(applied.previewText.contains("unbound"));
}

TEST_F(TimelineOpsTest, PlaceClipsTargetsByNameAndPlacesTheExactNotes) {
    const auto envelope = envelopeOf(R"([
        {"op": "addTrack", "kind": "midi", "name": "Bass"},
        {"op": "placeClips", "track": "Bass", "clips": [
            {"startBeat": 8, "lengthBeats": 4, "name": "Chorus", "notes": [
                {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100, "channel": 1},
                {"startBeat": 2, "lengthBeats": 0.5, "pitch": 43, "velocity": 64, "channel": 2}]}]}])");

    ASSERT_TRUE(apply(envelope).ok);

    const auto* track = findTrackByName(doc, "Bass");
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 1u);

    const auto& clip = track->clips[0];
    EXPECT_EQ(clip.name, "Chorus");
    EXPECT_DOUBLE_EQ(clip.startBeat, 8.0);
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0);
    // Notes are clip-relative and land exactly as sent — nothing is clamped, rounded or quantised.
    ASSERT_EQ(clip.notes.size(), 2u);
    EXPECT_DOUBLE_EQ(clip.notes[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(clip.notes[0].lengthBeats, 1.0);
    EXPECT_EQ(clip.notes[0].pitch, 36);
    EXPECT_EQ(clip.notes[0].velocity, 100);
    EXPECT_EQ(clip.notes[0].channel, 1);
    EXPECT_DOUBLE_EQ(clip.notes[1].startBeat, 2.0);
    EXPECT_DOUBLE_EQ(clip.notes[1].lengthBeats, 0.5);
    EXPECT_EQ(clip.notes[1].pitch, 43);
    EXPECT_EQ(clip.notes[1].velocity, 64);
    EXPECT_EQ(clip.notes[1].channel, 2);
}

TEST_F(TimelineOpsTest, PlaceClipsTargetsByIndex) {
    doc.addTrack(TrackKind::Midi, "Lead");
    doc.addTrack(TrackKind::Midi, "Bass");

    ASSERT_TRUE(apply(envelopeOf(R"([{"op": "placeClips", "track": {"index": 1},
                                     "clips": [{"startBeat": 0, "lengthBeats": 2}]}])"))
                    .ok);

    EXPECT_TRUE(doc.getTracks()[0].clips.empty());
    ASSERT_EQ(doc.getTracks()[1].clips.size(), 1u);
    EXPECT_DOUBLE_EQ(doc.getTracks()[1].clips[0].lengthBeats, 2.0);
}

TEST_F(TimelineOpsTest, WriteLaneFindOrCreatesTheLaneOnTheAutomationTrack) {
    const auto envelope = envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
        "points": [{"beat": 0, "value": 400}, {"beat": 4, "value": 6000, "tension": 0.5, "curve": 2}]}])");

    ASSERT_TRUE(apply(envelope).ok);

    // The find-or-create rule, exactly as MainComponent::automateParameter does it: the doc
    // had no Automation track, so one was created to hold the lane.
    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_EQ(doc.getTracks()[0].kind, TrackKind::Automation);

    const auto* lane = doc.getLaneForParam("filter-uuid", "cutoff");
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 2u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(lane->points[0].value, 400.0);
    EXPECT_DOUBLE_EQ(lane->points[1].value, 6000.0);
    EXPECT_FLOAT_EQ(lane->points[1].tension, 0.5f);
    EXPECT_EQ(lane->points[1].curve, static_cast<int>(synth::BreakpointCurve::Bezier));
    EXPECT_FLOAT_EQ(lane->range.minValue, static_cast<float>(kCutoffMin));
    EXPECT_FLOAT_EQ(lane->range.maxValue, static_cast<float>(kCutoffMax));

    // A second write reuses the SAME lane and the same Automation track — no duplicates.
    ASSERT_TRUE(apply(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
                                     "points": [{"beat": 16, "value": 900}]}])"))
                    .ok);
    EXPECT_EQ(doc.getTracks().size(), 1u);
    EXPECT_EQ(doc.getTracks()[0].lanes.size(), 1u);
}

TEST_F(TimelineOpsTest, WriteLaneReplacesOnlyThePointsInsideTheWrittenSpan) {
    ASSERT_TRUE(apply(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
        "points": [{"beat": 0, "value": 100}, {"beat": 4, "value": 200},
                   {"beat": 8, "value": 300}, {"beat": 12, "value": 400}]}])"))
                    .ok);

    // Span 4..8 inclusive: the two points inside it are replaced, the ones outside survive
    // untouched. One editBreakpoints call, so this is one revision bump however many points moved.
    ASSERT_TRUE(apply(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
        "points": [{"beat": 4, "value": 999}, {"beat": 6, "value": 888}, {"beat": 8, "value": 777}]}])"))
                    .ok);

    const auto* lane = doc.getLaneForParam("filter-uuid", "cutoff");
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 5u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(lane->points[0].value, 100.0);
    EXPECT_DOUBLE_EQ(lane->points[1].value, 999.0);
    EXPECT_DOUBLE_EQ(lane->points[2].beat, 6.0);
    EXPECT_DOUBLE_EQ(lane->points[2].value, 888.0);
    EXPECT_DOUBLE_EQ(lane->points[3].value, 777.0);
    EXPECT_DOUBLE_EQ(lane->points[4].beat, 12.0);
    EXPECT_DOUBLE_EQ(lane->points[4].value, 400.0);
}

TEST_F(TimelineOpsTest, PlaceMidiClipDecodesAndPlacesExactlyWhatTheBlobContains) {
    const auto envelope = envelopeOf(juce::String(R"([
        {"op": "addTrack", "kind": "midi", "name": "Bass"},
        {"op": "placeMidiClip", "track": "Bass", "startBeat": 8, "midBase64": ")") +
                                     kValidMidBase64 + "\"}]");

    const auto preview = validate(envelope);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(doc.isEmpty()) << "validate() must not touch the document";
    EXPECT_EQ(preview.previewText, "Adds midi track \"Bass\" (unbound - bind it in the timeline panel); "
                                   "places 2 MIDI clips (34 notes) from a .mid blob at beat 8 on \"Bass\"");

    ASSERT_TRUE(apply(envelope).ok);

    const auto* track = findTrackByName(doc, "Bass");
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->clips.size(), 2u);
    for (const auto& clip : track->clips) {
        EXPECT_DOUBLE_EQ(clip.startBeat, 8.0) << "both clips land at the op's startBeat, stacked";
        ASSERT_EQ(clip.notes.size(), 17u);
        // Clip-relative, unchanged from the blob's own beat zero.
        EXPECT_DOUBLE_EQ(clip.notes[0].startBeat, 0.0);
    }

    // One undo step for the whole batch, same as every other op.
    ASSERT_TRUE(undoManager.canUndo());
    ASSERT_TRUE(undoManager.undo());
    EXPECT_TRUE(doc.isEmpty());
}

// =============================================================================
// 2. The whole batch is ONE undo step
// =============================================================================

TEST_F(TimelineOpsTest, BatchIsOneUndoStep) {
    const juce::String before = dump(doc);

    const auto envelope = envelopeOf(R"([
        {"op": "addTrack", "kind": "midi", "name": "Bass"},
        {"op": "placeClips", "track": "Bass", "clips": [
            {"startBeat": 0, "lengthBeats": 4, "name": "A", "notes": [
                {"startBeat": 0, "lengthBeats": 1, "pitch": 36},
                {"startBeat": 1, "lengthBeats": 1, "pitch": 38}]}]},
        {"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
         "points": [{"beat": 0, "value": 400}, {"beat": 8, "value": 5000}]}])");

    ASSERT_TRUE(apply(envelope).ok);
    // Three ops touching a track, a clip, two notes, an Automation track, a lane and two
    // breakpoints — and still exactly one entry on the shared undo stack.
    ASSERT_TRUE(undoManager.canUndo());
    EXPECT_NE(dump(doc), before);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), before) << "one Cmd+Z must revert the entire batch";
    EXPECT_FALSE(undoManager.canUndo());

    ASSERT_TRUE(undoManager.redo());
    EXPECT_NE(dump(doc), before);
}

// =============================================================================
// 3. All-or-nothing: one bad op rejects the batch and the doc is untouched
// =============================================================================

TEST_F(TimelineOpsTest, AllOrNothing) {
    struct Case {
        const char* what;
        const char* thirdOp;
    };
    const std::vector<Case> cases = {
        {"a note pitch outside the MIDI range",
         R"({"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4,
             "notes": [{"startBeat": 0, "lengthBeats": 1, "pitch": 200}]}]})"},
        {"an automation value outside the LIVE parameter range",
         R"({"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
             "points": [{"beat": 0, "value": 999999}]})"},
        {"a track name nothing in the document carries",
         R"({"op": "placeClips", "track": "Nowhere", "clips": [{"startBeat": 0, "lengthBeats": 4}]})"},
        {"a placeMidiClip blob using SMPTE time format",
         R"({"op": "placeMidiClip", "track": "Bass", "startBeat": 0,
             "midBase64": "TVRoZAAAAAYAAQAB5yhNVHJrAAAADACQPGRkgDwAAP8vAA=="})"},
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.what);

        TimelineDoc localDoc;
        AppUndoManager localUndo;
        const juce::String before = dump(localDoc);

        const juce::String ops = juce::String(R"([
            {"op": "addTrack", "kind": "midi", "name": "Bass"},
            {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4}]},
            )") + testCase.thirdOp +
                                 "]";

        const auto envelope = envelopeOf(ops);
        const auto validated = TimelineOps::validate(envelope, localDoc, graph);
        EXPECT_FALSE(validated.ok);
        EXPECT_TRUE(validated.previewText.isEmpty()) << "a rejected batch has nothing to preview";

        const auto applied = TimelineOps::apply(envelope, localDoc, graph, localUndo);
        EXPECT_FALSE(applied.ok);
        // The failing op is named by its index, so a caller (or a model being corrected) knows
        // WHICH of the three was wrong — the first two were perfectly valid.
        EXPECT_TRUE(applied.message.contains("timelineOps[2]")) << applied.message;
        // The two valid ops ahead of it applied to NOTHING: the doc is byte-identical and no
        // no-op entry was left on the undo stack.
        EXPECT_EQ(dump(localDoc), before);
        EXPECT_FALSE(localUndo.canUndo());
    }
}

// =============================================================================
// 4. Caps, bounds, and the capabilities the grammar simply does not have
// =============================================================================

TEST_F(TimelineOpsTest, CapsAndBoundsRejected) {
    doc.addTrack(TrackKind::Midi, "Bass");

    // Beats, against validateTimeline's own kMaxPpqUntrusted.
    EXPECT_FALSE(validate(envelopeOf("[{\"op\": \"placeClips\", \"track\": \"Bass\", \"clips\": [{\"startBeat\": " +
                                     juce::String(synth::kMaxPpqUntrusted + 1.0) + ", \"lengthBeats\": 4}]}]"))
                     .ok);
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "placeClips", "track": "Bass",
                                 "clips": [{"startBeat": 0, "lengthBeats": 0}]}])"))
                     .ok);
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "placeClips", "track": "Bass",
                                 "clips": [{"startBeat": -1, "lengthBeats": 4}]}])"))
                     .ok);

    // Notes: rejected, never clamped.
    for (const char* note : {R"({"startBeat": 0, "lengthBeats": 1, "pitch": 128})",
                             R"({"startBeat": 0, "lengthBeats": 1, "pitch": 60, "velocity": 0})",
                             R"({"startBeat": 0, "lengthBeats": 1, "pitch": 60, "channel": 17})"}) {
        SCOPED_TRACE(note);
        EXPECT_FALSE(validate(envelopeOf(juce::String(R"([{"op": "placeClips", "track": "Bass",
                                                          "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [)") +
                                         note + "]}]}]"))
                         .ok);
    }

    // Notes per clip, against TimelineDoc's own constant.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0,
                                         "lengthBeats": 4, "notes": )" +
                                     notesJson(TimelineDoc::kMaxNotesPerClip + 1, 0) + "}]}]"))
                     .ok);

    // Automation values, against the LIVE parameter range — both ends, inclusive.
    EXPECT_FALSE(validate(envelopeOf("[{\"op\": \"writeLane\", \"nodeUuid\": \"filter-uuid\", \"paramId\": "
                                     "\"cutoff\", \"points\": [{\"beat\": 0, \"value\": " +
                                     juce::String(kCutoffMax + 1.0) + "}]}]"))
                     .ok);
    EXPECT_FALSE(validate(envelopeOf("[{\"op\": \"writeLane\", \"nodeUuid\": \"filter-uuid\", \"paramId\": "
                                     "\"cutoff\", \"points\": [{\"beat\": 0, \"value\": " +
                                     juce::String(kCutoffMin - 1.0) + "}]}]"))
                     .ok);
    EXPECT_TRUE(validate(envelopeOf("[{\"op\": \"writeLane\", \"nodeUuid\": \"filter-uuid\", \"paramId\": "
                                    "\"cutoff\", \"points\": [{\"beat\": 0, \"value\": " +
                                    juce::String(kCutoffMax) + "}]}]"))
                    .ok);

    // Tension and curve: rejected rather than clamped/coerced, exactly as validateTimeline does.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
                                         "points": [{"beat": 0, "value": 800, "tension": 2}]}])"))
                     .ok);
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
                                         "points": [{"beat": 0, "value": 800, "curve": 9}]}])"))
                     .ok);

    // An unresolvable binding is never authored from nothing.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "no-such-node", "paramId": "cutoff",
                                         "points": [{"beat": 0, "value": 800}]}])"))
                     .ok);
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "notAParameter",
                                         "points": [{"beat": 0, "value": 800}]}])"))
                     .ok);

    // Two points on one beat: one of them would be silently dropped, so the batch is refused.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
                                         "points": [{"beat": 4, "value": 800}, {"beat": 4, "value": 900}]}])"))
                     .ok);

    // The batch's own cap.
    juce::StringArray tooManyOps;
    for (int i = 0; i <= TimelineOps::kMaxOps; ++i)
        tooManyOps.add(R"({"op": "addTrack", "kind": "midi", "name": "T)" + juce::String(i) + "\"}");
    EXPECT_FALSE(validate(envelopeOf("[" + tooManyOps.joinIntoString(", ") + "]")).ok);

    // placeMidiClip's own bounds.
    // The size cap, checked on the STILL-ENCODED string before any decode: one character past
    // kMaxMidBlobBytes is enough, and the content need not even be valid base64.
    const juce::String oversized = juce::String::repeatedString("A", TimelineOps::kMaxMidBlobBytes + 1);
    EXPECT_FALSE(validate(envelopeOf("[{\"op\": \"placeMidiClip\", \"track\": \"Bass\", \"startBeat\": 0, "
                                     "\"midBase64\": \"" +
                                     oversized + "\"}]"))
                     .ok);
    // Invalid base64.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "placeMidiClip", "track": "Bass", "startBeat": 0,
                                         "midBase64": "not valid base64!!!"}])"))
                     .ok);
    // Valid base64, but the decoded bytes are not a Standard MIDI File at all.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "placeMidiClip", "track": "Bass", "startBeat": 0,
                                         "midBase64": "QUJDREVGRw=="}])"))
                     .ok);
    // An empty blob: nothing to decode, nothing to place.
    EXPECT_FALSE(validate(envelopeOf(R"([{"op": "placeMidiClip", "track": "Bass", "startBeat": 0,
                                         "midBase64": ""}])"))
                     .ok);
    // A non-MIDI target track, same restriction placeClips has.
    doc.addTrack(TrackKind::Automation, "Auto");
    EXPECT_FALSE(validate(envelopeOf(juce::String(R"([{"op": "placeMidiClip", "track": "Auto", "startBeat": 0,
                                         "midBase64": ")") +
                                     kValidMidBase64 + "\"}]"))
                     .ok);

    EXPECT_TRUE(doc.getTracks()[0].clips.empty()) << "not one rejected case may have mutated the document";
}

TEST_F(TimelineOpsTest, AssetsAndRecordModesUnreachable) {
    doc.addTrack(TrackKind::Midi, "Bass");

    // These capabilities are absent from the GRAMMAR rather than refused field by field: an op is a
    // closed object, so naming a field it does not have rejects the batch. That is what stops a
    // later build quietly honouring a field today's gate never inspected.
    struct Case {
        const char* what;
        const char* ops;
    };
    const std::vector<Case> cases = {
        {"a clip naming an audio asset",
         R"([{"op": "placeClips", "track": "Bass",
              "clips": [{"startBeat": 0, "lengthBeats": 4, "assetRef": "Audio/take-1.wav"}]}])"},
        {"a lane arming itself to record",
         R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff", "recordMode": 4,
              "points": [{"beat": 0, "value": 800}]}])"},
        {"a track binding itself to a module",
         R"([{"op": "addTrack", "kind": "midi", "name": "Bound", "bindingUuid": "osc-uuid"}])"},
        {"an audio track", R"([{"op": "addTrack", "kind": "audio", "name": "Take"}])"},
        {"a reserved track kind", R"([{"op": "addTrack", "kind": "7", "name": "Future"}])"},
        {"an unknown field inside a note",
         R"([{"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4,
              "notes": [{"startBeat": 0, "lengthBeats": 1, "pitch": 60, "aftertouch": 90}]}]}])"},
        {"an unknown field inside a point",
         R"([{"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
              "points": [{"beat": 0, "value": 800, "shape": "log"}]}])"},
        {"an unknown field inside placeMidiClip",
         R"([{"op": "placeMidiClip", "track": "Bass", "startBeat": 0, "midBase64": "abcd", "assetRef": "x"}])"},
        {"an unknown op", R"([{"op": "deleteEverything"}])"},
    };

    const juce::String before = dump(doc);
    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.what);
        const auto envelope = envelopeOf(testCase.ops);
        EXPECT_FALSE(validate(envelope).ok);
        EXPECT_FALSE(apply(envelope).ok);
        EXPECT_EQ(dump(doc), before);
    }
    EXPECT_FALSE(undoManager.canUndo());
}

// =============================================================================
// 5. The preview the user actually reads
// =============================================================================

TEST_F(TimelineOpsTest, PreviewTextSummarises) {
    const auto envelope = envelopeOf(R"([
        {"op": "addTrack", "kind": "midi", "name": "Bass"},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "name": "A",
         "notes": )" + notesJson(8) +
                                     R"(}]},
        {"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff", "points": )" +
                                     pointsJson(12) + "}]");

    const auto preview = validate(envelope);
    ASSERT_TRUE(preview.ok) << preview.message;

    // Pinned exactly: this string is what the user reads before agreeing to the edit, so it must
    // be deterministic and it must name the module by its DISPLAY name ("Filter"), never a uuid.
    EXPECT_EQ(preview.previewText, "Adds midi track \"Bass\" (unbound - bind it in the timeline panel); "
                                   "places 1 clip (8 notes) at 0-4 on \"Bass\"; "
                                   "writes 12 points to Filter cutoff over beats 0-11");

    // apply() reports the same summary it previewed — the card and the outcome cannot disagree.
    EXPECT_EQ(apply(envelope).previewText, preview.previewText);
}

TEST_F(TimelineOpsTest, PreviewTextCountsAndWindowsReadNaturally) {
    doc.addTrack(TrackKind::Midi, "Bass");

    const auto twoClips = validate(envelopeOf(R"([{"op": "placeClips", "track": "Bass", "clips": [
        {"startBeat": 0, "lengthBeats": 4}, {"startBeat": 8.5, "lengthBeats": 3.5}]}])"));
    ASSERT_TRUE(twoClips.ok) << twoClips.message;
    EXPECT_EQ(twoClips.previewText, "Places 2 clips (no notes) at 0-4, 8.5-12 on \"Bass\"");

    const auto onePoint = validate(envelopeOf(R"([{"op": "writeLane", "nodeUuid": "filter-uuid",
        "paramId": "cutoff", "points": [{"beat": 4, "value": 800}]}])"));
    ASSERT_TRUE(onePoint.ok) << onePoint.message;
    EXPECT_EQ(onePoint.previewText, "Writes 1 point to Filter cutoff at beat 4");

    const auto automationTrack = validate(envelopeOf(R"([{"op": "addTrack", "kind": "automation",
                                                          "name": "Moves"}])"));
    ASSERT_TRUE(automationTrack.ok) << automationTrack.message;
    // No "(unbound)" note: an Automation track is never bound to a module in the first place.
    EXPECT_EQ(automationTrack.previewText, "Adds automation track \"Moves\"");
}

// =============================================================================
// 6. The patch grammar stays closed — timelineOps is a SIBLING, never nested
// =============================================================================

TEST_F(TimelineOpsTest, PatchGrammarStillClosed) {
    // The document dialect smuggled into a patch under "timeline": still refused, permanently.
    doc.addTrack(TrackKind::Midi, "Bass");
    auto* patchWithTimeline = new juce::DynamicObject();
    patchWithTimeline->setProperty("nodes", juce::var(juce::Array<juce::var>{}));
    patchWithTimeline->setProperty("connections", juce::var(juce::Array<juce::var>{}));
    patchWithTimeline->setProperty("timeline", doc.toVar());

    const auto refused = synth::AIStateMapper::validatePatch(juce::var(patchWithTimeline), graph,
                                                             /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error, synth::PatchValidationError::TimelineNotAllowed);

    // The SAME intent as a SIBLING key on the same object: the patch half is accepted by
    // validatePatch (which has no business inspecting an ops envelope) and the timeline half is
    // accepted by its own gate. Two payloads, two doors, neither one weakening the other.
    const juce::var sibling = parse(R"({"nodes": [], "connections": [],
        "timelineOps": [{"op": "placeClips", "track": "Bass",
                         "clips": [{"startBeat": 0, "lengthBeats": 4, "name": "A"}]}]})");

    const auto patchHalf = synth::AIStateMapper::validatePatch(sibling, graph, /*clearExisting=*/true,
                                                               /*trusted=*/false);
    EXPECT_TRUE(patchHalf.ok) << patchHalf.message;
    EXPECT_TRUE(TimelineOps::carriesOps(sibling));

    const auto timelineHalf = validate(sibling);
    EXPECT_TRUE(timelineHalf.ok) << timelineHalf.message;
}

// =============================================================================
// 7. The service seam: detected, previewed, and applied through the host callback
// =============================================================================

#if SYNTH_ENABLE_TIMELINE

TEST_F(TimelineOpsTest, ServiceDetectsEnvelope) {
    synth::TransportService transport;
    transport.prepare(44100.0, 512);
    transport.tick(512);

    synth::AIIntegrationService service(graph);

    // A structured response, exactly as a model would send it: a patch and an ops envelope side by
    // side on one object, wrapped in the fenced block the patch extractor already handles.
    const juce::String response = R"(Here you go:
```json
{"mode": "merge", "nodes": [], "connections": [],
 "timelineOps": [{"op": "addTrack", "kind": "midi", "name": "Bass"},
                 {"op": "writeLane", "nodeUuid": "filter-uuid", "paramId": "cutoff",
                  "points": [{"beat": 0, "value": 400}, {"beat": 8, "value": 5000}]}]}
```)";

    const juce::var envelope = synth::AIIntegrationService::extractTimelineOps(response);
    ASSERT_FALSE(envelope.isVoid());

    // Gated: with no timeline context wired in there is nothing to validate against, and nothing
    // is offered to the user.
    EXPECT_FALSE(service.hasTimelineContext());
    EXPECT_FALSE(service.previewTimelineOps(envelope).ok);

    service.setTimelineContext(&doc, &transport);
    ASSERT_TRUE(service.hasTimelineContext());

    const auto preview = service.previewTimelineOps(envelope);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_TRUE(preview.previewText.contains("Bass"));
    EXPECT_TRUE(preview.previewText.contains("Filter cutoff"));
    EXPECT_TRUE(doc.isEmpty()) << "previewing must not apply";

    // No host callback yet: an Apply must report that it cannot apply, never silently do nothing.
    EXPECT_FALSE(service.applyTimelineOps(envelope).ok);
    EXPECT_TRUE(doc.isEmpty());

    // The wiring MainComponent installs.
    int applyCalls = 0;
    service.setTimelineOpsApplyCallback([&](const juce::var& e) {
        ++applyCalls;
        return TimelineOps::apply(e, doc, graph, undoManager);
    });

    const auto applied = service.applyTimelineOps(envelope);
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(applyCalls, 1);
    EXPECT_EQ(applied.previewText, preview.previewText);

    // One click, one batch, one undo step — the doc gained the MIDI track, the Automation track
    // and the lane, and a single Cmd+Z takes all of it back.
    EXPECT_EQ(doc.getTracks().size(), 2u);
    EXPECT_NE(doc.getLaneForParam("filter-uuid", "cutoff"), nullptr);
    ASSERT_TRUE(undoManager.undo());
    EXPECT_TRUE(doc.isEmpty());
}

TEST_F(TimelineOpsTest, ServiceIgnoresAResponseWithNoEnvelope) {
    // A plain patch response carries no ops, so no timeline card is ever offered for it.
    EXPECT_TRUE(synth::AIIntegrationService::extractTimelineOps(R"({"mode": "merge", "nodes": [], "connections": []})")
                    .isVoid());
    EXPECT_TRUE(synth::AIIntegrationService::extractTimelineOps("Just a chat reply, no JSON at all.").isVoid());

    // A malformed envelope is DETECTED (presence, not well-formedness) so its rejection can be
    // shown to the user rather than silently dropped.
    const juce::var malformed = synth::AIIntegrationService::extractTimelineOps(R"({"timelineOps": "nope"})");
    ASSERT_FALSE(malformed.isVoid());
    EXPECT_FALSE(TimelineOps::validate(malformed, doc, graph).ok);
}

#endif // SYNTH_ENABLE_TIMELINE
