// CanvasStaticLayer.cpp
//
// The picture is taken only after the viewport has been quiet for a few paints, and dropped by any repaint inside the
// viewport other than repaintOver(): a card, a cable, a border, the editor itself, a theme change. So it never needs to
// know what the layer depends on, only that someone asked for pixels to change. Interactive frames (a pan, a drag, a
// glide) keep painting the layer directly, as before: taking a picture of a frame that is about to change would only
// add work. A picture is aligned to the viewport's device pixels and drawn back with nearest-pixel sampling, so it is
// the same pixels a direct paint gives, never a resampled copy.

#include "CanvasStaticLayer.h"

#include "UI/Layout/ZoomFrozenCachedImage.h"

#include <cmath>

namespace synth::ui {

// The viewport's cached image does no caching: it paints the viewport as usual and reports every repaint inside it,
// which JUCE routes through invalidate() before asking the window for pixels.
class CanvasStaticLayer::RepaintTap : public juce::CachedComponentImage {
public:
    RepaintTap(juce::Component& owner, CanvasStaticLayer& layer) noexcept
        : owner_(owner)
        , layer_(layer) {}
    void paint(juce::Graphics& g) override { owner_.paintEntireComponent(g, false); }
    bool invalidateAll() override {
        layer_.invalidated(owner_.getLocalBounds());
        return true;
    }
    bool invalidate(const juce::Rectangle<int>& area) override {
        layer_.invalidated(area);
        return true;
    }
    void releaseResources() override { layer_.drop(); }

private:
    juce::Component& owner_;
    CanvasStaticLayer& layer_;
};

bool CanvasStaticLayer::Key::operator==(const Key& o) const noexcept {
    const auto& a = canvasToViewport;
    const auto& b = o.canvasToViewport;
    return a.mat00 == b.mat00 && a.mat01 == b.mat01 && a.mat02 == b.mat02 && a.mat10 == b.mat10 && a.mat11 == b.mat11 &&
           a.mat12 == b.mat12 && viewportBounds == o.viewportBounds && deviceScale == o.deviceScale &&
           softwareTarget == o.softwareTarget;
}

CanvasStaticLayer::CanvasStaticLayer(juce::Component& viewport, juce::Component& canvas)
    : viewport_(viewport)
    , canvas_(canvas) {
    jassert(viewport_.getCachedComponentImage() == nullptr);
    viewport_.setCachedComponentImage(new RepaintTap(viewport_, *this)); // the component owns it
}

CanvasStaticLayer::~CanvasStaticLayer() { viewport_.setCachedComponentImage(nullptr); }

void CanvasStaticLayer::repaintOver(juce::Rectangle<int> canvasArea) {
    const juce::ScopedValueSetter<bool> over(repaintingOver_, true);
    canvas_.repaint(canvasArea);
}

// A card's own repaint (a meter, a knob's mod ring, 15 times a second with audio running) reaches the viewport as the
// card's area, right after the card's cache noted it. It never changes the layer, which is drawn under the cards: what
// the layer draws moves only when a card moves, and that goes through GraphEditor::repaintCanvas(). The area is
// matched (within rounding, the canvas is zoomed) so a note left by a card that is not on this canvas never hides one
// of the canvas' own repaints.
bool CanvasStaticLayer::isCardsOwnRepaint(juce::Rectangle<int> viewportArea) const {
    auto& mark = ZoomFrozenCachedImage::lastRepaint();
    const auto* card = mark.card;
    const auto area = mark.area;
    mark = {};
    if (card == nullptr || card->getParentComponent() != &canvas_)
        return false;
    const auto inCanvas = canvas_.getLocalArea(card, area).getIntersection(canvas_.getLocalBounds());
    const auto expected = viewport_.getLocalArea(&canvas_, inCanvas).getIntersection(viewport_.getLocalBounds());
    constexpr int kRounding = 2;
    return expected.expanded(kRounding).contains(viewportArea) && viewportArea.expanded(kRounding).contains(expected);
}

void CanvasStaticLayer::invalidated(juce::Rectangle<int> viewportArea) {
    const bool cards = isCardsOwnRepaint(viewportArea);
    if (!repaintingOver_ && !cards && viewportArea.intersects(viewport_.getLocalBounds()))
        dirty_ = true;
}

void CanvasStaticLayer::drop() noexcept {
    picture_ = juce::Image();
    pictureValid_ = false;
    dirty_ = true;
}

// Measured through the component tree (three points), so the canvas' position and transform and any component
// between it and the viewport all count.
juce::AffineTransform CanvasStaticLayer::canvasToViewport() const {
    const auto o = viewport_.getLocalPoint(&canvas_, juce::Point<float>(0.0f, 0.0f));
    const auto x = viewport_.getLocalPoint(&canvas_, juce::Point<float>(1.0f, 0.0f));
    const auto y = viewport_.getLocalPoint(&canvas_, juce::Point<float>(0.0f, 1.0f));
    return {x.x - o.x, y.x - o.x, o.x, x.y - o.y, y.y - o.y, o.y};
}

void CanvasStaticLayer::paint(juce::Graphics& g, const std::function<void(juce::Graphics&)>& paintLayer) {
    lastPaintUsedPicture_ = false;
    // The viewport is untransformed, so the context's pixel scale over the canvas' own scale is the display's. A
    // fractional display scale puts the viewport between device pixels; the picture could not match them there.
    const auto toViewport = canvasToViewport();
    const float canvasScale = std::sqrt(std::abs(toViewport.getDeterminant()));
    const float ratio = canvasScale > 0.0f ? g.getInternalContext().getPhysicalPixelScaleFactor() / canvasScale : 0.0f;
    const int deviceScale = juce::roundToInt(ratio);
    const bool software = dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*>(&g.getInternalContext()) != nullptr;
    const Key key{toViewport, viewport_.getLocalBounds(), deviceScale, software};

    if (dirty_ || deviceScale < 1 || std::abs(ratio - (float)deviceScale) > 1.0e-3f || !(key == pictureKey_)) {
        dirty_ = false;
        pictureValid_ = false;
        pictureKey_ = key;
        quietPaints_ = 0;
        paintLayer(g);
        return;
    }
    if (!pictureValid_ && ++quietPaints_ < kQuietPaints) {
        paintLayer(g);
        return;
    }
    if (!pictureValid_)
        takePicture(key, paintLayer);

    const juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setOpacity(1.0f);
    g.drawImageTransformed(picture_,
                           juce::AffineTransform::scale(1.0f / (float)deviceScale).followedBy(toViewport.inverted()));
    lastPaintUsedPicture_ = true;
}

void CanvasStaticLayer::takePicture(const Key& key, const std::function<void(juce::Graphics&)>& paintLayer) {
    const auto size = key.viewportBounds.getSmallestIntegerContainer() * key.deviceScale;
    // The picture is rasterised the way the context it is drawn into would have drawn it: a native image for the window
    // (CoreGraphics on macOS), a software one for a software context (a snapshot), whose anti-aliasing rounds
    // differently.
    if (!picture_.isValid() || picture_.getWidth() != size.getWidth() || picture_.getHeight() != size.getHeight() ||
        pictureIsSoftware_ != key.softwareTarget) {
        const auto w = juce::jmax(1, size.getWidth()), h = juce::jmax(1, size.getHeight());
        picture_ = key.softwareTarget ? juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType())
                                      : juce::Image(juce::Image::ARGB, w, h, true);
        pictureIsSoftware_ = key.softwareTarget;
    } else
        picture_.clear(picture_.getBounds());
    {
        juce::Graphics pg(picture_);
        pg.addTransform(key.canvasToViewport.scaled((float)key.deviceScale));
        pg.reduceClipRegion(
            key.viewportBounds.toFloat().transformedBy(key.canvasToViewport.inverted()).getSmallestIntegerContainer());
        paintLayer(pg);
    }
    pictureValid_ = true;
    ++picturesTaken_;
}

} // namespace synth::ui
