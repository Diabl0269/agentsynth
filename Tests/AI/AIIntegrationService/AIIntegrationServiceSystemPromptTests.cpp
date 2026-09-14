// P2-9: worked few-shot examples embedded in the system prompt.
#include "AIIntegrationServiceTestFixture.h"

namespace synth {

// --- P2-9: worked few-shot examples in the system prompt ---
//
// The JSON below must stay in sync with the examples embedded in
// AIIntegrationService::initSystemPrompt() (Source/AI/AIIntegrationService/AIIntegrationService.cpp) — these tests
// exist to catch a hand-authoring mistake (a dangling node, a bad param) that eyeballing the prompt string would miss.
// They are necessary but not sufficient: the actual proof this feature works is a Tools/AIEvalHarness pass-rate delta,
// documented in the PR, not asserted here.

namespace {

constexpr const char* kWorkedExample1 = R"({
  "nodes": [
    { "id": 101, "type": "Oscillator", "params": { "waveform": "Saw", "octave": -1 } },
    { "id": 102, "type": "Filter", "params": { "cutoff": 800.0, "resonance": 0.4 } },
    { "id": 103, "type": "VCA" },
    { "id": 104, "type": "Filter Env", "params": { "attack": 0.01, "decay": 0.3, "sustain": 0.2, "release": 0.2 } },
    { "id": 105, "type": "Amp Env", "params": { "attack": 0.01, "decay": 0.15, "sustain": 0.7, "release": 0.2 } },
    { "id": 106, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 101, "srcPort": 0, "dst": 102, "dstPort": 0 },
    { "src": 102, "srcPort": 0, "dst": 103, "dstPort": 0 },
    { "src": 103, "srcPort": 0, "dst": 106, "dstPort": 0 }
  ],
  "modulations": [
    { "source": 104, "dest": 102, "destPort": 1, "amount": 0.6 },
    { "source": 105, "dest": 103, "destPort": 1, "amount": 1.0 }
  ]
})";

constexpr const char* kWorkedExample2 = R"({
  "nodes": [
    { "id": 201, "type": "Oscillator", "params": { "waveform": "Square", "octave": 1 } },
    { "id": 202, "type": "Filter", "params": { "cutoff": 3000.0, "resonance": 0.3 } },
    { "id": 203, "type": "VCA" },
    { "id": 204, "type": "Filter Env", "params": { "attack": 0.01, "decay": 0.2, "sustain": 0.0, "release": 0.1 } },
    { "id": 205, "type": "Amp Env", "params": { "attack": 0.01, "decay": 0.15, "sustain": 0.0, "release": 0.1 } },
    { "id": 206, "type": "LFO", "params": { "mode": false, "rateHz": 5.0, "level": 1.0 } },
    { "id": 207, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 201, "srcPort": 0, "dst": 202, "dstPort": 0 },
    { "src": 202, "srcPort": 0, "dst": 203, "dstPort": 0 },
    { "src": 203, "srcPort": 0, "dst": 207, "dstPort": 0 }
  ],
  "modulations": [
    { "source": 204, "dest": 202, "destPort": 1, "amount": 0.7 },
    { "source": 205, "dest": 203, "destPort": 1, "amount": 1.0 },
    { "source": 206, "dest": 201, "destPort": 4, "amount": 1.0 }
  ]
})";

constexpr const char* kWorkedExample3 = R"({
  "nodes": [
    { "id": 301, "type": "Oscillator", "params": { "waveform": "Saw" } },
    { "id": 302, "type": "Filter", "params": { "cutoff": 1500.0 } },
    { "id": 303, "type": "VCA" },
    { "id": 304, "type": "Distortion", "params": { "drive": 3.0, "mix": 0.3 } },
    { "id": 305, "type": "Chorus", "params": { "rate": 0.6, "depth": 0.4, "mix": 0.5 } },
    { "id": 306, "type": "Reverb", "params": { "roomSize": 0.7, "wet": 0.4 } },
    { "id": 307, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 301, "srcPort": 0, "dst": 302, "dstPort": 0 },
    { "src": 302, "srcPort": 0, "dst": 303, "dstPort": 0 },
    { "src": 303, "srcPort": 0, "dst": 304, "dstPort": 0 },
    { "src": 304, "srcPort": 0, "dst": 305, "dstPort": 0 },
    { "src": 305, "srcPort": 0, "dst": 306, "dstPort": 0 },
    { "src": 306, "srcPort": 0, "dst": 307, "dstPort": 0 }
  ]
})";

