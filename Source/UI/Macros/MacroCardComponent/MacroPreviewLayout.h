#pragma once

#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <vector>

// MacroPreviewLayout.h: where a collapsed macro card draws each member module's preview box. A pure function, so the
// card's paint and the fold animation (which flies each module onto its box) share the one layout and a module lands
// exactly on the box the card then draws.
namespace macro_preview {

/** One box per entry of `memberBounds` (the modules' canvas rects): their union scaled uniformly to fit
 *  `previewArea` and centred in it, in the same coordinate space as `previewArea`. Empty when no member has area. */
inline std::vector<juce::Rectangle<float>> boxes(const std::vector<juce::Rectangle<int>>& memberBounds,
                                                 juce::Rectangle<int> previewArea) {
    std::vector<juce::Rectangle<float>> out;
    juce::Rectangle<int> unionBounds;
    for (const auto& r : memberBounds)
        unionBounds = unionBounds.isEmpty() ? r : unionBounds.getUnion(r);
    if (unionBounds.isEmpty() || previewArea.isEmpty())
        return out;

    const float scale = std::min(previewArea.getWidth() / static_cast<float>(unionBounds.getWidth()),
                                 previewArea.getHeight() / static_cast<float>(unionBounds.getHeight()));
    const float scaledW = static_cast<float>(unionBounds.getWidth()) * scale;
    const float scaledH = static_cast<float>(unionBounds.getHeight()) * scale;
    const float offsetX =
        static_cast<float>(previewArea.getX()) + (static_cast<float>(previewArea.getWidth()) - scaledW) * 0.5f;
    const float offsetY =
        static_cast<float>(previewArea.getY()) + (static_cast<float>(previewArea.getHeight()) - scaledH) * 0.5f;
    for (const auto& r : memberBounds)
        out.emplace_back(static_cast<float>(r.getX() - unionBounds.getX()) * scale + offsetX,
                         static_cast<float>(r.getY() - unionBounds.getY()) * scale + offsetY,
                         std::max(2.0f, static_cast<float>(r.getWidth()) * scale),
                         std::max(2.0f, static_cast<float>(r.getHeight()) * scale));
    return out;
}

} // namespace macro_preview
