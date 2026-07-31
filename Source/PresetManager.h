#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace synth {

struct PresetInfo {
    juce::String name;
    juce::String category;
};

class PresetManager {
public:
    static juce::StringArray getPresetNames();
    static juce::Array<PresetInfo> getPresetList();
    static juce::StringArray getCategories();
    static bool loadPreset(int index, juce::AudioProcessorGraph& graph);
    static bool loadDefaultPreset(juce::AudioProcessorGraph& graph);

private:
    static juce::String getPresetJSON(int index);
};

} // namespace synth
