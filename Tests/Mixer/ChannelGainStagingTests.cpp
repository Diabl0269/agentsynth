// Gain staging of the channels an AI build adds (Source/Mixer/ChannelGainStaging): where the pass measures, which
// faders it moves, and that a real edit plan applied through the app's host leaves every new channel under the target.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlow/ChannelFlowTestFixture.h"
#include "Mixer/ChannelGainStaging/ChannelGainStaging.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Transport/OfflineTransportDriver.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace gs = synth::gainstaging;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

synth::MidiNote noteAt(double start, double length, int velocity = 100) {
    synth::MidiNote note;
    note.startBeat = start;
    note.lengthBeats = length;
    note.velocity = velocity;
    return note;
}

void addClip(synth::TimelineDoc& doc, synth::TrackId track, double start, double length,
             const std::vector<synth::MidiNote>& notes) {
    const auto clip = doc.addClip(track, start, length, "c");
    for (const auto& note : notes)
        doc.addNote(clip, note);
}

// A stereo source that writes one constant to both outputs.
class ConstantSource : public juce::AudioProcessor {
public:
    explicit ConstantSource(float value)
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
        , value_(value) {}
    const juce::String getName() const override { return "Constant"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), value_, buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    float value_;
};

void setGain(juce::AudioProcessorGraph& graph, NodeID strip, float db) {
    for (auto* param : graph.getNodeForId(strip)->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param); ranged && ranged->paramID == "gain")
            ranged->setValueNotifyingHost(ranged->convertTo0to1(db));
}

double gainOf(juce::AudioProcessorGraph& graph, NodeID strip) {
    for (auto* param : graph.getNodeForId(strip)->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param); ranged && ranged->paramID == "gain")
            return ranged->convertFrom0to1(ranged->getValue());
    return 0.0;
}

double peakOf(const std::vector<gs::ChannelPeak>& peaks, NodeID strip) {
    for (const auto& peak : peaks)
        if (peak.strip == strip)
            return peak.peakDb;
    return -kInf;
}

} // namespace

TEST(ChannelGainStagingPlan, EachTrackIsMeasuredWhereItPlaysMostAndTheRenderStartsEarlyEnoughForHeldNotes) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    const auto drums = doc.addTrack(synth::TrackKind::Midi, "Drums");
    addClip(doc, pad, 0.0, 64.0, {noteAt(0.0, 2.0, 60), noteAt(20.0, 8.0, 120), noteAt(20.0, 8.0, 120)});
    addClip(doc, drums, 0.0, 64.0, {noteAt(40.0, 0.25), noteAt(41.0, 0.25), noteAt(42.0, 0.25), noteAt(43.0, 0.25)});

    gs::WindowOptions options;
    options.windowsPerTrack = 1;
    options.maxTotalBeats = 16.0; // shorter than the song, so only the busiest stretches are rendered
    const auto windows = gs::planWindows(doc, options);
    ASSERT_EQ(windows.size(), 2u);
    EXPECT_DOUBLE_EQ(windows[0].startBeat, 20.0);
    EXPECT_DOUBLE_EQ(windows[0].endBeat, 24.0);
    EXPECT_DOUBLE_EQ(windows[1].startBeat, 40.0);
    EXPECT_DOUBLE_EQ(windows[1].endBeat, 44.0);
}

TEST(ChannelGainStagingPlan, ANoteStartedBeforeTheStretchIsRenderedFromItsStart) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    addClip(doc, pad, 0.0, 64.0, {noteAt(10.0, 12.0)});
    gs::WindowOptions options;
    options.windowsPerTrack = 1;
    options.maxTotalBeats = 8.0;
    const auto windows = gs::planWindows(doc, options);
    ASSERT_EQ(windows.size(), 1u);
    EXPECT_DOUBLE_EQ(windows[0].startBeat, 10.0) << "the held note is heard from its onset";
}

