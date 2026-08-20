#pragma once

#include "TimelineDoc.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class AssetManager
 * @brief The asset-lifecycle half of the audio-clip model — import-into-bundle, collect/clean of
 *        files no clip references, and the "adopt on save" sweep that moves an unsaved project's
 *        `Recordings/`-convention takes into a bundle's own `Audio/` the first time it is saved.
 *
 * Headless, message-thread-only (file I/O; never called from the audio thread), and stateless — a
 * bag of static functions over a `TimelineDoc` and a `juce::File` root, like `synth::PeaksFile` and
 * `synth::TimelineReconciler`. See `docs/architecture.md`'s asset-management subsection for the
 * policy this class enforces; the one invariant every function here shares is **never a silent
 * delete**: nothing is removed except the exact files `cleanUnusedAssets` names, and a clip whose
 * asset is simply MISSING (unresolvable, not "unused") never causes anything to be deleted.
 *
 * `importAudioFile` sniffs the source with a `juce::AudioFormatManager` and copies it into
 * `<bundleRoot>/Audio/` under a collision-free name, reusing an already-present same-named file
 * when its content is IDENTICAL (size, then a whole-file FNV-1a hash — dedupe bookkeeping, not a
 * security boundary, so a cryptographic hash would be the wrong tool). The source is never moved
 * or deleted.
 *
 * `collectUnusedAssets`/`cleanUnusedAssets` walk `<bundleRoot>/Audio/` for files no clip
 * references, alongside each one's `<bundleRoot>/Peaks/<stem>.agpk` sidecar. A clip whose assetRef
 * names a file that isn't actually there is simply skipped (nothing to enumerate), and the scan
 * never reaches outside `bundleRoot`.
 *
 * `adoptRecordingsAssets` imports every clip whose assetRef carries the reserved `Recordings/`
 * prefix (a take recorded before the project had ever been saved) into the SAVE TARGET bundle's
 * `Audio/`, rewriting every clip sharing that same old ref together so a duplicated take never
 * ends up split across two files. `MainComponent::saveToFile` calls this immediately before
 * `ProjectBundle::save`, as a plain, direct doc mutation — deliberately NOT wrapped in
 * `AppUndoManager::recordTimelineChange`, because saving must never create undo history. The
 * original `Recordings/` file is left in place; cleaning it up is `cleanUnusedAssets`'s job, which
 * never looks at `Recordings/` at all.
 *
 * Missing-asset painting itself lives in `synth::ui::TimelineClipLaneArea`, not here.
 */
class AssetManager {
public:
    /** Sniffs `source` with a `juce::AudioFormatManager` and, if it is a readable audio file,
     *  copies it into `<bundleRoot>/Audio/` under a collision-free name (see the class comment's
     *  dedupe rule). Returns the bundle-relative ref ("Audio/name.wav") on success, or an empty
     *  string on failure — with `*error` (if non-null) set to why: unreadable/non-audio source, or
     *  an I/O failure creating the directory / copying the file. Never moves or deletes `source`. */
    static juce::String importAudioFile(const juce::File& source, const juce::File& bundleRoot, juce::String* error);

    /** The primitive `importAudioFile` is a thin wrapper over: same sniff + collision-free-naming +
     *  content-dedupe copy, but into an ARBITRARY `destDir` rather than `<bundleRoot>/Audio/` —
     *  what a caller using a different ref convention needs (MainComponent's unsaved-project
     *  relink path, which writes into the app-data `Recordings/` folder directly, with no nested
     *  "Audio/" level). Returns the bare file name placed in `destDir` (NOT a ref — the caller
     *  composes whatever prefix its own convention uses), or empty on failure (see `*error`).
     *  Never moves or deletes `source`. */
    static juce::String importAudioFileToDirectory(const juce::File& source, const juce::File& destDir,
                                                   juce::String* error);

    /** Every file under `<bundleRoot>/Audio/` that no clip in `doc` references, as bundle-relative
     *  refs ("Audio/foo.wav") — regardless of which track kind the referencing clip sits on (the
     *  model is deliberately permissive about that; see `synth::Clip`'s own comment). A clip whose
     *  assetRef names a file that does not actually exist contributes nothing to this list (there
     *  is no file to enumerate) and can never cause a referenced file to be misreported as unused.
     *  Returns an empty array when `<bundleRoot>/Audio/` doesn't exist. */
    static juce::StringArray collectUnusedAssets(const TimelineDoc& doc, const juce::File& bundleRoot);

    /** Deletes exactly the files `collectUnusedAssets` would report, plus each one's
     *  `<bundleRoot>/Peaks/<stem>.agpk` sidecar (if present) — never anything else. Returns how
     *  many audio files were actually deleted (a sidecar deletion failure does not affect this
     *  count, and a missing sidecar is not an error). Bundle-internal only: never touches
     *  `Recordings/` or anything outside `bundleRoot`. */
    static int cleanUnusedAssets(const TimelineDoc& doc, const juce::File& bundleRoot);

    /** Imports every clip's `Recordings/`-convention asset (see the class comment) into
     *  `bundleRoot`'s own `Audio/`, copies its
     *  `.agpk` peaks sidecar alongside when one exists, and rewrites `assetRef` on every clip that
     *  shared the same old ref — as a PLAIN, direct `TimelineDoc::setClipAsset` mutation on each,
     *  never wrapped in `AppUndoManager::recordTimelineChange` (saving must not create undo
     *  history; see the caller, `MainComponent::saveToFile`). `recordingsRoot` is the same
     *  `<app data>/<settings folder>/Recordings` directory `MainComponent::chooseTakeFiles` writes
     *  into. A ref whose source file is no longer on disk, or that fails to import, is left
     *  untouched (still resolvable exactly as before, or already the "missing asset" case §2
     *  handles) — this pass never deletes the original `Recordings/` file either way. Returns the
     *  number of DISTINCT old refs adopted (0 if the doc has none). */
    static int adoptRecordingsAssets(TimelineDoc& doc, const juce::File& recordingsRoot, const juce::File& bundleRoot);

private:
    AssetManager() = delete;
};

} // namespace synth
