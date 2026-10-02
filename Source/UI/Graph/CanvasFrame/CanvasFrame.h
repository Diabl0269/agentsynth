#pragma once

// CanvasFrame.h
//
// The visible, growing frame of the patch canvas. It starts at kStartW x kStartH, keeps kPad canvas px of room past the
// outermost card (rounded up to kStep), and glides to every new size. It only ever grows or shrinks right and down: the
// origin (0,0) is the wall. Pure sizing lives in targetFor(); the animated rect is owned here and the owner repaints on
// onChanged. See CanvasFrame.cpp and docs/layout/layout.md "Canvas frame".

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class CanvasFrame {
public:
    static constexpr int kStartW = 1800;
    static constexpr int kStartH = 1100;
    static constexpr int kPad = 400;
    static constexpr int kStep = 400;
    static constexpr double kGlideMs = 220.0;
    /** Extra content size beyond the target frame, so a fast drag never clips a card. */
    static constexpr int kContentSlack = 2000;

    enum class Mode {
        Animate,  // glide current -> target (grow or shrink)
        GrowOnly, // live drags: the target never gets smaller than it is now
        Snap      // jump, no animation
    };

    explicit CanvasFrame(juce::VBlankAnimatorUpdater& updater);

    /** (0,0,w,h): kPad past the union's right/bottom, rounded up to kStep, never below the start size. Pure. */
    static juce::Rectangle<int> targetFor(juce::Rectangle<int> contentUnion);

    /** Retargets the frame for `contentUnion`. Does nothing (no callback) when the target is unchanged. */
    void update(juce::Rectangle<int> contentUnion, Mode mode);
    /** The next non-GrowOnly update() snaps instead of animating (project open). */
    void requestSnapOnNextUpdate() noexcept { snapPending_ = true; }

    /** The animated rect, canvas coordinates. */
    juce::Rectangle<float> current() const noexcept { return current_; }
    juce::Rectangle<int> target() const noexcept { return target_; }
    bool isAnimating() const noexcept { return driver_.isRunning(); }

    /** Translates the animated rect (a patch slide), keeping a running tween consistent. */
    void shiftCurrentBy(juce::Point<float> d);

    /** Test seam: lands any running tween on the target and fires onChanged. */
    void finishAnimationForTest();

    /** Fired whenever current() changes. */
    std::function<void()> onChanged;

private:
    void snapTo(juce::Rectangle<int> t);
    void glideTo(juce::Rectangle<int> t);
    void notify() const {
        if (onChanged)
            onChanged();
    }

    juce::VBlankAnimatorUpdater& updater_;
    synth::ui::AnimationDriver driver_;
    juce::Rectangle<int> target_{0, 0, kStartW, kStartH};
    juce::Rectangle<float> current_{0.0f, 0.0f, static_cast<float>(kStartW), static_cast<float>(kStartH)};
    juce::Rectangle<float> from_ = current_;
    bool snapPending_ = false;
};
