#pragma once

// The 14 px line icon at the start of a Sounds-view row. One place holds the artwork (the 16 x 16 paths of the approved
// design) and the stroke, so every row kind draws from the same set.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

enum class SoundRowIcon { Folder, OneShot, Loop, Instrument, Pack, Favourites, Recent, Shapes, Plus };

/** The icon's strokes as one path on a 16 x 16 grid. */
juce::Path soundRowIconPath(SoundRowIcon icon);

/** Strokes `icon` scaled into `cell` (1.6 px on the 16 grid, rounded caps and joins) in `colour`. */
void paintSoundRowIcon(juce::Graphics& g, SoundRowIcon icon, juce::Rectangle<float> cell, juce::Colour colour);

} // namespace synth::ui
