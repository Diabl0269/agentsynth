#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/**
 * @brief One human-readable change between two AIStateMapper::graphToJSON() snapshots.
 *
 * Every field that isn't relevant to `kind` is left at its default — callers should switch on
 * `kind` before reading kind-specific fields. `describe()` renders the one-line string the UI
 * shows; tests should prefer asserting on the structured fields below over string-matching it.
 */
struct PatchChange {
    enum class Kind {
        NodeAdded,
        NodeRemoved,
        ParamChanged,
        ConnectionAdded,
        ConnectionRemoved,
        ModulationAdded,
        ModulationRemoved,
    };

    Kind kind = Kind::NodeAdded;

    // NodeAdded / NodeRemoved / ParamChanged: the node's display type ("Filter", "Reverb", ...)
    // and its graphToJSON "uuid" (empty for connection/modulation entries).
    juce::String nodeType;
    juce::String nodeUuid;

    // ParamChanged only.
    juce::String paramId;    // the raw parameter ID (e.g. "cutoff")
    juce::String paramName;  // display name, falls back to paramId when unknown
    juce::var beforeValue;   // the value exactly as it appears in the "before" snapshot's params
    juce::var afterValue;    // the value exactly as it appears in the "after" snapshot's params
    juce::String beforeText; // beforeValue rendered for display, e.g. "400" or "sine" or "false"
    juce::String afterText;

    // ConnectionAdded / ConnectionRemoved / ModulationAdded / ModulationRemoved: endpoint types.
    juce::String srcType;
    juce::String dstType;
    int srcPort = 0;
    int dstPort = 0;
    bool isMidi = false;

    // ModulationAdded / ModulationRemoved only.
    juce::String destParamName; // the modulation target's display name (e.g. "cutoff"), if known
    double amount = 0.0;
    bool bypass = false;

    /** @brief One-line human-readable rendering, e.g. "Filter: cutoff 400 -> 800". */
    juce::String describe() const;
};

/**
 * @brief Diffs two AIStateMapper::graphToJSON() snapshots into a human-readable change list.
 *
 * This is the ONLY correct way to preview a proposed patch: diffing the raw patch JSON against
 * the live graph would miss merge-mode's auto-wiring of new nodes, replace-mode's implicit
 * deletion of everything the patch doesn't restate, and the untrusted-apply path's [0,1]
 * rescale heuristic. `before`/`after` must therefore both be graphToJSON() output — `after` from
 * a scratch graph the candidate patch was actually applied to (trusted-replayed from `before`'s
 * source graph first, for merge mode) — never the patch JSON itself. See
 * AIIntegrationService::computePatchPreview(), the intended way to obtain both.
 *
 * Node identity is keyed on graphToJSON's "uuid" field, not the integer "id": merge-mode apply
 * preserves uuid (and id) for nodes it updates in place, so before/after nodes with the same uuid
 * are the same node. A brand-new node has no uuid in `before`, so it never spuriously matches.
 * Connection/modulation endpoints are resolved to uuid via each snapshot's OWN id->uuid mapping
 * before comparing, since raw integer ids are only meaningful within a single snapshot.
 *
 * Replace-mode patches (clearExisting=true) have NO stable node identity between snapshots —
 * applyJSONToGraph only preserves id/uuid on the trusted path, and a replace-mode apply is always
 * untrusted — so a replace-mode diff will show the entire prior graph removed and the entire new
 * patch added, even where a node is conceptually unchanged. This matches what actually happens to
 * the running graph (every processor really is destroyed and recreated) and in practice replace
 * mode is reserved for from-scratch patches (see AIChatComponent's merge-vs-replace inference),
 * so this is not the common single-edit case.
 *
 * Attenuverter nodes/connections (the implementation detail behind the "modulations" array) are
 * excluded from the node/connection diff and reported exclusively via ModulationAdded/Removed.
 * An attenuverter wired on only one side (source XOR destination) produces no "modulations" entry
 * in graphToJSON at all, so it is silently omitted from the diff rather than shown as raw
 * add/remove noise — a deliberate tradeoff, since a half-wired attenuverter only arises from a
 * malformed patch.
 */
std::vector<PatchChange> computeDiff(const juce::var& before, const juce::var& after);

} // namespace synth
