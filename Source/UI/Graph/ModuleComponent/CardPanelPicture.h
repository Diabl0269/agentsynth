#pragma once
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::theme {
class AppLookAndFeel;
}

namespace synth::ui {

/** The module card's panel (shadow, fill, header band, border; not the title) drawn from a picture shared by every
 *  card of the same size and state, so a card re-raster blits instead of re-running gradients and clip paths.
 *  Message thread only. */
class CardPanelPicture {
public:
    /** Draws the panel at the origin of `g` for a card of `size`; `g` must be the card's own context. */
    static void draw(juce::Graphics& g, synth::theme::AppLookAndFeel& lf, juce::Point<int> size, int headerHeight,
                     bool selected, bool bypassed);

    static int renderCountForTest() noexcept;
    static void resetForTest(); ///< Empties the cache and zeroes the render count.
    static void setEnabledForTest(bool enabled) noexcept;
    static void setSoftwareImagesForTest(bool software) noexcept; ///< Pixel tests need the software rasteriser.
};

} // namespace synth::ui
