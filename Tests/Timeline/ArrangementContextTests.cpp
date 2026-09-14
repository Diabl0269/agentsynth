// ArrangementContext::summarize, the read-path arrangement summary folded into the AI
// request context beside the existing patch-JSON injection.

#include "AI/AIIntegrationService.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Timeline/ArrangementContext.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::ArrangementContext;
using synth::AutomationLane;
using synth::MidiNote;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::TransportService;

namespace {

MidiNote makeNote(double startBeat, int pitch) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = 1.0;
    note.pitch = pitch;
    note.velocity = 100;
    note.channel = 1;
    return note;
}

TransportService::PositionSnapshot makeSnapshot(double bpm = 120.0, int numerator = 4, int denominator = 4,
                                                bool looping = false, double loopStart = 0.0, double loopEnd = 0.0) {
    TransportService::PositionSnapshot snapshot;
    snapshot.bpm = bpm;
    snapshot.timeSigNumerator = numerator;
    snapshot.timeSigDenominator = denominator;
    snapshot.looping = looping;
    snapshot.loopStartPpq = loopStart;
    snapshot.loopEndPpq = loopEnd;
    return snapshot;
}

} // namespace

// =============================================================================
// 1. Bound / unbound / orphaned tracks, clips and a lane all render, flags shown
// =============================================================================

TEST(ArrangementContextTest, SummaryContainsTracksClipsLanes) {
    juce::AudioProcessorGraph graph;
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("uuid", "osc-uuid");
    auto filter = graph.addNode(std::make_unique<FilterModule>());
    filter->properties.set("uuid", "filter-uuid");

    TimelineDoc doc;

    // Bound + armed, with a compressed MIDI clip list and an automation lane.
    const auto lead = doc.addTrack(TrackKind::Midi, "Lead");
    doc.setTrackBinding(lead, "osc-uuid");
    doc.setTrackArmed(lead, true);
    const auto clipA = doc.addClip(lead, 0.0, 8.0, "Verse");
    doc.addNote(clipA, makeNote(0.0, 60));
    doc.addNote(clipA, makeNote(1.0, 64));
    const auto clipB = doc.addClip(lead, 8.0, 4.0, "Chorus");
    doc.addNote(clipB, makeNote(0.0, 67));

    AutomationLane::RangeSnapshot range;
    range.minValue = 20.0f;
    range.maxValue = 20000.0f;
    range.defaultValue = 440.0f;
    const auto lane = doc.addLane(lead, "filter-uuid", "cutoff", range);
    doc.addBreakpoint(lane, 0.0, 440.0);
    doc.addBreakpoint(lane, 4.0, 8000.0);

    // Unbound track, muted + soloed.
    const auto drums = doc.addTrack(TrackKind::Midi, "Drums");
    doc.setTrackMuted(drums, true);
    doc.setTrackSoloed(drums, true);

    // Orphaned: bound to a uuid no live node carries.
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");
    doc.setTrackBinding(bass, "no-such-uuid");

    const juce::String summary = ArrangementContext::summarize(doc, graph, makeSnapshot());

    // Header.
    EXPECT_TRUE(summary.startsWith("Arrangement: 3 tracks, bpm 120, 4/4, loop off")) << summary.toStdString();

    // Track names, kind and flags.
    EXPECT_TRUE(summary.contains("\"Lead\" (Midi)"));
    EXPECT_TRUE(summary.contains("[armed]"));
    EXPECT_TRUE(summary.contains("\"Drums\" (Midi)"));
    EXPECT_TRUE(summary.contains("[muted, soloed]"));
    EXPECT_TRUE(summary.contains("\"Bass\" (Midi)"));

    // Binding resolution: display name, unbound, and MISSING.
    EXPECT_TRUE(summary.contains("binding Oscillator"));
    EXPECT_TRUE(summary.contains("binding unbound"));
    EXPECT_TRUE(summary.contains("binding MISSING"));

    // Compressed MIDI clip list: two clips, beat windows, total note count.
    EXPECT_TRUE(summary.contains("2 clips @ 0-8, 8-12 beats; 3 notes total"));

    // Automation lane.
    EXPECT_TRUE(summary.contains("cutoff lane on Filter: 2 points, Read"));
}

// =============================================================================
// 2. maxChars is enforced at TRACK granularity — never mid-line — deterministically
// =============================================================================

