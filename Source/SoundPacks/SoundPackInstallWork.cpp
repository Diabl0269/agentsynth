#include "SoundPacks/SoundPackInstallWork.h"
#include "SoundPacks/SoundPackChecksum.h"

namespace synth::soundpacks {

namespace {

constexpr juce::int64 kUnpackedSlackBytes = 8 * 1024 * 1024;

InstallOutcome failed(const juce::String& message) {
    InstallOutcome outcome;
    outcome.error = message;
    return outcome;
}

InstallOutcome wasCancelled() {
    InstallOutcome outcome;
    outcome.cancelled = true;
    return outcome;
}

InstallOutcome succeeded() {
    InstallOutcome outcome;
    outcome.ok = true;
    return outcome;
}

juce::String badPackMessage() { return "The download was not a valid sound pack, so nothing was installed."; }

bool isCancelled(const InstallHooks& hooks) { return hooks.cancel != nullptr && hooks.cancel->load(); }

// Deletes the staging folder however the install ends.
struct StagingGuard {
    juce::File dir;
    ~StagingGuard() { dir.deleteRecursively(); }
};

InstallOutcome checkRoomOnDisk(const InstallRequest& request) {
    const juce::int64 free = request.root.getBytesFreeOnVolume();
    const juce::int64 needed = request.pack.downloadBytes + request.pack.installedBytes;
    if (free > 0 && free < needed)
        return failed("There is not enough free disk space for this pack (about " + formatPackSize(needed) +
                      " needed).");
    return succeeded();
}

InstallOutcome checkChecksum(const InstallRequest& request, const juce::File& zip, const InstallHooks& hooks) {
    if (hooks.phase)
        hooks.phase(InstallPhase::Verifying);
    static const std::atomic<bool> never{false};
    const juce::String actual = sha256Hex(zip, hooks.cancel != nullptr ? *hooks.cancel : never);
    if (isCancelled(hooks))
        return wasCancelled();
    if (actual != request.pack.sha256)
        return failed("The downloaded file did not match its checksum, so nothing was installed. Try again.");
    return succeeded();
}

// Every entry stays inside the pack folder, the pack.json is there, and the unpacked size is what the index said.
InstallOutcome checkZipEntries(juce::ZipFile& zip, const InstallRequest& request) {
    if (zip.getNumEntries() == 0)
        return failed(badPackMessage());
    bool hasManifest = false;
    juce::int64 unpacked = 0;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* entry = zip.getEntry(i);
        if (entry == nullptr || !isSafeEntryName(entry->filename, request.pack.id))
            return failed(badPackMessage());
        hasManifest = hasManifest || entry->filename == request.pack.id + "/pack.json";
        unpacked += entry->uncompressedSize;
    }
    if (!hasManifest || unpacked > request.pack.installedBytes * 2 + kUnpackedSlackBytes)
        return failed(badPackMessage());
    return succeeded();
}

InstallOutcome unpack(const InstallRequest& request, const juce::File& zipFile, const juce::File& into,
                      const InstallHooks& hooks) {
    juce::ZipFile zip(zipFile);
    if (const auto check = checkZipEntries(zip, request); !check.ok)
        return check;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        if (isCancelled(hooks))
            return wasCancelled();
        const auto result =
            zip.uncompressEntry(i, into, juce::ZipFile::OverwriteFiles::yes, juce::ZipFile::FollowSymlinks::no);
        if (result.failed())
            return failed("The pack could not be unpacked. The disk may be full.");
    }
    return succeeded();
}

// The unpacked pack.json must describe the pack the index promised.
InstallOutcome checkManifest(const InstallRequest& request, const juce::File& packDir) {
    const juce::var manifest = juce::JSON::parse(packDir.getChildFile("pack.json"));
    if (!manifest.isObject() || manifest.getProperty("id", {}).toString() != request.pack.id ||
        manifest.getProperty("version", {}).toString().isEmpty())
        return failed(badPackMessage());
    return succeeded();
}

// Moves the new folder in, parking an older one first so a failed move can put it back.
InstallOutcome swapIn(const juce::File& newDir, const juce::File& target, const juce::File& parking) {
    const bool hadOld = target.exists();
    if (hadOld && !target.moveFileTo(parking))
        return failed("The old version of the pack could not be replaced. Close anything using it and try again.");
    if (!newDir.moveFileTo(target)) {
        if (hadOld)
            parking.moveFileTo(target);
        return failed("The pack could not be moved into place.");
    }
    return succeeded();
}

} // namespace

bool isSafeEntryName(const juce::String& name, const juce::String& packId) {
    if (name.isEmpty() || name.containsChar('\\') || name.containsChar(':') || name.startsWithChar('/'))
        return false;
    if (name != packId + "/" && !name.startsWith(packId + "/"))
        return false;
    juce::StringArray parts;
    parts.addTokens(name, "/", "");
    for (int i = 0; i < parts.size(); ++i)
        if (parts[i] == ".." || parts[i] == "." || (parts[i].isEmpty() && i != parts.size() - 1))
            return false;
    return true;
}

InstallOutcome runInstall(const InstallRequest& request, const InstallHooks& hooks) {
    if (!request.root.createDirectory())
        return failed("The sound packs folder could not be created.");
    if (const auto room = checkRoomOnDisk(request); !room.ok)
        return room;

    const juce::File staging = request.root.getChildFile(".staging-" + request.pack.id);
    staging.deleteRecursively();
    if (!staging.createDirectory())
        return failed("The sound packs folder is not writable.");
    const StagingGuard guard{staging};

    const juce::File zipFile = staging.getChildFile("pack.zip");
    if (hooks.phase)
        hooks.phase(InstallPhase::Downloading);
    static const std::atomic<bool> never{false};
    const FetchResult fetched =
        request.fetch(request.pack.url, zipFile, hooks.progress ? hooks.progress : FetchProgress{},
                      hooks.cancel != nullptr ? *hooks.cancel : never);
    if (fetched.cancelled || isCancelled(hooks))
        return wasCancelled();
    if (!fetched.ok)
        return failed(fetched.error.isNotEmpty() ? fetched.error : juce::String("The download failed."));

    if (const auto check = checkChecksum(request, zipFile, hooks); !check.ok)
        return check;

    if (hooks.phase)
        hooks.phase(InstallPhase::Installing);
    const juce::File unpacked = staging.getChildFile("unpacked");
    if (const auto done = unpack(request, zipFile, unpacked, hooks); !done.ok)
        return done;
    const juce::File newDir = unpacked.getChildFile(request.pack.id);
    if (const auto check = checkManifest(request, newDir); !check.ok)
        return check;
    if (isCancelled(hooks))
        return wasCancelled();
    return swapIn(newDir, request.root.getChildFile(request.pack.id), staging.getChildFile("previous"));
}

} // namespace synth::soundpacks
