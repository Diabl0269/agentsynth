// AIStateMapper — patch validation: the untrusted-input security boundary.
//
// validatePatch (and validateNodeParams, its per-node parameter check) reject anything a model
// may not do before applyJSONToGraph ever touches the live graph. The class itself is declared in
// AIStateMapper.h; never relax a check here to raise the AI pass rate — fix validity upstream
// (schema / retry / prompt) instead. See the root CLAUDE.md's trust-boundary invariants.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

#include <cmath>
#include <map>
#include <set>

namespace synth {

namespace {

// -1 is the MIDI sentinel used by graphToJSON/applyJSONToGraph (mapped to
// juce::AudioProcessorGraph::midiChannelIndex on apply); anything else must be a plausible raw
// channel index.
bool isValidPatchPort(int port) {
    if (port == -1)
        return true;
    return port >= 0 && port <= AIStateMapper::kMaxPortIndex;
}

// Renders whatever the model put in an id field, so the rejection names the offending value even
// when it was not a usable integer at all.
juce::String describeId(const juce::var& value) {
    if (value.isVoid())
        return "(missing)";
    const juce::String text = value.toString();
    return text.isEmpty() ? "(empty)" : text;
}

// The ids the patch may legally reference. A rejection that only says "unknown node id" leaves a
// model nothing to aim at on the retry — naming the usable ids is what makes the correction
// actionable, and it is the difference between a retry that converges and one that repeats itself.
juce::String describeKnownIds(const std::set<juce::uint32>& knownIds) {
    if (knownIds.empty())
        return "This patch defines no node ids, so connections and modulations cannot reference any; "
               "every id used must appear as an \"id\" in the \"nodes\" array.";

    juce::StringArray ids;
    for (auto id : knownIds)
        ids.add(juce::String(id));
    return "Valid node ids here are: " + ids.joinIntoString(", ") + ".";
}

// Named step, extracted out of validatePatch to keep that function under the function-size
// ratchet (scripts/check-function-sizes.sh): the untrusted-only reserved top-level keys — each is
// app-authored project data a provider could never legitimately produce, and each is refused
// outright rather than ignored so a later build that starts honouring one of these can't silently
// begin executing provider-authored data. See the per-key comment at each call site's origin
// (docs/ai/patch-format.md#reserved-keys-and-forward-compatibility, docs/midi_remote.md §7) for
// why each one specifically is reserved.
PatchValidationResult checkReservedKeysNotAllowed(const juce::DynamicObject* rootObj) {
    if (rootObj->hasProperty("timeline"))
        return {false, PatchValidationError::TimelineNotAllowed,
                "Patch suggestions must not contain a \"timeline\" property - timeline and automation "
                "data is not accepted from a patch suggestion. Remove it and resend only nodes, "
                "connections and modulations."};

    if (rootObj->hasProperty("macros"))
        return {false, PatchValidationError::MacrosNotAllowed,
                "Patch suggestions must not contain a \"macros\" property - macro grouping is app-authored "
                "canvas data, not accepted from a patch suggestion. Remove it and resend only nodes, "
                "connections and modulations."};

    if (rootObj->hasProperty("midiRemote"))
        return {false, PatchValidationError::MidiRemoteNotAllowed,
                "Patch suggestions must not contain a \"midiRemote\" property - MIDI controller assignments "
                "are app-authored project data, not accepted from a patch suggestion. Remove it and resend "
                "only nodes, connections and modulations."};

    return {};
}

} // namespace

juce::String patchValidationErrorName(PatchValidationError error) {
    switch (error) {
    case PatchValidationError::None:
        return "None";
    case PatchValidationError::NotAnObject:
        return "NotAnObject";
    case PatchValidationError::MissingNodesOrRemove:
        return "MissingNodesOrRemove";
    case PatchValidationError::NodesNotArray:
        return "NodesNotArray";
    case PatchValidationError::ConnectionsNotArray:
        return "ConnectionsNotArray";
    case PatchValidationError::ModulationsNotArray:
        return "ModulationsNotArray";
    case PatchValidationError::RemoveNotArray:
        return "RemoveNotArray";
    case PatchValidationError::RemoveModulationsNotArray:
        return "RemoveModulationsNotArray";
    case PatchValidationError::TooManyNodes:
        return "TooManyNodes";
    case PatchValidationError::TooManyConnections:
        return "TooManyConnections";
    case PatchValidationError::TooManyModulations:
        return "TooManyModulations";
    case PatchValidationError::TooManyRemovals:
        return "TooManyRemovals";
    case PatchValidationError::TooManyRemoveModulations:
        return "TooManyRemoveModulations";
    case PatchValidationError::NodeEntryInvalid:
        return "NodeEntryInvalid";
    case PatchValidationError::NodeIdInvalid:
        return "NodeIdInvalid";
    case PatchValidationError::NodeTypeInvalid:
        return "NodeTypeInvalid";
    case PatchValidationError::UnknownNodeType:
        return "UnknownNodeType";
    case PatchValidationError::DuplicateNodeId:
        return "DuplicateNodeId";
    case PatchValidationError::NodeIdTypeMismatch:
        return "NodeIdTypeMismatch";
    case PatchValidationError::InvalidParameterValue:
        return "InvalidParameterValue";
    case PatchValidationError::InvalidChoiceValue:
        return "InvalidChoiceValue";
    case PatchValidationError::UnknownParameterKey:
        return "UnknownParameterKey";
    case PatchValidationError::ConnectionEntryInvalid:
        return "ConnectionEntryInvalid";
    case PatchValidationError::ConnectionUnknownNode:
        return "ConnectionUnknownNode";
    case PatchValidationError::ConnectionInvalidPort:
        return "ConnectionInvalidPort";
    case PatchValidationError::ConnectionSelfCycle:
        return "ConnectionSelfCycle";
    case PatchValidationError::ModulationEntryInvalid:
        return "ModulationEntryInvalid";
    case PatchValidationError::ModulationUnknownNode:
        return "ModulationUnknownNode";
    case PatchValidationError::ModulationInvalidPort:
        return "ModulationInvalidPort";
    case PatchValidationError::ModulationSelfCycle:
        return "ModulationSelfCycle";
    case PatchValidationError::RemoveEntryInvalid:
        return "RemoveEntryInvalid";
    case PatchValidationError::RemoveModulationEntryInvalid:
        return "RemoveModulationEntryInvalid";
    case PatchValidationError::TimelineNotAllowed:
        return "TimelineNotAllowed";
    case PatchValidationError::MacrosNotAllowed:
        return "MacrosNotAllowed";
    case PatchValidationError::MidiRemoteNotAllowed:
        return "MidiRemoteNotAllowed";
    case PatchValidationError::InternalModuleNotAllowed:
        return "InternalModuleNotAllowed";
    }
    return "Unknown";
}

PatchValidationResult AIStateMapper::validateNodeParams(juce::AudioProcessor* processor,
                                                        const juce::DynamicObject* paramsObj) {
    std::set<juce::String> knownParamIds;
    for (auto* param : processor->getParameters()) {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
            knownParamIds.insert(p->paramID);
    }

    // applyParamsToProcessor (see file) only ever walks the processor's real parameters and looks
    // each one up BY NAME in this object — a key that doesn't match any real paramID is never
    // visited there, so it is silently dropped and the parameter is left at its default instead of
    // being rejected. Catch that here so the mismatch surfaces as a validation failure and the
    // retry/repair loop actually engages, rather than reporting success on a patch that quietly did
    // less than it claimed.
    for (const auto& entry : paramsObj->getProperties()) {
        const juce::String key = entry.name.toString();
        if (knownParamIds.count(key) == 0) {
            // Name the real parameter IDs, not just that one was wrong — same reasoning as
            // describeKnownIds() above: a retry that can't see the valid options re-rolls blind.
            juce::StringArray ids;
            for (const auto& id : knownParamIds)
                ids.add(id);
            return {false, PatchValidationError::UnknownParameterKey,
                    "Unknown parameter \"" + key +
                        "\" - it doesn't match any real parameter on this module and would be silently ignored, "
                        "leaving that value at its default. This module's actual parameter IDs are: " +
                        ids.joinIntoString(", ") + "."};
        }
    }

    for (auto* param : processor->getParameters()) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (!p || !paramsObj->hasProperty(p->paramID))
            continue;

        juce::var jsonValue = paramsObj->getProperty(p->paramID);
        bool isNumeric = jsonValue.isDouble() || jsonValue.isInt() || jsonValue.isInt64();

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) {
            if (jsonValue.isString()) {
                if (findChoiceIndex(choice, jsonValue.toString()) < 0) {
                    return {false, PatchValidationError::InvalidChoiceValue,
                            "Unrecognized choice \"" + jsonValue.toString() + "\" for parameter \"" + p->paramID +
                                "\"."};
                }
            } else if (isNumeric) {
                if (!std::isfinite(static_cast<double>(jsonValue))) {
                    return {false, PatchValidationError::InvalidParameterValue,
                            "Non-finite value for parameter \"" + p->paramID + "\"."};
                }
            } else {
                return {false, PatchValidationError::InvalidParameterValue,
                        "Invalid value for choice parameter \"" + p->paramID + "\"."};
            }
        } else if (dynamic_cast<juce::AudioParameterBool*>(p) != nullptr) {
            // Any JSON value is coercible to bool; nothing to reject.
        } else {
            if (!isNumeric && !jsonValue.isBool()) {
                return {false, PatchValidationError::InvalidParameterValue,
                        "Invalid value for parameter \"" + p->paramID + "\"."};
            }
            if (isNumeric && !std::isfinite(static_cast<double>(jsonValue))) {
                return {false, PatchValidationError::InvalidParameterValue,
                        "Non-finite value for parameter \"" + p->paramID + "\"."};
            }
        }
    }
    return {};
}

