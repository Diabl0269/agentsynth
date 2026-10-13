#pragma once

// CardOutline.h (docs/layout/module-card.md): where a module card's body is drawn inside the component's bounds, so
// every outline that wraps a card (the drag-landing ghost, the ring an undo draws around a card it brought back, the
// macro-crossing flash, the placeholder of a card still loading) follows the card's own corners instead of a radius of
// its own. AppLookAndFeel::drawModulePanel draws the body this far in, with the theme's corner radius.

#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/** How far in from the component's bounds the card's body (and its border) is drawn. */
inline constexpr float kCardBodyInset = 2.0f;

/** The card body inside a card component's `bounds`: the rectangle an outline of the card is drawn on. */
inline juce::Rectangle<float> cardBodyRect(juce::Rectangle<float> bounds) noexcept {
    return bounds.reduced(kCardBodyInset);
}

} // namespace synth::ui
