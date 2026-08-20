#pragma once

#include "AI/AIStateMapper.h"
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
 * `"timeline"` is a reserved top-level key everywhere else: a plain preset stashes it inertly, and
 * `AIStateMapper::validatePatch` refuses it from provider output. `ProjectBundle::load` is the one
 * place it's meaningful, in this all-or-nothing order:
 *  1. Parse `project.json`; reject if unparseable/non-object.
 *  2. Detach `"timeline"` before validation, since the untrusted validator refuses any patch
 *     carrying that key.
 *  3. Gate: `AIStateMapper::validatePatch(patch, graph, clearExisting=true, trusted=false)`.
 *  4. Validate `"timeline"` into a local `TimelineDoc::fromVar` (all-or-nothing); a missing key
 *     means an empty timeline, a malformed one is rejected whole.
 *  5. Only once both pass: `applyJSONToGraph(..., trusted=true)` (replaying our own output, so
 *     uuids are honoured and values aren't rescaled), then `patchDocument.loadFromVar` and
 *     `timeline.fromVar`/`clear()`.
 *  6. `TimelineReconciler::reconcile(timeline, graph)` recomputes bindings against live node
 *     uuids; an unresolved binding is flagged `orphaned`, NEVER deleted.
 * On any earlier failure, `graph`/`timeline`/`patchDocument` are left untouched.
 *
 * ### Save
 * `graphToJSON` → `patchDocument.toVar` → `"timeline"` set to `timeline.toVar()` **last**, so the
 * live `TimelineDoc` (not a stale stashed value) is authoritative.
 */
class ProjectBundle {
public:
    /** On-disk extension for a project bundle directory. */
    static constexpr const char* kBundleExtension = ".agsproj";
    /** The one file inside a bundle carrying patch + timeline JSON. */
    static constexpr const char* kProjectFileName = "project.json";
    /** Reserved asset subdirectory names — see the class comment's Asset policy. */
    static constexpr const char* kAudioSubdirName = "Audio";
    static constexpr const char* kPeaksSubdirName = "Peaks";

    /** True if `dir` exists, is a directory, has the `.agsproj` extension, and contains a
     *  `project.json` file. Does not parse or validate that file's contents. */
    static bool isBundle(const juce::File& dir);

    /** Writes `<bundleDir>/project.json`, creating `bundleDir` and its `Audio/`/`Peaks/`
     *  subdirectories if they don't already exist. See the class comment for the exact JSON
     *  shape and the "timeline set last" rationale. */
    static ProjectLoadResult save(const juce::File& bundleDir, juce::AudioProcessorGraph& graph,
                                  const TimelineDoc& timeline, PatchDocument& patchDocument);

    /** Loads `<bundleDir>/project.json` into `graph`/`timeline`/`patchDocument`, following the
     *  fixed, all-or-nothing order documented on the class. On any failure, all three output
     *  parameters are left completely untouched. */
    static ProjectLoadResult load(const juce::File& bundleDir, juce::AudioProcessorGraph& graph, TimelineDoc& timeline,
                                  PatchDocument& patchDocument);
};

} // namespace synth
