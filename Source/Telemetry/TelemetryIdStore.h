#pragma once

#include <juce_core/juce_core.h>

namespace synth::telemetry {

/**
 * @class TelemetryIdStore
 * @brief The random id usage statistics are filed under, kept in a plain file.
 *
 * It exists only while the user has opted in: create() makes it when the toggle is switched on and
 * erase() deletes it when the toggle is switched off. It is never the device id (DeviceIdStore) and
 * is never sent with an account token, so a summary cannot be joined to an install or an account.
 * Docs: docs/development/usage-statistics.md.
 */
class TelemetryIdStore {
public:
    /** Stores the id as `telemetry_id` under synth::userSettingsRootDirectory(). */
    TelemetryIdStore();

    /** Test use: reads and writes exactly this file. */
    explicit TelemetryIdStore(juce::File idFile);

    /** The stored id, or an empty string when there is none or the file does not hold a plausible id. */
    juce::String load() const;

    /** Writes a fresh UUID (dashed, lower case) and returns it. Replaces any id already there. */
    juce::String create() const;

    /** Deletes the file. Safe when there is none. */
    void erase() const;

    juce::File getFile() const { return file; }

private:
    juce::File file;
};

} // namespace synth::telemetry
