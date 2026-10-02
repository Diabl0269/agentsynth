// Concern: the reduced-motion answer animations ask, and its test override.
#include "ReducedMotion.h"

#include <juce_core/juce_core.h>

namespace synth::ui {

namespace {
std::optional<bool>& overrideValue() {
    static std::optional<bool> value;
    return value;
}
} // namespace

bool prefersReducedMotion() {
    if (const auto forced = overrideValue())
        return *forced;
    return detail::systemPrefersReducedMotion();
}

void setReducedMotionForTest(std::optional<bool> value) { overrideValue() = value; }

#if !JUCE_MAC
namespace detail {
bool systemPrefersReducedMotion() { return false; }
} // namespace detail
#endif

} // namespace synth::ui
