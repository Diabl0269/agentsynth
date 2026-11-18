// PopoverPointer.h -- the pointer position a popover's click-away rules read, (PointerState.h's seam, which test mode
// feeds).
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** Where the pointer is for a popover (screen coordinates): the synthetic pointer when test mode set one, else the real
 * pointer. */
juce::Point<int> popoverPointerPosition();

} // namespace synth::ui
