#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth::ui {

/** The short name a mod cable shows at its end: a plugin parameter's own name, without the plugin's. Empty for a cable
 *  on anything else, whose knob already carries the name on a card the person knows. */
juce::String cableEndName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID dest, int channel);

/** Draws `name` in a small pill just before the cable's landing dot, clear of the knob, at `alpha` (a dimmed cable's
 * name dims with it). */
void paintCableEndName(juce::Graphics& g, juce::Point<float> dot, const juce::String& name,
                       const synth::theme::Theme& theme, float alpha);

} // namespace synth::ui
