#pragma once

// The fixed prompt set AIPatchHarness replays against a live model, factored out of Main.cpp so
// Tests/AIPatchFixtureReplayTests.cpp can share the EXACT SAME table when replaying a recorded
// corpus. One source of truth: a fixture's "scenario" field is looked up here to rebuild the same
// seed graph and mode the original run used, so the two can never silently drift apart.

#include <juce_core/juce_core.h>
#include <vector>

namespace synth {
namespace harness {

// One replayed scenario: what the user types, and what patch (if any) is already on the canvas
// when they type it. `seedPatch` is applied trusted, exactly as loading a preset would, so
// merge-mode prompts see realistic pre-existing node ids.
struct Scenario {
    const char* name;
    const char* prompt;
    bool mergeMode;
    const char* seedPatch; // nullptr for a from-scratch (replace-mode) request
};

// A small but real starting patch: MIDI -> Oscillator -> Filter -> VCA -> Output.
// Node ids here are what a merge-mode prompt must reference correctly.
constexpr const char* kBasicPatch = R"({
  "nodes": [
    {"id": 1, "type": "Poly MIDI"},
    {"id": 2, "type": "Oscillator", "params": {"waveform": "Saw"}},
    {"id": 3, "type": "Filter", "params": {"cutoff": 2000.0}},
    {"id": 4, "type": "VCA"},
    {"id": 5, "type": "Audio Output"}
  ],
  "connections": [
    {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
    {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0},
    {"src": 3, "srcPort": 0, "dst": 4, "dstPort": 0},
    {"src": 4, "srcPort": 0, "dst": 5, "dstPort": 0}
  ]
})";

// Prompts a real user would plausibly type. Deliberately spans both modes, because
// merge-mode failures (stale/hallucinated ids) and replace-mode failures (invented
// module types, bad choice strings) are different populations.
inline const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> s = {
        // --- from scratch (replace mode) ---
        {"basic-bass", "Create a fat analog bass patch", false, nullptr},
        {"pluck", "Make a short plucky lead sound", false, nullptr},
        {"pad", "Build a warm evolving pad with a slow filter sweep", false, nullptr},
        {"acid", "Create an acid bassline with a resonant filter and a sequencer", false, nullptr},
        {"bell", "Design a bell-like FM tone", false, nullptr},
        {"drone", "Make a dark drone with reverb", false, nullptr},
        {"stab", "Create a bright synth stab with a fast envelope", false, nullptr},
        {"organ", "Build a simple organ patch with multiple oscillators", false, nullptr},
        {"noise-sweep", "Make a white noise sweep riser", false, nullptr},
        {"poly-keys", "Create a polyphonic keys patch with chorus", false, nullptr},

        // --- modify an existing patch (merge mode) ---
        {"add-reverb", "Add reverb to the end of the chain", true, kBasicPatch},
        {"add-lfo-cutoff", "Add an LFO modulating the filter cutoff", true, kBasicPatch},
        {"brighter", "Make it brighter", true, kBasicPatch},
        {"add-delay", "Add a delay after the filter", true, kBasicPatch},
        {"change-wave", "Change the oscillator to a square wave", true, kBasicPatch},
        {"add-env", "Add an amp envelope controlling the VCA", true, kBasicPatch},
        {"remove-filter", "Remove the filter from the patch", true, kBasicPatch},
        {"add-distortion", "Add some distortion for grit", true, kBasicPatch},
        {"detune", "Detune the oscillator slightly for width", true, kBasicPatch},
        {"add-chorus-delay", "Add chorus and then a delay at the end", true, kBasicPatch},
    };
    return s;
}

} // namespace harness
} // namespace synth
