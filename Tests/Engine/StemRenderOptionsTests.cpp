// synth::StemSessionOptions (docs/mixer/stem-export.md#render-options): the filter, the tap before the solo gate and
// the temporary un-mute that "Render to audio track" drives StemSession with. Stem export's own behaviour is pinned in
// StemExportTests.cpp; here only the three options are. Hosted engine, no device, 48 kHz, 512-sample blocks.

#include "../Mixer/MasterSafetyOff.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/MasterModule.h"
#include "Transport/OfflineTransportDriver.h"
#include "Transport/StemExporter.h"
#include "Transport/StemSession.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

using synth::BounceOptions;
using synth::StemResult;
using synth::StemSession;
using synth::StemSessionOptions;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kRight = ChannelStripModule::kRightBase;
constexpr float kSourceA = 0.3f;
constexpr float kSourceB = -0.15f;
constexpr float kGainADb = 3.0f;
constexpr float kGainBDb = -6.0f;

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

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

void setGainDb(juce::AudioProcessor& processor, float db) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (ranged->getParameterID() == "gain")
                ranged->setValueNotifyingHost(ranged->convertTo0to1(db));
}

// Two strips (A, B), each fed by a constant source, both on Master's Mix.
struct Rig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    NodeID stripA, stripB;

    bool build() {
        engine.initialise();
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        const auto out = addFactoryNode(graph, "Audio Output");
        const auto master = addFactoryNode(graph, "Master");
        stripA = addFactoryNode(graph, "Channel Strip");
        stripB = addFactoryNode(graph, "Channel Strip");
        const auto srcA = graph.addNode(std::make_unique<ConstantSource>(kSourceA))->nodeID;
        const auto srcB = graph.addNode(std::make_unique<ConstantSource>(kSourceB))->nodeID;
        for (auto [src, strip] : {std::pair{srcA, stripA}, std::pair{srcB, stripB}}) {
            graph.addConnection({{src, 0}, {strip, 0}});
            graph.addConnection({{src, 1}, {strip, kRight}});
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {out, 0}});
        graph.addConnection({{master, 1}, {out, 1}});
        setGainDb(*stripAt(graph, stripA), kGainADb);
        setGainDb(*stripAt(graph, stripB), kGainBDb);
        if (auto* m = dynamic_cast<MasterModule*>(graph.getNodeForId(master)->getProcessor()))
            testutil::turnOffMasterSafetyLimiter(*m);
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);
        return true;
    }
    ~Rig() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

BounceOptions oneBeat() {
    BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 1.0;
    options.tailSeconds = 0.1;
    options.sampleRate = kSampleRate;
    options.blockSize = kBlockSize;
    options.bitDepth = 32;
    options.numChannels = 2;
    return options;
}

struct TempDir {
    TempDir()
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("agentsynth_stem_options_" + juce::Uuid().toString())) {}
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

// Runs a session to the end the way StemRunner does, optionally cancelling after the first chunk.
StemResult runSession(AudioEngine& engine, const juce::File& dir, const StemSessionOptions& options,
                      bool cancelAfterFirstChunk = false) {
    StemSession session(engine, dir, oneBeat(), {}, nullptr, options);
    while (!session.isRangeDone()) {
        session.stepRange(8);
        if (cancelAfterFirstChunk)
            session.requestCancel();
    }
    while (!session.isTailDone())
        session.stepTail(8);
    return session.finish();
}

// Magnitude of the left leg over the stem's second half (past every ramp).
float settledLeft(const juce::File& wav) {
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wav.createInputStream().release(), true));
    if (reader == nullptr)
        return -1.0f;
    juce::AudioBuffer<float> audio(2, (int)reader->lengthInSamples);
    reader->read(&audio, 0, (int)reader->lengthInSamples, 0, true, true);
    return audio.getSample(0, audio.getNumSamples() / 2);
}

} // namespace

