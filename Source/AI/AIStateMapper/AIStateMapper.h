#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace synth {
class ConnectionIndex; // Source/AudioEngine/ConnectionIndex.h

/** Hard cap on a module's user-set card title, applied wherever a "displayName" is accepted —
 *  including the untrusted patch path, where it is the only thing stopping a hostile patch from
 *  stuffing a megabyte of text into a title and wedging the canvas paint. */
inline constexpr int kMaxModuleDisplayNameChars = 64;

/**
 * @brief Why a patch JSON failed validation, so callers (and the UI) can say what was wrong.
 */
enum class PatchValidationError {
    None,
    NotAnObject,
    MissingNodesOrRemove,
    NodesNotArray,
    ConnectionsNotArray,
    ModulationsNotArray,
    RemoveNotArray,
    RemoveModulationsNotArray,
    TooManyNodes,
    TooManyConnections,
    TooManyModulations,
    TooManyRemovals,
    TooManyRemoveModulations,
    NodeEntryInvalid,
    NodeIdInvalid,
    NodeTypeInvalid,
    UnknownNodeType,
    DuplicateNodeId,
    NodeIdTypeMismatch,
    InvalidParameterValue,
    InvalidChoiceValue,
    UnknownParameterKey,
    ConnectionEntryInvalid,
    ConnectionUnknownNode,
    ConnectionInvalidPort,
    ConnectionSelfCycle,
    ModulationEntryInvalid,
    ModulationUnknownNode,
    ModulationInvalidPort,
    ModulationSelfCycle,
    RemoveEntryInvalid,
    RemoveModulationEntryInvalid,
    TimelineNotAllowed,
    MacrosNotAllowed,
    MidiRemoteNotAllowed,
    InternalModuleNotAllowed,
    MixerPanLawNotAllowed,
    MixerViewNotAllowed,
    ViewNotAllowed,
};

/**
 * @brief Result of validating a patch JSON: whether it passed, and if not, why.
 */
struct PatchValidationResult {
    bool ok = true;
    PatchValidationError error = PatchValidationError::None;
    juce::String message;
};

/**
 * @brief Stable, human-readable name for a PatchValidationError value.
 *
 * Used for tallying rejection reasons (see Tools/AIPatchHarness) and for labelling the
 * retry feedback sent back to the model. The strings match the enumerator names so a log
 * line or a harness histogram can be read straight against this header.
 */
juce::String patchValidationErrorName(PatchValidationError error);

/** Node ids an earlier step of the same edit plan created, for a MERGE patch that refers to them. */
struct PatchIdScope {
    std::map<juce::uint32, juce::AudioProcessorGraph::NodeID> boundIds; // plan id -> the node it denotes
    std::set<juce::AudioProcessorGraph::NodeID> hiddenNodes; // never addressable by raw uid (bound ones neither)
};

/**
 * @class AIStateMapper
 * @brief Handles conversion between AI-friendly JSON and juce::AudioProcessorGraph.
 */
class AIStateMapper {
public:
    using NodeRemovalHook = std::function<void(const std::vector<juce::AudioProcessorGraph::NodeID>&)>;

    // Limits enforced against untrusted (network/AI-authored) patches — see validatePatch().
    // Chosen generously above anything this app would author itself, while still bounding the
    // worst case an adversarial or misbehaving remote model could throw at applyJSONToGraph
    // while it holds the audio callback lock.
    static constexpr int kMaxNodes = 256;
    static constexpr int kMaxConnections = 1024;
    static constexpr int kMaxModulations = 512;
    static constexpr int kMaxRemovals = 256;
    static constexpr int kMaxRemoveModulations = 256;
    // The same bounds for the app's own data (a project, a plugin session, a snippet: the callers that pass
    // allowInternalModuleTypes). A big project goes past the model bounds legitimately (80 tracks is over a thousand
    // nodes), so these only bound a tampered file; they are not a cap on how big a project may grow.
    static constexpr int kMaxAppDataNodes = 16384;
    static constexpr int kMaxAppDataConnections = 65536;
    static constexpr int kMaxAppDataModulations = 32768;
    static constexpr int kMaxAppDataRemovals = 16384;
    static constexpr int kMaxTypeNameLength = 64;
    // No module in this codebase exposes anywhere near this many raw channels (poly buses top
    // out at 16 — see PolyMidiModule); this just bounds how large a port index we'll accept.
    static constexpr int kMaxPortIndex = 64;

