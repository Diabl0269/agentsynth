#pragma once

// SectionDropHintLook.h: where the Sections strip's drop words sit and what they are painted in, free of any
// component, so the strip's paint and the guard test that enumerates every drag read the same rect and colours.

#include "UI/Theme/ReadableText.h"
#include "UI/Theme/Theme.h"
#include <algorithm>
#include <juce_graphics/juce_graphics.h>

namespace synth::ui::sectionstrip {

constexpr float kHintPillH = 16.0f;
constexpr float kHintGap = 6.0f; // between the strip's edge and the pill

/** The pill's rect in strip coordinates: centred on the drop line at `lineX`, kept inside the strip's width, in the
 *  band just below the strip, or just above it when `roomBelow` is false. Never inside the strip's own row. */
inline juce::Rectangle<float> dropHintPillRect(float lineX, float pillWidth, float stripWidth, int stripHeight,
                                               bool roomBelow) noexcept {
    const float w = std::min(pillWidth, stripWidth);
    const float left = std::clamp(lineX - w * 0.5f, 0.0f, std::max(0.0f, stripWidth - w));
    const float y = roomBelow ? (float)stripHeight + kHintGap : -kHintGap - kHintPillH;
    return {left, y, w, kHintPillH};
}

struct DropHintColours {
    juce::Colour fill; // the pill's body
    juce::Colour rim;  // its 1 px outline
    juce::Colour text; // the words
};

/** The pill in the theme's inverse: the primary text colour as its fill (light on a dark theme, dark on Daylight),
 *  the page background as its words and the accent as its rim, so it never reads as one more section block. */
inline DropHintColours dropHintColours(const synth::theme::Colors& colors) {
    const auto fill = colors.textPrimary;
    return {fill, colors.accent, synth::theme::readableTextOn(fill, colors.bg0)};
}

} // namespace synth::ui::sectionstrip
