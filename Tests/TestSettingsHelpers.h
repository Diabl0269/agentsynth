#pragma once

// TestSettingsHelpers.h -- the one place a test reaches the REAL on-disk "Agent Synth" settings
// file (FRO58; header-only, not registered in Tests/CMakeLists.txt, included relatively like
// TestAudioHelpers.h).
//
// A headless MainComponent has no per-test ApplicationProperties seam: it opens the same settings
// file every shipped build and every other MainComponent test in this process reads (see
// synth::userSettingsOptions() and ChannelFlowTestFixture.h's "settings-file hygiene" comment).
// Any test that persists something -- a snap division, a panel's visibility, a detached window's
// bounds -- therefore leaks it into every later test and into the developer's own preferences.
// Seven test files carried byte-identical private copies of the two helpers below (FRO56/FRO57);
// this header replaces them so the Options can never drift from the production ones again: the
// old copies re-hardcoded the application and folder name, which a product rename would have
// silently split into a second settings file.
//
// Per-KEY reset guards that hard-reset a documented default instead of restoring the developer's
// value (BottomDockActiveTabResetGuard.h, ChannelFlowTestFixture.h's resetKeys()) stay where they
// are; they build on userSettingsTestOptions() from here.

#include "UserSettings.h"
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <utility>
#include <vector>

namespace synth::test {

/** The production Options, so a test opens exactly the file MainComponent opens -- never a copy. */
inline juce::PropertiesFile::Options userSettingsTestOptions() { return synth::userSettingsOptions(); }

/** Saves the named settings keys on construction and restores them EXACTLY on destruction,
 *  including the case where a key did not exist at all.
 *
 *  Clearing a key afterwards would not be enough: it would silently change the developer's own
 *  preferences, so the original values go back. Both the read and the write use their own
 *  short-lived juce::ApplicationProperties: a PropertiesFile saves its WHOLE in-memory property set,
 *  so a long-lived instance held across the test would write back a snapshot taken before the test
 *  and clobber every unrelated key the test happened to touch. */
class PersistedKeysGuard {
public:
    explicit PersistedKeysGuard(juce::StringArray keys) {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        for (const auto& key : keys) {
            std::optional<juce::String> value;
            if (settings != nullptr && settings->containsKey(key))
                value = settings->getValue(key);
            saved_.emplace_back(key, value);
        }
    }

    ~PersistedKeysGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        if (settings == nullptr)
            return;
        for (const auto& [key, value] : saved_) {
            if (value.has_value())
                settings->setValue(key, *value);
            else
                settings->removeValue(key);
        }
        settings->saveIfNeeded();
    }

    PersistedKeysGuard(const PersistedKeysGuard&) = delete;
    PersistedKeysGuard& operator=(const PersistedKeysGuard&) = delete;

private:
    std::vector<std::pair<juce::String, std::optional<juce::String>>> saved_;
};

} // namespace synth::test
