#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth {

/** The key press macOS delivers to the app but JUCE's NSView peer silently drops.
 *
 *  Why a key is dropped: the peer (juce_NSViewComponentPeer_mac.mm, handleKeyEvent) walks the
 *  characters of `[ev characters]` and calls handleKeyPress once per character. Control turns some
 *  chords into the NUL control code: Ctrl+Shift+2 (Ctrl+@) and Ctrl+Space both report `characters`
 *  as "\0", which JUCE's string conversion reads as an empty string, so the loop runs zero times and
 *  no KeyPress is ever built. `charactersIgnoringModifiers` is still there ('@' for Ctrl+Shift+2),
 *  so the key can be rebuilt exactly as the peer would have: key code = that character upper-cased,
 *  the event's modifiers, no text character.
 *
 *  firstCharacter is the first UTF-16 unit of `[ev characters]` (0 for NUL), firstUnmodified the
 *  first of `[ev charactersIgnoringModifiers]`. Returns nothing when the peer would have handled the
 *  event itself (so a key is never delivered twice). */
std::optional<juce::KeyPress> recoverKeyPressDroppedByMacPeer(int firstCharacter, int firstUnmodified,
                                                              juce::ModifierKeys modifiers);

} // namespace synth
