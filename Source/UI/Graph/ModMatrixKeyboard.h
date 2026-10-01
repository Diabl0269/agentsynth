#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Arrow-key movement inside the Mod Matrix, which is one focus region (Tab leaves it for the next
 *  region). Its controls form a grid read from the component tree by component id: the header line
 *  (Add Modulation, Flat Sources), then one line per routing row in on-screen order (source,
 *  destination, amount, bypass, delete).
 *
 *  - Left/Right step to the previous/next control in reading order, across lines, and stop at the
 *    ends.
 *  - Up/Down move to the same column on the line above/below, clamped to that line's length. The
 *    amount slider keeps Up/Down for itself (it nudges the amount, like a mixer fader).
 *  - From the matrix itself (where Tab lands), any arrow enters at the first control.
 *
 *  Returns the control to focus, or null when `key` is not an arrow or `from` is not in the matrix.
 *  At an end it returns `from` itself, so the key is still used and never falls through to the
 *  canvas behind the panel. */
juce::Component* modMatrixArrowTarget(juce::Component& matrix, juce::Component* from, const juce::KeyPress& key);

/** A stateless key listener that moves focus with modMatrixArrowTarget. The matrix attaches it to
 *  itself and to its row container (the row viewport would otherwise take Up/Down to scroll). */
juce::KeyListener& modMatrixArrowKeys();

// Component ids the grid is read from; the matrix gives its controls these ids.
namespace modmatrix_ids {
inline constexpr const char* kAdd = "addModulation";
inline constexpr const char* kFlat = "flatSources";
inline constexpr const char* kSource = "modSource";
inline constexpr const char* kDest = "modDest";
inline constexpr const char* kAmount = "modAmount";
inline constexpr const char* kBypass = "modBypass";
inline constexpr const char* kDelete = "modDelete";
} // namespace modmatrix_ids

} // namespace synth::ui
