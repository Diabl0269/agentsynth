#pragma once

#include "Modules/FX/ParametricEQ/ParametricEQBandTypes.h"
#include <juce_graphics/juce_graphics.h>

namespace synth::theme {
struct Colors;
}

namespace synth::ui {

/** What a band looks like on every Parametric EQ surface: its dot's ring and its chip take the colour of its kind. */
enum class EqBandKind { Bell, Shelf, Cut, Notch, Tilt, BandPass };

EqBandKind eqBandKindOf(synth::eq::BandType type) noexcept;

/** The kind's colour from the theme's fixed `eqKind*` tokens. */
juce::Colour eqBandKindColour(const synth::theme::Colors& colors, EqBandKind kind) noexcept;

/** The type as shown and spoken: "Bell", "Low Shelf", "High Cut", "Band Pass"... */
juce::String eqBandTypeName(synth::eq::BandType type);

/** A cut's slope as shown: "24 dB/oct". */
juce::String eqSlopeText(int slopeDbPerOct);

/** Text on a kind colour: the darker of the theme's icon ink and text colour (every kind colour is a light hue). */
juce::Colour eqBandKindInk(const synth::theme::Colors& colors) noexcept;

} // namespace synth::ui
