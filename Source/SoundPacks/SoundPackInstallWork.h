#pragma once

// The blocking steps of one pack install, run on a worker thread: download, checksum, unpack, check, swap in.

#include "SoundPacks/SoundPackFetch.h"
#include "SoundPacks/SoundPackIndex.h"
#include <atomic>
#include <functional>

namespace synth::soundpacks {

enum class InstallPhase { Downloading, Verifying, Installing };

struct InstallRequest {
    IndexPack pack;
    juce::File root; ///< The folder the pack folder lands in.
    Fetcher fetch;
};

struct InstallHooks {
    FetchProgress progress;
    std::function<void(InstallPhase)> phase;
    const std::atomic<bool>* cancel = nullptr;
};

struct InstallOutcome {
    bool ok = false;
    bool cancelled = false;
    juce::String error; ///< Plain words, set when !ok and not cancelled.
};

/** Installs `request.pack` into `request.root`/<id>. Either the whole new pack folder is in place afterwards (an
 *  older one replaced), or nothing under `root` changed: the work happens in a staging folder that is always removed.
 */
InstallOutcome runInstall(const InstallRequest& request, const InstallHooks& hooks);

/** True for a zip entry name that stays inside `<packId>/`: relative, no "..", no backslashes or drive letters. */
bool isSafeEntryName(const juce::String& name, const juce::String& packId);

} // namespace synth::soundpacks
