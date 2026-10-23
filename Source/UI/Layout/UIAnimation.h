#pragma once

// ============================================================================
// UIAnimation.h — Shared animation utilities for the app UI Phase 5
//
// Namespace: synth::ui
//
// Four sections:
//   1. Pure easing math   — <cmath> only, headless-testable, no JUCE/GUI deps.
//   2. AnimationDriver    — time-bounded 0→1 tween built on juce_animation.
//   3. PanelSlide         — one sliding panel's [0..1] open fraction (pure state).
//   4. formatShortcutHint — pure string helper, headless-testable.
//
// Sections 1, 3 and 4 are #include-able from headless (non-GUI) translation units.
// Section 2 requires juce_animation + juce_gui_basics (VBlankAttachment).
// ============================================================================

#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

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

/** Cubic ease-in: starts slow, accelerates away -- for a thing leaving or being sent back. */
inline float easeInCubic(float t) noexcept { return t * t * t; }

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

/** Ease-out-back with a gentle overshoot: peaks at 1.08 (8% past the end), then settles to 1. Things the
 *  user adds grow in with it. */
inline float easeOutBackGrow(float t) noexcept {
    constexpr float c1 = 1.5f;
    const float u = t - 1.0f;
    return 1.0f + (c1 + 1.0f) * u * u * u + c1 * u * u;
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
        testClockStart.reset();

        // Animations: Off lands on the final frame at once; nothing is left running.
        if (animationsOff()) {
            if (onUpdate)
                onUpdate(easingFn ? easingFn(1.0f) : 1.0f);
            if (onComplete)
                onComplete();
            return;
        }

        run = std::make_shared<Run>();
        run->updater = &updater;
        run->durationMs = durationMs;
        run->easing = easingFn ? std::move(easingFn) : [](float t) { return t; };
        run->onUpdate = std::move(onUpdate);
        run->onComplete = std::move(onComplete);
        run->launch();
    }

    // -------------------------------------------------------------------------
    /** Immediately stop a running animation and remove it from the updater.
     *  Safe to call even if no animation is in progress.
     */
    void stop(juce::VBlankAnimatorUpdater& updater) {
        if (run != nullptr) {
            ++run->generation;
            if (run->animator.has_value())
                updater.removeAnimator(*run->animator);
            run.reset();
        }
    }

    /** Returns true if an animation is currently in progress. A finished one is not: the animator stays held after its
     *  last frame, and a layout pass that defers to "the running tween" must not wait on one that already landed. */
    bool isRunning() const noexcept {
        return run != nullptr && run->animator.has_value() && !run->animator->isComplete();
    }

    /** Test seam: lands a running animation on its final frame and runs its complete callback, standing in for the
     *  VBlank frames that never reach an off-screen component. A no-op when nothing runs. */
    void completeForTest() {
        if (run == nullptr || !run->animator.has_value())
            return;
        run->testClock = true; // synthetic frame times: the stall guard must not mistake the jump for a stall
        const auto keepAlive = run;
        const juce::Animator current = *run->animator;
        const double now = juce::Time::getMillisecondCounterHiRes();
        current.update(now);                                 // the frame that starts it
        current.update(now + current.getDurationMs() + 1.0); // and the one past its end
    }

    /** Test seam: advances a running animation to `ms` after its first frame, standing in for the VBlank frames that
     *  never reach an off-screen component, so a test can read the values between the start and the end. The first
     *  call starts the clock; later calls move it. A no-op when nothing runs. After a stall restart (see
     *  `stallGuardForTest`) the clock starts again at the next call, so `ms` counts from the restart. */
    void stepForTest(double ms) {
        if (run == nullptr || !run->animator.has_value())
            return;
        run->testClock = true;
        const auto generationBefore = run->generation;
        const auto keepAlive = run; // a stall restart replaces the animator inside its own update()
        const juce::Animator current = *run->animator;
        if (!testClockStart.has_value()) {
            testClockStart = juce::Time::getMillisecondCounterHiRes();
            current.update(*testClockStart); // the frame that starts it
        }
        current.update(*testClockStart + ms);
        if (run != nullptr && run->generation != generationBefore)
            testClockStart.reset();
    }

    /** Test seam: makes the stall guard act on stepForTest's synthetic frame times too (it ignores them otherwise, so
     *  a test may jump the clock). Call after start(). */
    void stallGuardForTest(bool on) noexcept {
        if (run != nullptr)
            run->guardInTest = on;
    }

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
    // One running animation. Frames come from a VBlank clock, and a message-thread stall (a graph rebuild, a plugin
    // load) lets the next frame arrive far past the last. juce::Animator would answer with a jump to the end, so a
    // frame that arrives later than kStallMs after the previous one moves the animation by one nominal frame instead
    // and the rest of it is re-launched from there: the motion is still seen, it just ends later.
    struct Run : std::enable_shared_from_this<Run> {
        static constexpr double kStallMs = 80.0;
        static constexpr double kFrameMs = 16.0;

        juce::VBlankAnimatorUpdater* updater = nullptr;
        double durationMs = 0.0;
        std::function<float(float)> easing;
        std::function<void(float)> onUpdate;
        std::function<void()> onComplete;
        std::optional<juce::Animator> animator;
        double baseProgress = 0.0; // overall progress at which the current animator began
        double lastProgress = 0.0; // overall progress applied by the last frame
        bool hasFrame = false;     // a frame has been applied
        std::uint64_t generation = 0;
        bool testClock = false;   // frames come from stepForTest
        bool guardInTest = false; // act on stepForTest's frames too

        void launch() {
            const double remaining = durationMs * (1.0 - baseProgress);
            const auto gen = generation;
            // Weak, so the animator does not own the run; locked per call so a callback that stops or restarts the
            // driver cannot free the run (and the very function it is executing) under itself.
            const std::weak_ptr<Run> weak = weak_from_this();
            animator = juce::ValueAnimatorBuilder{}
                           .withDurationMs(std::max(remaining, 1.0))
                           .withEasing([](float t) { return t; })
                           .withValueChangedCallback([weak, gen](float p) {
                               if (const auto self = weak.lock())
                                   self->onFrame(p, gen);
                           })
                           .withOnCompleteCallback([weak, gen] {
                               if (const auto self = weak.lock();
                                   self != nullptr && gen == self->generation && self->onComplete)
                                   self->onComplete();
                           })
                           .build();
            updater->addAnimator(*animator);
            animator->start();
        }

        void onFrame(float p, std::uint64_t gen) {
            if (gen != generation)
                return;
            double progress = baseProgress + static_cast<double>(p) * (1.0 - baseProgress);
            const bool guardOn = !testClock || guardInTest;
            const bool stalled = guardOn && hasFrame && (progress - lastProgress) * durationMs > kStallMs;
            if (stalled) {
                progress = lastProgress + kFrameMs / durationMs;
                baseProgress = progress;
                ++generation;
                updater->removeAnimator(*animator);
                launch();
            }
            lastProgress = progress;
            hasFrame = true;
            if (onUpdate)
                onUpdate(easing(static_cast<float>(progress)));
        }
    };

    std::shared_ptr<Run> run;
    std::optional<double> testClockStart; // stepForTest's clock; reset with each animation
};

