#pragma once

#include "UI/Layout/ScrollTween.h"
#include <algorithm>
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerSectionViewport.h (docs/mixer/panel.md#shared-sections): the scrolling frame one column puts
// around its insert or send list, so a list longer than the shared section height scrolls with the
// wheel inside its own section. No scrollbar component: a 3 px thumb and a 10 px bottom fade are
// painted over the list instead, and the frame never takes keyboard focus.
namespace synth::ui {

// SmoothWheelViewport -- a juce::Viewport whose trackpad scroll lands once per display frame
// (docs/layout/animation.md, "Scroll glide"). A mouse notch and every other gesture keep juce::Viewport's own wheel
// handling; a trackpad's deltas (isSmooth or isInertial) pile up in a WheelScrollRouter and move the view in whole
// device-independent pixels, one move per frame, from a fractional position kept here so a slow swipe still adds up.
class SmoothWheelViewport : public juce::Viewport {
public:
    /** The wheel scrolls an axis whose bar is hidden (setScrollBarsShown(false, ...)): what juce keeps privately. */
    void setWheelWithoutBars(bool horizontal, bool vertical) noexcept {
        withoutBars_[0] = horizontal;
        withoutBars_[1] = vertical;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    WheelScrollRouter& getWheelForTest() noexcept { return wheel_; }

private:
    bool canScroll(int axis);
    int maxPosition(int axis) const;
    bool takeTrackpad(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel);
    double readExact(int axis);
    void moveExact(int axis, double delta);

    // juce::Viewport's wheel distance: a wheel unit is 14 steps of its single step (16 px here).
    static constexpr double kPxPerWheelUnit = 14.0 * 16.0;
    WheelScrollRouter wheel_;
    double exact_[2] = {0.0, 0.0};
    bool withoutBars_[2] = {false, false};
};

class MixerSectionViewport : public SmoothWheelViewport {
public:
    static constexpr int kThumbWidth = 3;
    static constexpr int kFadeHeight = 10;

    MixerSectionViewport();

    /** `list` must outlive this viewport; it is not owned. */
    void setList(juce::Component& list);
    /** The list's own content height (rows times row height); re-fits the list to the frame. */
    void setContentHeight(int contentHeight);

    /** True when the list is taller than the frame, i.e. the wheel scrolls it. */
    bool isScrollable() const noexcept;
    /** Scrolls the least that brings list rows `top` to `bottom` (list coordinates) into the frame. */
    void revealRange(int top, int bottom);

    void resized() override;
    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    void fitList();

    int contentHeight_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSectionViewport)
};

// ---- SmoothWheelViewport: trackpad path; the axis rules mirror juce::Viewport::useMouseWheelMoveIfNeeded ----
inline bool SmoothWheelViewport::canScroll(int axis) {
    const bool bar = axis == 0 ? getHorizontalScrollBar().isVisible() : getVerticalScrollBar().isVisible();
    return bar || withoutBars_[(size_t)axis];
}

inline int SmoothWheelViewport::maxPosition(int axis) const {
    const auto* content = getViewedComponent();
    if (content == nullptr)
        return 0;
    return axis == 0 ? std::max(0, content->getWidth() - getMaximumVisibleWidth())
                     : std::max(0, content->getHeight() - getMaximumVisibleHeight());
}

inline double SmoothWheelViewport::readExact(int axis) {
    const int actual = axis == 0 ? getViewPositionX() : getViewPositionY();
    if ((int)std::llround(exact_[axis]) != actual) // someone else moved the view: start from where it is
        exact_[axis] = actual;
    return exact_[axis];
}

// Moves by a clamped delta; the view itself only ever sits on a whole pixel, so a move that rounds to the same pixel
// changes nothing and repaints nothing.
inline void SmoothWheelViewport::moveExact(int axis, double delta) {
    const double target = std::clamp(readExact(axis) + delta, 0.0, (double)maxPosition(axis));
    exact_[axis] = target;
    auto pos = getViewPosition();
    (axis == 0 ? pos.x : pos.y) = (int)std::llround(target);
    if (pos != getViewPosition())
        setViewPosition(pos);
}

// Same axis choice as juce::Viewport: both deltas move both axes, else a sideways delta (or Shift, or no vertical bar)
// moves x, else the vertical delta moves y. False hands the event on (nothing could move), as juce does.
inline bool SmoothWheelViewport::takeTrackpad(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isAltDown() || e.mods.isCtrlDown() || e.mods.isCommandDown())
        return false;
    const bool horz = canScroll(0), vert = canScroll(1);
    const double dx = std::abs(wheel.deltaX) < 1.0e-6f ? 0.0 : wheel.deltaX * kPxPerWheelUnit;
    const double dy = std::abs(wheel.deltaY) < 1.0e-6f ? 0.0 : wheel.deltaY * kPxPerWheelUnit;
    double amount[2] = {0.0, 0.0};
    if (dx != 0.0 && dy != 0.0 && horz && vert) {
        amount[0] = -dx;
        amount[1] = -dy;
    } else if (horz && (dx != 0.0 || e.mods.isShiftDown() || !vert)) {
        amount[0] = -(dx != 0.0 ? dx : dy);
    } else if (vert && dy != 0.0) {
        amount[1] = -dy;
    }
    bool any = false;
    for (int axis = 0; axis < 2; ++axis) {
        const int pos = axis == 0 ? getViewPositionX() : getViewPositionY();
        if (amount[axis] < 0.0 ? pos <= 0 : pos >= maxPosition(axis))
            amount[axis] = 0.0; // already at that end: nothing to queue
        any = any || amount[axis] != 0.0;
    }
    if (!any)
        return false;
    const auto read = [this](int a) { return readExact(a); };
    const auto scroll = [this](int a, double d) { moveExact(a, d); };
    for (int axis = 0; axis < 2; ++axis)
        if (amount[axis] != 0.0 && !wheel_.route(*this, axis, amount[axis], false, read, scroll))
            moveExact(axis, amount[axis]); // not showing: apply now
    return true;
}

inline void SmoothWheelViewport::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.eventComponent == this) {
        if (wheel.isSmooth || wheel.isInertial) {
            if (takeTrackpad(e, wheel))
                return;
        } else {
            wheel_.settle(); // a notch after a swipe starts from where the swipe's backlog ends
        }
    }
    juce::Viewport::mouseWheelMove(e, wheel);
}

} // namespace synth::ui
