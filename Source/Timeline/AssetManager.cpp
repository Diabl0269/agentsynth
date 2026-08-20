#include "AssetManager.h"
#include "../ProjectBundle.h"
#include "AudioClipStreamer.h"
#include <cstdint>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <set>
#include <vector>

namespace synth {

namespace {

// Upper bound on the collision-free-naming search. A directory with 10000 same-named-but-
// different-content imports in it is a bug report, not a session — mirrors
// MainComponent::kMaxTakeNumber's identical reasoning for the same kind of bounded loop.
constexpr int kMaxImportNameAttempts = 10000;

// A cheap (FNV-1a, 64-bit) whole-file hash, streamed rather than loaded whole — good enough to
// tell apart two same-sized, same-named files that happen to have different content. NOT a
// cryptographic hash and not meant as one: this is import-dedupe bookkeeping (avoid writing a
// second copy of a file already sitting there), not a security boundary, so a collision-resistant
// hash would be the wrong tool for the job. Returns 0 (a value no real file's content is required
// to avoid, but this function is only ever compared against ANOTHER call of itself, never trusted
// alone) if the file can't be opened.
std::uint64_t fnv1a64File(const juce::File& file) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffsetBasis;

    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return 0;

    std::uint8_t buffer[8192];
    for (;;) {
        const int bytesRead = stream.read(buffer, (int)sizeof(buffer));
        if (bytesRead <= 0)
            break;
        for (int i = 0; i < bytesRead; ++i) {
            hash ^= buffer[(std::size_t)i];
            hash *= kPrime;
        }
    }
    return hash;
}

// "Audio/foo.wav" -> "Peaks/foo.agpk". Mirrors MainComponent.cpp's file-local
// peaksRefForAssetRef() (that copy also handles the "Recordings/" case, which never appears here —
// collect/clean/adopt's OUTPUT side always names an "Audio/" ref, and this file is deliberately
// self-contained rather than reaching into MainComponent for four lines).
juce::String peaksRefForAudioRef(const juce::String& audioRef) {
    juce::String ref = audioRef;
    const juce::String audioPrefix = juce::String(ProjectBundle::kAudioSubdirName) + "/";
    if (ref.startsWith(audioPrefix))
        ref = juce::String(ProjectBundle::kPeaksSubdirName) + "/" + ref.substring(audioPrefix.length());
    return ref.upToLastOccurrenceOf(".", false, false) + ".agpk";
}

} // namespace

juce::String AssetManager::importAudioFileToDirectory(const juce::File& source, const juce::File& destDir,
                                                      juce::String* error) {
    auto fail = [&](const juce::String& message) {
        if (error != nullptr)
            *error = message;
        return juce::String();
    };

    if (!source.existsAsFile())
        return fail("Source file does not exist: " + source.getFullPathName());

    // Sniff BEFORE touching destDir at all: a rejected import must leave no trace anywhere.
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(source));
        if (reader == nullptr || reader->numChannels == 0)
            return fail("Not a readable audio file: " + source.getFileName());
    }

    if (!destDir.exists() && !destDir.createDirectory().wasOk())
        return fail("Could not create \"" + destDir.getFullPathName() + "\".");

    const juce::String stem = source.getFileNameWithoutExtension();
    const juce::String ext = source.getFileExtension(); // includes the leading '.', or empty
    const juce::int64 sourceSize = source.getSize();

    for (int n = 0; n < kMaxImportNameAttempts; ++n) {
        const juce::String candidateName = n == 0 ? (stem + ext) : (stem + "-" + juce::String(n + 1) + ext);
        const auto candidate = destDir.getChildFile(candidateName);

        if (!candidate.existsAsFile()) {
            if (!source.copyFileTo(candidate))
                return fail("Could not copy to \"" + candidate.getFullPathName() + "\".");
            return candidateName;
        }

        // Same name already taken: reuse it if the content is IDENTICAL (cheap check first), else
        // keep counting up for a free name.
        if (candidate.getSize() == sourceSize && fnv1a64File(candidate) == fnv1a64File(source))
            return candidateName;
    }

    return fail("Too many same-named imports in \"" + destDir.getFullPathName() + "\".");
}

juce::String AssetManager::importAudioFile(const juce::File& source, const juce::File& bundleRoot,
                                           juce::String* error) {
    const auto audioDir = bundleRoot.getChildFile(ProjectBundle::kAudioSubdirName);
    const auto name = importAudioFileToDirectory(source, audioDir, error);
    if (name.isEmpty())
        return {};
    return juce::String(ProjectBundle::kAudioSubdirName) + "/" + name;
}

