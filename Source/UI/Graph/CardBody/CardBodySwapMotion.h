#pragma once

// CardBodySwapMotion.h -- the motion of a card's controls swapping in place (the ADSR's Sync flipping each
// stage between its time and its note division, or one look for the other): the leaving controls shrink to
// their centre over 190 ms, THEN the arriving ones grow from their centre with the 8% bounce, never both at
// once. A paint-only layer over a layout that has already landed: the card keeps its size, and a control is
// only held hidden or scaled while the motion runs. docs/layout/animation.md#controls-swapping-in-place.

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

    /** Starts the frames (a VBlank pump on the card); `onDone` runs once, after the controls have landed. */
    void run(std::function<void()> onDone);
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
    static constexpr double totalMs() noexcept {
        return ui::control_motion::kSwapShrinkMs + ui::control_motion::kGrowMs;
    }

private:
    void land();

    juce::Component& card_;
    std::vector<Arrival> arriving_;
    juce::OwnedArray<ui::control_motion::ShrinkGhost> ghosts_;
    ui::ReorderFramePump pump_;
    std::function<void()> onDone_;
    bool growing_ = false;
    bool finished_ = false;
};

} // namespace synth
