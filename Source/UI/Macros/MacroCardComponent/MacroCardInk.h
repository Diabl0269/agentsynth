#pragma once

#include "UI/Theme/ReadableText.h"
#include "UI/Theme/Theme.h"
#include <juce_graphics/juce_graphics.h>

// MacroCardInk.h: what a collapsed macro card draws in the user's colour. The body is the picked colour at full
// strength (what the picker's swatch shows), so every ink on it (title, count, port names, preview boxes, fold arrow)
// is black or white, whichever reads better on that colour. One place, so the card and the fold animation agree.
namespace macro_card {

/** The card body: the picked colour, opaque. */
inline juce::Colour bodyFill(juce::Colour picked) { return picked.withAlpha(1.0f); }

/** Black or white, whichever has the higher contrast on `body`. */
inline juce::Colour inkOn(juce::Colour body) {
    const auto opaque = bodyFill(body);
    return synth::theme::contrastBetween(juce::Colours::black, opaque) >=
                   synth::theme::contrastBetween(juce::Colours::white, opaque)
               ? juce::Colours::black
               : juce::Colours::white;
}

/** A theme whose text tokens are the card's ink and whose strip tint is the opposite of it, so the port names and
 *  '+'/'-' glyphs read on the strips as well as on the body. */
inline synth::theme::Colors inkedColours(synth::theme::Colors base, juce::Colour ink) {
    base.textPrimary = ink;
    base.textMuted = ink.withAlpha(0.75f);
    base.surfaceHi = ink == juce::Colours::black ? juce::Colours::white : juce::Colours::black;
    return base;
}

} // namespace macro_card
