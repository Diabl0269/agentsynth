#pragma once

#include <juce_core/juce_core.h>

namespace synth {

struct MigrationResult {
    bool migrated;
    juce::String fromName;
    int filesCopied;
};

// Copies user data (settings/presets/themes) forward from the newest existing legacy folder
// into the current one, so renaming the product doesn't orphan existing users' data. Pure and
// side-effect-free beyond the filesystem copy: takes directories as parameters rather than
// resolving them, so callers point it at the real ApplicationProperties parent at startup and
// tests point it at temp dirs. Never overwrites or moves data — see call sites for ordering
// requirements (must run before ApplicationProperties creates the current-name folder).
MigrationResult migrateUserData(const juce::File& parentDir, const juce::String& currentName,
                                const juce::StringArray& legacyNames);

} // namespace synth