constexpr const char* kWorkedExample4Seed = R"({
  "nodes": [
    { "id": 401, "type": "Oscillator", "params": { "waveform": "Saw" } },
    { "id": 402, "type": "Filter", "params": { "cutoff": 1200.0 } },
    { "id": 403, "type": "VCA" },
    { "id": 404, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 401, "srcPort": 0, "dst": 402, "dstPort": 0 },
    { "src": 402, "srcPort": 0, "dst": 403, "dstPort": 0 },
    { "src": 403, "srcPort": 0, "dst": 404, "dstPort": 0 }
  ]
})";

constexpr const char* kWorkedExample4Delta =
    R"({"mode": "merge", "nodes": [{"id": 9401, "type": "Oscillator", "params": {"waveform": "Saw", "coarse": 7}}], )"
    R"("connections": [{"src": 9401, "srcPort": 0, "dst": 402, "dstPort": 0}]})";

constexpr const char* kWorkedExample5Seed = R"({
  "nodes": [
    { "id": 501, "type": "Oscillator", "params": { "waveform": "Square" } },
    { "id": 502, "type": "Distortion" },
    { "id": 503, "type": "Filter", "params": { "cutoff": 2500.0 } },
    { "id": 504, "type": "VCA" },
    { "id": 505, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 501, "srcPort": 0, "dst": 502, "dstPort": 0 },
    { "src": 502, "srcPort": 0, "dst": 503, "dstPort": 0 },
    { "src": 503, "srcPort": 0, "dst": 504, "dstPort": 0 },
    { "src": 504, "srcPort": 0, "dst": 505, "dstPort": 0 }
  ]
})";

constexpr const char* kWorkedExample5Delta =
    R"({"mode": "merge", "remove": [502], "nodes": [], )"
    R"("connections": [{"src": 501, "srcPort": 0, "dst": 503, "dstPort": 0}]})";

PatchEvalResult applyAndEvaluate(const char* json) {
    juce::AudioProcessorGraph graph;
    prepareGraphForPatchEval(graph);
    juce::var parsed = juce::JSON::parse(juce::String(json));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(parsed, graph, /*clearExisting=*/true, /*trusted=*/true));
    return evaluatePatch(graph);
}

PatchEvalResult applyMergeAndEvaluate(const char* seedJson, const char* deltaJson) {
    juce::AudioProcessorGraph graph;
    prepareGraphForPatchEval(graph);
    juce::var seed = juce::JSON::parse(juce::String(seedJson));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(seed, graph, /*clearExisting=*/true, /*trusted=*/true));
    juce::var delta = juce::JSON::parse(juce::String(deltaJson));
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(delta, graph, /*clearExisting=*/false, /*trusted=*/true));
    return evaluatePatch(graph);
}

} // namespace

TEST_F(AIIntegrationServiceTest, SystemPromptContainsWorkedExamples) {
    auto prompt = service->getHistory()[0].content;
    EXPECT_TRUE(prompt.contains("WORKED EXAMPLES"));
    EXPECT_TRUE(prompt.contains("Patch together a growling, punchy analog-style bass line"));
    EXPECT_TRUE(prompt.contains("Design a snappy square-wave pluck lead"));
    EXPECT_TRUE(prompt.contains("Chain a saw-wave source through drive, chorus, and reverb"));
    EXPECT_TRUE(prompt.contains("Stack a second oscillator a fifth above the existing one"));
    EXPECT_TRUE(prompt.contains("Take out the distortion"));
}

