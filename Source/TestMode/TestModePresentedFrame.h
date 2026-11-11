#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

// TestModePresentedFrame.h: what the main window shows on screen, rebuilt from the areas its peer actually paints, so
// a pixel a missing repaint left behind (docs/development/test-mode.md#stale-pixels) is found by comparing it with a
// full repaint. A component snapshot always paints everything fresh and can never show such a pixel.
namespace synth::testmode {

/** The largest per-channel difference that still counts as the same pixel. A partial repaint and a full one can leave
 *  an anti-aliased pixel a few levels apart (up to 4 seen); a left-behind dot, cable or text pixel differs by 9 and
 * more. */
constexpr int kStaleTolerance = 6;

class PresentedFrame {
public:
    /** The one frame the test-mode commands and the main window share. Message thread only. */
    static PresentedFrame& shared();

    /** Starts following `window` from a full paint of it; a later start restarts from a full paint. */
    void start(juce::Component& window);
    void stop();
    bool isFollowing(const juce::Component& window) const { return window_ == &window; }

    /** The window's peer has just painted `area` (window coordinates): the same area of the frame is painted again,
     *  over what it held. Ignored for another window, while not following and inside a ScopedOffscreenPaint. */
    void notePainted(juce::Component& window, juce::Rectangle<int> area);

    /** Peer paints taken since the last call, and how many of them covered the whole window. */
    struct PaintCounts {
        int paints = 0;
        int wholeWindow = 0;
    };
    PaintCounts takePaintCounts() { return std::exchange(counts_, {}); }

    /** Called with the frame after every peer paint it took in. One observer; an empty function removes it. */
    void setPaintObserver(std::function<void(const juce::Image&)> observer) { observer_ = std::move(observer); }

    /** The whole window painted fresh, at scale 1: what the screen should show. */
    static juce::Image paintFullRepaint(juce::Component& window);

    /** What the window has on screen, at scale 1. Invalid while not following. */
    const juce::Image& image() const { return image_; }

    struct Difference {
        int pixels = 0;              // pixels outside `ignore` whose colour differs by more than the tolerance
        juce::Rectangle<int> bounds; // the box around them, window coordinates
        juce::Image fullRepaint;     // the whole window painted fresh, what the screen should show
    };
    /** Compares the frame with a full repaint of the window. A frame of another size (the window was resized) is
     *  rebuilt first and compares equal. `tolerance` is the largest per-channel difference that still counts as
     *  equal. */
    Difference compareWithFullRepaint(const juce::RectangleList<int>& ignore, int tolerance = 0);

    /** `fullRepaint` with every differing pixel of `presented` (outside `ignore`) drawn magenta. */
    static juce::Image markDifferences(const juce::Image& presented, const juce::Image& fullRepaint,
                                       const juce::RectangleList<int>& ignore, int tolerance = 0);

private:
    juce::Component::SafePointer<juce::Component> window_;
    juce::Image image_;
    PaintCounts counts_;
    std::function<void(const juce::Image&)> observer_;
};

/** While one lives, window paints are not peer paints (a screenshot, the frame's own repaint, a full repaint to
 *  compare with) and the frame ignores them. */
class ScopedOffscreenPaint {
public:
    ScopedOffscreenPaint();
    ~ScopedOffscreenPaint();
    ScopedOffscreenPaint(const ScopedOffscreenPaint&) = delete;
    ScopedOffscreenPaint& operator=(const ScopedOffscreenPaint&) = delete;
    static bool active();
};

/** For the main window's paintOverChildren: `g`'s clip is the area its peer is painting now. Costs a pointer
 *  compare when the frame is not following the window. */
void notePresentedPaint(juce::Component& window, juce::Graphics& g);

} // namespace synth::testmode
