// WaveformDraw.h
//
// Drawing of already-reduced waveform columns, shared by the committed clip waveform and the live-recording strip.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <utility>
#include <vector>

namespace synth::ui {

// Three washes: the peak outline, the darker RMS body inside it, and the single outline of data without RMS.
struct WaveformInk {
    juce::Colour outline, body, flat;
};

// Column i is at pixel bounds.x + firstColumn + i; `columns` are (min,max), `bodies` one RMS per column or empty.
void fillWaveformColumns(juce::Graphics& g, juce::Rectangle<int> bounds, int firstColumn,
                         const std::vector<std::pair<float, float>>& columns, const std::vector<float>& bodies,
                         const WaveformInk& ink);

} // namespace synth::ui
