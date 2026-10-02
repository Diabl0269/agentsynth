#pragma once

#include <cstddef>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::ui {

// Which of a lane's point handles are drawn. A handle whose on-screen neighbour on either side sits
// closer than `minSpacingPx` is left out, so a dense run (a stamped shape at a fine grid) reads as a
// curve instead of a blob of overlapping circles; zooming in spreads the points and brings their
// handles back. `screen` is in beat order. Pure; the editor still hit-tests every point.
inline std::vector<bool> visibleHandleMask(const std::vector<juce::Point<float>>& screen, float minSpacingPx) {
    std::vector<bool> visible(screen.size(), true);
    for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
        if (screen[i].getDistanceFrom(screen[i + 1]) < minSpacingPx) {
            visible[i] = false;
            visible[i + 1] = false;
        }
    }
    return visible;
}

} // namespace synth::ui
