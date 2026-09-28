#pragma once

#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerPanAccessibilityText.h: what VoiceOver reads for a pan-law slider's
// current value -- "Center"/"50% left"/"50% right". Shared by MixerColumnComponent's own strip
// pan knob and MixerSendList's per-send pan knobs, both of which must reapply this AFTER
// constructing their juce::SliderParameterAttachment -- its constructor unconditionally overwrites
// slider.textFromValueFunction with one built from the param's own getText(), which has no
// "% left"/"% right" phrasing (see MixerDbAccessibilityText.h's own comment for the identical fix
// on the dB-scale knobs).

namespace synth::ui {

inline void applyPanAccessibilityText(juce::Slider& slider) {
    slider.textFromValueFunction = [](double pan) {
        if (std::abs(pan) < 0.005)
            return juce::String("Center");
        const int percent = (int)std::round(std::abs(pan) * 100.0);
        return juce::String(percent) + (pan < 0.0 ? "% left" : "% right");
    };
}

} // namespace synth::ui
