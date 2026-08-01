#pragma once

// ============================================================================
// UIAnimation.h — Shared animation utilities for Gravisynth UI Phase 5
//
// Namespace: synth::ui
//
// Three sections:
//   1. Pure easing math   — <cmath> only, headless-testable, no JUCE/GUI deps.
//   2. AnimationDriver    — time-bounded 0→1 tween built on juce_animation.
//   3. formatShortcutHint — pure string helper, headless-testable.
//
// Section 1 and 3 are #include-able from headless (non-GUI) translation units.
// Section 2 requires juce_animation + juce_gui_basics (VBlankAttachment).
// ============================================================================

#include <cmath>
#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// ============================================================================
// Section 1 — Pure easing functions
//   Input t in [0, 1].  f(0) == 0, f(1) == 1.
//   All are constexpr-friendly (no external state).
// ============================================================================

/** Cubic ease-out: starts fast, decelerates to rest. */
inline float easeOutCubic(float t) noexcept {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

/** Cubic ease-in-out: slow start, fast middle, slow finish. */
inline float easeInOutCubic(float t) noexcept {
    if (t < 0.5f)
        return 4.0f * t * t * t;
    const float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}

/** Ease-out-back: overshoots slightly then settles (c1 ≈ 1.70158). */
inline float easeOutBack(float t) noexcept {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// ============================================================================
// Section 2 — AnimationDriver
//
// Drives a normalised value 0→1 over a given duration (ms) with a caller-
// supplied easing, calling onUpdate(easedValue) each VBlank frame and
// onComplete() once when the animation finishes.
//
// AUTO-STOPS when progress reaches 1.0 — never loops.
//
// OWNERSHIP REQUIREMENT (CRITICAL):
//   The caller MUST keep the AnimationDriver instance alive for the entire
//   duration of the animation (it owns the juce::Animator via shared_ptr).
//   Storing it as a class member is the correct pattern.
//
// VBLANK REQUIREMENT:
//   The caller MUST also keep a juce::VBlankAnimatorUpdater alive that is
//   associated with a visible Component.  AnimationDriver::start() registers
//   the animator with the provided updater.
//
// Minimal caller member declarations:
//
//     juce::VBlankAnimatorUpdater vblankUpdater { this };   // 'this' = a juce::Component
//     synth::ui::AnimationDriver someAnim;
//
// Usage:
//
//     someAnim.start(vblankUpdater, 300.0,
//                    synth::ui::easeOutCubic,
//                    [this](float t) { setAlpha(t); },
//                    [this]()        { /* done */ });
//
// To animate a Component's bounds between two Rectangles, use the static helper:
//
//     auto lerped = AnimationDriver::lerpBounds(from, to, t);
//     myComponent.setBounds(lerped);
//
// ============================================================================

class AnimationDriver {
public:
    AnimationDriver() = default;

    // Non-copyable: the animator captures lambdas that reference 'this' indirectly.
    AnimationDriver(const AnimationDriver&) = delete;
    AnimationDriver& operator=(const AnimationDriver&) = delete;

    // Movable.
    AnimationDriver(AnimationDriver&&) = default;
    AnimationDriver& operator=(AnimationDriver&&) = default;

    ~AnimationDriver() = default;

    // -------------------------------------------------------------------------
    /** Start a new time-bounded animation.
     *
     *  @param updater          A VBlankAnimatorUpdater already attached to a
     *                          visible Component (must outlive the animation).
     *  @param durationMs       Total animation duration in milliseconds (> 0).
     *  @param easingFn         A function float(float) mapping [0,1]→~[0,1].
     *                          Use any of the free functions above, or a lambda.
     *  @param onUpdate         Called each frame with the eased progress value.
     *                          MUST be safe to call on the message thread.
     *  @param onComplete       Optional; called once when progress reaches 1.0.
     *
     *  Calling start() while an animation is already running replaces it; the
     *  old animator is removed from the updater before the new one is added.
     */
    void start(juce::VBlankAnimatorUpdater& updater, double durationMs, std::function<float(float)> easingFn,
               std::function<void(float)> onUpdate, std::function<void()> onComplete = {}) {
        // Stop / remove previous animation if any.
        stop(updater);

        animator = juce::ValueAnimatorBuilder{}
                       .withDurationMs(durationMs)
                       .withEasing(std::move(easingFn))
                       .withValueChangedCallback(std::move(onUpdate))
                       .withOnCompleteCallback(std::move(onComplete))
                       .build();

        updater.addAnimator(*animator);
        animator->start();
    }

    // -------------------------------------------------------------------------
    /** Immediately stop a running animation and remove it from the updater.
     *  Safe to call even if no animation is in progress.
     */
    void stop(juce::VBlankAnimatorUpdater& updater) {
        if (animator.has_value()) {
            updater.removeAnimator(*animator);
            animator.reset();
        }
    }

    /** Returns true if an animation is currently in progress. */
    bool isRunning() const noexcept { return animator.has_value(); }

    // -------------------------------------------------------------------------
    /** Linearly interpolate between two rectangles by normalised t ∈ [0,1].
     *
     *  Intended for use inside the onUpdate callback to tween a Component's
     *  bounds.  The caller then calls component.setBounds(lerped).
     *
     *  Example:
     *      driver.start(updater, 250.0, easeOutCubic,
     *          [this, from = getBounds(), to = targetBounds](float t) {
     *              setBounds(AnimationDriver::lerpBounds(from, to, t));
     *          });
     */
    static juce::Rectangle<int> lerpBounds(juce::Rectangle<int> from, juce::Rectangle<int> to, float t) noexcept {
        auto lerp = [t](int a, int b) -> int {
            return a + static_cast<int>(std::round(static_cast<float>(b - a) * t));
        };
        return {lerp(from.getX(), to.getX()), lerp(from.getY(), to.getY()), lerp(from.getWidth(), to.getWidth()),
                lerp(from.getHeight(), to.getHeight())};
    }

private:
    // The Animator is held as optional so we can cheaply test "is active".
    // The shared_ptr inside Animator keeps the underlying impl alive.
    std::optional<juce::Animator> animator;
};

// ============================================================================
// Section 3 — formatShortcutHint
//   Pure, no JUCE/GUI deps (only juce::String).
// ============================================================================

/** Returns a tooltip string combining a description and an optional shortcut.
 *
 *  - If shortcutDisplay is empty, returns base unchanged.
 *  - Otherwise returns  base + "  (" + shortcutDisplay + ")"
 *
 *  Example: formatShortcutHint("Save", "Cmd+S") -> "Save  (Cmd+S)"
 *           formatShortcutHint("Save", "")       -> "Save"
 */
inline juce::String formatShortcutHint(const juce::String& base, const juce::String& shortcutDisplay) {
    if (shortcutDisplay.isEmpty())
        return base;
    return base + "  (" + shortcutDisplay + ")";
}

} // namespace synth::ui
