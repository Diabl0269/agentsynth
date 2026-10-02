// AudioEngine::refreshMidiWiring: every Sampler is told whether a cable feeds its MIDI input, so a
// MIDI-only Sampler waits for its first note instead of free-running.

#include "AudioEngine/AudioEngine.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/SequencerModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;
} // namespace

// Regression test for FRO480: a restored MIDI-only Sampler had no wiring flag and fired on block one.
TEST(SamplerMidiWiringTest, PublishTimelineTracksWhetherAMidiCableFeedsTheSampler) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    synth::TimelineDoc doc;

    auto source = graph.addNode(std::make_unique<SequencerModule>());
    auto samplerNode = graph.addNode(std::make_unique<SamplerModule>());
    auto* sampler = dynamic_cast<SamplerModule*>(samplerNode->getProcessor());
    ASSERT_NE(sampler, nullptr);

    engine.publishTimeline(doc);
    EXPECT_FALSE(sampler->isMidiInputWired()) << "nothing patched: the Sampler keeps free-running";

    ASSERT_TRUE(graph.addConnection({{source->nodeID, kMidi}, {samplerNode->nodeID, kMidi}}));
    engine.publishTimeline(doc);
    EXPECT_TRUE(sampler->isMidiInputWired());

    ASSERT_TRUE(graph.removeConnection({{source->nodeID, kMidi}, {samplerNode->nodeID, kMidi}}));
    engine.publishTimeline(doc);
    EXPECT_FALSE(sampler->isMidiInputWired());
}

TEST(SamplerMidiWiringTest, AnAudioCableIntoTheSamplerDoesNotCountAsMidi) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();

    auto source = graph.addNode(std::make_unique<OscillatorModule>());
    auto samplerNode = graph.addNode(std::make_unique<SamplerModule>());
    auto* sampler = dynamic_cast<SamplerModule*>(samplerNode->getProcessor());
    ASSERT_NE(sampler, nullptr);
    ASSERT_TRUE(graph.addConnection({{source->nodeID, 0}, {samplerNode->nodeID, SamplerModule::kTriggerCh}}));

    engine.refreshMidiWiring();
    EXPECT_FALSE(sampler->isMidiInputWired());
}
