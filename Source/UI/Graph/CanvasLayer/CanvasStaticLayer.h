#pragma once

// CanvasStaticLayer.h
//
// A picture of what a zoomable canvas draws under its cards, kept while nothing repaints it, so a frame that only moves
// something drawn over it (the cable flow dots) draws one image instead of every cable and macro border
// (docs/layout/rendering.md#an-idle-canvas-with-moving-flow-dots-draws-one-picture).

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Message thread only. `viewport` is the untransformed component that shows `canvas`, a transformed descendant; both
 *  must outlive the layer. The layer listens to every repaint inside `viewport` (it sets the viewport's cached
 *  component image, so the viewport must not have one of its own). */
class CanvasStaticLayer {
public:
    /** Paints the layer have to be quiet before the picture is taken. */
    static constexpr int kQuietPaints = 8;

    CanvasStaticLayer(juce::Component& viewport, juce::Component& canvas);
    ~CanvasStaticLayer();
    CanvasStaticLayer(const CanvasStaticLayer&) = delete;
    CanvasStaticLayer& operator=(const CanvasStaticLayer&) = delete;

    /** Repaints `canvasArea` (canvas coordinates) and keeps the picture: only for a frame that changes nothing the
     *  layer draws. */
    void repaintOver(juce::Rectangle<int> canvasArea);
    /** Draws the layer into `g` (the canvas' own paint context): from the picture once nothing has repainted the
     *  viewport for kQuietPaints paints, else by calling `paintLayer` directly. */
    void paint(juce::Graphics& g, const std::function<void(juce::Graphics&)>& paintLayer);

    /** Test seams: whether the last paint() drew the picture, and how many pictures were taken. */
    bool lastPaintUsedPicture() const noexcept { return lastPaintUsedPicture_; }
    int picturesTaken() const noexcept { return picturesTaken_; }

private:
    class RepaintTap;
    struct Key {
        juce::AffineTransform canvasToViewport;
        juce::Rectangle<int> viewportBounds;
        int deviceScale = 0;
        bool softwareTarget = false;
        bool operator==(const Key& o) const noexcept;
    };
    void invalidated(juce::Rectangle<int> viewportArea);
    bool isCardsOwnRepaint(juce::Rectangle<int> viewportArea) const;
    void drop() noexcept;
    juce::AffineTransform canvasToViewport() const;
    void takePicture(const Key& key, const std::function<void(juce::Graphics&)>& paintLayer);

    juce::Component& viewport_;
    juce::Component& canvas_;
    juce::Image picture_;
    Key pictureKey_;
    bool pictureValid_ = false;
    bool pictureIsSoftware_ = false;
    bool dirty_ = true;
    bool repaintingOver_ = false;
    int quietPaints_ = 0;
    bool lastPaintUsedPicture_ = false;
    int picturesTaken_ = 0;
};

} // namespace synth::ui
