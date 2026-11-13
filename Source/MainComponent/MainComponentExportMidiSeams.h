#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace synth {

/** What the user picked when Export MIDI found a loop range. Cancel abandons the export. */
enum class MidiExportRange { WholeArrangement, LoopRange, Cancel };

/** Test/automation seams for Export MIDI's two prompts. When set, each REPLACES the real async
 *  juce::AlertWindow / juce::FileChooser (same idiom as MainComponent::patchLoadPrompt). */
struct MidiExportSeams {
    std::function<void(std::function<void(MidiExportRange)> onChoice)> rangePrompt;
    std::function<void(std::function<void(const juce::File&)> onFile)> filePrompt;
};

} // namespace synth
