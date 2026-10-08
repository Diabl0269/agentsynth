#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"

// Lets a headless test run the animated path of FadeVisibility (no VBlank reaches an off-screen component, so
// the test steps the fades by hand with FadeVisibility::stepAllForTest) and restores the defaults on exit.
struct FadeAnimateGuard {
    explicit FadeAnimateGuard(synth::ui::AnimationMode mode = synth::ui::AnimationMode::full) {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
        synth::ui::setAnimationMode(mode);
    }
    ~FadeAnimateGuard() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    }
};

// Whether the component itself takes mouse clicks (the "self" half of Component::getInterceptsMouseClicks).
inline bool interceptsClicks(const juce::Component& c) {
    bool self = false;
    bool kids = false;
    c.getInterceptsMouseClicks(self, kids);
    return self;
}
