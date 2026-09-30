#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** The title a module is listed under everywhere a person picks or reads it: the custom "displayName"
 *  node property when the user set one, else the processor's auto-numbered name ("LFO 3").
 *
 *  The one place that rule lives: the canvas card header (GraphEditor::getModuleTitle), the Mod Matrix's
 *  pickers and the mixer's key-send names all read it, so a rename shows up everywhere alike. Pass the
 *  node's own properties and processor; message thread only, like the property itself. */
inline juce::String moduleTitle(const juce::NamedValueSet& nodeProperties, const juce::AudioProcessor* processor) {
    const auto custom = nodeProperties["displayName"].toString();
    if (custom.isNotEmpty())
        return custom;
    return processor != nullptr ? processor->getName() : juce::String();
}

inline juce::String moduleTitle(const juce::AudioProcessorGraph::Node& node) {
    return moduleTitle(node.properties, node.getProcessor());
}

} // namespace synth
