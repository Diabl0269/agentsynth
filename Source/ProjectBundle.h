#pragma once

#include "AI/AIStateMapper.h"
#include "MacroSet.h"
#include "PatchDocument.h"
#include "Timeline/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace synth {

/** Outcome of ProjectBundle::save()/load(): whether it succeeded, and if not, a message naming
 *  the step that failed (parse / patch validation / timeline validation / io), including the
 *  validator's own message where one exists. */
struct ProjectLoadResult {
    bool ok = false;
    juce::String message;
};

/**
 * @class ProjectBundle
 * @brief On-disk `.agsproj` project bundle: a directory holding `project.json` (the patch dialect
 *        plus a `"timeline"` key) and reserved `Audio/` / `Peaks/` asset subdirectories.
 *
 * `project.json` = `AIStateMapper::graphToJSON` output + any stashed unknown top-level keys +
 * a `"timeline"` key holding `TimelineDoc::toVar()`. `Audio/`/`Peaks/` hold recorded takes and
 * their waveform-peak sidecars.
 *
 * **Asset policy.** `synth::Clip::assetRef` must be a path relative to the bundle root
 * (`Audio/foo.wav`) — never absolute or escaping (`../`), enforced in
 * `TimelineDoc::isValidAssetRef` by both `setClipAsset` and `fromVar`, so a hand-edited
 * `project.json` can't smuggle an arbitrary file read, the same trust boundary as
 * `ModuleBase::setExtraState`. Before the project's first save, takes live under
 * `<app data>/Recordings/` with a reserved `Recordings/` ref prefix; `MainComponent::saveToFile`
 * runs `AssetManager::adoptRecordingsAssets` before `save()` to import them into `Audio/`/`Peaks/`
 * and rewrite refs. Nothing under `Audio/`/`Peaks/`/`Recordings/` is ever auto-deleted except via
 * `AssetManager::cleanUnusedAssets`, which never touches `Recordings/`.
 *
 * `"timeline"` and `"macros"` (P8-12) are both reserved top-level keys everywhere else: a plain
 * preset stashes them inertly, and `AIStateMapper::validatePatch` refuses either from provider
 * output. `ProjectBundle::load` is the one place they're meaningful, in this all-or-nothing
 * order:
 *  1. Parse `project.json`; reject if unparseable/non-object.
 *  2. Detach `"timeline"` AND `"macros"` before validation, since the untrusted validator
 *     refuses any patch carrying either key.
 *  3. Gate: `AIStateMapper::validatePatch(patch, graph, clearExisting=true, trusted=false)`.
 *  4. Validate `"timeline"` into a local `TimelineDoc::fromVar` and `"macros"` into a local
 *     `MacroSet::fromVar` (both all-or-nothing); a missing key means empty, a malformed one is
 *     rejected whole.
 *  5. Only once all three pass: `applyJSONToGraph(..., trusted=true)` (replaying our own output,
 *     so uuids are honoured and values aren't rescaled), then `patchDocument.loadFromVar`,
 *     `timeline.fromVar`/`clear()`, and `macros = std::move(localMacros)`.
 *  6. `TimelineReconciler::reconcile(timeline, graph)` recomputes bindings against live node
 *     uuids (an unresolved binding is flagged `orphaned`, NEVER deleted); `macros.retainOnly`
 *     drops any member uuid that doesn't resolve, dissolving a macro left with none.
 * On any earlier failure, `graph`/`timeline`/`patchDocument`/`macros` are left untouched.
 *
 * ### Save
 * `graphToJSON` → `patchDocument.toVar` → `"timeline"` set to `timeline.toVar()` and `"macros"`
 * set to `macros.toVar()`, both set **last**, so the live `TimelineDoc`/`MacroSet` (never a stale
 * stashed value) are authoritative.
 *
 * ### Autosave
 * `saveAutosave()` writes the identical JSON shape to a separate `autosave.json` sidecar —
 * `project.json` is never overwritten in place by autosave. `loadAutosave()` mirrors `load()`'s
 * validation exactly, reading the sidecar instead. See `MainComponent::performAutosave` /
 * `MainComponent::openFromFile` for the gate that writes it and the recovery prompt that reads it,
 * and docs/architecture.md for the full autosave design.
 *
 * ### Autosave backup history
 * Before each autosave overwrites `autosave.json`, the PREVIOUS sidecar is rotated into a numbered
 * history — `autosave-1.json` (most recent), `autosave-2.json`, ... up to `maxBackups` — classic
 * logrotate: `autosave-<maxBackups>.json` is evicted, everything else shifts up by one, then the old
 * `autosave.json` becomes `autosave-1.json`. `maxBackups <= 0` disables rotation entirely (plain
 * overwrite, as before). This history is a disk-only safety net: `discardAutosave()` only ever
 * removes the live `autosave.json`, never the numbered backups, and the in-app recovery prompt only
 * ever offers the live sidecar — the backups exist for manual recovery outside the app.
 */
