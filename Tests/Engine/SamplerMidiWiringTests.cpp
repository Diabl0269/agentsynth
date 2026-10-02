// AudioEngine::refreshMidiWiring: every Sampler is told whether a cable feeds its MIDI input, so a
// MIDI-only Sampler waits for its first note instead of free-running.

#include "AI/AIStateMapper/AIStateMapper.h"
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

// Applying a patch publishes the wiring itself rather than waiting for a later message-thread pass.
TEST(SamplerMidiWiringTest, ApplyingAPatchPublishesTheWiring) {
    AudioEngine source(AudioEngine::HostMode::Standalone);
    auto& sourceGraph = source.getGraph();
    auto midiSource = sourceGraph.addNode(std::make_unique<SequencerModule>());
    auto samplerNode = sourceGraph.addNode(std::make_unique<SamplerModule>());
    ASSERT_TRUE(sourceGraph.addConnection({{midiSource->nodeID, kMidi}, {samplerNode->nodeID, kMidi}}));
    const auto json = synth::AIStateMapper::graphToJSON(sourceGraph);

    AudioEngine target(AudioEngine::HostMode::Standalone);
    auto& targetGraph = target.getGraph();
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, targetGraph, /*clearExisting=*/true, /*trusted=*/true));

    SamplerModule* restored = nullptr;
    for (auto* node : targetGraph.getNodes())
        if (auto* candidate = dynamic_cast<SamplerModule*>(node->getProcessor()))
            restored = candidate;
    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->isMidiInputWired()) << "no publishTimeline or change broadcast has run yet";
}
