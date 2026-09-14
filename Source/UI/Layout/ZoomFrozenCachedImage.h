#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Buffered-image cache for a card on the zoomable canvas.
 *
 *  Identical to juce::detail::StandardCachedComponentImage except that setFrozen(true) pins the
 *  image's pixel size. JUCE keys the cached image on the ACCUMULATED device scale, which on this
 *  canvas is zoomLevel * deviceScale — so without this, one wheel tick re-rasterizes every visible
 *  card (panel + every child slider/label) at a new size. Frozen, the existing image is just
 *  resampled; unfreezing drops it so exactly one crisp re-render happens when the gesture settles.
 *  Still the setBufferedToImage(true) contract of docs/layout.md §10 — same cache, same
 *  invalidate() semantics, only the scale is deferred. */
class ZoomFrozenCachedImage : public juce::CachedComponentImage {
public:
    explicit ZoomFrozenCachedImage(juce::Component& c) noexcept
        : owner(c) {}

    void paint(juce::Graphics& g) override {
        const float realScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto compBounds = owner.getLocalBounds();
        // Frozen: reuse the scale already in the image. A resize still reallocates, because
        // compBounds changed and imageBounds follows it.
        const float useScale = (frozen && image.isValid()) ? scale : realScale;
        const auto imageBounds = compBounds * useScale;

        if (image.isNull() || image.getBounds() != imageBounds) {
            image = juce::Image(owner.isOpaque() ? juce::Image::RGB : juce::Image::ARGB,
                                juce::jmax(1, imageBounds.getWidth()), juce::jmax(1, imageBounds.getHeight()),
                                !owner.isOpaque());
            validArea.clear();
        }
        scale = useScale;

        if (!validArea.containsRectangle(compBounds)) {
            juce::Graphics imG(image);
            auto& lg = imG.getInternalContext();
            lg.addTransform(juce::AffineTransform::scale(scale));
            for (auto& i : validArea)
                lg.excludeClipRectangle(i);
            if (!owner.isOpaque()) {
                lg.setFill(juce::Colours::transparentBlack);
                lg.fillRect(compBounds, true);
                lg.setFill(juce::Colours::black);
            }
            owner.paintEntireComponent(imG, true);
            ++rasterCount;
        }
        validArea = compBounds;

        g.setColour(juce::Colours::black.withAlpha(owner.getAlpha()));
        g.drawImageTransformed(
            image,
            juce::AffineTransform::scale((float)compBounds.getWidth() / (float)imageBounds.getWidth(),
                                         (float)compBounds.getHeight() / (float)imageBounds.getHeight()),
            false);
    }

    bool invalidateAll() override {
        validArea.clear();
        return true;
    }
    bool invalidate(const juce::Rectangle<int>& a) override {
        validArea.subtract(a);
        return true;
    }
    void releaseResources() override { image = juce::Image(); }

    void setFrozen(bool shouldFreeze) {
        if (frozen == shouldFreeze)
            return;
        frozen = shouldFreeze;
        if (!frozen) {
            // Thaw: drop the image so the next paint sizes it from the real scale and renders
            // once, crisp. Keeping it would leave the card soft until something else resized it.
            image = juce::Image();
            validArea.clear();
            owner.repaint();
        }
    }
    bool isFrozen() const noexcept { return frozen; }

    // Test seam (docs/layout.md §11 paint-count pattern): how many times the owner's paint tree
    // has actually been re-run into this image.
    int getRasterCountForTest() const noexcept { return rasterCount; }
    juce::Rectangle<int> getImageBoundsForTest() const noexcept { return image.getBounds(); }

private:
    juce::Image image;
    juce::RectangleList<int> validArea;
    juce::Component& owner;
    float scale = 1.0f;
    bool frozen = false;
    int rasterCount = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomFrozenCachedImage)
};

} // namespace synth::ui
