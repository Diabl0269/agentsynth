#pragma once

#include "MacroSet.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** One snippet as surfaced in the library sidebar. */
struct SnippetInfo {
    juce::String name;
    juce::File file;
    int moduleCount = 0;
};

/**
 * @class SnippetManager
 * @brief Save a selection of modules as a reusable named group ("Snippet") and drop it back
 *        into any patch.
 *
 * A snippet is the same JSON dialect as a preset (see AIStateMapper::graphToJSON) narrowed to a
 * subset of nodes, with three deliberate rules:
 *
 *  - **Self-contained.** Only connections whose *both* endpoints are inside the selection are
 *    kept. A wire running to Audio Output, or to a module the user did not select, is dropped —
 *    a snippet must mean the same thing in every patch it lands in, and it cannot assume the
 *    destination patch has the node on the other end.
 *  - **Modulation survives as intent, not as wiring.** Attenuverter nodes are never stored.
 *    A modulation routing whose source and destination are both selected is emitted in the
 *    `modulations` array instead, and applyJSONToGraph rebuilds the attenuverter chain on
 *    insert. This is the same representation the AI patch format uses.
 *  - **Origin-relative positions.** Node positions are normalised so the selection's top-left
 *    bounding corner is (0,0); prepareForInsert() then offsets by the drop point, so the group
 *    keeps its internal layout wherever it is dropped.
 *
 * Everything here is static and free of GUI dependencies so it is testable headlessly.
 */
class SnippetManager {
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    /** On-disk extension for a saved snippet. */
    static constexpr const char* kFileExtension = ".agsnip";

    /** Prefix distinguishing a snippet drag payload from a plain module-name payload on the
     *  shared DragAndDrop channel between the library sidebar and the canvas. */
    static constexpr const char* kPayloadPrefix = "snippet:";

    /** Upper bound on a snippet name, applied before it becomes a filename. */
    static constexpr int kMaxNameLength = 64;

    // ---- Storage location -------------------------------------------------------------

    /** `<userAppData>/<settingsFolder>/Snippets`, created on demand. Mirrors
     *  ThemeManager::getUserThemesFolder() so all user-authored content sits side by side. */
    static juce::File getDefaultSnippetsDirectory();

    // ---- Pure JSON transforms (no filesystem, no graph mutation) ----------------------

    /** Builds snippet JSON for `selection` against `graph`.
     *
     *  Ids that are absent from the graph, are graph I/O nodes (Audio In/Out, MIDI In), or are
     *  Attenuverters are skipped — see the class comment. Returns a snippet with an empty
     *  `nodes` array when nothing eligible was selected.
     *
     *  @param includeExtraState  Carry each module's non-parameter `state` (the Sampler's loaded
     *         file, a Wavetable's custom table). **Off for anything that will be written to disk.**
     *         A `.agsnip` is a hand-editable file and `applyJSONToGraph` consumes `state` on the
     *         trusted path, so a snippet file carrying one could name an arbitrary file for the app
     *         to open. The in-memory clipboard has no such exposure — its payload comes straight
     *         from the live graph and never leaves the process — so copy/paste turns it on and a
     *         duplicated Sampler keeps its sample.
     *  @param macros  Optional (P8-12). A macro is captured into the snippet's own "macros" array
     *         — using the same node ids as `nodes` below, NOT uuids, since a snippet renumbers
     *         ids on insert — only when EVERY one of its members is inside `selection`; a macro
     *         straddling the selection boundary is dropped rather than partially captured, same
     *         "self-contained" rule connections/modulations already follow.
     */
    static juce::var extractSnippet(juce::AudioProcessorGraph& graph, const std::vector<NodeID>& selection,
                                    const juce::String& name, bool includeExtraState = false,
                                    const MacroSet& macros = {});

