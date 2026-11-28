#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Buffered-image cache for a card on the zoomable canvas.
 *
 *  Identical to juce::detail::StandardCachedComponentImage except that setFrozen(true) pins the
 *  image's pixel size. JUCE keys the cached image on the ACCUMULATED device scale, which on this
 *  canvas is zoomLevel * deviceScale — so without this, one wheel tick re-rasterizes every visible
 *  card (panel + every child slider/label) at a new size. Frozen, the existing image is just
 *  resampled; unfreezing drops it so exactly one crisp re-render happens when the gesture settles.
 *  Still the setBufferedToImage(true) contract of docs/layout/rendering.md — same cache, same
 *  invalidate() semantics, only the scale is deferred. */
class ZoomFrozenCachedImage : public juce::CachedComponentImage {
public:
    explicit ZoomFrozenCachedImage(juce::Component& c) noexcept
        : owner(c) {}
    ~ZoomFrozenCachedImage() override {
        if (lastRepaint().card == &owner)
            lastRepaint() = {};
    }

    void paint(juce::Graphics& g) override {
        movedSnapshot.clear();
        const float realScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto compBounds = owner.getLocalBounds();
        // Frozen: reuse the scale already in the image. A resize still reallocates, because
        // compBounds changed and imageBounds follows it.
        const float useScale = (frozen && (image.isValid() || pinned)) ? scale : realScale;
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
            {
                const juce::ScopedValueSetter<bool> inPicture(renderingPicture, true);
                owner.paintEntireComponent(imG, true);
            }
            ++rasterCount;
        }
        validArea = compBounds;

