#pragma once

// PopupMotion.h -- the ONE soft appear/disappear every popup window in the app shares: right-click
// menus and submenus, ComboBox dropdowns, call-out popovers, alert boxes and dialogs.
//
// Section 1 is pure math (headless-testable). Section 2 is the engine: PopupMotion::attach() hangs
// a self-deleting listener on a top-level window. How it is reached for each kind of window is in
// docs/layout/animation.md ("Popup windows").
//
// In: the window fades from 0 to 1 while sliding 4 px AWAY from its anchor (the pointer),
// 160 ms ease-out. Out: it fades to 0 while sliding 2 px back toward the anchor, 110 ms ease-in.
// With Reduce motion on, both are a plain 80 ms fade and nothing moves.

#include "UI/Layout/UIAnimation.h"
#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::popup_motion {

// ============================================================================
// Section 1 -- pure math
// ============================================================================

constexpr double kInMs = 160.0;
constexpr double kOutMs = 110.0;
constexpr double kReducedMs = 80.0;
constexpr float kInSlidePx = 4.0f;
constexpr float kOutSlidePx = 2.0f;

enum class Phase { In, Out };

/** How long a phase runs. Reduce motion is 80 ms both ways. */
inline double durationMs(Phase phase, bool reduceMotion) noexcept {
    if (reduceMotion)
        return kReducedMs;
    return phase == Phase::In ? kInMs : kOutMs;
}

/** The easing a phase uses: arriving decelerates, leaving accelerates away. */
inline float ease(Phase phase, float t) noexcept { return phase == Phase::In ? easeOutCubic(t) : easeInCubic(t); }

/** Ease-out with a soft overshoot: peaks at 1.03 (3% past the end) and settles back to 1. Back-ease with
 *  c1 = 0.9 (peak = 4 c1^3 / (27 (c1 + 1)^2)). */
inline float easeOutBackSoft(float t) noexcept {
    constexpr float c1 = 0.9f;
    const float u = t - 1.0f;
    return 1.0f + (c1 + 1.0f) * u * u * u + c1 * u * u;
}

/** Per-window look. The default is the shared popup motion; a window opts into more by passing a Style to
 *  PopupMotion::attach. */
struct Style {
    float inSlidePx = kInSlidePx;
    float outSlidePx = kOutSlidePx;
    bool overshoot = false; // the arrival eases with easeOutBackSoft: it lands 3% past, then settles
    /** Where the window grows out of (screen coordinates); null = the pointer. */
    std::function<juce::Point<int>()> anchor;
};

inline float ease(Phase phase, float t, const Style& style) noexcept {
    return phase == Phase::In && style.overshoot ? easeOutBackSoft(t) : ease(phase, t);
}

/** Unit step (one of (0,1), (0,-1), (1,0), (-1,0)) pointing from `anchor` to the window along the
 *  axis the window sits furthest off the anchor. A pointer inside the window's span on an axis has
 *  no distance on it, so a menu opened with its corner at the pointer slides down (the pointer is
 *  above it) and a submenu to the right of its parent's row slides right. Ties and a pointer inside
 *  the window resolve to down. */
inline juce::Point<int> slideDirection(juce::Rectangle<int> windowBounds, juce::Point<int> anchor) noexcept {
    const int outsideX = juce::jmax(windowBounds.getX() - anchor.x, anchor.x - windowBounds.getRight(), 0);
    const int outsideY = juce::jmax(windowBounds.getY() - anchor.y, anchor.y - windowBounds.getBottom(), 0);
    if (outsideX > outsideY)
        return {anchor.x < windowBounds.getX() ? 1 : -1, 0};
    if (outsideY > 0 && anchor.y > windowBounds.getBottom())
        return {0, -1};
    return {0, 1};
}

struct Frame {
    float alpha = 1.0f;              // window opacity
    juce::Point<float> offset{0, 0}; // displacement from the window's resting position
};