    /** Top-left canvas corner `extractSnippet` normalises `selection` against — the min (x, y) over
     *  every *eligible* selected node, or (0, 0) when the selection holds none.
     *
     *  Exposed because a caller that wants to place a copy relative to where it came from (paste,
     *  duplicate) has to know the corner the snippet's origin-relative positions were measured
     *  from. Deriving it independently would drift from the eligibility rule above the moment one
     *  of them changed, so both read this.
     */
    static juce::Point<int> selectionOrigin(juce::AudioProcessorGraph& graph, const std::vector<NodeID>& selection);

    /** Lowest node id that cannot collide with anything already in `graph` (max uid + 1, never
     *  below 1). Snippet ids MUST be renumbered above this before a merge-apply: applyJSONToGraph
     *  in merge mode treats a node id that already exists as "update that node", so a colliding
     *  id would silently retune an existing module instead of adding a copy. */
    static juce::uint32 nextFreeIdBase(const juce::AudioProcessorGraph& graph);

    /** Renumbers every id in `snippet` to a fresh range starting at `idBase` and offsets every
     *  node position by `dropPos`. Emits only the keys applyJSONToGraph consumes (nodes,
     *  connections, modulations) — snippet metadata is left behind. Does not mutate `snippet`,
     *  so one loaded snippet can be inserted repeatedly.
     *  @param includeExtraState  see extractSnippet; a `state` key is dropped unless asked for, so
     *         a hand-edited snippet file cannot smuggle one into the trusted apply. */
    static juce::var prepareForInsert(const juce::var& snippet, juce::Point<int> dropPos, juce::uint32 idBase,
                                      bool includeExtraState = false);

    // ---- Graph mutation --------------------------------------------------------------

    /** Inserts `snippet` into `graph` at `dropPos` without disturbing what is already there.
     *  @param includeExtraState  see extractSnippet.
     *  @param outMacros  Optional (P8-12). When non-null, filled with a fresh synth::Macro per
     *         macro the snippet carried, membership already resolved to the newly created nodes'
     *         real (freshly assigned) uuids — ready to hand straight to MacroSet::add(). A macro
     *         id is regenerated (never reused across a copy), same as node ids are.
     *  @return the node ids added (Attenuverters excluded), empty on rejection/failure. */
    static std::vector<NodeID> insertSnippet(const juce::var& snippet, juce::AudioProcessorGraph& graph,
                                             juce::Point<int> dropPos, bool includeExtraState = false,
                                             std::vector<Macro>* outMacros = nullptr);

    // ---- Snippet metadata ------------------------------------------------------------

    static int getModuleCount(const juce::var& snippet);
    static juce::String getSnippetName(const juce::var& snippet);

    // ---- Persistence -----------------------------------------------------------------

    /** Trims, length-caps and strips filesystem-hostile characters from a user-typed name.
     *  Returns an empty string when nothing usable remains (callers must reject that). */
    static juce::String sanitiseName(const juce::String& raw);

    /** `dir/<sanitised name>.agsnip`. Empty file when the name sanitises to nothing. */
    static juce::File fileForName(const juce::File& dir, const juce::String& name);

    static bool saveSnippet(const juce::File& dir, const juce::String& name, const juce::var& snippet);

    /** Parsed snippet, or a void var when the file is missing or not valid JSON. */
    static juce::var loadSnippet(const juce::File& file);

    /** Every readable snippet in `dir`, sorted by name (case-insensitive). */
    static juce::Array<SnippetInfo> listSnippets(const juce::File& dir);

    static bool deleteSnippet(const juce::File& dir, const juce::String& name);

    // ---- Drag payload encoding -------------------------------------------------------

    static juce::String payloadForName(const juce::String& name) { return juce::String(kPayloadPrefix) + name; }
    static bool isSnippetPayload(const juce::String& payload) { return payload.startsWith(kPayloadPrefix); }

    /** Snippet name carried by a drag payload, or an empty string when it isn't one. */
    static juce::String nameFromPayload(const juce::String& payload);
};

} // namespace synth