        g.setColour(juce::Colours::black.withAlpha(owner.getAlpha()));
        g.drawImageTransformed(
            image,
            juce::AffineTransform::scale((float)compBounds.getWidth() / (float)imageBounds.getWidth(),
                                         (float)compBounds.getHeight() / (float)imageBounds.getHeight()),
            false);
        paintLiveLayer(g, owner.getAlpha());
    }

    /** `paintLive` draws over the picture on every paint, in the owner's coordinates and at its alpha; the owner's
     *  paint leaves that content out while isRenderingPicture(). `hasLive` says whether there is any. */
    void setLivePainter(std::function<bool()> hasLive, std::function<void(juce::Graphics&)> paintLive) {
        hasLive_ = std::move(hasLive);
        paintLive_ = std::move(paintLive);
    }
    bool isRenderingPicture() const noexcept { return renderingPicture; }
    /** `owner.repaint(area)` for a change only the live painter draws: the picture keeps the area. */
    void repaintLive(juce::Rectangle<int> area) {
        const juce::ScopedValueSetter<bool> keep(keepingPicture, true);
        ++liveRepaintCount;
        owner.repaint(area);
    }
    int getLivePaintCountForTest() const noexcept { return livePaintCount; }
    int getLiveRepaintCountForTest() const noexcept { return liveRepaintCount; }

    bool invalidateAll() override {
        if (keepingPicture) {
            lastRepaint() = {&owner, owner.getLocalBounds()};
            return true;
        }
        movedSnapshot = validArea;
        validArea.clear();
        lastRepaint() = {&owner, owner.getLocalBounds()};
        return true;
    }
    bool invalidate(const juce::Rectangle<int>& a) override {
        if (keepingPicture) {
            lastRepaint() = {&owner, a};
            return true;
        }
        movedSnapshot.clear();
        validArea.subtract(a);
        lastRepaint() = {&owner, a};
        return true;
    }

    /** Message thread only. The last repaint a card buffered by this cache asked for (the card and the area in its own
     *  coordinates), noted just before JUCE passes it on to the card's parent. CanvasStaticLayer reads it to tell a
     *  card's own repaint, which never changes what is drawn under the cards, from the canvas'. `card` may name a card
     *  that has since gone: compare it, never dereference it. */
    struct Repaint {
        const juce::Component* card = nullptr;
        juce::Rectangle<int> area;
    };
    static Repaint& lastRepaint() noexcept {
        static Repaint last;
        return last;
    }
    void releaseResources() override {
        image = juce::Image();
        movedSnapshot.clear();
    }

    /** The owner's moved() calls this. JUCE's Component::setBounds drops the whole picture of a card
     *  that is moved while not showing (invalidateAll, just before moved()), though a pure move changes nothing the
     *  card draws: its picture is in its own coordinates. So moving 200 hidden cards re-rastered all 200. This puts
     *  back what that invalidateAll dropped. Only when the owner is not showing (setBounds invalidates only then,
     *  so a snapshot left by an earlier repaint() is never trusted for a showing card). A move that also resizes
     *  is safe: paint() reallocates on a size change and clears the valid area. */
    void noteMoved() {
        if (!owner.isShowing())
            validArea = movedSnapshot;
        movedSnapshot.clear();
    }

    /** The owner's setAlpha() calls this. Alpha is applied when the picture is drawn (paint()), so a change drops
     *  nothing the picture holds, yet JUCE's setAlpha repaints the card whole, which invalidates the picture. The undo
     *  glide hides each moving card at alpha 0 and shows it again at the end, so every card it moved was re-rastered
     *  twice (about 120 ms for 160 cards on the Load test project). This puts back what that invalidateAll dropped. */
    void noteAlphaChanged() {
        if (owner.isVisible())
            validArea = movedSnapshot;
        movedSnapshot.clear();
    }

    /** `comp.setAlpha(alpha)` that keeps the card's picture (see noteAlphaChanged). Any component works: one without
     *  this cache is just set. */
    static void setAlphaKeepingPicture(juce::Component& comp, float alpha) {
        const float before = comp.getAlpha();
        comp.setAlpha(alpha);
        if (auto* cache = dynamic_cast<ZoomFrozenCachedImage*>(comp.getCachedComponentImage());
            cache != nullptr && comp.getAlpha() != before)
            cache->noteAlphaChanged();
    }

    void setFrozen(bool shouldFreeze) {
        if (frozen == shouldFreeze)
            return;
        frozen = shouldFreeze;
        pinned = false;
        if (!frozen) {
            // Thaw: drop the image so the next paint sizes it from the real scale and renders
            // once, crisp. Keeping it would leave the card soft until something else resized it.
            image = juce::Image();
            validArea.clear();
            movedSnapshot.clear();
            owner.repaint();
        }
    }
    bool isFrozen() const noexcept { return frozen; }
    /** Rasterises at `pinnedScale` until unpin(), even before the first paint: a card first painted while a
     *  temporary component transform scales it (the project-load pop) is still drawn crisp at its real scale. */
    void pinScale(float pinnedScale) {
        frozen = true;
        pinned = true;
        scale = pinnedScale;
    }
    /** Ends pinScale() and keeps the image: at the card's real scale nothing is re-rendered. */
    void unpin() {
        if (!pinned)
            return;
        pinned = false;
        frozen = false;
    }

    /** The last raster of the whole card at its raster scale, or a null image when there is none at the card's
     *  current size. Without live content it shares pixels with the cache, so it shows what the card paints next; a
     *  part the card invalidated since its last paint is one paint stale. With live content it is a copy with the live
     *  layer drawn in as it is now. */
    juce::Image lastRaster() const {
        if (!image.isValid() || image.getBounds() != owner.getLocalBounds() * scale)
            return {};
        if (paintLive_ == nullptr || (hasLive_ != nullptr && !hasLive_()))
            return image;
        auto copy = image.createCopy();
        juce::Graphics cg(copy);
        cg.addTransform(juce::AffineTransform::scale(scale));
        paintLive_(cg);
        return copy;
    }

    // Test seam (docs/layout/animation.md's paint-count pattern): how many times the owner's paint tree
    // has actually been re-run into this image.
    int getRasterCountForTest() const noexcept { return rasterCount; }
    juce::Rectangle<int> getImageBoundsForTest() const noexcept { return image.getBounds(); }
    /** Pixels (in the owner's own units) the next paint would re-raster: what a repaint since the last paint
     *  invalidated. A whole-owner repaint makes this the owner's full area. */
    int getStaleAreaForTest() const {
        juce::RectangleList<int> stale(owner.getLocalBounds());
        stale.subtract(validArea);
        int area = 0;
        for (const auto& r : stale)
            area += r.getWidth() * r.getHeight();
        return area;
    }

private:
    // The live layer is drawn straight into the window's context, after the picture, so the cards above still cover
    // it and nothing of it is ever rasterised: a ring frame costs one blit of the picture plus the ring.
    void paintLiveLayer(juce::Graphics& g, float alpha) {
        if (paintLive_ == nullptr || alpha <= 0.0f || (hasLive_ != nullptr && !hasLive_()))
            return;
        ++livePaintCount;
        const juce::Graphics::ScopedSaveState state(g);
        if (alpha < 1.0f)
            g.beginTransparencyLayer(alpha);
        paintLive_(g);
        if (alpha < 1.0f)
            g.endTransparencyLayer();
    }

    juce::Image image;
    juce::RectangleList<int> validArea;
    juce::RectangleList<int> movedSnapshot; // what the last invalidateAll() dropped, for noteMoved()
    juce::Component& owner;
    float scale = 1.0f;
    bool frozen = false;
    bool pinned = false;
    int rasterCount = 0;
    int livePaintCount = 0;
    int liveRepaintCount = 0;
    bool renderingPicture = false;
    bool keepingPicture = false;
    std::function<bool()> hasLive_;
    std::function<void(juce::Graphics&)> paintLive_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomFrozenCachedImage)
};

} // namespace synth::ui
