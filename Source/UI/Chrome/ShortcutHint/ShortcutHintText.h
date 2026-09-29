#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::hint {

/** The text a key cap shows for `key`: the Mac symbols in Mac order (control, option, shift,
 *  command, then the key: "<shift><command>Z") when `macGlyphs`, else "Ctrl+Shift+Z". Empty for an
 *  invalid key. */
juce::String formatKeyCapText(const juce::KeyPress& key, bool macGlyphs);

/** `formatKeyCapText` for the platform this build runs on. */
juce::String formatKeyCapTextForPlatform(const juce::KeyPress& key);

} // namespace synth::ui::hint
