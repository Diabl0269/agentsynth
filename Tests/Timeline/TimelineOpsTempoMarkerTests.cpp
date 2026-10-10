// TimelineOps `setTempo` and `addMarker`: the field checks (bounds, closed objects), a preview that
// mutates nothing, the tempo going through the host inside the batch, markers landing in the document,
// and the all-or-nothing rule across a batch that mixes them with other ops. The tempo's undo step
// through the real app host is Tests/Mixer/ChannelFlow/ChannelFlowProjectEditTests.cpp.

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::TimelineDoc;
using synth::TimelineOps;
using synth::TimelineOpsResult;

namespace {

juce::var envelopeOf(const juce::String& opsArrayJson) {
    const juce::var value = juce::JSON::parse("{\"timelineOps\": " + opsArrayJson + "}");
    EXPECT_FALSE(value.isVoid()) << "test JSON did not parse: " << opsArrayJson;
    return value;
}

// A host that can only set the tempo: records the calls and runs a batch's mutation directly.
struct TempoHost : synth::TimelineOpsHost {
    std::optional<synth::InstrumentTrackBuildResult>
    addInstrumentTrack(const juce::String&, const juce::String&, bool, const std::vector<synth::InstrumentTrackInsert>&,
                       const juce::var&, const juce::var&) override {
        return std::nullopt;
    }
    bool recordBatch(const std::function<void()>& mutation) override {
        ++batches;
        mutation();
        return true;
    }
    bool canSetTempo() const override { return true; }
    bool setTempo(double bpm) override {
        tempos.push_back(bpm);
        return true;
    }

    int batches = 0;
    std::vector<double> tempos;
};

class TimelineOpsTempoMarkerTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    TimelineDoc doc;
    TempoHost host;

    TimelineOpsResult validate(const juce::String& ops, const synth::TimelineOpsHost* h = nullptr) {
        return TimelineOps::validate(envelopeOf(ops), doc, graph, h);
    }
};

} // namespace

TEST_F(TimelineOpsTempoMarkerTest, SetTempoAcceptsTheSongRangeAndNothingOutsideIt) {
    for (const char* bpm : {"40", "128", "174.5", "220"})
        EXPECT_TRUE(validate(juce::String(R"([{"op": "setTempo", "bpm": )") + bpm + "}]", &host).ok) << bpm;
    for (const char* bpm : {"39.9", "221", "0", "-120", "\"fast\"", "null"}) {
        SCOPED_TRACE(bpm);
        const auto result = validate(juce::String(R"([{"op": "setTempo", "bpm": )") + bpm + "}]", &host);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.startsWith("timelineOps[0] (setTempo): ")) << result.message;
    }
    EXPECT_FALSE(validate(R"([{"op": "setTempo"}])", &host).ok);
}

TEST_F(TimelineOpsTempoMarkerTest, SetTempoIsAClosedObjectAndNeedsAHostThatCanSetIt) {
    EXPECT_FALSE(validate(R"([{"op": "setTempo", "bpm": 120, "swing": 0.2}])", &host).ok);
    const auto noHost = validate(R"([{"op": "setTempo", "bpm": 120}])");
    EXPECT_FALSE(noHost.ok);
    EXPECT_TRUE(noHost.message.contains("cannot change the tempo")) << noHost.message;
    struct NoTempoHost : TempoHost {
        bool canSetTempo() const override { return false; }
    } cannot;
    EXPECT_FALSE(validate(R"([{"op": "setTempo", "bpm": 120}])", &cannot).ok);
}

TEST_F(TimelineOpsTempoMarkerTest, ValidateNeverSetsTheTempoAndApplyHandsItToTheHostInsideTheBatch) {
    const auto envelope = envelopeOf(R"([{"op": "setTempo", "bpm": 174}])");
    const auto preview = TimelineOps::validate(envelope, doc, graph, &host);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(preview.previewText, "Sets the tempo to 174 BPM");
    EXPECT_TRUE(host.tempos.empty());

    const auto applied = TimelineOps::apply(envelope, doc, graph, undoManager, &host);
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(host.tempos, std::vector<double>{174.0});
    EXPECT_EQ(host.batches, 1) << "a tempo needs the host's transaction, not the doc-only one";
}

