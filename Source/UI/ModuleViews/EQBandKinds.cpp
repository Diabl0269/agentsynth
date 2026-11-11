// EQBandKinds.cpp -- band type -> kind -> colour for the Parametric EQ surfaces (see EQBandKinds.h).
#include "EQBandKinds.h"

#include "UI/Theme/Theme.h"

namespace synth::ui {

using synth::eq::BandType;

EqBandKind eqBandKindOf(BandType type) noexcept {
    switch (type) {
    case BandType::Peak:
        return EqBandKind::Bell;
    case BandType::LowShelf:
    case BandType::HighShelf:
        return EqBandKind::Shelf;
    case BandType::LowCut:
    case BandType::HighCut:
        return EqBandKind::Cut;
    case BandType::Notch:
        return EqBandKind::Notch;
    case BandType::Tilt:
        return EqBandKind::Tilt;
    case BandType::BandPass:
        return EqBandKind::BandPass;
    }
    return EqBandKind::Bell;
}

juce::Colour eqBandKindColour(const synth::theme::Colors& colors, EqBandKind kind) noexcept {
    switch (kind) {
    case EqBandKind::Bell:
        return colors.eqKindBell;
    case EqBandKind::Shelf:
        return colors.eqKindShelf;
    case EqBandKind::Cut:
        return colors.eqKindCut;
    case EqBandKind::Notch:
        return colors.eqKindNotch;
    case EqBandKind::Tilt:
        return colors.eqKindTilt;
    case EqBandKind::BandPass:
        return colors.eqKindBandPass;
    }
    return colors.eqKindBell;
}

juce::String eqBandTypeName(BandType type) {
    switch (type) {
    case BandType::LowShelf:
        return "Low Shelf";
    case BandType::Peak:
        return "Bell";
    case BandType::HighShelf:
        return "High Shelf";
    case BandType::LowCut:
        return "Low Cut";
    case BandType::HighCut:
        return "High Cut";
    case BandType::Notch:
        return "Notch";
    case BandType::BandPass:
        return "Band Pass";
    case BandType::Tilt:
        return "Tilt";
    }
    return "Bell";
}

juce::String eqSlopeText(int slopeDbPerOct) { return juce::String(slopeDbPerOct) + " dB/oct"; }

// Every kind colour is a light hue, so one dark ink reads on all of them in every theme.
juce::Colour eqBandKindInk(const synth::theme::Colors& colors) noexcept { return colors.iconInk; }

} // namespace synth::ui
