// OutputMeterTapTests.cpp -- FRO148 (docs/mixer/meters.md): AudioEngine::takeOutputMeterPeak, the post-graph peak
// latch the Master column reads once Master has inserts. Latched straight after the graph, before the metronome click
// and the master-mute zero-fill, one latch per leg with its own slot per MeterReader.
#include "../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;

// Audio Input -> Audio Output, `numChannels` wide.
void buildPassthrough(AudioEngine& engine, int numChannels) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(numChannels, numChannels, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < numChannels; ++channel)
        graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
}

} // namespace

TEST(OutputMeterTapTest, LatchesEachLegsGraphOutputPeakAndConsumesOnRead) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildPassthrough(engine, 2);
    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    std::vector<float> inLeft((std::size_t)kBlockSize, 0.25f), inRight((std::size_t)kBlockSize, 0.5f);
    inLeft[10] = -0.3f; // the peak is a magnitude, so the negative excursion counts
    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    EXPECT_NEAR(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 0), 0.3f, 1e-6f);
    EXPECT_NEAR(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 1), 0.5f, 1e-6f);
    EXPECT_EQ(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 0), 0.0f) << "read-and-reset";
    EXPECT_NEAR(engine.takeOutputMeterPeak(synth::MeterReader::TrackHeader, 1), 0.5f, 1e-6f)
        << "another reader's slot is untouched by the Mixer reader's reads";
    engine.audioDeviceStopped();
}

TEST(OutputMeterTapTest, ASilentGraphLatchesNothing) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    buildPassthrough(engine, 2);
    engine.prepareForHost(kSampleRate, kBlockSize, 2, 2);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);

    EXPECT_EQ(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 0), 0.0f);
    EXPECT_EQ(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 1), 0.0f);
    engine.releaseFromHost();
}

TEST(OutputMeterTapTest, AMonoBufferReadsTheSameOnTheRightLeg) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    buildPassthrough(engine, 1);
    engine.prepareForHost(kSampleRate, kBlockSize, 1, 1);

    juce::AudioBuffer<float> buffer(1, kBlockSize);
    juce::FloatVectorOperations::fill(buffer.getWritePointer(0), 0.4f, kBlockSize);
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);

    EXPECT_NEAR(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 0), 0.4f, 1e-6f);
    EXPECT_NEAR(engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 1), 0.4f, 1e-6f)
        << "leg 1 falls back to the left channel when the buffer has one channel";
    engine.releaseFromHost();
}
