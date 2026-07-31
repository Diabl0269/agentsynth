#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <memory>

namespace synth {

/**
 * @class AIStateMapper
 * @brief Handles conversion between AI-friendly JSON and juce::AudioProcessorGraph.
 */
class AIStateMapper {
public:
    /**
     * @brief Converts the current graph state to a JSON-compatible juce::var.
     */
    static juce::var graphToJSON(juce::AudioProcessorGraph& graph);

    /**
     * @brief Applies a JSON-compatible juce::var to the graph.
     * @return true if the patch was applied successfully.
     */
    static bool applyJSONToGraph(const juce::var& json, juce::AudioProcessorGraph& graph, bool clearExisting = true,
                                 bool trusted = false);

    /**
     * @brief Gets a Markdown-formatted string of all available modules and their parameters.
     */
    static juce::String getModuleSchema();

    /**
     * @brief Generates a JSON schema for patch validation and structured AI output.
     */
    static juce::var getPatchSchema();

    static std::unique_ptr<juce::AudioProcessor> createModule(const juce::String& type);

private:
    static bool validatePatchJSON(const juce::var& json);

    /**
     * @brief Helper to find the index of a string choice in an AudioParameterChoice.
     * @return index if found, -1 otherwise.
     */
    static int findChoiceIndex(juce::AudioParameterChoice* p, const juce::String& choiceText);

    static void applyParamsToProcessor(juce::AudioProcessor* processor, const juce::DynamicObject* paramsObj,
                                       bool trusted = false);
};

} // namespace synth