    // Emitted as the root "schemaVersion" of every patch graphToJSON writes. Readers treat an
    // ABSENT version as 1 and must not gate behaviour on it: the field exists so a future,
    // genuinely breaking format change can be detected, not so additive fields can be versioned.
    // Adding a property is always additive — never bump this for one.
    static constexpr int kSchemaVersion = 1;

    /**
     * @brief Converts the current graph state to a JSON-compatible juce::var.
     *
     * Each node carries a "uuid" that is stable for the lifetime of the node: it is generated
     * lazily here and written back into the graph node's properties, so repeated saves of an
     * unchanged graph emit the same identity. That uuid — not the integer "id", which merge-mode
     * apply can renumber — is what long-lived references (automation lanes, track bindings) key on.
     */
    static juce::var graphToJSON(juce::AudioProcessorGraph& graph);
    /** One node's entry in graphToJSON's "nodes" (assigning its uuid the same way); void for a node with no processor.
     */
    static juce::var nodeToJSON(juce::AudioProcessorGraph::Node& node);
    /** graphToJSON's "connections" array from one index of the graph's cables; only those `include` accepts, if set. */
    static juce::var
    connectionsToJSON(const ConnectionIndex& cables,
                      const std::function<bool(const juce::AudioProcessorGraph::Connection&)>& include = {});
    /** graphToJSON's "modulations" array (every attenuverter wired at both ends), from the same cable index. */
    static juce::var modulationsToJSON(juce::AudioProcessorGraph& graph, const ConnectionIndex& cables);

    /** Returns `node`'s persistent "uuid", generating and persisting a fresh one first if it
     *  doesn't have one yet — the same lazy-generation graphToJSON above uses, exposed so a
     *  caller that just created a node (a merge-mode apply assigns none unless `trusted` and the
     *  source JSON carried one — see applyJSONToGraph) can get a real uuid to key on without
     *  serialising the whole graph. SnippetManager::insertSnippet's macro-membership
     *  resolution is the reference caller: a freshly pasted node has no uuid until this runs. */
    static juce::String ensureNodeUuid(juce::AudioProcessorGraph::Node* node);

    /**
     * @brief Applies a JSON-compatible juce::var to the graph.
     *
     * When `trusted` is false (the default), the patch is fully validated via validatePatch()
     * before anything is touched, and rejected outright — never partially applied — on any
     * violation. Only set `trusted` for JSON that originates locally and was already produced
     * or vetted by this app (e.g. loading the user's own saved preset from disk, or undo/redo
     * replaying prior graph state). Anything that could originate off-device (an AI provider,
     * local or remote) must go through the default strict path.
     *
     * @param autoConnectNewNodes  Merge mode only (ignored when clearExisting is true). When true,
     *        newly created audio nodes with no outgoing audio wire are connected to Audio Output,
     *        and new MIDI-accepting nodes are connected to an existing MIDI source. That is an
     *        affordance for AI-authored merge patches — a model that adds an Oscillator mid-patch
     *        means for it to be audible. Pass false whenever the caller is reproducing an EXACT
     *        sub-graph and the absence of a wire is meaningful (snippet insertion): the
     *        convenience wires would otherwise splice the inserted group into the surrounding
     *        patch. See SnippetManager::insertSnippet.
     *
     * @param outIdMap  Optional. When non-null, filled with the same json-id -> live NodeID map the
     *        function already builds internally to wire connections/modulations (a merge-mode
     *        apply does not honour the requested id — see SnippetManager::insertSnippet's own
     *        comment). SnippetManager uses this to carry macro membership (keyed by the
     *        snippet's own node ids) through to the freshly created nodes' real NodeIDs.
     *
     * @return true if the patch was applied successfully.
     */
    /** @param scope null (the default) keeps the plain patch namespace; merge mode only otherwise. */
    static bool applyJSONToGraph(const juce::var& json, juce::AudioProcessorGraph& graph, bool clearExisting = true,
                                 bool trusted = false, bool autoConnectNewNodes = true,
                                 std::map<int, juce::AudioProcessorGraph::NodeID>* outIdMap = nullptr,
                                 const PatchIdScope* scope = nullptr);

