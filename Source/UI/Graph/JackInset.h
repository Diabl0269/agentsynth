#pragma once

// JackInset.h (docs/layout/module-card.md, "Jacks"): how far in from a card's own edge a jack's dot centre sits, and
// where its label starts. One number for both sides, so an input jack on the left and an output jack on the right
// are exactly the same distance from their edge; every place that paints, hit-tests, wires or previews a jack reads
// it from here (ModuleComponent::getPortCenter / getMidiPortCenter / paint, the hosted-outputs strip, the macro
// card's add-port hint and the drag ghost's estimatePortCenter), so the two sides cannot drift apart.

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** A jack dot's centre, in from the card edge it sits on (left edge for an input, right edge for an output). */
inline constexpr int kJackInset = 10;

/** The dot centre's x for an input (left edge) or an output (right edge) jack of a card `width` wide. */
constexpr int jackCentreX(int width, bool isInput) noexcept { return isInput ? kJackInset : width - kJackInset; }

/** The gap between a jack dot's edge (radius 5) and its label, the same on both sides. */
inline constexpr int kJackLabelGap = 5;

/** A label's width and height beside a jack (audio jacks; text is vertically centred on the dot's row). */
inline constexpr int kJackLabelWidth = 60;
inline constexpr int kJackLabelHeight = 20;

/** Where an audio jack's label is drawn: to the right of an input dot, to the left of an output dot. */
inline juce::Rectangle<int> jackLabelBounds(juce::Point<int> centre, bool isInput) noexcept {
    constexpr int kDotRadius = 5;
    const int x =
        isInput ? centre.x + kDotRadius + kJackLabelGap : centre.x - kDotRadius - kJackLabelGap - kJackLabelWidth;
    return {x, centre.y - kJackLabelHeight / 2, kJackLabelWidth, kJackLabelHeight};
}

} // namespace synth::ui
