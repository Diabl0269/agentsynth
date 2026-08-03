#pragma once

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

    /**
     * @brief Converts the current graph state to a JSON-compatible juce::var.
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
     * @return true if the patch was applied successfully.
     */
    static bool applyJSONToGraph(const juce::var& json, juce::AudioProcessorGraph& graph, bool clearExisting = true,
                                 bool trusted = false);

    /**
     * @brief Validates a patch JSON without applying it, returning a reason on failure.
     *
     * @param graph existing graph the patch would be applied to — used in merge mode
     *              (clearExisting == false) so connections/modulations may reference nodes
     *              that already exist rather than only nodes newly created by this patch.
     * @param trusted when true, only minimal structural checks are performed (matches legacy
     *              behaviour); when false, the full strict validation runs.
     */
    static PatchValidationResult validatePatch(const juce::var& json, const juce::AudioProcessorGraph& graph,
                                               bool clearExisting, bool trusted);

    /**
     * @brief Gets a Markdown-formatted string of all available modules and their parameters.
     */
    static juce::String getModuleSchema();

    /**
     * @brief Generates a JSON schema for patch validation and structured AI output.
     */
    static juce::var getPatchSchema();

    static std::unique_ptr<juce::AudioProcessor> createModule(const juce::String& type);

private:
    /**
     * @brief Helper to find the index of a string choice in an AudioParameterChoice.
     * @return index if found, -1 otherwise.
     */
    static int findChoiceIndex(juce::AudioParameterChoice* p, const juce::String& choiceText);

    static void applyParamsToProcessor(juce::AudioProcessor* processor, const juce::DynamicObject* paramsObj,
                                       bool trusted = false);

    // Validates JSON-provided parameter values for one node against its actual processor
    // instance. Only used on the strict/untrusted validation path.
    static PatchValidationResult validateNodeParams(juce::AudioProcessor* processor,
                                                    const juce::DynamicObject* paramsObj);
};

} // namespace synth