    /**
     * @brief Restores one of OUR OWN graphToJSON snapshots by diffing it against the live graph.
     *
     * The undo/redo path only. applyJSONToGraph(clearExisting=true) reaches the same end state by
     * destroying and re-creating every node, which throws away all module runtime state (sequencer
     * step, envelope stage, sounding voices), and does it while holding the graph callback lock.
     * This entry point instead computes the difference and touches only what actually changed:
     * an undo of a parameter-only edit performs ZERO topology operations, so JUCE never rebuilds
     * its render sequence and the audio callback never blocks.
     *
     * Node identity is the per-node "uuid" (see graphToJSON) — NOT the integer "id", which merge
     * mode renumbers. A live node whose uuid appears in the snapshot is KEPT and updated in place;
     * one whose uuid is absent is removed; a snapshot node with no live match is created (adopting
     * both its uuid and, when free, its original id).
     *
     * Everything is planned before anything is mutated, and the function returns false WITHOUT
     * having touched the graph whenever identity cannot be established with certainty — a live or
     * snapshot node with no uuid, a duplicate uuid or id, a uuid whose type no longer matches the
     * live processor, an unknown module type, a connection naming a node the snapshot does not
     * define, or a merge delta ("remove"/"removeModulations") rather than a full snapshot. The
     * caller must then fall back to applyJSONToGraph(..., clearExisting=true, trusted=true), which
     * is always correct.
     *
     * The snapshot's "modulations" array is ignored on purpose: in a graphToJSON snapshot it is
     * derived from the attenuverter nodes and their wires, both of which are already carried
     * verbatim by "nodes" and "connections". For the same reason no auto-promotion, auto-connect
     * or value rescaling happens here — a snapshot is reproduced exactly, not interpreted.
     *
     * @param beforeNodeRemoval Invoked at most once, with the nodes about to go, immediately before
     *        the first is removed, i.e. before any processor is freed: the caller's only chance to
     *        detach UI that points into exactly those processors (GraphEditor::detachModuleComponentsFor).
     *        It is NOT called when the restore removes no nodes, which is exactly when the UI has
     *        nothing to detach from and can keep its components.
     *
     * @return true if the snapshot was applied; false if the caller must fall back (graph untouched).
     */
    static bool applySnapshotPreservingNodes(const juce::var& snapshot, juce::AudioProcessorGraph& graph,
                                             NodeRemovalHook beforeNodeRemoval = {});

    /**
     * @brief Validates a patch JSON without applying it, returning a reason on failure.
     *
     * @param graph existing graph the patch would be applied to — used in merge mode
     *              (clearExisting == false) so connections/modulations may reference nodes
     *              that already exist rather than only nodes newly created by this patch.
     * @param trusted when true, only minimal structural checks are performed (matches legacy
     *              behaviour); when false, the full strict validation runs.
     *
     * @param allowInternalModuleTypes only meaningful when `trusted` is false. The strict path
     *              normally refuses any node whose type is internal-only ("Attenuverter",
     *              "Mod Slot", "Track In") — that is what makes "non-authorable" mean
     *              untrusted-UNREACHABLE rather than merely absent from the schema handed to the
     *              model. But two different callers pass trusted=false: a MODEL-facing one, which
     *              wants exactly that restriction, and an APP-DATA one gating something this app
     *              wrote and is about to re-apply trusted (session state, an .agsproj, a snippet
     *              file — see docs/layout/snippets-clipboard.md). Our own saves legitimately contain internal
     *              nodes, so the second kind passes true: it is still gating structure, ids,
     *              ranges and tampering, just not authorship. Defaults to false so a new
     *              model-facing caller is protected without having to know this exists.
     */
    /** @param scope null (the default) keeps the plain patch namespace; ignored in replace mode. */
    static PatchValidationResult validatePatch(const juce::var& json, const juce::AudioProcessorGraph& graph,
                                               bool clearExisting, bool trusted, bool allowInternalModuleTypes = false,
                                               const PatchIdScope* scope = nullptr);

    /**
     * @brief Gets a Markdown-formatted string of all available modules and their parameters.
     */
    static juce::String getModuleSchema();

    /**
     * @brief Generates a JSON schema for patch validation and structured AI output.
     */
    static juce::var getPatchSchema();

