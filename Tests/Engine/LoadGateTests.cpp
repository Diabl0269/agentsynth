// LoadGateTests.cpp -- the project-open audio gate (AudioEngine::setLoadGateOpen): silent while closed with the graph
// still running, the user's master mute never touched, and opening ramps the output in so a sound already playing
// does not click on. The metronome is the sound: a hosted engine with an empty graph is silent by construction.

#include "../TestAudioHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "Transport/Metronome.h"
#include "Transport/OfflineTransportDriver.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct GateRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;

    GateRig() {
        engine.initialise();
        engine.getGraph().clear();
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kRate, kBlock, 2);
        engine.getMetronome().setEnabled(true);
    }
    ~GateRig() {
        engine.releaseFromHost();
        engine.shutdown();
    }
};

// A steady 1 kHz sine on its one output.
class SineSource final : public juce::AudioProcessor {
public:
    SineSource()
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::mono(), true)) {}
    const juce::String getName() const override { return "Sine"; }
    void prepareToPlay(double, int) override { phase_ = 0.0; }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            buffer.setSample(0, i, 0.5f * (float)std::sin(phase_));
            phase_ += juce::MathConstants<double>::twoPi * 1000.0 / kRate;
        }
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
    double phase_ = 0.0;
};

float peak(const juce::AudioBuffer<float>& audio, int start, int length) {
    return audio.getMagnitude(0, start, length);
}

} // namespace

TEST(LoadGate, ClosedIsSilentAndLeavesTheMasterMuteAlone) {
    GateRig rig;
    rig.engine.setLoadGateOpen(false);
    ASSERT_TRUE(rig.engine.getTransport().play());
    const auto audio = rig.driver->renderBlocks(120); // spans the clicks at beats 0 and 1
    EXPECT_TRUE(TestAudioHelpers::isSilent(audio, 0));
    EXPECT_TRUE(TestAudioHelpers::isSilent(audio, 1));
    EXPECT_FALSE(rig.engine.isMasterMuted()) << "the gate is not the user's mute";
    EXPECT_FALSE(rig.engine.isLoadGateOpen());
}

TEST(LoadGate, OpenByDefault) {
    GateRig rig;
    EXPECT_TRUE(rig.engine.isLoadGateOpen());
    ASSERT_TRUE(rig.engine.getTransport().play());
    EXPECT_GT(peak(rig.driver->renderBlocks(4), 0, 4 * kBlock), 0.02f);
}

// A steady sine through the graph's own output node, rendered by a rig whose gate never closed (the reference) and by
// one whose gate is closed for block 0 and opens before block 1. Block 1 starts from silence and ramps up, and the two
// match exactly once the 15 ms ramp has passed.
TEST(LoadGate, OpeningRampsInWithoutAClick) {
    const auto addSine = [](GateRig& rig) {
        auto& graph = rig.engine.getGraph();
        auto sine = graph.addNode(std::make_unique<SineSource>());
        auto out = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
        graph.addConnection({{sine->nodeID, 0}, {out->nodeID, 0}});
        graph.rebuild();
    };
    GateRig reference;
    addSine(reference);
    const auto ref = reference.driver->renderBlocks(6);

    GateRig gated;
    addSine(gated);
    gated.engine.setLoadGateOpen(false);
    int block = 0;
    const auto out = gated.driver->renderBlocks(6, [&](const juce::AudioBuffer<float>&, const auto&) {
        if (block++ == 0)
            gated.engine.setLoadGateOpen(true);
    });
    ASSERT_GT(peak(ref, kBlock, kBlock), 0.1f) << "the reference sounds in block 1";
    EXPECT_LT(peak(out, 0, kBlock), 1.0e-6f) << "block 0 rendered closed";
    EXPECT_LT(std::abs(out.getSample(0, kBlock)), 0.01f) << "the first open sample starts from silence";
    EXPECT_LT(peak(out, kBlock, 64), peak(ref, kBlock, 64)) << "the start of block 1 is ramped";
    const int afterRamp = kBlock + (int)(0.015 * kRate) + 1;
    for (int i = afterRamp; i < 6 * kBlock; ++i)
        ASSERT_FLOAT_EQ(out.getSample(0, i), ref.getSample(0, i)) << "sample " << i;
    float largestStep = 0.0f;
    float referenceStep = 0.0f;
    for (int i = kBlock + 1; i < afterRamp; ++i) {
        largestStep = std::max(largestStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        referenceStep = std::max(referenceStep, std::abs(ref.getSample(0, i) - ref.getSample(0, i - 1)));
    }
    // d(signal x gain) is at most the signal's own step plus its size times one ramp step.
    const float rampStep = 1.0f / (float)(0.015 * kRate);
    EXPECT_LE(largestStep, referenceStep + peak(ref, kBlock, afterRamp - kBlock) * rampStep * 1.01f)
        << "the ramp adds no jump of its own";
}
