#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/** Preserves top-level JSON keys that this build doesn't understand across a save/load
 *  round-trip, so an older build never destroys a newer build's data (e.g. a future
 *  "timeline" key) just by re-saving the patch.
 *
 *  graphToJSON() rebuilds its root object from scratch on every save — anything it doesn't
 *  emit itself is gone unless something else stashes it first. PatchDocument is that stash:
 *  loadFromVar() pulls every unrecognised top-level property out of a loaded file and holds
 *  it; toVar() merges those properties back into a freshly generated JSON object before it's
 *  written out again.
 *
 *  Preserved keys are INERT data as far as this class (and every caller) is concerned: they
 *  are never interpreted, never validated, and never fed into setExtraState or any other
 *  apply path. They only ever get merged into an outgoing juce::var.
 *
 *  Lifecycle: the stash is per-loaded-file. A "new patch" action must call clear() — preserved
 *  keys must never survive into a patch the user didn't load them from.
 *
 *  Scope: only the user preset save/load path should own a PatchDocument. Undo/redo, snippets,
 *  and AI-authored patch application all replay in-session graph state and must not resurrect
 *  file-level keys into that flow. */
class PatchDocument {
public:
    /** Extracts and stores every top-level property of `root` that isn't one of the known
     *  patch keys ("nodes", "connections", "modulations", "mode", "remove",
     *  "removeModulations", "schemaVersion"). Replaces whatever was previously stashed —
     *  call this once per load. Does nothing if `root` isn't a JSON object. */
    void loadFromVar(const juce::var& root);

    /** Returns `freshGraphJson` with every previously-stashed key merged in. `freshGraphJson`
     *  is expected to be the object graphToJSON() just produced; its own (known) keys always
     *  win — a stashed key can never overwrite one of the known keys above, since loadFromVar
     *  never stores one in the first place. If `freshGraphJson` isn't an object, it's returned
     *  unchanged. */
    juce::var toVar(const juce::var& freshGraphJson) const;

    /** Drops the stash. Call this when starting a fresh patch (GraphEditor::newPatch) — stashed
     *  keys are per-loaded-file and must never be resurrected into a patch the user didn't load
     *  them from. */
    void clear();

    /** True if there is nothing stashed (e.g. a plain patch with no unknown keys, or after
     *  clear()). */
    bool empty() const { return stashed.size() == 0; }

    /** The set of top-level keys graphToJSON/applyJSONToGraph know about. Anything else is
     *  stashed verbatim by loadFromVar(). */
    static const juce::StringArray& knownKeys();

private:
    juce::NamedValueSet stashed;
};

} // namespace synth
