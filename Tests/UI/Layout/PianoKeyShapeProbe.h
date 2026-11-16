#pragma once

// Pixel probe for the keyboard shape tests: how many rows, first to last, a colour covers in one image column.
#include <juce_graphics/juce_graphics.h>

namespace pianokeyprobe {

struct Extent {
    int first = -1, last = -1;
    int height() const { return first < 0 ? 0 : last - first + 1; }
};

inline bool near(juce::Colour a, juce::Colour b) {
    const auto d = [](juce::uint8 x, juce::uint8 y) { return std::abs((int)x - (int)y); };
    return d(a.getRed(), b.getRed()) <= 2 && d(a.getGreen(), b.getGreen()) <= 2 && d(a.getBlue(), b.getBlue()) <= 2;
}

// The first unbroken run of `colour` down the column (a key starts where its first row is, whatever is below it).
inline Extent extentOf(const juce::Image& image, int x, juce::Colour colour) {
    Extent e;
    for (int y = 0; y < image.getHeight(); ++y) {
        const bool hit = near(image.getPixelAt(x, y), colour);
        if (hit && e.first < 0)
            e.first = y;
        if (hit && e.first >= 0)
            e.last = y;
        if (!hit && e.first >= 0)
            break;
    }
    return e;
}

// The unbroken run of whatever colour the pixel at (x, y) has, up and down from y: a key read from its own pixels.
inline Extent runAround(const juce::Image& image, int x, int y) {
    const auto colour = image.getPixelAt(x, y);
    Extent e{y, y};
    while (e.first > 0 && near(image.getPixelAt(x, e.first - 1), colour))
        --e.first;
    while (e.last + 1 < image.getHeight() && near(image.getPixelAt(x, e.last + 1), colour))
        ++e.last;
    return e;
}

} // namespace pianokeyprobe