/** The opacity at eased progress `eased`: linear in TIME, whatever curve moves the window. The window's
 *  slide uses a cubic ease, and a cubic ease on the opacity too front-loads the fade (an arriving menu is 87%
 *  there halfway through; a leaving one is still 87% there halfway through and only vanishes in its last
 *  third), so it reads as a pop. The cube root undoes the cubic, which leaves a plain linear fade. */
inline float alphaAt(Phase phase, float eased) noexcept {
    const float e = juce::jlimit(0.0f, 1.0f, eased);
    return phase == Phase::In ? 1.0f - std::cbrt(1.0f - e) : 1.0f - std::cbrt(e);
}

/** The window's state when the phase's eased progress is `eased` (0 at the start of the phase, 1 at
 *  its end). In: alpha 0 -> 1, offset -dir * 4 px -> 0. Out: alpha 1 -> 0, offset 0 -> -dir * 2 px.
 *  `dir` is slideDirection(); Reduce motion keeps the alpha and drops the offset. */
inline Frame frameAt(Phase phase, float eased, juce::Point<int> dir, bool reduceMotion,
                     const Style& style = {}) noexcept {
    Frame f;
    f.alpha = alphaAt(phase, eased);
    if (!reduceMotion) {
        const float distance = phase == Phase::In ? -style.inSlidePx * (1.0f - eased) : -style.outSlidePx * eased;
        f.offset = {(float)dir.x * distance, (float)dir.y * distance};
    }
    return f;
}

} // namespace synth::ui::popup_motion

namespace synth::ui {

// ============================================================================
// Section 2 -- the engine
// ============================================================================

class PopupMotion {
public:
    /** Make `window` (a top-level window, not a child component) animate every time it is shown and
     *  hidden. Safe to call many times: only the first does anything. The listener it installs
     *  deletes itself with the window. A window with no native peer, or any window while the
     *  engine is disabled, is left exactly as JUCE would show it. */
    static void attach(juce::Component& window, popup_motion::Style style = {});

    /** Master switch (default on). Off: attached windows show and hide instantly. */
    static void setEnabled(bool enabled);
    static bool isEnabled();

    /** Close `window` softly: fade the LIVE window out (the leaving half of the table in
     *  docs/layout/animation.md, 80 ms plain fade under Reduce motion), then run `reallyClose`
     *  one message-loop turn later. Use it for every close the app itself decides (Escape, a
     *  button, a pick, a programmatic close) instead of closing directly; JUCE's own closes
     *  (click-away, a menu choice, the title-bar button) are picked up by the leaving picture.
     *
     *  `reallyClose` runs at once, before this returns, when the window has no native peer, was
     *  never attached, is not showing or the engine is disabled (every headless run), so a test
     *  sees the final state immediately. A second dismiss while one is running is ignored.
     *  While the fade runs the window ignores the mouse, so it cannot be acted on twice. */
    static void dismiss(juce::Component& window, std::function<void()> reallyClose);

    /** dismiss() whose close is `window.exitModalState(0)`, for a modal window launched with
     *  LaunchOptions (the window is deleted by the modal manager). */
    static void dismissModal(juce::Component& window);

    /** dismiss() whose close is `box.dismiss()` (a call-out popover). */
    static void dismissCallOut(juce::CallOutBox& box);

    /** True from dismiss() until its `reallyClose` has run. */
    static bool isDismissing(const juce::Component& window);

    /** How many leaving pictures are on screen right now (a window JUCE hid or deleted that is still
     *  fading). For tests. */
    static int getNumLeavingGhosts();

    /** Test seam: lets dismiss() animate a window that has no native peer (the tween then ends on
     *  its watchdog timer, since no VBlank ever arrives). Default off. */
    static void setAnimateOffScreenForTest(bool animate);

    /** Open a DialogWindow from `options` and attach. Use instead of options.launchAsync(). */
    static juce::DialogWindow* launchDialog(juce::DialogWindow::LaunchOptions& options) {
        auto* dialog = options.launchAsync();
        if (dialog != nullptr)
            attach(*dialog);
        return dialog;
    }
};

} // namespace synth::ui
