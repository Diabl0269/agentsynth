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
 *  - `Audio/`, `Peaks/` — created empty. Reserved for sample/audio-clip assets and their cached
 *    waveform peaks (TL6). **Asset policy, enforced when that work lands, not here:** any asset
 *    reference is a path *relative to the bundle root* — `Audio/foo.wav`, never `/Users/.../foo.wav`
 *    or `../foo.wav`. An absolute (or escaping) path must be rejected outright, the same way a
 *    provider-authored patch's `"state"` is restricted to the trusted path (see
 *    `ModuleBase::setExtraState`) — a bundle can be handed to someone else, and an absolute path
 *    baked into it would read whatever happens to be at that path on their machine. Nothing in this
 *    class reads or writes into these directories yet; nothing today references an asset by path.
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
