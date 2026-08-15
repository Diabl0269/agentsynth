#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <memory>

namespace synth {

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
    InternalModuleNotAllowed,
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

/**
 * @class AIStateMapper
 * @brief Handles conversion between AI-friendly JSON and juce::AudioProcessorGraph.
 */
class AIStateMapper {
public:
    // Limits enforced against untrusted (network/AI-authored) patches — see validatePatch().
    // Chosen generously above anything this app would author itself, while still bounding the
    // worst case an adversarial or misbehaving remote model could throw at applyJSONToGraph
    // while it holds the audio callback lock.
    static constexpr int kMaxNodes = 256;
    static constexpr int kMaxConnections = 1024;
    static constexpr int kMaxModulations = 512;
    static constexpr int kMaxRemovals = 256;
    static constexpr int kMaxRemoveModulations = 256;
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
     * @return true if the patch was applied successfully.
     */
    static bool applyJSONToGraph(const juce::var& json, juce::AudioProcessorGraph& graph, bool clearExisting = true,
                                 bool trusted = false, bool autoConnectNewNodes = true);

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
     * @param beforeNodeRemoval Invoked at most once, immediately before the first node is removed,
     *        i.e. before any processor is freed. This is the caller's only chance to detach UI that
     *        points into those processors (GraphEditor::detachAllModuleComponents). It is NOT
     *        called when the restore removes no nodes, which is exactly when the UI has nothing to
     *        detach from and can keep its components.
     *
     * @return true if the snapshot was applied; false if the caller must fall back (graph untouched).
     */
    static bool applySnapshotPreservingNodes(const juce::var& snapshot, juce::AudioProcessorGraph& graph,
                                             std::function<void()> beforeNodeRemoval = {});

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
     *              file — see docs/layout.md §12.5). Our own saves legitimately contain internal
     *              nodes, so the second kind passes true: it is still gating structure, ids,
     *              ranges and tampering, just not authorship. Defaults to false so a new
     *              model-facing caller is protected without having to know this exists.
     */
    static PatchValidationResult validatePatch(const juce::var& json, const juce::AudioProcessorGraph& graph,
                                               bool clearExisting, bool trusted, bool allowInternalModuleTypes = false);

    /**
     * @brief Gets a Markdown-formatted string of all available modules and their parameters.
     */
    static juce::String getModuleSchema();

    /**
     * @brief Generates a JSON schema for patch validation and structured AI output.
     */
    static juce::var getPatchSchema();

    static std::unique_ptr<juce::AudioProcessor> createModule(const juce::String& type);

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

    // Validates JSON-provided parameter values for one node against its actual processor
    // instance. Only used on the strict/untrusted validation path.
    static PatchValidationResult validateNodeParams(juce::AudioProcessor* processor,
                                                    const juce::DynamicObject* paramsObj);
};

} // namespace synth
