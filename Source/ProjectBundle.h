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
 *        plus a `"timeline"` key) and reserved `Audio/` / `Peaks/` asset subdirectories (TL2-4).
 *
 * ### Format
 *
 * A bundle is a directory named `*.agsproj` containing:
 *  - `project.json` — everything `AIStateMapper::graphToJSON` writes (nodes, connections,
 *    modulations, schemaVersion, ...), any top-level keys `PatchDocument` had stashed from a prior
 *    load, and a `"timeline"` key holding `TimelineDoc::toVar()`.
 *  - `Audio/`, `Peaks/` — audio-clip assets and their cached waveform peaks. Since TL6-3 the record
 *    flow writes into both: a take is `Audio/take-<n>.wav` (32-bit float) plus
 *    `Peaks/take-<n>.agpk` (the sidecar `RecordTapModule` documents).
 *
 *    **Asset policy.** Any asset reference stored in the document (`synth::Clip::assetRef`) is a
 *    path *relative to the bundle root* — `Audio/foo.wav`, never `/Users/.../foo.wav` or
 *    `../foo.wav`. An absolute (or escaping) path is rejected outright, the same way a
 *    provider-authored patch's `"state"` is restricted to the trusted path (see
 *    `ModuleBase::setExtraState`) — a bundle can be handed to someone else, and an absolute path
 *    baked into it would read whatever happens to be at that path on their machine. The rule is
 *    enforced in `TimelineDoc::isValidAssetRef`, by BOTH `setClipAsset` and `fromVar`, so a
 *    hand-edited `project.json` is not a way around it.
 *
 *    **The unsaved-project case (TL6-3, temporary).** A take can be recorded before the project has
 *    ever been saved, when there is no bundle to write into. Those takes go to
 *    `<user app data>/<settings folder>/Recordings/` and their clips carry the reserved prefix
 *    `Recordings/take-<n>.wav` — still relative in form (so the one path rule above holds), but
 *    resolved against app data rather than against a bundle root. **TODO(TL6-6):** the import/adopt
 *    pass moves those files into `Audio/` on the first save and rewrites the refs to `Audio/...`,
 *    after which `Recordings/` is never seen inside a bundle. `Recordings/` is therefore reserved:
 *    do not use it as a real subdirectory name inside a bundle.
 *
 *    Nothing is ever auto-deleted from these directories — undoing a take removes the clip, not the
 *    recording. Reaping files no clip references is TL6-6's clean pass.
 *
 * `"timeline"` is otherwise a **reserved** top-level key everywhere else in the codebase: a plain
 * `.json` preset that happens to carry one only ever stashes it inertly (`PatchDocument`), and
 * `AIStateMapper::validatePatch` refuses it outright from provider output
 * (`PatchValidationError::TimelineNotAllowed`). `ProjectBundle` is the one place that key is
 * actually meaningful — see the load order below for how that tension is resolved.
 *
 * ### Load order (all-or-nothing, mirrors `SnippetManager::insertSnippet`'s trusted/untrusted
 * pairing)
 *
 *  1. Parse `project.json`. Unparseable or non-object → reject.
 *  2. Detach `"timeline"` into its own `juce::var`; the **remaining root is the patch**. This has
 *     to happen *before* step 3: the untrusted validator refuses any patch carrying a `"timeline"`
 *     key (see above), and a `.agsproj`'s `"timeline"` is this format's own dialect, not
 *     provider-authored data smuggled in — so it is pulled out first and validated on its own
 *     terms in step 4, rather than being rejected by a check that exists for a different threat.
 *  3. **Gate:** `AIStateMapper::validatePatch(patch, graph, clearExisting=true, trusted=false)`.
 *     A bundle's `project.json` is a file on disk exactly like a preset or a snippet — a
 *     hand-edited or corrupt one is rejected whole, never partially applied.
 *  4. Validate the timeline into a **local** `TimelineDoc` via `fromVar` (already all-or-nothing).
 *     A **missing** `"timeline"` key is not an error — it means a plain patch is being opened as
 *     (or upgraded into) a bundle, and the timeline starts empty. A **present but malformed** one
 *     is rejected whole.
 *  5. Only once *both* validations have passed does anything mutate: `applyJSONToGraph(patch,
 *     graph, clearExisting=true, trusted=true)` (trusted, so node `"uuid"`s are honoured and
 *     parameter values aren't rescaled by the untrusted-input heuristic — replaying our own
 *     `graphToJSON` output, same reasoning as the snippet insert path);
 *     `patchDocument.loadFromVar(patch)` on the *timeline-stripped* root, so `"timeline"` is never
 *     double-stored in the stash; then the live `timeline` is brought to the validated state
 *     (`timeline.fromVar()` on the same var already proven valid against the local doc, or
 *     `timeline.clear()` when the key was absent).
 *  6. **Binding pass (TL2-6):** `TimelineReconciler::reconcile(timeline, graph)` recomputes every
 *     track's `bindingUuid` and lane's `nodeUuid` against the freshly-loaded graph's live node
 *     uuids. A binding that no longer resolves is retained and flagged `orphaned` — NEVER
 *     deleted; a resolvable one is (re-)confirmed. This is what makes a freshly opened project
 *     show correct orphan state immediately, without the user having to touch anything first.
 *
 * On any failure before step 5, `graph`, `timeline` and `patchDocument` are left exactly as they
 * were — nothing partially applied.
 *
 * ### Save
 *
 * `graphToJSON(graph)` → `patchDocument.toVar(...)` (re-merges whatever unknown top-level keys
 * were stashed on the bundle's last load) → **then** `"timeline"` is set to `timeline.toVar()`
 * **last**. Order matters: a bundle opened from a plain `.json` that happened to carry a stale
 * `"timeline"` leaves that value sitting in the `PatchDocument` stash exactly like any other
 * unknown key, and the live `TimelineDoc` — not the stash — is authoritative on save.
 *
 * Plain-`.json` preset save/load (`GraphEditor::savePreset`/`loadPreset`) is entirely unaffected by
 * any of this: those two functions are untouched, and a `.agsproj` is a parallel format, not a
 * replacement.
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