TEST(ChannelGainStagingPlan, AnArrangementThatFitsTheCapIsRenderedWholeFromBeatZeroToTheLastNoteEnd) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    const auto bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
    addClip(doc, pad, 8.0, 64.0, {noteAt(0.0, 2.0)});
    addClip(doc, bass, 0.0, 200.0, {noteAt(100.0, 4.0)});
    const auto windows = gs::planWindows(doc);
    ASSERT_EQ(windows.size(), 1u);
    EXPECT_DOUBLE_EQ(windows[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(windows[0].endBeat, 104.0);
}

TEST(ChannelGainStagingPlan, AnArrangementOverTheCapFallsBackToTheBusiestStretches) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    addClip(doc, pad, 0.0, 800.0, {noteAt(0.0, 1.0), noteAt(100.0, 1.0, 120), noteAt(700.0, 1.0, 90)});
    const auto windows = gs::planWindows(doc);
    double total = 0.0;
    for (const auto& window : windows)
        total += window.endBeat - window.startBeat;
    EXPECT_LT(total, 100.0) << "stretches, not the 701-beat song";
    ASSERT_GE(windows.size(), 3u) << "the default keeps up to four stretches per track";
}

TEST(ChannelGainStagingPlan, NoNotesMeansNothingToRenderAndAMutedTrackIsSkipped) {
    synth::TimelineDoc doc;
    EXPECT_TRUE(gs::planWindows(doc).empty());
    const auto bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
    addClip(doc, bass, 0.0, 8.0, {noteAt(0.0, 1.0)});
    doc.setTrackMuted(bass, true);
    EXPECT_TRUE(gs::planWindows(doc).empty());
}

TEST(ChannelGainStagingPlan, TheRenderIsCappedAndTheBusiestStretchOfEveryTrackIsKept) {
    synth::TimelineDoc doc;
    gs::WindowOptions options;
    options.windowsPerTrack = 2;
    options.maxTotalBeats = 96.0;
    for (int t = 0; t < 12; ++t) {
        const auto track = doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(t));
        addClip(doc, track, 0.0, 400.0, {noteAt(30.0 * t, 1.0, 120), noteAt(30.0 * t + 200.0, 1.0, 90)});
    }
    const auto windows = gs::planWindows(doc, options);
    double total = 0.0;
    for (const auto& window : windows)
        total += window.endBeat - window.startBeat;
    EXPECT_LE(total, 96.0);
    for (int t = 0; t < 12; ++t) {
        bool covered = false;
        for (const auto& window : windows)
            covered = covered || (window.startBeat <= 30.0 * t && window.endBeat >= 30.0 * t + 1.0);
        EXPECT_TRUE(covered) << "track " << t << " keeps its busiest stretch";
    }
}

TEST(ChannelGainStagingTrims, OnlyAChannelOverTheTargetMovesAndOnlyDownByTheExcess) {
    const NodeID hot{1}, quiet{2}, silent{3}, other{4};
    const std::vector<gs::ChannelPeak> peaks{{hot, 5.4}, {quiet, -11.0}, {silent, -kInf}, {other, 2.0}};
    const auto trims = gs::planTrims(peaks, {hot, quiet, silent}, [](NodeID) { return 0.0; });
    ASSERT_EQ(trims.size(), 1u) << "quiet and silent stay; `other` is not a new channel";
    EXPECT_EQ(trims[0].strip, hot);
    EXPECT_NEAR(trims[0].toDb, -8.9, 0.11) << "5.4 down to the target less the margin";
    EXPECT_LE(5.4 + trims[0].toDb, gs::kTargetPeakDb - gs::kTrimMarginDb) << "rounded down, never short of it";
}

TEST(ChannelGainStagingTrims, TheFaderIsReadFromWhereItIsAndStopsAtItsFloor) {
    const NodeID a{1}, b{2};
    const auto trims = gs::planTrims({{a, 0.0}, {b, 60.0}}, {a, b}, [&](NodeID id) { return id == a ? -2.0 : 0.0; });
    ASSERT_EQ(trims.size(), 2u);
    EXPECT_NEAR(trims[0].toDb, -5.5, 0.11);
    EXPECT_DOUBLE_EQ(trims[1].toDb, ChannelStripModule::kMinGainDb);
}

