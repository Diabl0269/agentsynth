#pragma once

#include <cmath>
#include <cstddef>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::ui {

// Which of a lane's point handles are drawn. Handles whose neighbours sit closer than `minSpacingPx` along the time
// axis form a cluster (horizontal distance, so a square's alternating top/bottom points count as crowded too). A
// cluster keeps its first and last handle, so a pair a tick apart shows both dots, and thins the ones between, so a
// dense run (a stamped shape at a fine grid) reads as a curve; zooming in spreads the points and brings the rest
// back. `screen` is in beat order. Pure; the editor still hit-tests every point.
inline std::vector<bool> visibleHandleMask(const std::vector<juce::Point<float>>& screen, float minSpacingPx) {
    std::vector<bool> visible(screen.size(), true);
    std::size_t i = 0;
    while (i < screen.size()) {
        std::size_t last = i;
        while (last + 1 < screen.size() && std::abs(screen[last + 1].x - screen[last].x) < minSpacingPx)
            ++last;
        for (std::size_t k = i + 1; k < last; ++k)
            visible[k] = false;
        i = last + 1;
    }
    return visible;
}

} // namespace synth::ui
