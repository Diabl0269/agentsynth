// Concern: the macOS half of ReducedMotion.h -- the Accessibility > Display > Reduce motion setting.
#include "ReducedMotion.h"

#include <juce_core/juce_core.h>

#if JUCE_MAC

#import <AppKit/AppKit.h>

namespace synth::ui::detail {

// NSWorkspace publishes the system setting; it is read on each ask, so a change made while the app runs applies to
// the next animation without any notification plumbing.
bool systemPrefersReducedMotion() {
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] == YES;
}

} // namespace synth::ui::detail

#endif
