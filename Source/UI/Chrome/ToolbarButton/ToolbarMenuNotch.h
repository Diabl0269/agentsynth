#pragma once

#include "ToolbarButton.h"
#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

// Paints the small corner notch that marks a toolbar button as one whose click opens a menu: call it after
// ToolbarButton::paintButton from the button's own paintButton override.
void paintToolbarMenuNotch(juce::Graphics& g, const ToolbarButton& button);

} // namespace synth::ui
