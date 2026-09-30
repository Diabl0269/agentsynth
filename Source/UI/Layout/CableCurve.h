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

// Shortest tangent handle, so a near-vertical cable still leaves and enters horizontally instead of
// collapsing onto a vertical line. Capped at half the vertical gap, so a short hop (a macro's interior jack
// to a member a few px away) keeps a gentle S instead of overshooting into a kink.
inline constexpr float kCableMinHandle = 50.0f;
// Longest handle for a backward cable. The handle is the full backward run up to this, which keeps the loop
// out of the output (and into the input) clearly visible; beyond it the loop stays bounded.
inline constexpr float kCableMaxBackHandle = 300.0f;

inline juce::Path makeCablePath(juce::Point<float> p1, juce::Point<float> p2) {
    const float dx = p2.x - p1.x;
    // Forward: half the run (the original curve, exact for dx >= 2 * kCableMinHandle).
    const float minHandle = std::min(kCableMinHandle, std::abs(p2.y - p1.y) * 0.5f);
    const float handle =
        dx >= 0.0f ? std::max(dx * 0.5f, minHandle) : juce::jlimit(kCableMinHandle, kCableMaxBackHandle, -dx);
    juce::Path wp;
    wp.startNewSubPath(p1);
    wp.cubicTo(p1.x + handle, p1.y, p2.x - handle, p2.y, p2.x, p2.y);
    return wp;
}

} // namespace synth::ui
