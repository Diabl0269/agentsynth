#pragma once

// CardBodySwapMotion.h -- the motion of a card's controls swapping in place (the ADSR's Sync flipping each
// stage between its time and its note division, or one look for the other): the leaving controls shrink to
// their centre over 190 ms, THEN the arriving ones grow from their centre with the 8% bounce, never both at
// once; with nothing leaving the arrivals grow from the first frame. A paint-only layer over a layout that has
// already landed: a control is only held hidden or scaled while the motion runs. When the card itself glides to
// a new height with the change (the Macros count), the controls ride that glide instead (rideContainer): they
// start on its first frame and land on its last, with its ease. docs/layout/animation.md#controls-swapping-in-place.

#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <functional>
#include <vector>

namespace synth {

class SwapMotion {
public:
    /** A control (widget and its caption) that arrives: `rect` is its cell on the card. */
    struct Arrival {
        juce::Component::SafePointer<juce::Component> widget;
        juce::Component::SafePointer<juce::Component> label;
        juce::Rectangle<int> rect;
    };
    /** A control that leaves: the picture of its cell, taken before the swap. */
    struct Departure {
        juce::Image image;
        juce::Rectangle<int> rect;
    };

    SwapMotion(juce::Component& card, std::vector<Departure> leaving, std::vector<Arrival> arriving);
    ~SwapMotion();

    /** The card the controls sit on glides with this change for `containerMs` (CardHeightGlide::durationMs()):
     *  a change that only adds controls grows them from the glide's first frame, one that only takes controls away
     *  shrinks them from it, both over the glide's duration with its ease (easeOutCubic), ending on its last frame.
     *  A mixed swap (some leave, others arrive) keeps its two phases. Call before run / holdForTest. */
    void rideContainer(double containerMs);
    /** Starts the frames (a VBlank pump on the card); `onDone` runs once, after the controls have landed. */
    void run(std::function<void()> onDone);
    /** Test seam: like run, but no frame arrives; the test steps the motion with setElapsed. */
    void holdForTest(std::function<void()> onDone) { onDone_ = std::move(onDone); }
    /** Draws the frame `elapsedMs` into the motion (the pump's frame; tests step it by hand). */
    void setElapsed(double elapsedMs);
    /** Lands every control at once and ends the motion; runs `onDone` if it has not. */
    void finish();

    /** True while `component` is an arrival the motion keeps hidden (the leaving controls are still shrinking). */
    bool holdsHidden(const juce::Component& component) const;
    bool isRunning() const noexcept { return !finished_; }
    int numGhosts() const noexcept { return ghosts_.size(); }
    /** The picture rectangles of the leaving controls now, in card pixels. */
    std::vector<juce::Rectangle<float>> ghostRects() const;
    /** True when the controls ride the card's glide (rideContainer took effect: only arrivals or only departures). */
    bool ridesContainer() const noexcept { return containerMs_ > 0.0; }
    /** The leaving phase: 0 when nothing leaves, the card's glide when riding it, else 190 ms. */
    double shrinkMs() const noexcept;
    /** The arriving phase: 0 when nothing arrives, the card's glide when riding it, else 200 ms. */
    double growMs() const noexcept;
    double totalMs() const noexcept { return shrinkMs() + growMs(); }
    /** The two-phase length of a swap that does not ride a container: 190 ms leaving, then 200 ms arriving. */
    static constexpr double mixedTotalMs() noexcept {
        return ui::control_motion::kSwapShrinkMs + ui::control_motion::kGrowMs;
    }

private:
    void land();

    juce::Component& card_;
    std::vector<Arrival> arriving_;
    juce::OwnedArray<ui::control_motion::ShrinkGhost> ghosts_;
    ui::ReorderFramePump pump_;
    std::function<void()> onDone_;
    bool hasLeaving_ = false;
    bool hasArriving_ = false;
    double containerMs_ = 0.0;
    bool growing_ = false;
    bool finished_ = false;
};

} // namespace synth
