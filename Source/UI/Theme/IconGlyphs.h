#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::theme {

// The glyphs drawn as plain paths instead of IconLibrary SVGs, so they render the same in the
// headless test build (where the embedded SVGs are null). They are the glyph set of
// synth::ui::IconButton; IconLibrary stays the home of every other icon.
enum class Glyph {
    Play,
    Stop,
    RecordIdle,
    RecordOn,
    Loop,
    ReturnToStart,
    Metronome,
    Close,
    Pin,
    PinOn,
    Delete,
    MenuDots,
    EyeOpen,
    EyeHidden,
    SidePane,    // a window with a divider after its left strip: a side pane that is closed
    SidePaneOpen // the same window with the left strip filled: a side pane that is open
};

// Fills or strokes `glyph` in `colour`, centred in the shorter side of `area`. Nothing else is drawn.
void paintGlyph(juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area, juce::Colour colour);

} // namespace synth::theme