juce::StringArray AssetManager::collectUnusedAssets(const TimelineDoc& doc, const juce::File& bundleRoot) {
    juce::StringArray unused;

    const auto audioDir = bundleRoot.getChildFile(ProjectBundle::kAudioSubdirName);
    if (!audioDir.isDirectory())
        return unused;

    std::set<juce::String> referenced;
    for (const auto& track : doc.getTracks())
        for (const auto& clip : track.clips)
            if (clip.assetRef.isNotEmpty())
                referenced.insert(clip.assetRef);

    for (const auto& file : audioDir.findChildFiles(juce::File::findFiles, false)) {
        const juce::String ref = juce::String(ProjectBundle::kAudioSubdirName) + "/" + file.getFileName();
        if (referenced.find(ref) == referenced.end())
            unused.add(ref);
    }

    return unused;
}

int AssetManager::cleanUnusedAssets(const TimelineDoc& doc, const juce::File& bundleRoot) {
    const auto unused = collectUnusedAssets(doc, bundleRoot);

    int deleted = 0;
    for (const auto& ref : unused) {
        const auto audioFile = bundleRoot.getChildFile(ref);
        if (audioFile.existsAsFile() && audioFile.deleteFile())
            ++deleted;

        const auto peaksFile = bundleRoot.getChildFile(peaksRefForAudioRef(ref));
        if (peaksFile.existsAsFile())
            peaksFile.deleteFile();
    }

    return deleted;
}

int AssetManager::adoptRecordingsAssets(TimelineDoc& doc, const juce::File& recordingsRoot,
                                        const juce::File& bundleRoot) {
    const juce::String prefix = AudioClipStreamer::kRecordingsRefPrefix;

    // 1. Every DISTINCT old ref, in first-seen order — read-only pass, nothing mutated yet.
    std::vector<juce::String> oldRefs;
    {
        std::set<juce::String> seen;
        for (const auto& track : doc.getTracks())
            for (const auto& clip : track.clips)
                if (clip.assetRef.startsWith(prefix) && seen.insert(clip.assetRef).second)
                    oldRefs.push_back(clip.assetRef);
    }
    if (oldRefs.empty())
        return 0;

    // The directory the "Recordings/..." ref is relative to — the same one
    // AudioClipStreamer::resolveAssetRef resolves it against (the folder CONTAINING Recordings/,
    // not Recordings/ itself).
    const auto refBaseDir = recordingsRoot.getParentDirectory();

    // 2. Import each distinct ref's file into the bundle (+ its peaks sidecar, if any), building
    //    the rewrite map. A ref whose source file is gone, or that fails to import, is left out of
    //    the map entirely — its clips keep pointing at the old (still possibly resolvable, or
    //    already "missing") ref, exactly as before. Nothing is ever deleted here.
    std::map<juce::String, juce::String> rewrite;
    for (const auto& oldRef : oldRefs) {
        const auto sourceFile = refBaseDir.getChildFile(oldRef);
        if (!sourceFile.existsAsFile())
            continue;

        juce::String error;
        const auto newRef = importAudioFile(sourceFile, bundleRoot, &error);
        if (newRef.isEmpty())
            continue;

        rewrite[oldRef] = newRef;

        const auto oldPeaksFile = refBaseDir.getChildFile(oldRef.upToLastOccurrenceOf(".", false, false) + ".agpk");
        if (oldPeaksFile.existsAsFile()) {
            const auto newPeaksFile = bundleRoot.getChildFile(peaksRefForAudioRef(newRef));
            // Resaving is stable: if adoption already ran once (an earlier save copied this
            // sidecar in), don't recopy or clobber it.
            if (!newPeaksFile.existsAsFile()) {
                newPeaksFile.getParentDirectory().createDirectory();
                oldPeaksFile.copyFileTo(newPeaksFile);
            }
        }
    }
    if (rewrite.empty())
        return 0;

    // 3. Snapshot every clip that needs rewriting BEFORE mutating anything — a TimelineDoc
    //    reference/pointer does not survive a mutation (see TimelineDoc::getClip's own contract),
    //    so the id + the field to preserve are captured first rather than iterating getTracks()
    //    while calling setClipAsset on it.
    struct Update {
        ClipId id;
        juce::String newRef;
        double sourceStartSeconds;
    };
    std::vector<Update> updates;
    for (const auto& track : doc.getTracks())
        for (const auto& clip : track.clips) {
            auto it = rewrite.find(clip.assetRef);
            if (it != rewrite.end())
                updates.push_back({clip.id, it->second, clip.sourceStartSeconds});
        }

    // 4. Rewrite. A PLAIN, direct mutation per clip — deliberately NOT wrapped in
    //    AppUndoManager::recordTimelineChange: saving must never create undo history (see the
    //    class comment and MainComponent::saveToFile, the one caller).
    for (const auto& update : updates)
        doc.setClipAsset(update.id, update.newRef, update.sourceStartSeconds);

    return (int)rewrite.size();
}

} // namespace synth
