#pragma once

#include <array>
#include <cstddef>
#include <juce_core/juce_core.h>

// The fixed palette a new section takes its colour from, in order, wrapping after the eighth. Defined once so
// the strip's drag-to-make, Make unique, "Make sections from markers" and the AI's makeSection op all cycle
// through the same hues.
namespace synth {

inline constexpr std::array<juce::uint32, 8> kSectionColours{0xffA0A8B4, 0xff56D4C0, 0xff4FC1FF, 0xffC792EA,
                                                             0xffFFB454, 0xff7FD962, 0xffFF8A80, 0xffF2D45C};

/** The palette colour for the section that would be number `index` (0-based; the count of sections so far). */
inline juce::uint32 sectionColourFor(int index) noexcept {
    const auto n = kSectionColours.size();
    return kSectionColours[(std::size_t)(((index % (int)n) + (int)n) % (int)n)];
}

} // namespace synth
