#pragma once

// SectionStripGeometry.h: the Sections strip's pure maths, free of any component: block rects, the bar-snapped span
// a drag makes, the words a screen reader and a tooltip use. The strip's paint, hit-test and tests all read it, so a
// drawn block, a clickable block and an asserted one are the same rect.

#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <vector>

namespace synth::ui::sectionstrip {

constexpr float kBlockInsetY = 3.0f;   // above and below every block
constexpr float kMinBlockWidth = 1.0f; // a block never vanishes, however far out the zoom
constexpr float kBlockPadX = 6.0f;     // name inset from the block's left edge
constexpr float kCountReserve = 34.0f; // room kept for the "2/3" count when it is drawn
constexpr float kDragStartPx = 3.0f;   // a press that moves less than this is a click
constexpr double kBarEpsilon = 1e-9;
constexpr float kChipMinWidth = 16.0f;    // the variation chip's width: the letter and a little air
constexpr float kChipInsetRight = 4.0f;   // gap between the chip and the block's right edge
constexpr float kChipVerticalTrim = 6.0f; // the chip is this much shorter than its block
constexpr float kChipNameRoom = 24.0f;    // the name keeps at least this much width, or the chip is not drawn

struct Span {
    double start = 0.0;
    double end = 0.0;
    double length() const noexcept { return end - start; }
};

/** A block from its two edge x positions: inset kBlockInsetY top and bottom, never narrower than kMinBlockWidth. */
inline juce::Rectangle<float> blockRect(float xStart, float xEnd, int stripHeight) noexcept {
    const float height = std::max(0.0f, (float)stripHeight - 2.0f * kBlockInsetY);
    return {xStart, kBlockInsetY, std::max(kMinBlockWidth, xEnd - xStart), height};
}

/** The variation chip's rect inside a block: a pill at the right end, 4 px in, 6 px shorter than the block. Empty when
 *  the block would leave under kChipNameRoom for its name beside the chip (the block menu still reaches every action).
 */
inline juce::Rectangle<float> chipBounds(juce::Rectangle<float> block) noexcept {
    const float taken = kChipMinWidth + kChipInsetRight;
    if (block.getWidth() - kBlockPadX - taken < kChipNameRoom || block.getHeight() <= kChipVerticalTrim)
        return {};
    return {block.getRight() - taken, block.getY() + kChipVerticalTrim * 0.5f, kChipMinWidth,
            block.getHeight() - kChipVerticalTrim};
}

inline double floorToBar(double beat, double beatsPerBar) noexcept {
    return std::floor(std::max(0.0, beat) / beatsPerBar + kBarEpsilon) * beatsPerBar;
}

inline double ceilToBar(double beat, double beatsPerBar) noexcept {
    return std::ceil(std::max(0.0, beat) / beatsPerBar - kBarEpsilon) * beatsPerBar;
}

/** The whole bars a drag from `anchorBeat` to `currentBeat` covers (the bars holding either end, at least one), held
 *  inside the gap between the blocks around the anchor so the drag stops at a neighbour instead of covering it.
 *  `blocks` are the existing placements' spans in any order. */
inline Span makeDragSpan(double anchorBeat, double currentBeat, double beatsPerBar, const std::vector<Span>& blocks) {
    const double lo = std::min(anchorBeat, currentBeat);
    const double hi = std::max(anchorBeat, currentBeat);
    Span span{floorToBar(lo, beatsPerBar), ceilToBar(hi, beatsPerBar)};
    if (span.end - span.start < beatsPerBar - kBarEpsilon)
        span.end = span.start + beatsPerBar;
    double leftLimit = 0.0;
    double rightLimit = 1.0e12;
    for (const auto& block : blocks) {
        if (block.end <= anchorBeat + kBarEpsilon)
            leftLimit = std::max(leftLimit, block.end);
        else if (block.start >= anchorBeat - kBarEpsilon)
            rightLimit = std::min(rightLimit, block.start);
    }
    span.start = std::max(span.start, leftLimit);
    span.end = std::min(span.end, rightLimit);
    return span;
}

constexpr double kBoundarySnapPx = 12.0; // a dragged block's edge this close to another block's edge snaps to it

inline double nearestBar(double beat, double beatsPerBar) noexcept {
    return std::max(0.0, std::round(beat / beatsPerBar) * beatsPerBar);
}

/** Where a dragged block of `lengthBeats` lands when its left edge is over `rawStart`. Its start or end snaps to a
 *  neighbour's edge (the closest of every start and end in `others`) when within kBoundarySnapPx, else to the nearest
 *  whole bar; a result strictly inside one of `others` goes to that block's nearer edge (the later one on a tie). */
inline double dropBeat(double rawStart, double lengthBeats, double beatsPerBar, double pixelsPerBeat,
                       const std::vector<Span>& others) {
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const double snapBeats = pixelsPerBeat > 0.0 ? kBoundarySnapPx / pixelsPerBeat : 0.0;
    double best = nearestBar(rawStart, bpb);
    double bestDistance = snapBeats + 1.0;
    for (const auto& other : others) {
        const double candidates[] = {other.start, other.end, other.start - lengthBeats, other.end - lengthBeats};
        for (const double candidate : candidates) {
            const double distance = std::abs(candidate - rawStart);
            if (candidate >= 0.0 && distance <= snapBeats && distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }
    }
    for (const auto& other : others) {
        if (best > other.start + kBarEpsilon && best < other.end - kBarEpsilon)
            return (best - other.start < other.end - best) ? other.start : other.end;
    }
    return best;
}

/** Whether a span is long enough to become a section: one whole bar or more. */
inline bool spansAtLeastOneBar(const Span& span, double beatsPerBar) noexcept {
    return span.length() >= beatsPerBar - kBarEpsilon;
}

/** "bars 13 to 20": 1-based, the end bar included. */
inline juce::String barsPhrase(double startBeat, double lengthBeats, double beatsPerBar) {
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const int first = (int)std::floor(startBeat / bpb + kBarEpsilon) + 1;
    const int last = std::max(first, (int)std::ceil((startBeat + lengthBeats) / bpb - kBarEpsilon));
    return last == first ? "bar " + juce::String(first) : "bars " + juce::String(first) + " to " + juce::String(last);
}

/** "8 bars", "1 bar", "2.5 bars". */
inline juce::String lengthPhrase(double lengthBeats, double beatsPerBar) {
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const double bars = lengthBeats / bpb;
    if (std::abs(bars - std::round(bars)) < 1e-6) {
        const int whole = (int)std::llround(bars);
        return juce::String(whole) + (whole == 1 ? " bar" : " bars");
    }
    return juce::String(bars, 1) + " bars";
}

/** "plays once", "plays 3 times". */
inline juce::String playsPhrase(int count) {
    return count <= 1 ? juce::String("plays once") : "plays " + juce::String(count) + " times";
}

/** The screen-reader title of one block: "Chorus, bars 13 to 20, 1 of 3". */
inline juce::String blockTitle(const juce::String& name, double startBeat, double lengthBeats, double beatsPerBar,
                               int ordinal, int count, const juce::String& variation = {}) {
    return name + ", " + barsPhrase(startBeat, lengthBeats, beatsPerBar) + ", " + juce::String(ordinal) + " of " +
           juce::String(count) + (variation.isEmpty() ? juce::String() : ", variation " + variation);
}

/** The tooltip over one block; `variation` (a letter) is named only when the section has more than one. */
inline juce::String blockTooltip(const juce::String& name, double lengthBeats, double beatsPerBar, int count,
                                 const juce::String& commandKey, const juce::String& variation = {}) {
    return name + (variation.isEmpty() ? juce::String() : " - variation " + variation) +
           juce::String::fromUTF8(" \xC2\xB7 ") + lengthPhrase(lengthBeats, beatsPerBar) +
           juce::String::fromUTF8(" \xC2\xB7 ") + playsPhrase(count) + ". Drag to move, Option-drag to place again (" +
           commandKey + " + D)";
}

/** The tooltip over a block's variation chip. */
inline juce::String chipTooltip(const juce::String& letter, const juce::String& sectionName) {
    return "Variation " + letter + " of " + sectionName + ". Click to switch, add or play it everywhere.";
}

/** The screen-reader description of one block: its tooltip and the keys that reach its menus. */
inline juce::String blockDescription(const juce::String& tooltip) {
    return tooltip + ". Shift+F10: menu. V: variations.";
}

/** The name for a section made unique from `name`: its base (a trailing " 2", " 3"... dropped) with the first number
 *  from 2 up that no name in `taken` already uses. */
inline juce::String uniqueSectionName(const juce::String& name, const juce::StringArray& taken) {
    juce::String base = name.trim();
    const int space = base.lastIndexOfChar(' ');
    if (space > 0 && base.substring(space + 1).containsOnly("0123456789") && base.substring(space + 1).isNotEmpty())
        base = base.substring(0, space).trimEnd();
    for (int n = 2;; ++n) {
        const auto candidate = base + " " + juce::String(n);
        if (!taken.contains(candidate))
            return candidate;
    }
}

} // namespace synth::ui::sectionstrip
