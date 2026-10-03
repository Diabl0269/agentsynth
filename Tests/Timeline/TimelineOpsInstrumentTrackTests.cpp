// TimelineOps' addInstrumentTrack op: validated on the scratch doc (the graph side cannot be
// dry-run), built by the host inside ONE host->recordBatch transaction, and refused outright when
// no host is wired in. The MainComponent-level build itself is covered by
// Tests/Mixer/ChannelFlow/ChannelFlowTimelineOpsHostTests.cpp; here a FakeHost stands in so the
// op's contract with the host is observable call by call.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/AIStateMapper/AIStateMapperInternal.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

using synth::InstrumentTrackInsert;
using synth::TimelineDoc;
using synth::TimelineOps;
using synth::TimelineOpsResult;
using synth::TrackKind;

namespace {

juce::var parse(const juce::String& json) {
    const juce::var value = juce::JSON::parse(json);
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << json;
    return value;
}

juce::var envelopeOf(const juce::String& opsArrayJson) { return parse("{\"timelineOps\": " + opsArrayJson + "}"); }

juce::String dump(const TimelineDoc& doc) { return juce::JSON::toString(doc.toVar()); }

const synth::Track* findTrackByName(const TimelineDoc& doc, const juce::String& name) {
    for (const auto& track : doc.getTracks())
        if (track.name == name)
            return &track;
    return nullptr;
}

constexpr const char* kNoHostMessage = "This build cannot create instrument tracks from here.";

// Stands in for MainComponentTimelineOpsHost: adds the MIDI track to the doc and a Track In plus
// the instrument to the graph, binds the track, and runs recordBatch through the REAL
// AppUndoManager transaction the app uses, so "one undo step" is the real manager's verdict.
class FakeHost : public synth::TimelineOpsHost {
public:
    FakeHost(TimelineDoc& d, juce::AudioProcessorGraph& g, AppUndoManager& u)
        : doc(d)
        , graph(g)
        , undo(u) {}

    bool addInstrumentTrack(const juce::String& name, const juce::String& instrumentType, bool poly,
                            const std::vector<InstrumentTrackInsert>& inserts, juce::String& instrumentUuid) override {
        ++addCalls;
        calledInsideRecordBatch = insideRecordBatch;
        lastName = name;
        lastType = instrumentType;
        lastPoly = poly;
        lastInserts = inserts;
        if (failBuild)
            return false;

        const auto trackId = doc.addTrack(TrackKind::Midi, name);
        if (!trackId.isValid())
            return false;
        auto trackIn = graph.addNode(synth::AIStateMapper::createModule("Track In"));
        trackIn->properties.set("uuid", "track-in-" + name);
        auto instrument = graph.addNode(synth::AIStateMapper::createModule(instrumentType));
        instrumentUuid = "instrument-" + name;
        instrument->properties.set("uuid", instrumentUuid);
        doc.setTrackBinding(trackId, "track-in-" + name);
        return true;
    }

    bool recordBatch(const std::function<void()>& mutation) override {
        ++recordCalls;
        insideRecordBatch = true;
        const bool pushed = undo.recordGraphTimelineAndMacroChange(graph, doc, macros, mutation);
        insideRecordBatch = false;
        return pushed;
    }

    TimelineDoc& doc;
    juce::AudioProcessorGraph& graph;
    AppUndoManager& undo;
    synth::MacroSet macros;

    int addCalls = 0;
    int recordCalls = 0;
    bool insideRecordBatch = false;
    bool calledInsideRecordBatch = false;
    bool failBuild = false;
    juce::String lastName, lastType;
    bool lastPoly = false;
    std::vector<InstrumentTrackInsert> lastInserts;
};

} // namespace

class TimelineOpsInstrumentTrackTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    TimelineDoc doc;
    FakeHost host{doc, graph, undoManager};

    void SetUp() override {
        graph.clear();
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        filter->properties.set("uuid", "filter-uuid");
    }

    TimelineOpsResult validate(const juce::var& envelope, bool withHost = true) {
        return TimelineOps::validate(envelope, doc, graph, withHost ? &host : nullptr);
    }
    TimelineOpsResult apply(const juce::var& envelope, bool withHost = true) {
        return TimelineOps::apply(envelope, doc, graph, undoManager, withHost ? &host : nullptr);
    }

    // Must fail naming `fragment`, never call the host, and leave the doc untouched.
    void expectRejected(const juce::String& opsJson, const juce::String& fragment) {
        SCOPED_TRACE(opsJson);
        const juce::String before = dump(doc);
        const auto result = apply(envelopeOf(opsJson));
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains(fragment)) << "actual message: " << result.message;
        EXPECT_EQ(dump(doc), before);
        EXPECT_EQ(host.addCalls, 0);
        EXPECT_EQ(host.recordCalls, 0);
    }
};

// =============================================================================
// 1. No host: refused with a clear message, nothing touched
// =============================================================================

