// Concern: the presented frame of the stale-pixel check (docs/development/test-mode.md#stale-pixels).
//
// The platform keeps what a window painted and repaints only the areas that asked for it, so a component that moves,
// shrinks or goes without repainting where it was leaves its old pixels on screen. JUCE gives no way to read the
// window's backing store, and a snapshot paints everything fresh, so the frame keeps its own copy: each time the peer
// paints an area (the window's paintOverChildren sees it as the clip), that area of the copy is painted again over what
// it held, exactly as the screen is. What no repaint reached keeps its old pixels, here as there.

#include "TestMode/TestModePresentedFrame.h"
#include <algorithm>
#include <cstdlib>

namespace synth::testmode {

namespace {

int& offscreenDepth() {
    static int depth = 0;
    return depth;
}

juce::Image paintFresh(juce::Component& window) {
    const ScopedOffscreenPaint offscreen;
    return window.createComponentSnapshot(window.getLocalBounds(), true, 1.0f);
}

bool differs(juce::Colour a, juce::Colour b, int tolerance) {
    return std::abs((int)a.getRed() - (int)b.getRed()) > tolerance ||
           std::abs((int)a.getGreen() - (int)b.getGreen()) > tolerance ||
           std::abs((int)a.getBlue() - (int)b.getBlue()) > tolerance ||
           std::abs((int)a.getAlpha() - (int)b.getAlpha()) > tolerance;
}

} // namespace

ScopedOffscreenPaint::ScopedOffscreenPaint() { ++offscreenDepth(); }
ScopedOffscreenPaint::~ScopedOffscreenPaint() { --offscreenDepth(); }
bool ScopedOffscreenPaint::active() { return offscreenDepth() > 0; }

PresentedFrame& PresentedFrame::shared() {
    static PresentedFrame frame;
    return frame;
}

void PresentedFrame::start(juce::Component& window) {
    window_ = &window;
    image_ = paintFresh(window);
    counts_ = {};
}

void PresentedFrame::stop() {
    window_ = nullptr;
    image_ = {};
}

// Painted in place, inside the peer's own paint: the state is the one the screen got, never a later one.
void PresentedFrame::notePainted(juce::Component& window, juce::Rectangle<int> area) {
    if (window_ != &window || ScopedOffscreenPaint::active() || !image_.isValid())
        return;
    if (image_.getWidth() != window.getWidth() || image_.getHeight() != window.getHeight()) {
        image_ = paintFresh(window); // a resize repaints the whole window
        return;
    }
    area = area.getIntersection(window.getLocalBounds());
    if (area.isEmpty())
        return;
    ++counts_.paints;
    counts_.wholeWindow += area == window.getLocalBounds() ? 1 : 0;
    const ScopedOffscreenPaint offscreen;
    juce::Graphics g(image_);
    g.reduceClipRegion(area);
    window.paintEntireComponent(g, true);
}

PresentedFrame::Difference PresentedFrame::compareWithFullRepaint(const juce::RectangleList<int>& ignore,
                                                                  int tolerance) {
    Difference out;
    if (window_ == nullptr)
        return out;
    out.fullRepaint = paintFresh(*window_);
    if (!image_.isValid() || image_.getBounds() != out.fullRepaint.getBounds()) {
        image_ = out.fullRepaint.createCopy();
        return out;
    }
    const juce::Image::BitmapData shown(image_, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData fresh(out.fullRepaint, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < shown.height; ++y)
        for (int x = 0; x < shown.width; ++x)
            if (differs(shown.getPixelColour(x, y), fresh.getPixelColour(x, y), tolerance) &&
                !ignore.containsPoint({x, y})) {
                ++out.pixels;
                out.bounds =
                    out.bounds.isEmpty() ? juce::Rectangle<int>(x, y, 1, 1) : out.bounds.getUnion({x, y, 1, 1});
            }
    return out;
}

juce::Image PresentedFrame::markDifferences(const juce::Image& presented, const juce::Image& fullRepaint,
                                            const juce::RectangleList<int>& ignore, int tolerance) {
    auto marked = fullRepaint.createCopy();
    if (presented.getBounds() != fullRepaint.getBounds())
        return marked;
    const juce::Image::BitmapData shown(presented, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData out(marked, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
            if (differs(shown.getPixelColour(x, y), out.getPixelColour(x, y), tolerance) &&
                !ignore.containsPoint({x, y}))
                out.setPixelColour(x, y, juce::Colours::magenta);
    return marked;
}

void notePresentedPaint(juce::Component& window, juce::Graphics& g) {
    auto& frame = PresentedFrame::shared();
    if (frame.isFollowing(window))
        frame.notePainted(window, g.getClipBounds());
}

} // namespace synth::testmode
