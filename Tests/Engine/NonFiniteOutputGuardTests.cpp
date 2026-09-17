// FRO161: AudioEngine::renderNextBlock's non-finite output guard (see AudioEngineRenderPass.cpp's
// scrubNonFiniteOutput). NaN/Inf reaching CoreAudio (or a plugin host) is undefined behaviour, and
// nothing upstream of the device/host boundary checked for it before this -- a runaway feedback
// patch or an upstream divide-by-zero could ride the buffer all the way out. This guard lives in
// AudioEngine::renderNextBlock, not in MasterModule, because that is the one place both host modes
// actually funnel through (see the file header of AudioEngineRenderPass.cpp) -- MasterModule is
// splice-in-only (createDefaultPatch's default patch has none at all) and is not last on the
// signal path even when present (Rec Tap, and the metronome's post-graph click, both sit after it).
//
// Two layers:
//   * GUARD-ONLY layer -- a raw audioInputNode -> audioOutputNode passthrough (no modules at all),
//     driven through the real Standalone device callback (audioDeviceIOCallbackWithContext) exactly
//     like FeedbackGuardTests.cpp's own guard-only layer. No arithmetic happens on this path besides
//     the guard's own branchless select, so a finite sample must come back with the EXACT SAME bit
//     pattern it went in with -- these tests use exact float equality, not a tolerance.
//   * MASTER layer -- a Hosted-mode graph with a real MasterModule spliced in (the
//     Tests/Mixer/MixerSoloTests.cpp SoloRig shape), proving the guard still reaches a non-finite
//     sample when Master is bypassed (its dry branch sums Direct into Mix and forwards Mix
//     unchanged -- no gain multiply, no scrub of its own) or muted (Master's own mute already
//     clears its channels; the guard downstream is then a no-op, which this proves rather than
//     assumes). NOTE: `AudioEngine::masterMuted_` (the engine's own "mute everything" atomic, wired
//     to a separate transport-level control) is NOT the same thing as MasterModule::isMuted() --
//     these tests exercise the latter and leave the former at its default (false).
//
// Headless house rules as everywhere else: no real audio device, no sleeps.

#include "../FakeAudioIODevice.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/MasterModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
using NodeID = juce::AudioProcessorGraph::NodeID;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kPosInf = std::numeric_limits<float>::infinity();
constexpr float kNegInf = -std::numeric_limits<float>::infinity();

// Same shape as FeedbackGuardTests.cpp's buildRawPassthrough -- nothing to do with any module,
// stands in for "whatever a patch handed the graph's output" that the guard must still catch.
void buildRawPassthrough(AudioEngine& engine, int numChannels) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(numChannels, numChannels, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < numChannels; ++channel)
        graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
}

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

} // namespace

// ============================================================================
// Guard-only layer (Standalone device callback, raw passthrough)
// ============================================================================

TEST(NonFiniteOutputGuardTest, NaNSampleBecomesSilence) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildRawPassthrough(engine, 2);
    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    std::vector<float> inLeft((std::size_t)kBlockSize, 0.25f), inRight((std::size_t)kBlockSize, 0.25f);
    inLeft[10] = kNaN;
    std::vector<float> outLeft((std::size_t)kBlockSize, -9999.0f), outRight((std::size_t)kBlockSize, -9999.0f);
    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    EXPECT_EQ(outLeft[10], 0.0f) << "the NaN sample must come out as silence";
    EXPECT_EQ(outLeft[9], 0.25f) << "neighbouring finite samples must be untouched";
    EXPECT_EQ(outLeft[11], 0.25f);
    engine.audioDeviceStopped();
}

TEST(NonFiniteOutputGuardTest, PositiveAndNegativeInfinityBecomeSilence) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildRawPassthrough(engine, 2);
    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    std::vector<float> inLeft((std::size_t)kBlockSize, 0.5f), inRight((std::size_t)kBlockSize, -0.5f);
    inLeft[0] = kPosInf;
    inRight[(std::size_t)(kBlockSize - 1)] = kNegInf;
    std::vector<float> outLeft((std::size_t)kBlockSize, -9999.0f), outRight((std::size_t)kBlockSize, -9999.0f);
    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    EXPECT_EQ(outLeft[0], 0.0f) << "+Inf must come out as silence";
    EXPECT_EQ(outRight[(std::size_t)(kBlockSize - 1)], 0.0f) << "-Inf must come out as silence too";
    engine.audioDeviceStopped();
}

TEST(NonFiniteOutputGuardTest, MixtureOfFiniteAndNonFiniteLeavesFiniteSamplesUntouched) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildRawPassthrough(engine, 2);
    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    std::vector<float> inLeft((std::size_t)kBlockSize), inRight((std::size_t)kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        inLeft[(std::size_t)i] = 0.001f * (float)i - 0.25f; // a finite ramp crossing zero
        inRight[(std::size_t)i] = inLeft[(std::size_t)i];
    }
    // Scatter every non-finite bit pattern the guard has to handle through the same block.
    inLeft[5] = kNaN;
    inLeft[50] = kPosInf;
    inLeft[200] = kNegInf;
    inRight[100] = kNaN;

    std::vector<float> outLeft((std::size_t)kBlockSize, -9999.0f), outRight((std::size_t)kBlockSize, -9999.0f);
    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    for (int i = 0; i < kBlockSize; ++i) {
        const auto idx = (std::size_t)i;
        if (i == 5 || i == 50 || i == 200)
            EXPECT_EQ(outLeft[idx], 0.0f) << "left channel non-finite at " << i;
        else
            EXPECT_EQ(outLeft[idx], inLeft[idx]) << "left channel finite sample at " << i;

        if (i == 100)
            EXPECT_EQ(outRight[idx], 0.0f) << "right channel non-finite at " << i;
        else
            EXPECT_EQ(outRight[idx], inRight[idx]) << "right channel finite sample at " << i;
    }
    engine.audioDeviceStopped();
}