TEST(StemRenderOptionsTest, FilterWritesOnlyTheChosenStrips) {
    Rig rig;
    ASSERT_TRUE(rig.build());
    TempDir out;
    StemSessionOptions options;
    options.onlyStrips = {rig.stripB};
    const auto result = runSession(rig.engine, out.dir, options);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 1);
    ASSERT_EQ(result.stemStrips.size(), 1u);
    EXPECT_EQ(result.stemStrips[0], rig.stripB);
    EXPECT_NEAR(settledLeft(result.stemFiles[0]), kSourceB * juce::Decibels::decibelsToGain(kGainBDb), 1.0e-5f);
}

TEST(StemRenderOptionsTest, UnfilteredExportStillWritesEveryStripAndNamesTheirNodes) {
    Rig rig;
    ASSERT_TRUE(rig.build());
    TempDir out;
    const auto result = runSession(rig.engine, out.dir, {});
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.stemFiles.size(), 2);
    ASSERT_EQ(result.stemStrips.size(), 2u);
    EXPECT_EQ(result.stemStrips[0], rig.stripA);
    EXPECT_EQ(result.stemStrips[1], rig.stripB);
}

TEST(StemRenderOptionsTest, TapBeforeSoloRendersAStripAnotherSoloSilences) {
    Rig rig;
    ASSERT_TRUE(rig.build());
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, true));

    TempDir gated;
    const auto normal = runSession(rig.engine, gated.dir, {});
    ASSERT_TRUE(normal.ok) << normal.message;
    EXPECT_EQ(settledLeft(normal.stemFiles[1]), 0.0f) << "export keeps the solo gate: B is silenced by A's solo";

    TempDir ungated;
    StemSessionOptions options;
    options.onlyStrips = {rig.stripB};
    options.tapBeforeSolo = true;
    const auto result = runSession(rig.engine, ungated.dir, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NEAR(settledLeft(result.stemFiles[0]), kSourceB * juce::Decibels::decibelsToGain(kGainBDb), 1.0e-5f);
    EXPECT_FALSE(stripAt(rig.engine.getGraph(), rig.stripB)->getLiveTapBuffer() != nullptr) << "live tap disarmed";
    EXPECT_TRUE(stripAt(rig.engine.getGraph(), rig.stripA)->isSoloed()) << "the solo is untouched";
}

TEST(StemRenderOptionsTest, AMutedSourceRendersItsSoundAndItsMuteIsRestored) {
    Rig rig;
    ASSERT_TRUE(rig.build());
    auto* strip = stripAt(rig.engine.getGraph(), rig.stripB);
    strip->setMuted(true);

    TempDir silent;
    StemSessionOptions plain;
    plain.onlyStrips = {rig.stripB};
    plain.tapBeforeSolo = true;
    const auto muted = runSession(rig.engine, silent.dir, plain);
    ASSERT_TRUE(muted.ok) << muted.message;
    EXPECT_EQ(settledLeft(muted.stemFiles[0]), 0.0f) << "without the option a muted strip taps silence";

    TempDir heard;
    auto options = plain;
    options.unmuteStrips = {rig.stripB};
    const auto result = runSession(rig.engine, heard.dir, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NEAR(settledLeft(result.stemFiles[0]), kSourceB * juce::Decibels::decibelsToGain(kGainBDb), 1.0e-5f);
    EXPECT_TRUE(strip->isMuted()) << "the mute is put back";
}

TEST(StemRenderOptionsTest, CancelLeavesNoFilesAndRestoresTheMutes) {
    Rig rig;
    ASSERT_TRUE(rig.build());
    auto* strip = stripAt(rig.engine.getGraph(), rig.stripB);
    strip->setMuted(true);

    TempDir out;
    StemSessionOptions options;
    options.onlyStrips = {rig.stripB};
    options.tapBeforeSolo = true;
    options.unmuteStrips = {rig.stripB};
    const auto result = runSession(rig.engine, out.dir, options, /*cancelAfterFirstChunk=*/true);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.stemFiles.isEmpty());
    EXPECT_EQ(out.dir.findChildFiles(juce::File::findFiles, false).size(), 0);
    EXPECT_TRUE(strip->isMuted());
    EXPECT_EQ(strip->getLiveTapBuffer(), nullptr);
}
