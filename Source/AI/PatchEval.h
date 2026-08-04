#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/**
 * @brief Structural, model-independent checks run against a graph an AI patch was applied to.
 *
 * These answer a different question than AIStateMapper::validatePatch(): validatePatch only
 * checks that the JSON is well-formed and every value is legal, which a patch can satisfy while
 * still being useless (e.g. an "Audio Output" node nobody wired anything into). Used by
 * Tools/AIEvalHarness to score candidate models against golden prompts.
 */
struct PatchEvalResult {
    bool hasAudioOutput = false;      ///< an "Audio Output" node exists in the graph
    bool sourceReachesOutput = false; ///< an "Oscillator" is connected, transitively, to it
    bool allParamsInRange = true;     ///< every parameter's current value lies within its own declared range
    juce::String detail;              ///< semicolon-joined reasons for any false field above

    bool passed() const { return hasAudioOutput && sourceReachesOutput && allParamsInRange; }
};

/**
 * @brief Evaluates the current state of `graph` against the checks in PatchEvalResult.
 */
PatchEvalResult evaluatePatch(const juce::AudioProcessorGraph& graph);

} // namespace synth
