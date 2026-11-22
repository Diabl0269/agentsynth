#pragma once

#include "UI/Theme/Theme.h"
#include <functional>
#include <initializer_list>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// An icon button with a side menu (the Timeline's Clip sizing chip and the pen): a small notch in the lower-right
// corner marks the menu, and the menu opens by a corner press, a held press or a right-click (SideMenuGesture).

/** How far the notch's legs run along the chip's right and bottom edges, in px. */
constexpr float kSideMenuNotchLeg = 10.0f;
/** The square in the chip's lower-right corner whose press opens the menu, in px. */
constexpr int kSideMenuCornerSize = 10;

/** The notch's leg on a chip with corner radius `chipRadius`: kSideMenuNotchLeg, longer on a very round chip so the arc
 *  that rounds its corner never leaves only a sliver of the notch. */
float sideMenuNotchLegFor(float chipRadius) noexcept;

/** The ink of the notch on a chip filled with any of `fills`: the theme's best ink at rest, easing to `litInk` (the lit
 *  glyph's ink) as `lit` (0 to 1) rises, toned toward black or white only as far as 3.5:1 needs. */
juce::Colour sideMenuNotchInk(const synth::theme::Colors& colours, std::initializer_list<juce::Colour> fills,
                              juce::Colour litInk, float lit);

/** Paints the notch clipped to the chip's rounded border; `squash` is the chip's press squash about its centre. */
void paintSideMenuNotch(juce::Graphics& g, juce::Rectangle<float> chip, float chipRadius, juce::Point<float> squash,
                        juce::Colour ink, bool enabled);

/** True for a point (button coordinates) in the chip's lower-right press corner. */
bool isInSideMenuCorner(juce::Point<int> point, juce::Rectangle<int> chipBounds) noexcept;

// The gesture half of the side menu: a right-click or a left press in the corner opens the menu at once and picks
// nothing; a left press held for kHoldMs opens it and swallows the release; a drag past 4 px or leaving the button
// cancels the hold. A button owns one and forwards mouseDown / mouseDrag / mouseUp / mouseExit; mouseDown returns true
// when the press was the gesture's (the button then skips its own press). Message thread only.
class SideMenuGesture : private juce::Timer {
public:
    static constexpr int kHoldMs = 400;

    SideMenuGesture() = default;
    ~SideMenuGesture() override { stopTimer(); }

    /** Where a corner press lands, in the button's coordinates. */
    std::function<juce::Rectangle<int>()> chipBounds;
    /** Opens the menu. */
    std::function<void()> onOpen;
    /** The button drops its pressed look: a hold swallowed the press, so no click will follow. */
    std::function<void()> onPressSwallowed;
    /** After a hold opened the menu, the release lands here (screen position) while the hold is still down. */
    std::function<void(juce::Point<int>)> onHoldReleased;

    /** True when the press opened the menu (right-click, corner) or armed the hold; the button then skips its own
     *  mouseDown for an opening press and calls it normally for an armed one. */
    bool mouseDown(const juce::MouseEvent& e);
    /** True when the drag belongs to the gesture (a menu press), so the button ignores it. */
    bool mouseDrag(const juce::MouseEvent& e);
    /** True when the release belongs to the gesture (it opened the menu), so the button must not click. */
    bool mouseUp(const juce::MouseEvent& e);
    void mouseExit();

    /** True from the moment a hold opened the menu until that press is released. */
    bool isHoldGestureDown() const noexcept { return holdFired_; }
    /** Test seam: what the hold timer does when it fires (headless tests have no message loop). */
    void holdElapsedForTest() { timerCallback(); }
    bool isHoldPendingForTest() const noexcept { return isTimerRunning(); }

private:
    void timerCallback() override;
    void open();

    bool menuPress_ = false;
    bool holdFired_ = false;
};

} // namespace synth::ui
