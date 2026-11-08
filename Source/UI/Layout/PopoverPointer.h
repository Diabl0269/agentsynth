// PopoverPointer.h -- the pointer position a popover's click-away rules read, as a seam test mode and tests can set.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace synth::ui::test_hooks {

/** Where the pointer is for the click-away rules (screen coordinates); empty = the real pointer. Test mode sets it on
 *  every synthetic mouse event, which never moves the OS cursor. */
std::optional<juce::Point<int>>& popoverMouseForTest();

} // namespace synth::ui::test_hooks
