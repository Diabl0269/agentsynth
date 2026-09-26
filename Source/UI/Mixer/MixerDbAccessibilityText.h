#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// MixerDbAccessibilityText.h -- FRO301: what VoiceOver reads for a dB-scale slider's current
// value, e.g. "-3.0 dB" (spoken "minus 3 dB"). Shared by MixerFader (its own readout_ label uses
// the same formatting) and MixerSendList's send-level knobs, both of which must reapply this
// AFTER constructing their juce::SliderParameterAttachment -- its constructor unconditionally
// overwrites slider.textFromValueFunction with one built from the param's own getText(), which
// has no " dB" suffix.

namespace synth::ui {

inline void applyDbAccessibilityText(juce::Slider& slider) {
    slider.textFromValueFunction = [](double db) { return juce::String(db, 1) + " dB"; };
}

} // namespace synth::ui
