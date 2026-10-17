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
    SidePane,     // a window with a divider after its left strip: a side pane that is closed
    SidePaneOpen, // the same window with the left strip filled: a side pane that is open
    // One period of an oscillator's waveform, for a modulator row's shape icon.
    ShapeSine,
    ShapeTriangle,
    ShapeSaw,
    ShapeSquare,
    ShapeRandom, // sample and hold: a staircase of uneven steps
    ShapeCustom, // a drawn wave: a curve with its points marked
    // The markers before a timeline lane header's name, drawn on a 14 px grid at any size.
    LaneAutomation, // a rising and falling line with two points on it: changes drawn over time
    LaneMod,        // one wave: a modulator moves the parameter
    Swap,           // two opposite arrows: two views trade places
    Detach          // a window with an arrow leaving its corner: a view opens in its own window
};

// Fills or strokes `glyph` in `colour`, centred in the shorter side of `area`. Nothing else is drawn.
void paintGlyph(juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area, juce::Colour colour);

} // namespace synth::theme
