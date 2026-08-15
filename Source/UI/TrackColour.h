#pragma once

#include <array>
#include <juce_graphics/juce_graphics.h>

// Track colour resolution (TL5-3) — the single source of truth for what colour a timeline track's
// header, and later its clips, are drawn in.
//
// Everything here is a pure function over (stored colour, track index, muted): no GUI state, no
// LookAndFeel, no ApplicationProperties. Deliberately much smaller than CableColour.h's
// machinery: a cable has no place to store a user's choice, so that file needs a persisted
// override layer, whereas a track carries its own `colourArgb` inside the document. The palette
// here is only the DEFAULT a track that has never been coloured falls back to.
namespace synth::ui {

// Eight distinguishable hues, in the same family as the theme's accent / cable-category colours
// (Theme::Colors::cableCategory) so a coloured track never clashes with the canvas it sits under.
// Fixed rather than theme-derived on purpose: the colour a track resolves to is written into the
// document by the add-track flow, and a value that moved with the active theme would mean a
// project opened under another theme came back with different track colours.
inline constexpr int kTrackPaletteSize = 8;

inline const std::array<juce::uint32, kTrackPaletteSize>& trackPaletteArgb() noexcept {
    static const std::array<juce::uint32, kTrackPaletteSize> palette{
        0xff4FC1FF, // blue
        0xff7FD962, // green
        0xffFFB454, // amber
        0xffC792EA, // violet
        0xff56D4C0, // teal
        0xffFF7AB2, // pink
        0xffF07178, // salmon
        0xffA0A8B4  // slate
    };
    return palette;
}

// Palette entry for a track index. Wraps, and tolerates a negative index, so a caller can hand it
// a raw position in the track list without bounds-checking first.
inline juce::Colour trackPaletteColour(int trackIndex) noexcept {
    const auto& palette = trackPaletteArgb();
    const int wrapped = ((trackIndex % kTrackPaletteSize) + kTrackPaletteSize) % kTrackPaletteSize;
    return juce::Colour(palette[(size_t)wrapped]);
}

// The next palette entry after `currentArgb` — what a click on a header's colour swatch cycles to.
// A colour that is not in the palette (the doc's 0xff808080 default placeholder, or a value from a
// future colour picker) lands on the first entry, so one click always gets a track out of grey.
inline juce::uint32 nextTrackPaletteColour(juce::uint32 currentArgb) noexcept {
    const auto& palette = trackPaletteArgb();
    for (int i = 0; i < kTrackPaletteSize; ++i)
        if (palette[(size_t)i] == currentArgb)
            return palette[(size_t)((i + 1) % kTrackPaletteSize)];
    return palette[0];
}

// Treatment applied to a muted track's colour: desaturated and dimmed, never a different hue, so
// the track is still identifiable at a glance while reading as "off".
inline constexpr float kMutedTrackSaturation = 0.3f;
inline constexpr float kMutedTrackAlpha = 0.55f;

// THE resolver. `storedArgb` is Track::colourArgb straight out of the document: any non-zero value
// is the user's (or the add-track flow's) explicit choice and wins outright; zero means "never
// coloured" and falls back to the palette entry for `trackIndex`. A muted track comes back
// desaturated and dimmed.
inline juce::Colour resolveTrackColour(juce::uint32 storedArgb, int trackIndex, bool muted) noexcept {
    const juce::Colour base = storedArgb != 0 ? juce::Colour(storedArgb) : trackPaletteColour(trackIndex);
    if (!muted)
        return base;
    return base.withSaturation(base.getSaturation() * kMutedTrackSaturation).withMultipliedAlpha(kMutedTrackAlpha);
}

} // namespace synth::ui
