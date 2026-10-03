#pragma once

// The small "Save preset" window both layout editors (the list and the on-card one) ask a preset's name in.
// docs/layout/module-card-layout.md#editing-a-layout.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Opens a modal window with a name field and Save / Cancel; `onName` runs with the trimmed name when Save
 *  is chosen and the name is not empty. The window deletes itself. Message thread only. */
void promptForPresetName(std::function<void(const juce::String& name)> onName);

} // namespace synth::ui