PatchValidationResult AIStateMapper::validatePatch(const juce::var& json, const juce::AudioProcessorGraph& graph,
                                                   bool clearExisting, bool trusted, bool allowInternalModuleTypes) {
    if (!json.isObject())
        return {false, PatchValidationError::NotAnObject, "Root is not an object."};
    auto* rootObj = json.getDynamicObject();
    if (!rootObj)
        return {false, PatchValidationError::NotAnObject, "Root dynamic object is null."};

    const juce::Array<juce::var>* nodesList = nullptr;
    if (rootObj->hasProperty("nodes")) {
        nodesList = rootObj->getProperty("nodes").getArray();
        if (nodesList == nullptr)
            return {false, PatchValidationError::NodesNotArray, "'nodes' property is not an array."};
    } else if (!rootObj->hasProperty("remove")) {
        return {false, PatchValidationError::MissingNodesOrRemove, "'nodes' and 'remove' properties are both missing."};
    }

    const juce::Array<juce::var>* connList = nullptr;
    if (rootObj->hasProperty("connections")) {
        connList = rootObj->getProperty("connections").getArray();
        if (connList == nullptr)
            return {false, PatchValidationError::ConnectionsNotArray, "'connections' property is not an array."};
    }

    const juce::Array<juce::var>* modList = nullptr;
    if (rootObj->hasProperty("modulations")) {
        modList = rootObj->getProperty("modulations").getArray();
        if (modList == nullptr)
            return {false, PatchValidationError::ModulationsNotArray, "'modulations' property is not an array."};
    }

    const juce::Array<juce::var>* removeList = nullptr;
    if (rootObj->hasProperty("remove")) {
        removeList = rootObj->getProperty("remove").getArray();
        if (removeList == nullptr)
            return {false, PatchValidationError::RemoveNotArray, "'remove' property is not an array."};
    }

    const juce::Array<juce::var>* removeModList = nullptr;
    if (rootObj->hasProperty("removeModulations")) {
        removeModList = rootObj->getProperty("removeModulations").getArray();
        if (removeModList == nullptr)
            return {false, PatchValidationError::RemoveModulationsNotArray,
                    "'removeModulations' property is not an array."};
    }

    // Trusted callers (locally-authored JSON: the user's own saved presets, undo/redo replay)
    // keep the legacy, purely-structural validation above and skip the strict checks below.
    if (trusted)
        return {};

    // "timeline"/"macros"/"midiRemote" are reserved for app-authored project data and are refused
    // here rather than ignored — see checkReservedKeysNotAllowed's own comment. Same class of rule
    // as the node "state" blob (see applyExtraStateToProcessor).
    if (const auto reserved = checkReservedKeysNotAllowed(rootObj); !reserved.ok)
        return reserved;

    if (nodesList && nodesList->size() > kMaxNodes)
        return {false, PatchValidationError::TooManyNodes,
                "Patch has " + juce::String(nodesList->size()) + " nodes, exceeding the limit of " +
                    juce::String(kMaxNodes) + "."};
    if (connList && connList->size() > kMaxConnections)
        return {false, PatchValidationError::TooManyConnections,
                "Patch has " + juce::String(connList->size()) + " connections, exceeding the limit of " +
                    juce::String(kMaxConnections) + "."};
    if (modList && modList->size() > kMaxModulations)
        return {false, PatchValidationError::TooManyModulations,
                "Patch has " + juce::String(modList->size()) + " modulations, exceeding the limit of " +
                    juce::String(kMaxModulations) + "."};
    if (removeList && removeList->size() > kMaxRemovals)
        return {false, PatchValidationError::TooManyRemovals,
                "Patch has " + juce::String(removeList->size()) + " removals, exceeding the limit of " +
                    juce::String(kMaxRemovals) + "."};
    if (removeModList && removeModList->size() > kMaxRemoveModulations)
        return {false, PatchValidationError::TooManyRemoveModulations,
                "Patch has " + juce::String(removeModList->size()) + " modulation removals, exceeding the limit of " +
                    juce::String(kMaxRemoveModulations) + "."};

    // Ids this patch may legally reference: nodes it creates, plus (in merge mode) nodes that
    // already exist in the live graph. Populated fully before any connection/modulation is
    // checked, and nothing here mutates the graph — that only happens after validation passes.
    std::set<juce::uint32> knownIds;
    // Merge mode only: the live node each patch id would land on, so a patch node that reuses an
    // existing id for a DIFFERENT module can be rejected before anything is touched (see below).
    // Ids the patch also removes are excluded — apply processes "remove" first, so re-using such
    // an id creates a genuinely new node and aliases nothing.
    std::map<juce::uint32, juce::AudioProcessor*> liveNodesById;
    if (!clearExisting) {
        std::set<juce::uint32> removedIds;
        if (removeList) {
            for (const auto& idVar : *removeList) {
                juce::uint32 removedId = 0;
                if (detail::extractUnsignedInt(idVar, removedId))
                    removedIds.insert(removedId);
            }
        }

        for (auto* node : graph.getNodes()) {
            knownIds.insert(node->nodeID.uid);
            if (removedIds.count(node->nodeID.uid) == 0)
                liveNodesById[node->nodeID.uid] = node->getProcessor();
        }
    }

    std::set<juce::uint32> patchNodeIds;
    if (nodesList) {
        for (const auto& nVar : *nodesList) {
            auto* nObj = nVar.getDynamicObject();
            if (!nObj)
                return {false, PatchValidationError::NodeEntryInvalid, "Node entry is not an object."};

            if (!nObj->hasProperty("id"))
                return {false, PatchValidationError::NodeIdInvalid, "Node is missing 'id'."};
            juce::uint32 nodeId = 0;
            if (!detail::extractUnsignedInt(nObj->getProperty("id"), nodeId))
                return {false, PatchValidationError::NodeIdInvalid,
                        "Node 'id' must be an integer within uint32 range."};

            if (!nObj->hasProperty("type"))
                return {false, PatchValidationError::NodeTypeInvalid, "Node is missing 'type'."};
            juce::var typeVar = nObj->getProperty("type");
            if (!typeVar.isString())
                return {false, PatchValidationError::NodeTypeInvalid, "Node 'type' must be a string."};
            juce::String type = typeVar.toString();
            if (type.isEmpty() || type.length() > kMaxTypeNameLength)
                return {false, PatchValidationError::NodeTypeInvalid,
                        "Node 'type' must be 1-" + juce::String(kMaxTypeNameLength) + " characters."};

            if (patchNodeIds.count(nodeId) > 0)
                return {false, PatchValidationError::DuplicateNodeId,
                        "Duplicate node id " + juce::String(nodeId) + " within patch."};
            patchNodeIds.insert(nodeId);

            // Internal-only types are refused here, on the validator itself, rather than being
            // left to the schema's `type` enum. The schema is a hint the backend enforces as a
            // grammar for OUR provider; a patch can also arrive from a local model, or from any
            // future caller that never saw the schema — and "non-authorable" has to mean
            // untrusted-unreachable, not merely un-suggested. The trusted path is untouched — our
            // own saves must round-trip a Track In node — and a caller gating app-authored data it
            // is about to apply trusted opts out via allowInternalModuleTypes (see AIStateMapper.h).
            if (!allowInternalModuleTypes && detail::isInternalOnlyModule(type))
                return {false, PatchValidationError::InternalModuleNotAllowed,
                        "Module type \"" + type +
                            "\" is internal to the app and cannot be created from a patch. Use one of the module "
                            "types listed in the schema."};

            // Resolve the type via the real factory up front, rather than discovering an
            // unknown type mid-apply after other nodes may already have been created.
            auto probe = createModule(type);
            if (!probe)
                return {false, PatchValidationError::UnknownNodeType, "Unknown module type: \"" + type + "\"."};

            // Merge mode: an id that already names a live node of a DIFFERENT type is an identity
            // collision, not a new module. Applying it would create a second node and rebind
            // idMap[id] to it, so every later connection/modulation in the same patch that meant
            // the ORIGINAL node silently re-points at the new one. Reject the patch whole — the
            // model has to pick an unused id (or match the existing type to edit it in place).
            // Checked after the factory probe so a made-up type is still reported as such.
            if (auto live = liveNodesById.find(nodeId); live != liveNodesById.end()) {
                if (!detail::patchTypeMatchesProcessor(live->second, type))
                    return {false, PatchValidationError::NodeIdTypeMismatch,
                            "Node id " + juce::String(nodeId) + " already exists in this patch as a \"" +
                                getFactoryTypeName(live->second) + "\", so it cannot be declared as a \"" + type +
                                "\". Give a new module an id that no existing node uses, or repeat the existing "
                                "type to change that module's parameters instead."};
            }

            if (nObj->hasProperty("params")) {
                if (auto* pObj = nObj->getProperty("params").getDynamicObject()) {
                    auto paramResult = validateNodeParams(probe.get(), pObj);
                    if (!paramResult.ok)
                        return paramResult;
                }
            }
        }
    }
    knownIds.insert(patchNodeIds.begin(), patchNodeIds.end());

    if (connList) {
        for (const auto& cVar : *connList) {
            auto* cObj = cVar.getDynamicObject();
            if (!cObj)
                return {false, PatchValidationError::ConnectionEntryInvalid, "Connection entry is not an object."};

            juce::uint32 src = 0, dst = 0;
            if (!detail::extractUnsignedInt(cObj->getProperty("src"), src) || knownIds.count(src) == 0)
                return {false, PatchValidationError::ConnectionUnknownNode,
                        "Connection references unknown source node id " + describeId(cObj->getProperty("src")) + ". " +
                            describeKnownIds(knownIds)};
            if (!detail::extractUnsignedInt(cObj->getProperty("dst"), dst) || knownIds.count(dst) == 0)
                return {false, PatchValidationError::ConnectionUnknownNode,
                        "Connection references unknown destination node id " + describeId(cObj->getProperty("dst")) +
                            ". " + describeKnownIds(knownIds)};
            if (src == dst)
                return {false, PatchValidationError::ConnectionSelfCycle,
                        "Connection would create a self-cycle (src == dst == " + juce::String(src) + ")."};

            int srcPort = static_cast<int>(cObj->getProperty("srcPort"));
            int dstPort = static_cast<int>(cObj->getProperty("dstPort"));
            if (!isValidPatchPort(srcPort) || !isValidPatchPort(dstPort))
                return {false, PatchValidationError::ConnectionInvalidPort, "Connection port index out of range."};
        }
    }

    if (modList) {
        for (const auto& mVar : *modList) {
            auto* mObj = mVar.getDynamicObject();
            if (!mObj)
                return {false, PatchValidationError::ModulationEntryInvalid, "Modulation entry is not an object."};

            juce::uint32 source = 0, dest = 0;
            if (!detail::extractUnsignedInt(mObj->getProperty("source"), source) || knownIds.count(source) == 0)
                return {false, PatchValidationError::ModulationUnknownNode,
                        "Modulation references unknown source node id " + describeId(mObj->getProperty("source")) +
                            ". " + describeKnownIds(knownIds)};
            if (!detail::extractUnsignedInt(mObj->getProperty("dest"), dest) || knownIds.count(dest) == 0)
                return {false, PatchValidationError::ModulationUnknownNode,
                        "Modulation references unknown destination node id " + describeId(mObj->getProperty("dest")) +
                            ". " + describeKnownIds(knownIds)};
            if (source == dest)
                return {false, PatchValidationError::ModulationSelfCycle,
                        "Modulation would create a self-cycle (source == dest == " + juce::String(source) + ")."};

            // Modulation ports are always real audio/CV channels, never MIDI — reject the -1
            // sentinel here even though it's legal for plain connections.
            int destPort = static_cast<int>(mObj->getProperty("destPort"));
            if (destPort < 0 || !isValidPatchPort(destPort))
                return {false, PatchValidationError::ModulationInvalidPort, "Modulation destPort out of range."};

            if (mObj->hasProperty("sourcePort")) {
                int sourcePort = static_cast<int>(mObj->getProperty("sourcePort"));
                if (sourcePort < 0 || !isValidPatchPort(sourcePort))
                    return {false, PatchValidationError::ModulationInvalidPort, "Modulation sourcePort out of range."};
            }
        }
    }

    if (removeList) {
        for (const auto& idVar : *removeList) {
            juce::uint32 tmp = 0;
            if (!detail::extractUnsignedInt(idVar, tmp))
                return {false, PatchValidationError::RemoveEntryInvalid,
                        "Entries in 'remove' must be integers within uint32 range."};
        }
    }

    if (removeModList) {
        for (const auto& rVar : *removeModList) {
            auto* rObj = rVar.getDynamicObject();
            juce::uint32 tmp = 0;
            if (!rObj || !rObj->hasProperty("source") ||
                !detail::extractUnsignedInt(rObj->getProperty("source"), tmp) || !rObj->hasProperty("dest") ||
                !detail::extractUnsignedInt(rObj->getProperty("dest"), tmp) || !rObj->hasProperty("destPort"))
                return {false, PatchValidationError::RemoveModulationEntryInvalid,
                        "Invalid 'removeModulations' entry."};
        }
    }

    return {};
}

} // namespace synth
