// KeyedColourTween.h -- the colour flavour of KeyedTween (KeyedTween.h): a colour eases from the one drawn now to the
// one it now rests at, with the shared 160 ms easeOutCubic. Colours are tweened as linear 0..1 channels. An owner with
// colours takes the default whole-owner repaint (a colour has no box of its own).
#pragma once

#include "UI/Layout/KeyGlide/KeyedTween.h"

namespace synth::ui {

/** A colour as four 0..1 channels, so differences can be scaled toward zero like any other tweened value. */
struct TweenColour {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

    static TweenColour from(juce::Colour c) {
        return {c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), c.getFloatAlpha()};
    }
    juce::Colour toColour() const {
        return juce::Colour::fromFloatRGBA(juce::jlimit(0.0f, 1.0f, r), juce::jlimit(0.0f, 1.0f, g),
                                           juce::jlimit(0.0f, 1.0f, b), juce::jlimit(0.0f, 1.0f, a));
    }
    bool operator==(const TweenColour& o) const noexcept { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const TweenColour& o) const noexcept { return !(*this == o); }
};

template <>
struct TweenMath<TweenColour> {
    static TweenColour sub(TweenColour x, TweenColour y) { return {x.r - y.r, x.g - y.g, x.b - y.b, x.a - y.a}; }
    static TweenColour add(TweenColour x, TweenColour y) { return {x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a}; }
    static TweenColour scale(TweenColour x, float f) { return {x.r * f, x.g * f, x.b * f, x.a * f}; }
    static juce::Rectangle<float> bounds(TweenColour) { return {}; }
};

using KeyedColourTween = KeyedTween<TweenColour>;

extern template class KeyedTween<TweenColour>;

} // namespace synth::ui
