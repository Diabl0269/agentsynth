#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** True when a lane's range runs from below 0 to above it, so the lane gets a centre line at 0. */
bool isBipolarRange(float minValue, float maxValue) noexcept;

/**
 * Paints a bipolar lane's guide over `lane`'s backdrop: a dashed, muted line at value 0 (`zeroY`, in the
 * component's own pixels), and for a -1..1 lane the scale labels "+100%", "0" and "-100%" at the left
 * edge. Does nothing for a lane that is not bipolar.
 */
void paintBipolarGuide(juce::Graphics& g, const juce::Component& lane, float minValue, float maxValue, float zeroY);

} // namespace synth::ui