TEST_F(TimelineOpsInstrumentTrackTest, NoHostRejectsWithTheMessageAndLeavesTheDocUntouched) {
    const auto envelope = envelopeOf(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"}])");

    const auto preview = validate(envelope, /*withHost=*/false);
    EXPECT_FALSE(preview.ok);
    EXPECT_TRUE(preview.message.contains(kNoHostMessage)) << preview.message;
    EXPECT_TRUE(preview.message.startsWith("timelineOps[0] (addInstrumentTrack): "));

    const auto applied = apply(envelope, /*withHost=*/false);
    EXPECT_FALSE(applied.ok);
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_FALSE(undoManager.canUndo());
}

// =============================================================================
// 2. Field checks
// =============================================================================

TEST_F(TimelineOpsInstrumentTrackTest, RejectsBadFields) {
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "bindingUuid": "x"}])",
                   "unknown field \"bindingUuid\"");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Hosted Plugin"}])",
                   "asks for instrument \"Hosted Plugin\"");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Poly MIDI"}])",
                   "asks for instrument \"Poly MIDI\"");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass"}])", "needs a string \"instrument\"");
    expectRejected(R"([{"op": "addInstrumentTrack", "instrument": "Sampler"}])", "needs a string \"name\"");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Sampler", "poly": true}])",
                   "poly Sampler");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "poly": "yes"}])",
                   "non-boolean \"poly\"");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": "a"}])",
                   "non-integer \"instrumentId\"");
}

TEST_F(TimelineOpsInstrumentTrackTest, RejectsADuplicateTrackName) {
    // Already in the doc, of any kind...
    doc.addTrack(TrackKind::Automation, "Bass");
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"}])",
                   "already has a track with that name");

    // ...or added earlier in the same batch.
    doc.fromVar(TimelineDoc().toVar());
    expectRejected(R"([{"op": "addTrack", "kind": "midi", "name": "Lead"},
                       {"op": "addInstrumentTrack", "name": "Lead", "instrument": "Sampler"}])",
                   "timelineOps[1] (addInstrumentTrack): names its track \"Lead\"");
}

TEST_F(TimelineOpsInstrumentTrackTest, RejectsPastTheTrackCap) {
    for (int i = 0; i < TimelineDoc::kMaxTracks; ++i)
        ASSERT_TRUE(doc.addTrack(TrackKind::Midi, "T" + juce::String(i)).isValid());
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"}])",
                   "past its limit of " + juce::String(TimelineDoc::kMaxTracks) + " tracks");
}

TEST_F(TimelineOpsInstrumentTrackTest, AcceptsAnIntegerInstrumentId) {
    const auto result = validate(
        envelopeOf(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7}])"));
    EXPECT_TRUE(result.ok) << result.message;
}

// =============================================================================
// 3. Inserts
// =============================================================================

TEST_F(TimelineOpsInstrumentTrackTest, RejectsBadInserts) {
    const juce::String head =
        R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "inserts": )";
    expectRejected(head + R"([{"type": "Flanger 9000"}]}])", "unknown module type \"Flanger 9000\"");
    expectRejected(head + R"([{"type": "Oscillator"}]}])", "a MIDI instrument");
    expectRejected(head + R"([{"type": "Channel Strip"}]}])", "only the app itself may create");
    expectRejected(head + R"([{"type": "Track In"}]}])", "only the app itself may create");
    expectRejected(head + R"([{"type": "Filter", "params": {"nonsense": 1}}]}])", "Unknown parameter \"nonsense\"");
    expectRejected(head + R"([{"type": "Filter", "params": 3}]}])", "\"params\" that is not an object");
    expectRejected(head + R"([{"type": "Filter", "id": "x"}]}])", "non-integer \"id\"");
    expectRejected(head + R"([{"type": "Filter", "uuid": "x"}]}])", "unknown field \"uuid\"");
    expectRejected(head + R"({"type": "Filter"}}])", "\"inserts\" that is not an array");

    juce::StringArray nine;
    for (int i = 0; i < TimelineOps::kMaxInstrumentInserts + 1; ++i)
        nine.add(R"({"type": "Filter"})");
    expectRejected(head + "[" + nine.joinIntoString(", ") + "]}]", "exceeding the limit of 8");
}

TEST_F(TimelineOpsInstrumentTrackTest, HostReceivesTheValidatedInserts) {
    const auto applied = apply(envelopeOf(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
        "inserts": [{"type": "Filter", "id": 3, "params": {"cutoff": 800}}, {"type": "Distortion"}]}])"));
    ASSERT_TRUE(applied.ok) << applied.message;

    ASSERT_EQ(host.lastInserts.size(), 2u);
    EXPECT_EQ(host.lastInserts[0].type, "Filter");
    EXPECT_EQ(static_cast<double>(host.lastInserts[0].params.getProperty("cutoff", {})), 800.0);
    EXPECT_EQ(host.lastInserts[1].type, "Distortion");
    EXPECT_TRUE(host.lastInserts[1].params.isVoid());
}

// =============================================================================
// 4. The preview the user reads
// =============================================================================

