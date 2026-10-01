#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// A thin strip along an edge that resizes something by dragging: a track row's bottom edge, the
// timeline's header-column seam. It never sizes anything itself. A drag reports how far the pointer
// has moved since the press, in screen pixels along its axis, and the owner turns that into a size,
// clamps it and re-lays out (so the strip may move under the pointer without the delta chasing itself).
// A double-click asks for the default size.
//
// Mouse-only by default. setKeyboardStep() makes it a Tab stop too: arrows nudge by that step, Return
// resets, and it draws the accent focus ring. Its screen-reader name and tooltip come from the owner.
class EdgeResizeHandle
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    enum class Axis {
        Vertical,  ///< resizes a height: drag up/down, UpDownResizeCursor
        Horizontal ///< resizes a width: drag left/right, LeftRightResizeCursor
    };

    explicit EdgeResizeHandle(Axis axis);

    Axis getAxis() const noexcept { return axis_; }

    /** A press on the strip, before any movement: the owner records the size it starts from. */
    std::function<void()> onDragStarted;
    /** Every drag step: pointer movement since the press, px (down/right positive). */
    std::function<void(int deltaPx)> onDragged;
    /** Mouse-up after a drag that moved: the owner's cue to commit (one undo step, persist). */
    std::function<void()> onDragEnded;
    /** Double-click, or Return when focused: back to the default size. */
    std::function<void()> onResetRequested;
    /** An arrow key when focused: +1 grows (Down/Right), -1 shrinks (Up/Left). */
    std::function<void(int direction)> onKeyboardStep;

    /** false (the default) = mouse-only; true = a Tab stop whose arrows call onKeyboardStep. */
    void setKeyboardFocusable(bool focusable);

    bool isHighlighted() const noexcept { return hovered_ || dragging_; }
    bool isDragging() const noexcept { return dragging_; }

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    int alongAxis(juce::Point<int> p) const noexcept { return axis_ == Axis::Vertical ? p.y : p.x; }

    Axis axis_;
    bool hovered_ = false;
    bool dragging_ = false;
    bool moved_ = false;
    int pressScreen_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdgeResizeHandle)
};

} // namespace synth::ui
