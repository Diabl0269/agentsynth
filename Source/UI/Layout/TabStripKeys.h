#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

// The keys of a tab strip that is one keyboard stop (the bottom dock's strip, the Settings window's
// strip): plain Left / Right step to the neighbouring tab and stop at the ends (nothing wraps), Home
// and End jump to the first and last tab. `current` is the open tab's position among `count` tabs.
// Answers the position to open, which may equal `current` at an end (the key is still the strip's,
// so the caller consumes it), or nullopt for any other key, for any key with a modifier held, and
// when there are no tabs. Pure: it never touches focus or the tabs themselves.
std::optional<int> tabStripKeyTarget(const juce::KeyPress& key, int current, int count);

} // namespace synth::ui