// ============================================================================
// Section 3 — PanelSlide
//
// The [0..1] open fraction of ONE sliding panel, plus the from/to snapshot of
// the tween currently moving it.  Pure float state: no animator, no VBlank, no
// GUI — which is the point twice over.
//
//   * The owner's layout derives the panel's GEOMETRY from the fraction rather
//     than from its visible/hidden bool, so a layout pass lands correctly
//     whenever it runs (window resize, theme change, a drag mid-slide) and a
//     toggle only has to move the fraction and re-lay-out.  Flipping the bool
//     and laying out the FINAL bounds before starting a tween from the old ones
//     is what makes a panel appear open for a frame and then yank back — the
//     "jump" this type exists to remove.
//   * Holding no animator means a window with several sliding panels drives ALL
//     of them from ONE AnimationDriver, instead of racing one animator per
//     panel and freezing whichever slide the next toggle didn't mention.
//
// Owner shape (see MainComponent::beginPanelSlide for the real one):
//
//     synth::ui::PanelSlide librarySlide_, aiSlide_;      // members
//     synth::ui::AnimationDriver panelSlideAnim_;
//
//     void resized() override {
//         const int libW = librarySlide_.sizeBetween(0, kLibraryWidth);  // fraction -> px
//         ...
//     }
//
//     void togglePanel() {
//         showing_ = ! showing_;
//         if (! librarySlide_.retarget(showing_ ? 1.0f : 0.0f, isShowing())) {
//             resized();   // snapped: no VBlank to wait for, land NOW
//             return;
//         }
//         resized();       // frame 0, before the first VBlank can paint stale bounds
//         panelSlideAnim_.start(updater, 190.0, easeInOutCubic,
//                               [this](float t) { librarySlide_.applyTweenAt(t); resized(); },
//                               [this] { librarySlide_.finish(); resized(); });
//     }
// ============================================================================

