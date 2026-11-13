#include "TelemetryIdStore.h"
#include "UserSettings.h"

// Concern: the on-disk usage statistics id. A file that does not hold a canonical UUID is treated as
// absent, so a corrupted id can never be sent (the server rejects anything that is not a UUID).

namespace synth::telemetry {

namespace {

bool isCanonicalUuid(const juce::String& value) {
    if (value.length() != 36)
        return false;
    for (int i = 0; i < 36; ++i) {
        const auto c = value[i];
        const bool dashSlot = i == 8 || i == 13 || i == 18 || i == 23;
        if (dashSlot) {
            if (c != '-')
                return false;
        } else if (juce::CharacterFunctions::getHexDigitValue(c) < 0) {
            return false;
        }
    }
    return true;
}

} // namespace

TelemetryIdStore::TelemetryIdStore()
    : file(synth::userSettingsRootDirectory().getChildFile("telemetry_id")) {}

TelemetryIdStore::TelemetryIdStore(juce::File idFile)
    : file(std::move(idFile)) {}

juce::String TelemetryIdStore::load() const {
    if (!file.existsAsFile())
        return {};
    const auto text = file.loadFileAsString().trim();
    return isCanonicalUuid(text) ? text.toLowerCase() : juce::String();
}

juce::String TelemetryIdStore::create() const {
    const auto id = juce::Uuid().toDashedString();
    const auto parent = file.getParentDirectory();
    if (!parent.exists())
        parent.createDirectory();
    file.replaceWithText(id);
    return id;
}

void TelemetryIdStore::erase() const { file.deleteFile(); }

} // namespace synth::telemetry
