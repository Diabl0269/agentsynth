// Unit coverage for the structural checks Tools/AIEvalHarness scores golden-prompt patches
// against. These are pure graph-analysis functions (Source/AI/PatchEval.h) with no dependency on
// a live model, so — unlike the harness itself — they belong in the regular test suite and CI.
//
// Every graph here is built via AIStateMapper::applyJSONToGraph(..., trusted=true), the same
// entry point AIEvalHarness and preset loading use, rather than hand-assembling
// juce::AudioProcessorGraph nodes/connections directly.

#include "AI/AIStateMapper.h"
#include "AI/PatchEval.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {
namespace {

// juce::AudioProcessorGraph has no copy/move constructor, so callers build into an out-param
// rather than returning by value.
//
// A bare AudioProcessorGraph's "Audio Output" IO node mirrors the graph's OWN bus channel
// count (AudioGraphIOProcessor::setParentGraph), which defaults to zero — a connection into it
// would silently no-op, exactly like an unconnected node, without this. AudioEngine configures
// this for real via setPlayConfigDetails()+prepareToPlay() before device audio starts; do the
// same here so "Audio Output" behaves like it does in the running app.
void buildGraph(const char* patchJson, juce::AudioProcessorGraph& graph) {
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    graph.prepareToPlay(44100.0, 512);

    juce::var json = juce::JSON::parse(juce::String(patchJson));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
}

TEST(PatchEvalTest, FullyConnectedChainPasses) {
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Poly MIDI"},
            {"id": 2, "type": "Oscillator"},
            {"id": 3, "type": "Filter"},
            {"id": 4, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
            {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0},
            {"src": 3, "srcPort": 0, "dst": 4, "dstPort": 0}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.hasAudioOutput);
    EXPECT_TRUE(result.sourceReachesOutput);
    EXPECT_TRUE(result.allParamsInRange);
    EXPECT_TRUE(result.passed()) << result.detail.toStdString();
}

// A Sampler is a sound source in the module library, but it is silent until a user loads a file and
// nothing in a model-authored patch can do that. Counting it here would let AIIntegrationService's
// structural gate accept a patch that can only ever play silence, so it must NOT satisfy
// sourceReachesOutput — and the reason has to say why rather than claiming there is no source.
TEST(PatchEvalTest, SamplerAloneDoesNotCountAsAReachableSource) {
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Sampler"},
            {"id": 2, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.hasAudioOutput);
    EXPECT_FALSE(result.sourceReachesOutput);
    EXPECT_FALSE(result.passed());
    EXPECT_TRUE(result.detail.contains("Sampler")) << "the rejection must name the Sampler: " << result.detail;
    EXPECT_TRUE(result.detail.contains("silent until a file is loaded"))
        << "the rejection must be actionable: " << result.detail;
}

TEST(PatchEvalTest, SamplerAlongsideAnOscillatorStillPasses) {
    // The Sampler is not disqualifying — it just cannot be the only thing feeding the output.
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Sampler"},
            {"id": 2, "type": "Oscillator"},
            {"id": 3, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 3, "dstPort": 0},
            {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.sourceReachesOutput);
    EXPECT_TRUE(result.passed()) << result.detail.toStdString();
}

TEST(PatchEvalTest, MissingAudioOutputFails) {
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [{"id": 1, "type": "Oscillator"}],
        "connections": []
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_FALSE(result.hasAudioOutput);
    EXPECT_FALSE(result.sourceReachesOutput);
    EXPECT_FALSE(result.passed());
    EXPECT_TRUE(result.detail.contains("Audio Output"));
}

TEST(PatchEvalTest, UnconnectedOscillatorDoesNotReachOutput) {
    // Both nodes exist, but nothing wires the oscillator to the output — a model that emits
    // node soup without connections must not pass.
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Oscillator"},
            {"id": 2, "type": "Audio Output"}
        ],
        "connections": []
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.hasAudioOutput);
    EXPECT_FALSE(result.sourceReachesOutput);
    EXPECT_FALSE(result.passed());
}

TEST(PatchEvalTest, DanglingChainNeverReachesOutputFails) {
    // Oscillator -> Filter is wired, but the Filter is never connected onward to Audio Output.
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Oscillator"},
            {"id": 2, "type": "Filter"},
            {"id": 3, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.hasAudioOutput);
    EXPECT_FALSE(result.sourceReachesOutput);
}

TEST(PatchEvalTest, NonOscillatorFeedIntoOutputDoesNotCountAsSource) {
    // A Filter fed straight into Audio Output with nothing upstream of it: Audio Output IS
    // connected to something, but that something is not a sound source.
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Filter"},
            {"id": 2, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.hasAudioOutput);
    EXPECT_FALSE(result.sourceReachesOutput);
}

TEST(PatchEvalTest, MidiOnlyConnectionDoesNotCountAsAudioPath) {
    // Poly MIDI feeds the Oscillator's MIDI input only; Audio Output is never wired to anything
    // (contrived, but exercises that a MIDI edge must not be mistaken for an audio path).
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Poly MIDI"},
            {"id": 2, "type": "Oscillator"},
            {"id": 3, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.hasAudioOutput);
    EXPECT_FALSE(result.sourceReachesOutput);
}

TEST(PatchEvalTest, OutOfRangeRequestIsClampedIntoRangeOnApply) {
    // Filter cutoff's declared range is [20, 20000] (FilterModule.h); a wildly out-of-range
    // request is silently clamped by applyParamsToProcessor's snapToLegalValue, not rejected.
    // This asserts PatchEval sees the resulting (clamped) value as in-range, not the requested
    // one — the check is exercising the real applied state, not the JSON request.
    juce::AudioProcessorGraph graph;
    buildGraph(R"({
        "nodes": [
            {"id": 1, "type": "Oscillator"},
            {"id": 2, "type": "Filter", "params": {"cutoff": 999999.0}},
            {"id": 3, "type": "Audio Output"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0}
        ]
    })",
               graph);

    const auto result = evaluatePatch(graph);
    EXPECT_TRUE(result.allParamsInRange);
    EXPECT_TRUE(result.passed()) << result.detail.toStdString();
}

} // namespace
} // namespace synth
