// MixerKeySendDuckingTests.cpp -- FRO318 (docs/mixer/sends-and-buses.md#sending-to-a-key-input): the
// end-to-end proof a Key send is heard. A real juce::AudioProcessorGraph renders
//
//   Audio In ch0 (kick pulses) ─► kick strip ── send 0 ──► Compressor Key L/R
//   Audio In ch1 (bass tone)   ─────────────────────────► Compressor ─► bass strip ─► Audio Out
//
// with the send wired by synth::addSend (the shipped flow) and the Compressor keyed by
// synth::publishSidechainConnections (what the engine's graph listener runs). The bass must drop
// while each pulse plays and recover between them; muting the send must stop the ducking while the
// Key cable stays plugged.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/SidechainConnections.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/CompressorModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;
// One bar of the pattern: a 100 ms pulse, then 400 ms of silence (the Compressor's default release
// is 100 ms, so the gap leaves it fully recovered).
constexpr int kPulseSamples = 4800;
constexpr int kCycleSamples = 24000;

struct DuckRig {
    juce::AudioProcessorGraph graph;
    NodeID input, output, kick, comp, bass;
    juce::int64 sample = 0;

    DuckRig() {
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        input = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode))->nodeID;
        output = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode))->nodeID;
        kick = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"))->nodeID;
        comp = graph.addNode(synth::AIStateMapper::createModule("Compressor"))->nodeID;
        bass = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"))->nodeID;

        graph.addConnection({{input, 0}, {kick, 0}});
        graph.addConnection({{input, 0}, {kick, kRight}});
        graph.addConnection({{input, 1}, {comp, 0}});
        graph.addConnection({{input, 1}, {comp, 1}});
        graph.addConnection({{comp, 0}, {bass, 0}});
        graph.addConnection({{comp, 1}, {bass, kRight}});
        graph.addConnection({{bass, 0}, {output, 0}});
        graph.addConnection({{bass, kRight}, {output, 1}});
    }

    void prepare() {
        synth::publishSidechainConnections(graph);
        graph.prepareToPlay(kSampleRate, kBlockSize);
        graph.rebuild();
    }

    /** Renders one pattern cycle; returns the bass output's RMS late in the pulse and late in the gap. */
    std::pair<float, float> renderCycle() {
        double pulseSum = 0.0, gapSum = 0.0;
        int pulseCount = 0, gapCount = 0;
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;
        for (int block = 0; block < kCycleSamples / kBlockSize; ++block) {
            for (int i = 0; i < kBlockSize; ++i) {
                const auto n = sample + i;
                const int inCycle = (int)(n % kCycleSamples);
                const double t = (double)n / kSampleRate;
                const float kickSample =
                    inCycle < kPulseSamples ? (float)std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * t) : 0.0f;
                buffer.setSample(0, i, kickSample);
                buffer.setSample(1, i, 0.25f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            }
            graph.processBlock(buffer, midi);
            for (int i = 0; i < kBlockSize; ++i) {
                const int inCycle = (int)((sample + i) % kCycleSamples);
                const double v = buffer.getSample(0, i);
                if (inCycle >= kPulseSamples - 2400 && inCycle < kPulseSamples) { // last 50 ms of the pulse
                    pulseSum += v * v;
                    ++pulseCount;
                } else if (inCycle >= kCycleSamples - 2400) { // last 50 ms of the gap
                    gapSum += v * v;
                    ++gapCount;
                }
            }
            sample += kBlockSize;
        }
        return {(float)std::sqrt(pulseSum / juce::jmax(1, pulseCount)),
                (float)std::sqrt(gapSum / juce::jmax(1, gapCount))};
    }
};

} // namespace

TEST(MixerKeySendDuckingTest, AKeySendDucksTheBassOnEveryPulseAndMutingItStops) {
    DuckRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.kick, synth::SendTarget{rig.comp, true}), 0);
    rig.prepare();
    auto* compressor = dynamic_cast<CompressorModule*>(rig.graph.getNodeForId(rig.comp)->getProcessor());
    ASSERT_NE(compressor, nullptr);
    ASSERT_TRUE(compressor->isSidechainConnected());

    rig.renderCycle(); // warm-up: smoothers and the detector settle
    const auto [duckedPulse, duckedGap] = rig.renderCycle();
    ASSERT_GT(duckedGap, 0.1f) << "the bass must be audible between pulses";
    EXPECT_LT(duckedPulse, duckedGap * 0.6f)
        << "the bass ducks while the kick plays (pulse " << duckedPulse << ", gap " << duckedGap << ")";

    ASSERT_TRUE(synth::setSendMuted(rig.graph, rig.kick, 0, true));
    rig.renderCycle();
    const auto [mutedPulse, mutedGap] = rig.renderCycle();
    EXPECT_NEAR(mutedPulse, mutedGap, mutedGap * 0.05f) << "a muted Key send stops the ducking";
    EXPECT_NEAR(mutedGap, duckedGap, duckedGap * 0.05f);
    synth::publishSidechainConnections(rig.graph);
    EXPECT_TRUE(compressor->isSidechainConnected()) << "mute silences the send -- it does not unplug the Key";
}

TEST(MixerKeySendDuckingTest, WithoutTheKeySendTheBassIsNeverDucked) {
    // Negative control: the identical rig with the send never added renders the bass flat.
    DuckRig rig;
    rig.prepare();
    rig.renderCycle();
    const auto [pulse, gap] = rig.renderCycle();
    ASSERT_GT(gap, 0.1f);
    EXPECT_NEAR(pulse, gap, gap * 0.05f);
}
