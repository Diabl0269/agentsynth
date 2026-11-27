#pragma once

#include <algorithm>
#include <cmath>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::theme {

// WCAG contrast ratio of two opaque colours (1 to 21).
inline double contrastBetween(juce::Colour a, juce::Colour b) {
    const auto luminance = [](juce::Colour c) {
        const auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) +
               0.0722 * channel(c.getFloatBlue());
    };
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The smallest contrast body text must reach (WCAG AA); the theme sweep test checks the same bar.
inline constexpr double kReadableTextContrast = 4.5;

// `preferred` when it already reads on `fill`; otherwise white or black, whichever reads better.
inline juce::Colour readableTextOn(juce::Colour fill, juce::Colour preferred) {
    if (contrastBetween(preferred, fill) >= kReadableTextContrast)
        return preferred;
    return contrastBetween(juce::Colours::white, fill) >= contrastBetween(juce::Colours::black, fill)
               ? juce::Colours::white
               : juce::Colours::black;
}

// `text` moved toward white or black (toward the side that reads better) until it reaches `minContrast` on every one
// of `fills`; unchanged when it already does. Keeps the hue, so a danger red stays red.
inline juce::Colour nudgeToContrast(juce::Colour text, const std::vector<juce::Colour>& fills, double minContrast) {
    const auto worst = [&](juce::Colour c) {
        double w = 1000.0;
        for (const auto f : fills)
            w = std::min(w, contrastBetween(c, f));
        return w;
    };
    if (worst(text) >= minContrast)
        return text;
    const auto towards =
        worst(juce::Colours::white) >= worst(juce::Colours::black) ? juce::Colours::white : juce::Colours::black;
    for (int step = 1; step <= 20; ++step) {
        const auto candidate = text.interpolatedWith(towards, (float)step / 20.0f);
        if (worst(candidate) >= minContrast + 0.1)
            return candidate;
    }
    return towards;
}

// The same at the readable-text contrast.
inline juce::Colour nudgeToReadable(juce::Colour text, const std::vector<juce::Colour>& fills) {
    return nudgeToContrast(text, fills, kReadableTextContrast);
}

} // namespace synth::theme
