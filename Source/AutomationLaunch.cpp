// Concern: FRO29 automation-launch detection -- a pure, headless-testable helper so
// MainComponent's decision reduces to one function call instead of duplicating the
// flag/env-var parsing at every call site.

#include "AutomationLaunch.h"

namespace synth {

bool isNoAudioDeviceLaunch(const juce::StringArray& commandLineArgs, const juce::String& envValue) {
    if (commandLineArgs.contains(kNoAudioDeviceFlag))
        return true;

    const juce::String trimmed = envValue.trim();
    return trimmed.equalsIgnoreCase("1") || trimmed.equalsIgnoreCase("true") || trimmed.equalsIgnoreCase("yes");
}

} // namespace synth