class ProjectBundle {
public:
    /** On-disk extension for a project bundle directory. */
    static constexpr const char* kBundleExtension = ".agsproj";
    /** The one file inside a bundle carrying patch + timeline JSON. */
    static constexpr const char* kProjectFileName = "project.json";
    /** Autosave sidecar — same JSON shape as `project.json`, written by a periodic autosave
     *  (MainComponent::performAutosave) and never touched by save()/load(). See saveAutosave(). */
    static constexpr const char* kAutosaveFileName = "autosave.json";
    /** Reserved asset subdirectory names — see the class comment's Asset policy. */
    static constexpr const char* kAudioSubdirName = "Audio";
    static constexpr const char* kPeaksSubdirName = "Peaks";

    /** True if `dir` exists, is a directory, has the `.agsproj` extension, and contains a
     *  `project.json` file. Does not parse or validate that file's contents. */
    static bool isBundle(const juce::File& dir);

    /** Writes `<bundleDir>/project.json`, creating `bundleDir` and its `Audio/`/`Peaks/`
     *  subdirectories if they don't already exist. See the class comment for the exact JSON
     *  shape and the "timeline set last" rationale — "macros" (P8-12) follows the identical
     *  reserved-key treatment. */
    static ProjectLoadResult save(const juce::File& bundleDir, juce::AudioProcessorGraph& graph,
                                  const TimelineDoc& timeline, PatchDocument& patchDocument, const MacroSet& macros);

    /** Loads `<bundleDir>/project.json` into `graph`/`timeline`/`patchDocument`/`macros`,
     *  following the fixed, all-or-nothing order documented on the class. On any failure, all
     *  four output parameters are left completely untouched. */
    static ProjectLoadResult load(const juce::File& bundleDir, juce::AudioProcessorGraph& graph, TimelineDoc& timeline,
                                  PatchDocument& patchDocument, MacroSet& macros);

    /** `<userMusicDirectory>/<kProjectsFolderName>`, created on demand. Starting directory for
     *  project save/open/export dialogs (see MainComponent). */
    static juce::File getDefaultProjectsDirectory();

    /** Writes `<bundleDir>/autosave.json` — the exact same JSON shape save() writes to
     *  `project.json` (same "timeline set last" ordering), to a SEPARATE sidecar file.
     *  `project.json` and `Audio/`/`Peaks/` are never touched. `bundleDir` must already exist (an
     *  unsaved project has no bundle yet, so MainComponent never calls this before the first
     *  explicit save — see docs/architecture.md). Before writing, the PREVIOUS `autosave.json` (if
     *  any) is rotated into the numbered backup history — see the class comment's "Autosave backup
     *  history" section. `maxBackups <= 0` disables rotation (plain overwrite). */
    static ProjectLoadResult saveAutosave(const juce::File& bundleDir, juce::AudioProcessorGraph& graph,
                                          const TimelineDoc& timeline, PatchDocument& patchDocument,
                                          const MacroSet& macros, int maxBackups);

    /** True if `<bundleDir>/autosave.json` exists. Checked on open, before `project.json` loads —
     *  see MainComponent::openFromFile. */
    static bool hasAutosave(const juce::File& bundleDir);

    /** Loads `<bundleDir>/autosave.json` — same all-or-nothing validation and output contract as
     *  load(), just reading the sidecar instead of `project.json`. */
    static ProjectLoadResult loadAutosave(const juce::File& bundleDir, juce::AudioProcessorGraph& graph,
                                          TimelineDoc& timeline, PatchDocument& patchDocument, MacroSet& macros);

    /** Deletes `<bundleDir>/autosave.json` if present. A no-op if it doesn't exist. Called once a
     *  pending sidecar has been resolved: after the user answers the recovery prompt (either arm),
     *  and after any subsequent explicit save (a fresh save supersedes it). */
    static void discardAutosave(const juce::File& bundleDir);

private:
    // Shared by save()/saveAutosave(): graphToJSON -> patchDocument.toVar -> "timeline"/"macros"
    // set LAST. Identical content either way; only the destination file name differs.
    static juce::var buildProjectJson(juce::AudioProcessorGraph& graph, const TimelineDoc& timeline,
                                      PatchDocument& patchDocument, const MacroSet& macros);

    // Shared by load()/loadAutosave(): the fixed, all-or-nothing validation order documented on the
    // class, parametrized only on which file to read.
    static ProjectLoadResult loadFromFile(const juce::File& jsonFile, juce::AudioProcessorGraph& graph,
                                          TimelineDoc& timeline, PatchDocument& patchDocument, MacroSet& macros);

    // The logrotate step saveAutosave() runs before writing new content to autosave.json — see the
    // class comment's "Autosave backup history" section. A no-op when maxBackups <= 0.
    static void rotateAutosaveBackups(const juce::File& bundleDir, int maxBackups);
};

} // namespace synth
