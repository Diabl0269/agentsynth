// Concern: the cross-platform half of hidden automation runs; the macOS process settings are in AutomationHidingMac.mm.

#include "TestMode/AutomationHiding.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_MAC
namespace synth::testmode {
void applyHiddenProcessSettingsMac(); // AutomationHidingMac.mm
}
#endif

namespace synth::testmode {

void hideAutomationWindows() {
    // Only macOS hides anything; Windows and Linux windows behave as in a normal run (docs/development/test-mode.md).
#if JUCE_MAC
    juce::ComponentPeer::hideWindowsForAutomation = true;
    applyHiddenProcessSettingsMac();
#endif
}

bool automationWindowsAreHidden() { return juce::ComponentPeer::hideWindowsForAutomation; }

namespace {
std::optional<bool>& appInFrontOverride() {
    static std::optional<bool> forced;
    return forced;
}
} // namespace

bool appIsInFront() {
    if (appInFrontOverride().has_value())
        return *appInFrontOverride();
    return automationWindowsAreHidden() || juce::Process::isForegroundProcess();
}

void setAppInFrontForTest(std::optional<bool> inFront) { appInFrontOverride() = inFront; }

} // namespace synth::testmode
