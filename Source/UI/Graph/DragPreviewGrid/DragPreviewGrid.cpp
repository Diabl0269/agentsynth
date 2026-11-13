#include "UI/Graph/DragPreviewGrid/DragPreviewGrid.h"

#include "UI/Layout/LayoutUtil.h"

namespace synth::ui {

namespace {

constexpr int kSpacing = synth::LayoutUtil::kGridSize * 5; // canvas px between dots
constexpr int kSupersample = 4;                            // tile px per canvas px: a zoomed-in dot stays round
constexpr float kDotRadius = 1.2f;                         // canvas px

} // namespace

// One tiled image fill instead of one ellipse per grid point. A zoomed-out view of a big patch shows several thousand
// canvas px each way, which at one dot every 40 px was tens of thousands of ellipses per frame, and a drag repaints the
// whole view every edge-scroll tick. The tile holds one dot at its centre; the fill's transform scales it to canvas px
// and shifts that centre onto the grid point, so the dots sit exactly where the per-point loop put them. The tile is
// 160 px square and rebuilt per paint: cheaper than keeping a cache in step with the theme colour.
void paintDragPreviewGrid(juce::Graphics& g, juce::Colour dotColour) {
    const auto clip = g.getClipBounds();
    if (clip.isEmpty())
        return;
    constexpr int tilePx = kSpacing * kSupersample;
    juce::Image tile(juce::Image::ARGB, tilePx, tilePx, true);
    {
        juce::Graphics tg(tile);
        tg.setColour(dotColour);
        constexpr float r = kDotRadius * kSupersample;
        tg.fillEllipse(tilePx * 0.5f - r, tilePx * 0.5f - r, 2.0f * r, 2.0f * r);
    }
    constexpr float half = kSpacing * 0.5f;
    g.saveState();
    g.setFillType(juce::FillType(tile, juce::AffineTransform::scale(1.0f / kSupersample).translated(-half, -half)));
    g.fillRect(clip);
    g.restoreState();
}

} // namespace synth::ui
