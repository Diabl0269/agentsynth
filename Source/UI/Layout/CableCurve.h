#pragma once

// CableCurve.h -- the one cubic bezier a cable is drawn and hit-tested along.
//
// p1 is ALWAYS the output (source) end and p2 ALWAYS the input (destination) end: the cable leaves
// p1 heading right and arrives at p2 heading right (entering from the left), the node-editor
// convention. Callers fix orientation at the call site; this function never inspects port types.

#include <algorithm>
#include <cmath>
#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

// Shortest tangent handle, so a near-vertical or backward cable still leaves and enters
// horizontally instead of collapsing onto a vertical line.
inline constexpr float kCableMinHandle = 50.0f;
// Longest handle for a backward cable: a visible loop that stays bounded however far back it runs.
inline constexpr float kCableMaxBackHandle = 150.0f;

inline juce::Path makeCablePath(juce::Point<float> p1, juce::Point<float> p2) {
    const float dx = p2.x - p1.x;
    // Forward: half the run (the original curve, exact for dx >= 2 * kCableMinHandle).
    const float handle = dx >= 0.0f ? std::max(dx * 0.5f, kCableMinHandle)
                                    : juce::jlimit(kCableMinHandle, kCableMaxBackHandle, -dx * 0.5f);
    juce::Path wp;
    wp.startNewSubPath(p1);
    wp.cubicTo(p1.x + handle, p1.y, p2.x - handle, p2.y, p2.x, p2.y);
    return wp;
}

} // namespace synth::ui