TEST(ChannelGainStagingRender, ARenderedWindowReadsEachChannelsPeakAndTheTrimPutsItAtTheTarget) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, 48000.0, 512);
    auto addFactory = [&](const juce::String& type) {
        return graph.addNode(synth::AIStateMapper::createModule(type))->nodeID;
    };
    const auto out = addFactory("Audio Output");
    const auto master = addFactory("Master");
    const auto hotStrip = addFactory("Channel Strip");
    const auto quietStrip = addFactory("Channel Strip");
    const auto hotSource = graph.addNode(std::make_unique<ConstantSource>(2.0f))->nodeID;
    const auto quietSource = graph.addNode(std::make_unique<ConstantSource>(0.05f))->nodeID;
    constexpr int kRight = ChannelStripModule::kRightBase;
    for (auto [src, strip] : {std::pair{hotSource, hotStrip}, std::pair{quietSource, quietStrip}}) {
        graph.addConnection({{src, 0}, {strip, 0}});
        graph.addConnection({{src, 1}, {strip, kRight}});
        graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
        graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
    }
    graph.addConnection({{master, 0}, {out, 0}});
    graph.addConnection({{master, 1}, {out, 1}});
    synth::OfflineTransportDriver driver(engine, 48000.0, 512, 2);

    const std::vector<gs::BeatWindow> windows{{0.0, 1.0}};
    auto peaks = gs::measureChannelPeaks(engine, windows);
    EXPECT_NEAR(peakOf(peaks, hotStrip), 6.0, 0.2);
    EXPECT_NEAR(peakOf(peaks, quietStrip), -26.0, 0.2);

    const auto trims = gs::planTrims(peaks, {hotStrip, quietStrip}, [&](NodeID id) { return gainOf(graph, id); });
    ASSERT_EQ(trims.size(), 1u);
    setGain(graph, trims[0].strip, (float)trims[0].toDb);

    peaks = gs::measureChannelPeaks(engine, windows);
    EXPECT_NEAR(peakOf(peaks, hotStrip), gs::kTargetPeakDb - gs::kTrimMarginDb, 0.3);
    EXPECT_NEAR(peakOf(peaks, quietStrip), -26.0, 0.2) << "a quiet channel is never raised";
    engine.releaseFromHost();
    engine.shutdown();
}

// The app path: an edit plan with a track too hot for its own channel, applied through the real host, and the one
// undo step it comes in.
TEST_F(ChannelFlowTest, AnAiBuiltTrackThatIsOverTheTargetComesOutAtItAsPartOfTheSameUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& aiService = mc.getAiServiceForTest();

    const juce::var envelope = juce::JSON::parse(R"({"timelineOps": [
        {"op": "addInstrumentTrack", "name": "Lead", "instrument": "Oscillator", "poly": true,
         "instrumentParams": {"waveform": "Saw", "level": 1.0, "unison": 7, "detune": 0.5},
         "inserts": [{"type": "Filter", "params": {"cutoff": 20000, "outputLevel": 12}}]},
        {"op": "placeClips", "track": "Lead", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 2, "pitch": 36, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 39, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 42, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 45, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 48, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 51, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 54, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 57, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 60, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 63, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 66, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 69, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 72, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 75, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 78, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 81, "velocity": 127}]}]}]})");
    const auto before = gs::channelStripIds(graph);
    const auto applied = aiService.applyTimelineOps(envelope);
    ASSERT_TRUE(applied.ok) << applied.message;

    std::set<NodeID> added;
    for (const auto id : gs::channelStripIds(graph))
        if (before.count(id) == 0)
            added.insert(id);
    ASSERT_EQ(added.size(), 1u);
    const auto strip = *added.begin();

    const auto peaks = gs::measureChannelPeaks(mc.getAudioEngine(), gs::planWindows(mc.getTimelineDoc()));
    EXPECT_LE(peakOf(peaks, strip), gs::kTargetPeakDb + 0.3) << "the new channel is at or under the target";
    EXPECT_GE(peakOf(peaks, strip), gs::kTargetPeakDb - 3.0) << "and was lowered only as far as needed";
    EXPECT_LT(gainOf(graph, strip), 0.0);

    mc.getUndoManager().undo();
    EXPECT_TRUE(gs::channelStripIds(graph).size() == before.size()) << "one undo takes the whole build away";
    mc.getUndoManager().redo();
    ASSERT_EQ(gs::channelStripIds(graph).size(), before.size() + 1);
    EXPECT_LT(gainOf(graph, strip), 0.0) << "redo brings the build back with its fader";
}
