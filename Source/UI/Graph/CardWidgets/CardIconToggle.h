#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** A small icon-only toggle a card lays over a picture's corner (the Filter's Show Response and Show Spectrum): a
 *  rounded chip that lights in the accent while on, one ActionGlyphs painter, a Tab stop with the shared focus ring.
 *  It has no text, so its title names the action for a screen reader and the tooltip starts with that name
 *  (ButtonTooltip.h). */
class CardIconToggle : public juce::ToggleButton {
public:
    enum class Glyph { response, spectrum };
    static constexpr int kSize = 18; ///< The chip's side, in card pixels.

    /** `name` is the action ("Show Response"), `explanation` the tooltip's words after it. */
    CardIconToggle(Glyph glyph, const juce::String& name, const juce::String& explanation);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Glyph glyph_;
};

} // namespace synth::ui
