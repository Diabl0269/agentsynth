// Concern: telling a Bluetooth audio device from a wired one, for the Device section's notes.
#include "UI/Settings/AudioSettingsTab/AudioBluetooth.h"

#if JUCE_MAC
#include <CoreAudio/CoreAudio.h>
#include <vector>
#endif

namespace synth::audio {

#if JUCE_MAC
namespace {
juce::String deviceName(AudioObjectID device) {
    AudioObjectPropertyAddress address{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    CFStringRef cf = nullptr;
    UInt32 size = sizeof(cf);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &cf) != noErr || cf == nullptr)
        return {};
    const auto result = juce::String::fromCFString(cf);
    CFRelease(cf);
    return result;
}

bool isBluetoothTransport(AudioObjectID device) {
    AudioObjectPropertyAddress address{kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 transport = 0;
    UInt32 size = sizeof(transport);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &transport) != noErr)
        return false;
    return transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE;
}
} // namespace

bool isBluetoothDevice(const juce::String& name) {
    if (name.isEmpty())
        return false;
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr || size == 0)
        return false;
    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
        return false;
    for (const auto device : devices)
        if (deviceName(device) == name && isBluetoothTransport(device))
            return true;
    return false;
}
#else
bool isBluetoothDevice(const juce::String&) { return false; }
#endif

} // namespace synth::audio