TEST(NonFiniteOutputGuardTest, FiniteOnlyAudioIsBitIdentical) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildRawPassthrough(engine, 2);
    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    std::vector<float> inLeft((std::size_t)kBlockSize), inRight((std::size_t)kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        inLeft[(std::size_t)i] =
            0.8f * std::sin(juce::MathConstants<float>::twoPi * 7.0f * (float)i / (float)kBlockSize);
        inRight[(std::size_t)i] = -inLeft[(std::size_t)i];
    }
    // A denormal and a negative zero: both finite, both must survive the bit test unchanged.
    inLeft[0] = std::numeric_limits<float>::denorm_min();
    inRight[0] = -0.0f;

    std::vector<float> outLeft((std::size_t)kBlockSize, -9999.0f), outRight((std::size_t)kBlockSize, -9999.0f);
    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    for (int i = 0; i < kBlockSize; ++i) {
        const auto idx = (std::size_t)i;
        EXPECT_EQ(outLeft[idx], inLeft[idx]) << "left sample " << i << " must be bit-identical";
        EXPECT_EQ(outRight[idx], inRight[idx]) << "right sample " << i << " must be bit-identical";
    }
    // std::signbit distinguishes -0.0 from 0.0, which == alone would not.
    EXPECT_TRUE(std::signbit(outRight[0])) << "negative zero must survive with its sign bit intact";
    engine.audioDeviceStopped();
}

// ============================================================================
// Master layer (Hosted mode, a real MasterModule spliced in)
// ============================================================================

namespace {

// A stereo source that writes one constant sample value to both its outputs, every block -- local
// to this file rather than reused from Tests/Mixer/MixerSoloTests.cpp's ConstantSource, since NaN
// is not a value that file's own tests ever need.
class FixedSource : public juce::AudioProcessor {
public:
    explicit FixedSource(float value)
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
        , value_(value) {}

    const juce::String getName() const override { return "Fixed"; }
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

constexpr int kMasterBlockSize = 64;

MasterModule* masterAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<MasterModule*>(node->getProcessor()) : nullptr;
}

// Builds Fixed(NaN) -> Master (Mix L/R only, Direct left disconnected/silent) -> Audio Output, and
// prepares the engine for one Hosted render. Returns the MasterModule so the test can flip
// bypass/mute before rendering.
MasterModule* buildMasterRig(AudioEngine& engine) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kMasterBlockSize);

    const auto out = addFactoryNode(graph, "Audio Output");
    const auto master = addFactoryNode(graph, "Master");
    const auto src = graph.addNode(std::make_unique<FixedSource>(kNaN))->nodeID;

    graph.addConnection({{src, 0}, {master, MasterModule::kMixLeft}});
    graph.addConnection({{src, 1}, {master, MasterModule::kMixRight}});
    graph.addConnection({{master, 0}, {out, 0}});
    graph.addConnection({{master, 1}, {out, 1}});

    return masterAt(graph, master);
}

} // namespace

TEST(NonFiniteOutputGuardTest, GuardStillAppliesWhenMasterIsBypassed) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto* masterModule = buildMasterRig(engine);
    ASSERT_NE(masterModule, nullptr);
    masterModule->setBypassed(true); // dry branch: sums Direct into Mix, forwards Mix unchanged

    engine.prepareForHost(kSampleRate, kMasterBlockSize, 2, 2);

    juce::AudioBuffer<float> buffer(2, kMasterBlockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);

    for (int i = 0; i < kMasterBlockSize; ++i) {
        EXPECT_EQ(buffer.getSample(0, i), 0.0f)
            << "bypassed Master forwards the NaN unchanged -- renderNextBlock must still scrub it, sample " << i;
        EXPECT_EQ(buffer.getSample(1, i), 0.0f);
    }
    engine.releaseFromHost();
}

TEST(NonFiniteOutputGuardTest, MutedMasterYieldsSilence) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto* masterModule = buildMasterRig(engine);
    ASSERT_NE(masterModule, nullptr);
    ASSERT_TRUE(masterModule->hasMuteParameter());
    masterModule->setMuted(true);

    engine.prepareForHost(kSampleRate, kMasterBlockSize, 2, 2);

    juce::AudioBuffer<float> buffer(2, kMasterBlockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);

    for (int i = 0; i < kMasterBlockSize; ++i) {
        EXPECT_EQ(buffer.getSample(0, i), 0.0f)
            << "Master's own mute already clears -- silence must survive the guard too, sample " << i;
        EXPECT_EQ(buffer.getSample(1, i), 0.0f);
    }
    engine.releaseFromHost();
}