TEST_F(TimelineOpsInstrumentTrackTest, PreviewStringsArePinned) {
    struct Case {
        const char* ops;
        const char* preview;
    };
    const Case cases[] = {
        {R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"}])",
         R"(Adds instrument track "Bass" (Oscillator with envelope and channel strip))"},
        {R"([{"op": "addInstrumentTrack", "name": "Keys", "instrument": "Sampler"}])",
         R"(Adds instrument track "Keys" (Sampler with channel strip))"},
        {R"([{"op": "addInstrumentTrack", "name": "Pad", "instrument": "Wavetable", "poly": true}])",
         R"(Adds instrument track "Pad" (poly Wavetable with envelope and channel strip))"},
        {R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "poly": true}])",
         R"(Adds instrument track "Bass" (poly Oscillator with envelope and channel strip))"},
        {R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
              "inserts": [{"type": "Filter"}, {"type": "Distortion"}]}])",
         R"(Adds instrument track "Bass" (Oscillator with envelope and channel strip, inserts: Filter, Distortion))"},
    };
    for (const auto& c : cases) {
        const auto result = validate(envelopeOf(c.ops));
        ASSERT_TRUE(result.ok) << result.message;
        EXPECT_EQ(result.previewText, juce::String(c.preview));
    }
    EXPECT_TRUE(doc.isEmpty()) << "validate() must not touch the document";
    EXPECT_EQ(host.addCalls, 0) << "validate() must never build anything";
}

// =============================================================================
// 5. Apply: the host builds inside ONE recordBatch, and later ops see the track
// =============================================================================

TEST_F(TimelineOpsInstrumentTrackTest, AppliesWithLaterOpsAsOneUndoStepInsideRecordBatch) {
    const juce::String docBefore = dump(doc);
    const int nodesBefore = graph.getNumNodes();
    const auto envelope = envelopeOf(R"([
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]}])");

    const auto preview = validate(envelope);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(host.addCalls, 0);

    const auto applied = apply(envelope);
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(applied.previewText, preview.previewText) << "the preview describes exactly what applied";
    EXPECT_EQ(host.addCalls, 1);
    EXPECT_EQ(host.recordCalls, 1);
    EXPECT_TRUE(host.calledInsideRecordBatch);
    EXPECT_EQ(host.lastName, "Bass");
    EXPECT_EQ(host.lastType, "Oscillator");
    EXPECT_FALSE(host.lastPoly);

    ASSERT_EQ(doc.getTracks().size(), 1u) << "the host made the track; runBatch must not add a second";
    const auto* bass = findTrackByName(doc, "Bass");
    ASSERT_NE(bass, nullptr);
    EXPECT_EQ(bass->bindingUuid, "track-in-Bass");
    ASSERT_EQ(bass->clips.size(), 1u);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore + 2);

    // ONE undo step reverts graph and doc together.
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), docBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(undoManager.canUndo()) << "the batch must have been exactly one undo step";
}

TEST_F(TimelineOpsInstrumentTrackTest, ALaterInvalidOpNeverCallsTheHost) {
    expectRejected(R"([{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"},
                       {"op": "placeClips", "track": "Nope", "clips": [{"startBeat": 0, "lengthBeats": 4}]}])",
                   "timelineOps[1] (placeClips)");
    EXPECT_EQ(graph.getNumNodes(), 1);
}

TEST_F(TimelineOpsInstrumentTrackTest, ABatchWithoutTheOpStillUsesRecordTimelineChange) {
    const auto applied = apply(envelopeOf(R"([{"op": "addTrack", "kind": "midi", "name": "Lead"}])"));
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(host.recordCalls, 0) << "a doc-only batch keeps the timeline-only undo path";
    EXPECT_EQ(host.addCalls, 0);
    ASSERT_TRUE(undoManager.undo());
    EXPECT_TRUE(doc.isEmpty());
}

TEST_F(TimelineOpsInstrumentTrackTest, AHostFailureAppliesNothingAndPushesNoUndoStep) {
    host.failBuild = true;
    const int nodesBefore = graph.getNumNodes();
    const auto applied = apply(envelopeOf(R"([{"op": "addTrack", "kind": "midi", "name": "Lead"},
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Sampler"}])"));
    EXPECT_FALSE(applied.ok);
    EXPECT_TRUE(applied.message.contains("could not build the instrument track")) << applied.message;
    EXPECT_EQ(host.addCalls, 1);
    EXPECT_TRUE(doc.isEmpty()) << "the earlier addTrack is rolled back with the rest of the batch";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(undoManager.canUndo());
}

// =============================================================================
// 6. The instrument list cannot drift from the app's own rules
// =============================================================================

TEST(TimelineOpsInstrumentTypesTest, AuthorableInstrumentTypesAreAuthorableMidiInstruments) {
    for (const char* type : TimelineOps::kAuthorableInstrumentTypes) {
        SCOPED_TRACE(type);
        EXPECT_EQ(synth::detail::kNonAuthorableModuleTypes.count(type), 0u);
        EXPECT_TRUE(synth::AIStateMapper::authorableModuleTypes().contains(type));
        auto processor = synth::AIStateMapper::createModule(type);
        ASSERT_NE(processor, nullptr);
        auto* module = dynamic_cast<ModuleBase*>(processor.get());
        ASSERT_NE(module, nullptr);
        EXPECT_TRUE(isMidiInstrumentType(module->getModuleType()));
    }
}
