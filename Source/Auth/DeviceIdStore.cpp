#include "DeviceIdStore.h"
#include "../Branding.h"

namespace synth {

namespace {

/** A plausible device id: a juce::Uuid rendered via toDashedString() is 36 characters
    (8-4-4-4-12 hex groups joined by hyphens). Loosely validated by shape rather than a strict
    parse — the goal is "does this look like a real id, or is it empty/truncated/binary garbage
    left behind by a crash or a hand-edited file", not cryptographic verification. Anything that
    doesn't pass just means a fresh id gets generated, which is harmless (see class doc comment). */
bool isPlausibleDeviceId(const juce::String& value) {
    if (value.length() < 32 || value.length() > 36)
        return false;

    int hexCount = 0;
    for (auto character : value) {
        const bool isHex = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                           (character >= 'A' && character <= 'F');
        if (isHex) {
            ++hexCount;
        } else if (character != '-') {
            return false;
        }
    }

    return hexCount >= 32;
}

juce::File defaultDeviceIdFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(synth::branding::kSettingsFolderName)
        .getChildFile("device_id");
}

} // namespace

DeviceIdStore::DeviceIdStore()
    : file(defaultDeviceIdFile())
    , deviceId(loadOrCreate()) {}

DeviceIdStore::DeviceIdStore(juce::File idFile)
    : file(std::move(idFile))
    , deviceId(loadOrCreate()) {}

juce::String DeviceIdStore::loadOrCreate() const {
    const auto existing = file.existsAsFile() ? file.loadFileAsString().trim() : juce::String();
    if (isPlausibleDeviceId(existing))
        return existing;

    // Missing, empty, or corrupted: generate a fresh id and persist it so subsequent runs (and
    // subsequent DeviceIdStore instances pointed at the same file) see the same value from here on.
    const auto freshId = juce::Uuid().toDashedString();

    const auto parent = file.getParentDirectory();
    if (!parent.exists())
        parent.createDirectory();
    file.replaceWithText(freshId);

    return freshId;
}

} // namespace synth
