// Concern: the FRO27 fix -- stripping an input device name off a device-state XML that never
// actually enabled input channels.

#include "DeviceStateInputs.h"

namespace synth {

bool deviceStateEnablesInput(const juce::XmlElement& deviceSetup) {
    return deviceSetup.getStringAttribute("audioDeviceInChans").containsChar('1');
}

// juce::AudioDeviceManager::updateXml() (juce_AudioDeviceManager.cpp) always writes
// audioInputDeviceName = currentSetup.inputDeviceName, and JUCE's own device-selection logic
// (insertDefaultDeviceNames) can fill inputDeviceName with a default input device on paths that
// never asked for one -- picking the system default mic purely because SOME name has to go in
// that field. deviceManager.initialise(0, 2, savedState, true) then opens whatever device that
// name points at, which is how an output-only user ends up with a live microphone (and the macOS
// TCC prompt/indicator) on their next launch. Since every caller in this codebase always requests
// 0 input channels (Source/CLAUDE.md), an input device name only matters when the state's own
// channel mask says input is actually on -- so an output-only state can simply never carry one.
//
// The legacy "audioDeviceName" attribute (predating separate in/out names -- initialiseFromXML
// still reads it as BOTH the input and output device name when present) gets the same treatment:
// its value is exactly as likely to be a stale input-capable interface name, so it is folded into
// audioOutputDeviceName (only if that is not already set to something) and then dropped, rather
// than left behind to feed the legacy branch on the next load.
void stripUnusedInputDevice(juce::XmlElement& deviceSetup) {
    if (deviceStateEnablesInput(deviceSetup))
        return;

    deviceSetup.removeAttribute("audioInputDeviceName");

    if (deviceSetup.hasAttribute("audioDeviceName")) {
        if (deviceSetup.getStringAttribute("audioOutputDeviceName").isEmpty())
            deviceSetup.setAttribute("audioOutputDeviceName", deviceSetup.getStringAttribute("audioDeviceName"));
        deviceSetup.removeAttribute("audioDeviceName");
    }
}

} // namespace synth
