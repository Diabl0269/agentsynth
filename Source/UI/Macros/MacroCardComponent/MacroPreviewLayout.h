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

/** A nested macro's preview box: its own colour, with the fold mark of a closed macro (a small right-pointing chevron,
 *  the card's own closed fold arrow) in white, centred. White, not a theme token: the fill is the user's macro colour,
 *  and only a fixed contrast colour reads on every one (the open border's chip and collapse button do the same). The
 *  mark is at most 6 px tall and is left out of a box too small to hold it. Drawn by the card and by the fold, so a
 *  nested macro lands on exactly what the card then draws. */
inline void paintMacroBox(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour, float alpha = 1.0f) {
    g.setColour(colour.withAlpha(0.85f * alpha));
    g.fillRoundedRectangle(box, 1.5f);
    const float size = std::min(6.0f, std::min(box.getWidth(), box.getHeight()) - 4.0f);
    if (size < 3.0f)
        return;
    const auto mark = juce::Rectangle<float>(size * 0.5f, size).withCentre(box.getCentre());
    juce::Path chevron;
    chevron.startNewSubPath(mark.getX(), mark.getY());
    chevron.lineTo(mark.getRight(), mark.getCentreY());
    chevron.lineTo(mark.getX(), mark.getBottom());
    g.setColour(juce::Colours::white.withAlpha(0.9f * alpha));
    g.strokePath(chevron, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace macro_preview