TEST_F(AIIntegrationServiceTest, WorkedExamplePatchesAreStructurallyValid) {
    for (const char* json : {kWorkedExample1, kWorkedExample2, kWorkedExample3}) {
        auto result = applyAndEvaluate(json);
        EXPECT_TRUE(result.passed()) << "example failed: " << result.detail;
    }

    auto merge4 = applyMergeAndEvaluate(kWorkedExample4Seed, kWorkedExample4Delta);
    EXPECT_TRUE(merge4.passed()) << "example 4 failed: " << merge4.detail;

    auto merge5 = applyMergeAndEvaluate(kWorkedExample5Seed, kWorkedExample5Delta);
    EXPECT_TRUE(merge5.passed()) << "example 5 failed: " << merge5.detail;
}

// Reusing an AIEvalHarness eval prompt as a few-shot example would be train/test contamination and
// invalidate the P2-8 measurement — see the P2-9 task card. This is a manual copy of the 40 scenario
// prompts from Tools/AIEvalHarness/Main.cpp's scenarios(); if that list changes, re-sync it here.
TEST_F(AIIntegrationServiceTest, WorkedExamplePromptsDoNotOverlapEvalScenarios) {
    static const std::vector<juce::String> kEvalScenarioPrompts = {
        "Create a fat analog bass patch",
        "Make a short plucky lead sound",
        "Build a warm evolving pad with a slow filter sweep",
        "Create an acid bassline with a resonant filter and a sequencer",
        "Design a bell-like FM tone",
        "Make a dark drone with reverb",
        "Create a bright synth stab with a fast envelope",
        "Build a simple organ patch with multiple oscillators",
        "Make a white noise sweep riser",
        "Create a polyphonic keys patch with chorus",
        "Build a patch with compression and limiting on the output",
        "Create a swirling flanger effect on a saw wave",
        "Build a polyphonic pad using Poly MIDI and a voice mixer",
        "Make a sequenced arpeggio with a poly sequencer",
        "Create a pumping bass sound with heavy compression",
        "Build a lead patch driven by external MIDI input",
        "Create a thick unison lead with two oscillators",
        "Design an ambient wash with chorus, phaser, and reverb",
        "Make a percussive pluck with a fast filter envelope",
        "Build a vintage organ patch with a slow chorus and a flanger",
        "Add reverb to the end of the chain",
        "Add an LFO modulating the filter cutoff",
        "Make it brighter",
        "Add a delay after the filter",
        "Change the oscillator to a square wave",
        "Add an amp envelope controlling the VCA",
        "Remove the filter from the patch",
        "Add some distortion for grit",
        "Detune the oscillator slightly for width",
        "Add chorus and then a delay at the end",
        "Make the attack slower",
        "Increase the filter resonance",
        "Add a compressor for punch",
        "Add a limiter at the very end",
        "Add a flanger for movement",
        "Add an LFO for subtle pitch vibrato",
        "Add a filter envelope controlling the cutoff",
        "Make it quieter",
        "Remove the VCA",
        "Add an LFO modulating the cutoff at reduced depth",
    };

    static const std::vector<juce::String> kFewShotPrompts = {
        "Patch together a growling, punchy analog-style bass line",
        "Design a snappy square-wave pluck lead",
        "Chain a saw-wave source through drive, chorus, and reverb for a wide, driven texture",
        "Stack a second oscillator a fifth above the existing one, feeding into the same filter",
        "Take out the distortion - it's too harsh - and connect straight through instead",
    };

    for (const auto& fewShot : kFewShotPrompts) {
        for (const auto& evalPrompt : kEvalScenarioPrompts) {
            EXPECT_FALSE(fewShot.equalsIgnoreCase(evalPrompt))
                << "few-shot prompt exactly matches an eval scenario: " << fewShot;
            EXPECT_FALSE(fewShot.containsIgnoreCase(evalPrompt))
                << "few-shot prompt contains an eval scenario verbatim: " << fewShot;
        }
    }
}

} // namespace synth