class PanelSlide {
public:
    /** 0 = fully closed, 1 = fully open.  The only value layout may read. */
    float getProgress() const noexcept { return progress_; }

    /** Land on `target` with no tween — a restore from settings, or a test seam standing in for
     *  the VBlank frame that would have left the slide here. */
    void snapTo(float target) noexcept {
        progress_ = from_ = to_ = clamp(target);
        tweening_ = false;
    }

    /** Point the slide at `target`, starting from WHEREVER it is right now — a mid-flight
     *  re-toggle reverses from there rather than jumping to an extreme first.
     *
     *  @returns true when the caller should start (or restart) its animation driver; false when
     *           the slide has ALREADY landed on the target, either because `canAnimate` is false
     *           (no VBlank reaches an off-screen component: headless tests, or a restore before
     *           the window exists — both must land synchronously) or because it was there
     *           anyway.
     */
    bool retarget(float target, bool canAnimate) noexcept {
        from_ = progress_;
        to_ = clamp(target);
        tweening_ = canAnimate && from_ != to_;
        if (!tweening_) {
            progress_ = to_;
            return false;
        }
        return true;
    }

    /** Per-frame: `t` is the driver's eased 0→1 progress through the CURRENT tween. */
    void applyTweenAt(float t) noexcept { progress_ = clamp(from_ + (to_ - from_) * t); }

    /** Pin the exact end value.  A driver's last frame is not guaranteed to land on t == 1, so
     *  the completion callback settles the fraction here rather than leaving it a hair short. */
    void finish() noexcept {
        progress_ = to_;
        tweening_ = false;
    }

    /** True while a tween is in flight for THIS panel (the others sharing the driver may be
     *  standing still — their from == to makes applyTweenAt a no-op). */
    bool isMoving() const noexcept { return tweening_; }

    /** The value the current tween is heading for (or resting on). */
    float getTarget() const noexcept { return to_; }

    /** The value the LAST retarget started from — kept after the slide settles, because it is the
     *  anti-jump property in test terms: a mid-flight reversal must show the panel's mid-slide
     *  fraction here, never 0 or 1. */
    float getTweenStart() const noexcept { return from_; }

    /** The interpolated size in px of a panel measuring `closedPx` when shut and `openPx` when
     *  fully open.  Both endpoints are exact: fraction 0 gives `closedPx`, 1 gives `openPx`. */
    int sizeBetween(int closedPx, int openPx) const noexcept {
        return closedPx +
               static_cast<int>(std::lround(static_cast<double>(openPx - closedPx) * static_cast<double>(progress_)));
    }

private:
    static float clamp(float v) noexcept { return juce::jlimit(0.0f, 1.0f, v); }

    float progress_ = 0.0f;
    float from_ = 0.0f;
    float to_ = 0.0f;
    bool tweening_ = false;
};

// ============================================================================
// Section 4 — formatShortcutHint
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
