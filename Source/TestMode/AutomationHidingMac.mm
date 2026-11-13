// Concern: macOS process settings for a hidden automation run -- no Dock icon, no activation, no App Nap.

#include "TestMode/AutomationHiding.h"
#include <juce_core/juce_core.h>

#if JUCE_MAC

#import <AppKit/AppKit.h>

namespace synth::testmode {

// Accessory policy: the process shows no Dock icon or menu bar and macOS never makes it the active app on its own, so
// nothing it does can pull focus from what the user is working in. The activity assertion is held for the process
// lifetime: a process whose windows are all invisible is a candidate for App Nap, which would stretch timers and
// message delivery and slow every scripted step.
void applyHiddenProcessSettingsMac() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    static id activity = [[NSProcessInfo processInfo]
        beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                          reason:@"Hidden automation run"];
    (void)activity;
}

} // namespace synth::testmode

#endif
