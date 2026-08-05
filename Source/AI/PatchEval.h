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

/**
 * @brief Configures a graph the way AudioEngine does before device audio starts, so evaluatePatch()
 *        reports real bus connectivity instead of every "Audio Output" node reporting zero channels.
 *
 * Without this, an unconfigured graph's AudioGraphIOProcessor("Audio Output") reports zero
 * channels and every connection into it silently no-ops, making every patch look unconnected
 * regardless of content. Callers that build a scratch graph purely to run evaluatePatch() on it
 * (Tools/AIEvalHarness, Tests/AIIntegrationServiceTests, and the live structural retry gate in
 * AIIntegrationService) must call this before applying any patch JSON to that graph.
 */
void prepareGraphForPatchEval(juce::AudioProcessorGraph& graph);

} // namespace synth
