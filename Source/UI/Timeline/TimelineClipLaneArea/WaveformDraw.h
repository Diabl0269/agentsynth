// WaveformDraw.h
//
// Drawing of already-reduced waveform columns, shared by the committed clip waveform and the live-recording strip.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <utility>
#include <vector>

namespace synth::ui {

// Three inks: the peak shape, the denser RMS core inside it, and the single shape of data without RMS.
struct WaveformInk {
    juce::Colour outline, body, flat;
};

// The smallest contrast between the peak shape and the clip it is drawn on.
inline constexpr float kWaveformMinContrast = 3.0f;
// The contrast the RMS core keeps against the peak shape, where the shade leaves room for it.
inline constexpr float kWaveformCoreStep = 1.25f;

// The inks for a waveform drawn on `clipOnLane`, the clip's colour as it shows (its fill over the lane background).
WaveformInk waveformInkFor(juce::Colour clipOnLane);

// Column i is at pixel bounds.x + firstColumn + i; `columns` are (min,max), `bodies` one RMS per column or empty.
void fillWaveformColumns(juce::Graphics& g, juce::Rectangle<int> bounds, int firstColumn,
                         const std::vector<std::pair<float, float>>& columns, const std::vector<float>& bodies,
                         const WaveformInk& ink);

} // namespace synth::ui
