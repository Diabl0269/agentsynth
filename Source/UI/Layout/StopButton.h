#pragma once

#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The stop action of an AI card ("Cancel" on a song plan or a running build): an outlined button, transparent fill,
// a 1 px outline and a small stop square before the text, all in the theme's warning tone nudged to read on the card
// panel (never a filled red, so it reads as "stop", not "delete", and never outshouts the primary). Hover and press
// tint the fill; the focus ring is the shared one. Size it to getIdealWidth() and the card action row's 24 px height.
class StopButton : public juce::Button {
public:
    explicit StopButton(const juce::String& text);

    // Narrowest width that shows the glyph and the whole text with the side padding.
    int getIdealWidth() const;

    // The one colour of outline, glyph and text: the warning tone, moved toward white or black until it reads
    // (WCAG 4.5:1) on every panel surface a card can sit on. Static so a test can hold every theme to it.
    static juce::Colour toneFor(const synth::theme::Colors& colours);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
};

} // namespace synth::ui