    /**
     * @brief getPatchSchema() plus an OPTIONAL top-level "timelineOps" array — the local
     *        (Ollama) structured-output contract while the timeline AI tools are enabled
     *        (AIIntegrationService::setTimelineToolsEnabled).
     *
     * The ops item schema is deliberately PERMISSIVE (one object shape, only "op" required, every
     * per-op field optional): it is a GRAMMAR that lets the model express any of the five ops, not
     * a validator — `TimelineOps::validate` is the gate, and it rejects unknown fields, bad
     * shapes and out-of-range values whole-batch regardless of what the grammar allowed through.
     * A strict discriminated union here would double-maintain the validator's rules in a dialect
     * (llama.cpp's grammar compiler) that handles `anyOf` poorly.
     *
     * The reserved-fields rule is unchanged: "timeline" (the document dialect), "schemaVersion"
     * and node "uuid" stay absent — "timelineOps" is the separate door (see TimelineOps).
     */
    static juce::var getPatchSchemaWithTimelineOps();

    static std::unique_ptr<juce::AudioProcessor> createModule(const juce::String& type);

    // Validates JSON-provided parameter values for one node against its actual processor
    // instance. The strict/untrusted path only: validatePatch, and a timelineOps insert's params.
    static PatchValidationResult validateNodeParams(juce::AudioProcessor* processor,
                                                    const juce::DynamicObject* paramsObj);

    /** Applies `paramsObj` to a processor not yet in a graph, untrusted (in-[0,1] values rescale). */
    static void applyUntrustedParams(juce::AudioProcessor* processor, const juce::DynamicObject* paramsObj);

    /**
     * @brief The factory key graphToJSON writes for a live processor.
     *
     * The inverse of createModule() for every canonical type: createModule(k) must produce a
     * processor for which this returns k again, or that module silently changes type on every
     * save/load and every structural undo (which replays the same JSON). Guarded by
     * AIStateMapperTest.FactoryTypeNamesRoundTrip.
     */
    static juce::String getFactoryTypeName(juce::AudioProcessor* processor);

    /** @brief Every key registered in the module factory, sorted. */
    static juce::StringArray moduleFactoryTypeNames();

    /**
     * @brief Every factory type whose module carries the Dual I/O parameter, sorted.
     *
     * THE authoritative answer to "does this module type support the Dual I/O toggle" — derived by
     * probing the factory (one throwaway instance per type, computed once and cached) and asking
     * each module `ModuleBase::hasDualIOParameter()`, never from a hand-kept list. Both consumers
     * of that question read it: `PreferencesSettingsTab::getDualIOModuleTypes()` (the per-module
     * defaults popup) and the Dual I/O default that `GraphEditor::applyDefaultDualIOForNewModule`
     * applies to a newly created module. Since `ModuleBase`'s constructor adds the toggle from the
     * module's channel shape (`ModuleBase::StereoAudio`), a new stereo module appears in the
     * Preferences popup without a single extra edit anywhere — the Ring Modulator was missing from
     * that popup for exactly as long as this list was written out by hand.
     */
    static const juce::StringArray& dualIOCapableModuleTypes();

    /**
     * @brief The module types a model is allowed to author, sorted.
     *
     * Derived from the factory minus the non-authorable set, so registering a module makes it
     * model-authorable — which is why the exact contents are pinned by a golden test.
     */
    static juce::StringArray authorableModuleTypes();

private:
    /**
     * @brief Helper to find the index of a string choice in an AudioParameterChoice.
     * @return index if found, -1 otherwise.
     */
    static int findChoiceIndex(juce::AudioParameterChoice* p, const juce::String& choiceText);

    // skipUnchanged suppresses writes to parameters that already hold the target value. Only for
    // processors that are already IN the graph: setValueNotifyingHost notifies unconditionally, and
    // some listeners mutate the graph (ModuleComponent re-anchors a module's cables when "poly"
    // changes), so re-applying a whole snapshot's worth of no-op writes is not free.
    static void applyParamsToProcessor(juce::AudioProcessor* processor, const juce::DynamicObject* paramsObj,
                                       bool trusted = false, bool skipUnchanged = false);

    // Feeds a node's "state" property back into ModuleBase::setExtraState — trusted callers only.
    static void applyExtraStateToProcessor(juce::AudioProcessor* processor, const juce::DynamicObject* nodeObj,
                                           bool trusted);
};

} // namespace synth
