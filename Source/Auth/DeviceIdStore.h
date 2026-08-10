#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class DeviceIdStore
 * @brief Generates and persists a stable per-install device identifier.
 *
 * The device id is NOT a secret -- it is not tied to account access and grants no capability by
 * itself; it just lets the backend recognize "this install" for an anonymous free-trial tier and
 * as anti-abuse signal once signed in (see docs/AI_Engine.md). That is exactly why it belongs in a
 * plain file rather than the Keychain (contrast with KeychainTokenStore, which stores the refresh
 * token -- a real credential).
 *
 * One id is generated with juce::Uuid on first run and reused for the lifetime of the install.
 * If the backing file is missing, empty, or its contents don't look like a plausible id (wrong
 * shape -- truncated, corrupted, binary garbage), a fresh id is generated and written rather than
 * crashing or leaving the id blank; a lost/corrupted id just means the backend sees this install
 * as new, which is harmless.
 */
class DeviceIdStore {
public:
    /** Production use: stores the id under the app's standard settings folder (same
        userApplicationDataDirectory/<kSettingsFolderName> convention as ThemeManager and
        SnippetManager). */
    DeviceIdStore();

    /** Test use (and any caller that wants an explicit location): reads/writes the id at exactly
        this file, so tests never share a location with (and can never collide with) a real
        install's device id. */
    explicit DeviceIdStore(juce::File idFile);

    /** The device id for this install. Stable across instances constructed against the same file,
        and across process restarts. Never empty. */
    juce::String getDeviceId() const { return deviceId; }

private:
    juce::File file;
    juce::String deviceId;

    /** Reads `file`; if its contents are missing or don't look like a plausible id, generates a
        fresh juce::Uuid, writes it to `file` (creating parent directories as needed), and returns
        that instead. */
    juce::String loadOrCreate() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceIdStore)
};

} // namespace synth