TEST_F(TimelineOpsTempoMarkerTest, AddMarkerChecksBeatAndName) {
    EXPECT_TRUE(validate(R"([{"op": "addMarker", "beat": 0, "name": "Intro"}])").ok);
    EXPECT_TRUE(validate(R"([{"op": "addMarker", "beat": 100000, "name": "End"}])").ok);
    const juce::String longest(std::string(40, 'x'));
    EXPECT_TRUE(validate(R"([{"op": "addMarker", "beat": 4, "name": ")" + longest + R"("}])").ok);
    EXPECT_FALSE(validate(R"([{"op": "addMarker", "beat": 4, "name": ")" + longest + R"(x"}])").ok);

    for (const char* bad :
         {R"({"beat": -1, "name": "A"})", R"({"beat": 100001, "name": "A"})", R"({"beat": 0, "name": ""})",
          R"({"beat": 0, "name": 7})", R"({"beat": 0})", R"({"name": "A"})", R"({"beat": "0", "name": "A"})",
          R"({"beat": 0, "name": "A", "colour": "red"})"}) {
        SCOPED_TRACE(bad);
        const auto result =
            validate("[" + juce::String(bad).replaceFirstOccurrenceOf("{", R"({"op": "addMarker", )") + "]");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.startsWith("timelineOps[0] (addMarker): ")) << result.message;
    }
}

TEST_F(TimelineOpsTempoMarkerTest, AddMarkerPreviewsOnACopyAndApplyAddsTheMarkersAsOneUndoStep) {
    const auto envelope = envelopeOf(R"([{"op": "addMarker", "beat": 0, "name": "Intro"},
                                         {"op": "addMarker", "beat": 16, "name": "Drop"}])");
    const auto preview = TimelineOps::validate(envelope, doc, graph);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(preview.previewText, "Adds 2 markers (\"Intro\" at beat 0, \"Drop\" at beat 16)");
    EXPECT_TRUE(doc.getMarkers().empty());

    const auto applied = TimelineOps::apply(envelope, doc, graph, undoManager);
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_EQ(doc.getMarkers().size(), 2u);
    EXPECT_EQ(doc.getMarkers()[0].text, "Intro");
    EXPECT_EQ(doc.getMarkers()[0].beat, 0.0);
    EXPECT_EQ(doc.getMarkers()[1].text, "Drop");
    EXPECT_EQ(doc.getMarkers()[1].beat, 16.0);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_TRUE(doc.getMarkers().empty()) << "one Cmd+Z removes both";
}

TEST_F(TimelineOpsTempoMarkerTest, AManyMarkerPreviewIsElided) {
    juce::StringArray ops;
    for (int i = 0; i < 6; ++i)
        ops.add("{\"op\": \"addMarker\", \"beat\": " + juce::String(i * 8) + ", \"name\": \"S" + juce::String(i) +
                "\"}");
    const auto result = validate("[" + ops.joinIntoString(", ") + "]");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.previewText.contains("Adds 6 markers (")) << result.previewText;
    EXPECT_TRUE(result.previewText.contains("and 2 more")) << result.previewText;
    EXPECT_FALSE(result.previewText.contains("S5")) << result.previewText;
}

TEST_F(TimelineOpsTempoMarkerTest, ASingleMarkerIsNamedPlainly) {
    EXPECT_EQ(validate(R"([{"op": "addMarker", "beat": 8, "name": "Drop"}])").previewText,
              "Adds marker \"Drop\" at beat 8");
}

TEST_F(TimelineOpsTempoMarkerTest, AMarkerPastTheDocumentCapRejectsTheBatch) {
    for (int i = 0; i < TimelineDoc::kMaxMarkers; ++i)
        ASSERT_TRUE(doc.addMarker(i, "M", 0).isValid());
    const auto result = validate(R"([{"op": "addMarker", "beat": 5, "name": "One too many"}])");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("limit")) << result.message;
}

TEST_F(TimelineOpsTempoMarkerTest, ABadOpLaterInTheBatchLeavesTheTempoAndMarkersUntouched) {
    const auto envelope = envelopeOf(R"([{"op": "setTempo", "bpm": 150},
                                         {"op": "addMarker", "beat": 0, "name": "Intro"},
                                         {"op": "addMarker", "beat": -4, "name": "Bad"}])");
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const auto applied = TimelineOps::apply(envelope, doc, graph, undoManager, &host);
    EXPECT_FALSE(applied.ok);
    EXPECT_TRUE(applied.message.startsWith("timelineOps[2] (addMarker): ")) << applied.message;
    EXPECT_TRUE(host.tempos.empty());
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
}

TEST_F(TimelineOpsTempoMarkerTest, UnknownOpMessageNamesTheNewOps) {
    const auto result = validate(R"([{"op": "setSwing"}])");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("\"setTempo\"") && result.message.contains("\"addMarker\"")) << result.message;
}
