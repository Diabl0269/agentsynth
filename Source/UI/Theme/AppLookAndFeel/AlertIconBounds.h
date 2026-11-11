#pragma once

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** Side of the square an alert box's icon is drawn in. */
constexpr int kAlertIconSize = 22;

/** Where an alert box draws its icon: kAlertIconSize square, in the left column JUCE keeps clear of the
 *  message (a fixed gap before it), top-aligned with the title, never outside `windowBounds`. */
juce::Rectangle<int> alertIconBounds(const juce::Rectangle<int>& windowBounds);

} // namespace synth::ui