TEST(ArrangementContextTest, BudgetTruncatesAtTrackGranularity) {
    juce::AudioProcessorGraph graph;
    TimelineDoc doc;
    for (int i = 0; i < 50; ++i)
        doc.addTrack(TrackKind::Midi, "Track " + juce::String(i));

    const auto snapshot = makeSnapshot();
    const juce::String first = ArrangementContext::summarize(doc, graph, snapshot, 500);
    const juce::String second = ArrangementContext::summarize(doc, graph, snapshot, 500);

    // Deterministic across calls.
    EXPECT_EQ(first, second);

    // Truncated: not all 50 tracks fit in 500 chars, so the tail marker must appear.
    ASSERT_TRUE(first.contains("more tracks]"));

    // Every line before the marker is a complete, untouched track line (or the header) — never a
    // partial line. Split on '\n': the last line is the marker, everything before it is either
    // the header (line 0) or a line beginning "Track " (one whole track each, since none of these
    // 50 tracks has any clips or lanes to add extra lines).
    juce::StringArray lines;
    lines.addLines(first);
    ASSERT_GE(lines.size(), 2);
    EXPECT_TRUE(lines[0].startsWith("Arrangement: 50 tracks"));
    for (int i = 1; i < lines.size() - 1; ++i)
        EXPECT_TRUE(lines[i].startsWith("Track \"")) << "line " << i << ": " << lines[i].toStdString();
    EXPECT_TRUE(lines[lines.size() - 1].endsWith("more tracks]"));

    // Fewer than all 50 tracks were actually included.
    EXPECT_LT(lines.size() - 2, 50); // -1 header, -1 marker
}

// =============================================================================
// 3. An empty document summarises to an empty string
// =============================================================================

TEST(ArrangementContextTest, EmptyDocEmptyString) {
    juce::AudioProcessorGraph graph;
    TimelineDoc doc;
    EXPECT_EQ(ArrangementContext::summarize(doc, graph, makeSnapshot()), juce::String());
}

// =============================================================================
// 4. An audio clip's asset directory never leaks — only the bare file name does
// =============================================================================

TEST(ArrangementContextTest, FilePathsNeverLeak) {
    juce::AudioProcessorGraph graph;
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "Kick");
    const auto clip = doc.addClip(track, 0.0, 4.0, "Take 1");
    doc.setClipAsset(clip, "Audio/secret-dir/take-1.wav", 0.0);

    const juce::String summary = ArrangementContext::summarize(doc, graph, makeSnapshot());

    EXPECT_NE(summary.indexOf("take-1.wav"), -1) << summary.toStdString();
    EXPECT_EQ(summary.indexOf("Audio/"), -1) << summary.toStdString();
    EXPECT_EQ(summary.indexOf("secret-dir"), -1) << summary.toStdString();
}

// =============================================================================
// 5. Seam-level: the outgoing request gains an "## Arrangement" section
// =============================================================================

namespace {

// Minimal stand-in for a real provider, same idiom as MockAIProvider in
// AIIntegrationServiceTests.cpp: answers synchronously and records the exact conversation sent.
class RecordingMockProvider : public synth::AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        lastConversation = conversation;
        AIResponse response;
        response.success = true;
        response.content = "{\"nodes\": [], \"connections\": []}";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    juce::String getProviderName() const override { return "RecordingMockProvider"; }

    juce::String currentModel;
    int requestTimeoutMs = 240000;
    std::vector<Message> lastConversation;
};

} // namespace

TEST(ArrangementContextTest, InjectionAppendsWhenNonEmpty) {
    juce::AudioProcessorGraph graph;
    synth::AIIntegrationService service(graph);

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "Lead");

    TransportService transport;
    transport.prepare(44100.0, 512);
    transport.tick(512); // publishes an initial snapshot so getPositionSnapshot() is well-formed

    service.setTimelineContext(&doc, &transport);

    auto provider = std::make_unique<RecordingMockProvider>();
    auto* rawProvider = provider.get();
    service.setProvider(std::move(provider));

    service.sendMessage("Add a filter", nullptr, /*useStructuredOutput=*/true);

    ASSERT_FALSE(rawProvider->lastConversation.empty());
    const juce::String content = rawProvider->lastConversation.back().content;
    EXPECT_TRUE(content.contains("## Arrangement"));
    EXPECT_TRUE(content.contains("\"Lead\""));
    EXPECT_TRUE(content.contains("Add a filter"));
}
