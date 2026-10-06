// Concern: the reduced-motion answer animations ask, and its test override.
#include "ReducedMotion.h"

#include <juce_core/juce_core.h>

namespace synth::ui {

namespace {
std::optional<bool>& overrideValue() {
    static std::optional<bool> value;
    return value;
}

AnimationMode& modeValue() {
    static AnimationMode mode = AnimationMode::followSystem;
    return mode;
}
} // namespace

AnimationMode animationMode() { return modeValue(); }

void setAnimationMode(AnimationMode mode) { modeValue() = mode; }

juce::String animationModeToString(AnimationMode mode) {
    switch (mode) {
    case AnimationMode::full:
        return "full";
    case AnimationMode::reduced:
        return "reduced";
    case AnimationMode::off:
        return "off";
    case AnimationMode::followSystem:
        break;
    }
    return "follow";
}

AnimationMode animationModeFromString(const juce::String& text) {
    if (text == "full")
        return AnimationMode::full;
    if (text == "reduced")
        return AnimationMode::reduced;
    if (text == "off")
        return AnimationMode::off;
    return AnimationMode::followSystem;
}

bool prefersReducedMotion() {
    switch (modeValue()) {
    case AnimationMode::full:
        return false;
    case AnimationMode::reduced:
    case AnimationMode::off:
        return true;
    case AnimationMode::followSystem:
        break;
    }
    if (const auto forced = overrideValue())
        return *forced;
    return detail::systemPrefersReducedMotion();
}

bool animationsOff() { return modeValue() == AnimationMode::off; }

double motionMs(double full, double reduced) {
    if (animationsOff())
        return 0.0;
    return prefersReducedMotion() ? reduced : full;
}

void setReducedMotionForTest(std::optional<bool> value) { overrideValue() = value; }

#if !JUCE_MAC
namespace detail {
bool systemPrefersReducedMotion() { return false; }
} // namespace detail
#endif

} // namespace synth::ui
