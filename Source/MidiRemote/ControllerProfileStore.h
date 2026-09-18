#pragma once

// App-layer, NOT Core: resolving "where do controller profiles live" reaches into
// synth::userSettingsOptions() (Source/UserSettings.h) for the settings folder location, and Core
// must never depend on where settings live (Source/CLAUDE.md's Core-layering rule; see
// docs/control/midi-remote.md#persistence-and-the-trust-boundary's "Core never touches juce::ApplicationProperties"
// line). This store is injected into whatever app-layer owner needs it (MainComponent) and lives under
// Source/MidiRemote/ purely because it is RemoteModel.h's one and only file store, not because it
// is registered in the Core CMake target — it is built into AppUI instead.

#include "MidiRemote/RemoteModel.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/**
 * @class ControllerProfileStore
 * @brief One JSON file per ControllerProfile, under `<settings folder>/MidiRemote/Controllers/
 *        <profileId>.json` (docs/control/midi-remote.md#persistence-and-the-trust-boundary).
 *
 * The root directory defaults to the real, resolved settings folder (see
 * resolveDefaultControllersDirectory()) but is injectable via the constructor so tests can point
 * an instance at a temp directory instead. This class never constructs or opens a
 * juce::PropertiesFile/juce::ApplicationProperties itself, even for the default constructor.
 */
class ControllerProfileStore {
public:
    /** Uses the real, resolved `<settings folder>/MidiRemote/Controllers` directory. */
    ControllerProfileStore();

    /** Test/injection constructor: `controllersDir` is used verbatim (created on demand by
     *  save()/importProfile()), so a test can point this at a temp directory. */
    explicit ControllerProfileStore(juce::File controllersDir);

    /** `<settings folder>/MidiRemote/Controllers`, computed the same way
     *  Source/UserSettings.h's userSettingsOptions() resolves the settings folder itself
     *  (userSettingsOptions().getDefaultFile().getParentDirectory()) — pure path arithmetic, no
     *  file I/O and no juce::PropertiesFile construction. */
    static juce::File resolveDefaultControllersDirectory();

    const juce::File& getControllersDirectory() const { return controllersDir_; }

    /** Result of loadAll(): every profile that parsed, plus the file names (not full paths) of
     *  any `*.json` file that did NOT parse — a malformed/hand-edited profile is skipped, never
     *  fatal to the rest of the load. */
    struct LoadAllResult {
        std::vector<ControllerProfile> profiles;
        std::vector<juce::String> skippedFiles;
    };

    /** Scans the Controllers/ directory for `*.json` files and parses each via
     *  ControllerProfile::fromVar. Returns an empty result (no profiles, no skipped files) if the
     *  directory doesn't exist yet — that is the normal "no controllers configured" state, not an
     *  error. */
    LoadAllResult loadAll() const;

    /** Writes/overwrites `<profile.id>.json`, creating the Controllers/ directory (and its
     *  parents) first if needed. Fails (returns false) if `profile.id` is empty. */
    bool save(const ControllerProfile& profile) const;

    /** Plain file copy of `<profileId>.json` to `destFile`
     * (docs/control/midi-remote.md#persistence-and-the-trust-boundary: "Export ... is a file copy"). Fails if the
     * profile isn't in this store. */
    bool exportProfile(const juce::String& profileId, const juce::File& destFile) const;

    /** Plain file copy FROM `srcFile` into this store, keyed by the id INSIDE the file (parsed via
     *  ControllerProfile::fromVar), never by srcFile's own name — the destination is always
     *  `<parsedId>.json`, matching loadAll()'s own convention.
     *
     *  Design decision (docs/control/midi-remote.md#persistence-and-the-trust-boundary says only "a name check",
     * underspecified beyond that): refuses — returns false, copies nothing — if a profile with that id ALREADY EXISTS
     *  in this store. Since the store's filename IS the profile id (`<id>.json`), an id conflict
     *  and a destination-filename conflict are the same check by construction; this is the "name
     *  check" the doc means. On success, `outProfile` receives the parsed profile that was
     *  imported (so a caller doesn't have to loadAll() again just to see what arrived). */
    bool importProfile(const juce::File& srcFile, ControllerProfile& outProfile) const;

private:
    juce::File controllersDir_;
};

} // namespace synth
