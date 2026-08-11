#include "AIStateMapper.h"
#include "../Modules/ADSRModule.h"
#include "../Modules/AttenuverterModule.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/FX/ChorusModule.h"

#include "../Modules/EnvelopeFollowerModule.h"
#include "../Modules/FX/BitcrusherModule.h"
#include "../Modules/FX/CompressorModule.h"
#include "../Modules/FX/DelayModule.h"
#include "../Modules/FX/DistortionModule.h"
#include "../Modules/FX/FlangerModule.h"
#include "../Modules/FX/LimiterModule.h"
#include "../Modules/FX/ParametricEQModule.h"
#include "../Modules/FX/PhaserModule.h"
#include "../Modules/FX/PitchShifterModule.h"
#include "../Modules/FX/ReverbModule.h"
#include "../Modules/FilterModule.h"
#include "../Modules/LFOModule.h"
#include "../Modules/MacroControlModule.h"
#include "../Modules/MathModule.h"
#include "../Modules/MidiKeyboardModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/NoiseModule.h"
#include "../Modules/OscillatorModule.h"
#include "../Modules/PolyMidiModule.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SampleHoldModule.h"
#include "../Modules/SamplerModule.h"
#include "../Modules/SequencerModule.h"
#include "../Modules/VCAModule.h"
#include "../Modules/VoiceMixerModule.h"
#include "../Modules/WavetableOscillatorModule.h"
#include <cmath>
#include <functional> // For std::function
#include <limits>
#include <map>
#include <set>
#include <unordered_map> // For the factory map

namespace synth {

typedef juce::AudioProcessorGraph::AudioGraphIOProcessor AudioGraphIOProcessor;

using ModuleFactoryFunc = std::function<std::unique_ptr<juce::AudioProcessor>()>;

// Factory map for module creation
static const std::unordered_map<juce::String, ModuleFactoryFunc> moduleFactory = {
    {"Audio Input", []() { return std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioInputNode); }},
    {"Audio Output", []() { return std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode); }},
    {"Midi Input", []() { return std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::midiInputNode); }},
    {"Oscillator", []() { return std::make_unique<OscillatorModule>(); }},
    {"Filter", []() { return std::make_unique<FilterModule>(); }},
    {"VCA", []() { return std::make_unique<VCAModule>(); }},
    {"ADSR", []() { return std::make_unique<ADSRModule>("ADSR"); }}, // ADSR constructor for generic case
    {"Sequencer", []() { return std::make_unique<SequencerModule>(); }},
    {"LFO", []() { return std::make_unique<LFOModule>(); }},
    {"Distortion", []() { return std::make_unique<DistortionModule>(); }},
    {"Delay", []() { return std::make_unique<DelayModule>(); }},
    {"Reverb", []() { return std::make_unique<ReverbModule>(); }},
    {"MIDI Keyboard", []() { return std::make_unique<MidiKeyboardModule>(); }},
    {"Amp Env", []() { return std::make_unique<ADSRModule>("Amp Env"); }},
    {"Filter Env", []() { return std::make_unique<ADSRModule>("Filter Env"); }},
    {"Poly MIDI", []() { return std::make_unique<PolyMidiModule>(); }},
    {"Poly Sequencer", []() { return std::make_unique<PolySequencerModule>(); }},
    {"Attenuverter", []() { return std::make_unique<AttenuverterModule>(); }},
    {"Mod Slot", []() { return std::make_unique<AttenuverterModule>(); }},
    {"Chorus", []() { return std::make_unique<ChorusModule>(); }},
    {"Phaser", []() { return std::make_unique<PhaserModule>(); }},
    {"Compressor", []() { return std::make_unique<CompressorModule>(); }},
    {"Flanger", []() { return std::make_unique<FlangerModule>(); }},
    {"Limiter", []() { return std::make_unique<LimiterModule>(); }},
    {"Parametric EQ", []() { return std::make_unique<ParametricEQModule>(); }},
    {"Voice Mixer", []() { return std::make_unique<VoiceMixerModule>(); }},
    {"Bitcrusher", []() { return std::make_unique<BitcrusherModule>(); }},
    {"Pitch Shifter", []() { return std::make_unique<PitchShifterModule>(); }},
    {"Noise", []() { return std::make_unique<NoiseModule>(); }},
    {"Envelope Follower", []() { return std::make_unique<EnvelopeFollowerModule>(); }},
    {"Math", []() { return std::make_unique<MathModule>(); }},
    {"Macros", []() { return std::make_unique<MacroControlModule>(); }},
    {"Sample & Hold", []() { return std::make_unique<SampleHoldModule>(); }},
    {"Sampler", []() { return std::make_unique<SamplerModule>(); }},
    {"Wavetable", []() { return std::make_unique<WavetableOscillatorModule>(); }},
    {"External MIDI", []() { return std::make_unique<ExternalMidiModule>(); }}};

namespace {

// The module types a model may never author, as an explicit set with a reason recorded against
// each entry — not a chain of equality tests, which said nothing about why a name was on it.
//
// Registering a module in moduleFactory above makes it model-authorable BY DEFAULT. That default
// is right for an ordinary DSP module and wrong for anything that names an external resource or
// carries privileged state (a hosted plugin binary, a timeline feed, a file path): such a module
// belongs in this set at the moment it is registered. The exact resulting allowlist is pinned by
// AIStateMapperTest.AuthorableModuleTypesGolden, so either kind of addition fails the build until
// the choice is made deliberately.
const std::set<juce::String> kNonAuthorableModuleTypes = {
    // Attenuverters are an implementation detail of the `modulations` array — applyJSONToGraph
    // creates them itself — so exposing them would invite the model to hand-build modulation
    // chains that the mod matrix then can't read back.
    "Attenuverter",
    // The same AttenuverterModule, registered under the name the modulation UI uses for it.
    "Mod Slot",
};

bool isInternalOnlyModule(const juce::String& typeName) { return kNonAuthorableModuleTypes.count(typeName) > 0; }

// Whether a merge patch's "type" designates the module that already lives under that node id.
// Two namespaces meet here: the factory key graphToJSON writes, and the processor's display name.
// They differ for the ADSR aliases — an "Amp Env" node serializes as type "ADSR" — so comparing
// against only one of them makes an update-in-place silently fall through to "create a second
// node", which is the aliasing this predicate exists to prevent.
bool patchTypeMatchesProcessor(juce::AudioProcessor* processor, const juce::String& type) {
    return processor->getName() == type || AIStateMapper::getFactoryTypeName(processor) == type;
}

// Accepts only whole numbers representable as a uint32 (matches juce::AudioProcessorGraph::NodeID's
// underlying type). Rejects negatives, fractional values, and non-numeric JSON values outright —
// there is no legitimate node id that isn't one of these.
bool extractUnsignedInt(const juce::var& v, juce::uint32& out) {
    if (v.isInt() || v.isInt64()) {
        auto i = static_cast<juce::int64>(v);
        if (i < 0 || i > static_cast<juce::int64>(std::numeric_limits<juce::uint32>::max()))
            return false;
        out = static_cast<juce::uint32>(i);
        return true;
    }
    if (v.isDouble()) {
        double d = static_cast<double>(v);
        if (!std::isfinite(d) || d < 0.0 || d > static_cast<double>(std::numeric_limits<juce::uint32>::max()) ||
            d != std::floor(d))
            return false;
        out = static_cast<juce::uint32>(d);
        return true;
    }
    return false;
}

// -1 is the MIDI sentinel used by graphToJSON/applyJSONToGraph (mapped to
// juce::AudioProcessorGraph::midiChannelIndex on apply); anything else must be a plausible raw
// channel index.
bool isValidPatchPort(int port) {
    if (port == -1)
        return true;
    return port >= 0 && port <= AIStateMapper::kMaxPortIndex;
}

// Adopts a patch node's "uuid" onto the live node — trusted callers only. Untrusted input never
// dictates identity: a model could otherwise hand two nodes the same uuid, or claim the uuid of a
// node that automation lanes and track bindings already point at. Nodes created from untrusted
// JSON simply have no uuid until graphToJSON generates a fresh one for them.
void adoptUuidIfTrusted(juce::AudioProcessorGraph::Node* node, const juce::DynamicObject* nObj, bool trusted) {
    if (!trusted || node == nullptr || !nObj->hasProperty("uuid"))
        return;
    const juce::var uuidVar = nObj->getProperty("uuid");
    if (uuidVar.isString() && uuidVar.toString().isNotEmpty())
        node->properties.set("uuid", uuidVar.toString());
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
    }
    return "Unknown";
}

PatchValidationResult AIStateMapper::validateNodeParams(juce::AudioProcessor* processor,
                                                        const juce::DynamicObject* paramsObj) {
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
                                                   bool clearExisting, bool trusted) {
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

    // "timeline" is reserved for app-authored project data and is refused here rather than
    // ignored. The validator lets unknown keys through, so a later build that starts honouring
    // timeline data would silently begin executing provider-authored automation against patches
    // accepted today; refusing now means that door can only be opened by a commit that deletes
    // this check. Same class of rule as the node "state" blob (see applyExtraStateToProcessor).
    if (rootObj->hasProperty("timeline"))
        return {false, PatchValidationError::TimelineNotAllowed,
                "Patch suggestions must not contain a \"timeline\" property — timeline and automation "
                "data is not accepted from a patch suggestion. Remove it and resend only nodes, "
                "connections and modulations."};

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
                if (extractUnsignedInt(idVar, removedId))
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
            if (!extractUnsignedInt(nObj->getProperty("id"), nodeId))
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
                if (!patchTypeMatchesProcessor(live->second, type))
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
            if (!extractUnsignedInt(cObj->getProperty("src"), src) || knownIds.count(src) == 0)
                return {false, PatchValidationError::ConnectionUnknownNode,
                        "Connection references unknown source node id " + describeId(cObj->getProperty("src")) + ". " +
                            describeKnownIds(knownIds)};
            if (!extractUnsignedInt(cObj->getProperty("dst"), dst) || knownIds.count(dst) == 0)
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
            if (!extractUnsignedInt(mObj->getProperty("source"), source) || knownIds.count(source) == 0)
                return {false, PatchValidationError::ModulationUnknownNode,
                        "Modulation references unknown source node id " + describeId(mObj->getProperty("source")) +
                            ". " + describeKnownIds(knownIds)};
            if (!extractUnsignedInt(mObj->getProperty("dest"), dest) || knownIds.count(dest) == 0)
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
            if (!extractUnsignedInt(idVar, tmp))
                return {false, PatchValidationError::RemoveEntryInvalid,
                        "Entries in 'remove' must be integers within uint32 range."};
        }
    }

    if (removeModList) {
        for (const auto& rVar : *removeModList) {
            auto* rObj = rVar.getDynamicObject();
            juce::uint32 tmp = 0;
            if (!rObj || !rObj->hasProperty("source") || !extractUnsignedInt(rObj->getProperty("source"), tmp) ||
                !rObj->hasProperty("dest") || !extractUnsignedInt(rObj->getProperty("dest"), tmp) ||
                !rObj->hasProperty("destPort"))
                return {false, PatchValidationError::RemoveModulationEntryInvalid,
                        "Invalid 'removeModulations' entry."};
        }
    }

    return {};
}

std::unique_ptr<juce::AudioProcessor> AIStateMapper::createModule(const juce::String& type) {
    auto it = moduleFactory.find(type);
    if (it != moduleFactory.end()) {
        return it->second();
    }

    // Strip trailing number suffix for backwards compatibility (e.g., "Oscillator 1" → "Oscillator")
    juce::String baseName = type;
    int lastSpace = baseName.lastIndexOf(" ");
    if (lastSpace != -1 && baseName.substring(lastSpace + 1).containsOnly("0123456789"))
        baseName = baseName.substring(0, lastSpace);

    it = moduleFactory.find(baseName);
    if (it != moduleFactory.end())
        return it->second();

    // Handle ADSR variants with custom names (e.g., "Amp Env", "Filter Env")
    if (baseName.containsIgnoreCase("Env") || baseName.containsIgnoreCase("ADSR"))
        return std::make_unique<ADSRModule>(baseName);

    if (baseName == "MidiKeyboard")
        return std::make_unique<MidiKeyboardModule>();

    // JUCE's display name for the graph's MIDI input node, which older saves emitted as the node
    // type before getFactoryTypeName mapped it back to the factory key.
    if (baseName == "MIDI Input")
        return std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::midiInputNode);

    juce::Logger::writeToLog("AIStateMapper: Unknown module type: " + type);
    return nullptr;
}

juce::StringArray AIStateMapper::moduleFactoryTypeNames() {
    juce::StringArray names;
    for (const auto& entry : moduleFactory)
        names.add(entry.first);
    names.sort(false); // moduleFactory is unordered; callers want a stable order
    return names;
}

juce::StringArray AIStateMapper::authorableModuleTypes() {
    juce::StringArray names;
    for (const auto& name : moduleFactoryTypeNames())
        if (!isInternalOnlyModule(name))
            names.add(name);
    return names;
}

juce::String AIStateMapper::getFactoryTypeName(juce::AudioProcessor* processor) {
    if (auto* mb = dynamic_cast<ModuleBase*>(processor)) {
        switch (mb->getModuleType()) {
        case ModuleType::Oscillator:
            return "Oscillator";
        case ModuleType::Filter:
            return "Filter";
        case ModuleType::VCA:
            return "VCA";
        case ModuleType::ADSR:
            // "ADSR", "Amp Env" and "Filter Env" are three factory keys for one module, told apart
            // only by the display name it was constructed with — so emit that name rather than the
            // generic key, or an Amp Env comes back from every save/undo as a plain "ADSR".
            // createModule resolves any name containing "Env"/"ADSR", so this always round-trips.
            return mb->getName();
        case ModuleType::LFO:
            return "LFO";
        case ModuleType::Sequencer:
            return "Sequencer";
        case ModuleType::PolySequencer:
            // Must be the factory key "Poly Sequencer", not "Sequencer": this string is what
            // graphToJSON writes, so returning the mono type here downgraded a Poly Sequencer to a
            // SequencerModule on every save/load and every structural undo (issue #196).
            return "Poly Sequencer";
        case ModuleType::MidiKeyboard:
            return "MIDI Keyboard";
        case ModuleType::PolyMidi:
            return "Poly MIDI";
        case ModuleType::Attenuverter:
            return "Attenuverter";
        case ModuleType::Delay:
            return "Delay";
        case ModuleType::Distortion:
            return "Distortion";
        case ModuleType::Reverb:
            return "Reverb";
        case ModuleType::Chorus:
            return "Chorus";
        case ModuleType::Phaser:
            return "Phaser";
        case ModuleType::Compressor:
            return "Compressor";
        case ModuleType::Flanger:
            return "Flanger";
        case ModuleType::Limiter:
            return "Limiter";
        case ModuleType::ParametricEQ:
            return "Parametric EQ";
        case ModuleType::VoiceMixer:
            return "Voice Mixer";
        case ModuleType::Bitcrusher:
            return "Bitcrusher";
        case ModuleType::PitchShifter:
            return "Pitch Shifter";
        case ModuleType::Noise:
            return "Noise";
        case ModuleType::EnvelopeFollower:
            return "Envelope Follower";
        case ModuleType::Math:
            return "Math";
        case ModuleType::MacroControl:
            return "Macros";
        case ModuleType::Sampler:
            return "Sampler";
        case ModuleType::Wavetable:
            return "Wavetable";
        case ModuleType::SampleHold:
            return "Sample & Hold";
        case ModuleType::ExternalMidi:
            return "External MIDI";
        }
    }

    // JUCE names the graph's MIDI input node "MIDI Input", which is NOT the factory key
    // ("Midi Input"): falling through to getName() below would emit a type string createModule
    // cannot resolve, and the node would silently vanish on the next load. The audio I/O nodes'
    // names already match their keys exactly, so only this one needs mapping.
    if (auto* io = dynamic_cast<AudioGraphIOProcessor*>(processor))
        if (io->getType() == AudioGraphIOProcessor::midiInputNode)
            return "Midi Input";

    return processor->getName();
}

juce::var AIStateMapper::graphToJSON(juce::AudioProcessorGraph& graph) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("schemaVersion", kSchemaVersion);

    juce::Array<juce::var> nodes;
    for (auto* node : graph.getNodes()) {
        if (auto* processor = node->getProcessor()) {
            juce::DynamicObject::Ptr n = new juce::DynamicObject();
            n->setProperty("id", (int)node->nodeID.uid);
            n->setProperty("type", getFactoryTypeName(processor));

            // Stable per-node identity, generated on first save and persisted back onto the node so
            // every later save of the same node emits the same string. The integer "id" cannot
            // serve this purpose: merge-mode apply renumbers nodes, so anything holding a
            // long-lived reference (automation lanes, timeline track bindings) keys on the uuid.
            juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isEmpty()) {
                uuid = juce::Uuid().toDashedString();
                node->properties.set("uuid", uuid);
            }
            n->setProperty("uuid", uuid);

            // Params — store denormalized values to match applyJSONToGraph expectations
            juce::DynamicObject::Ptr params = new juce::DynamicObject();
            for (auto* param : processor->getParameters()) {
                if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                        // Store choice as string name for readability
                        params->setProperty(choice->paramID, choice->getCurrentChoiceName());
                    } else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                        params->setProperty(boolParam->paramID, boolParam->get());
                    } else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param)) {
                        // Store denormalized value
                        float denormalized = ranged->getNormalisableRange().convertFrom0to1(ranged->getValue());
                        params->setProperty(ranged->paramID, denormalized);
                    } else {
                        params->setProperty(p->paramID, p->getValue());
                    }
                }
            }
            n->setProperty("params", juce::var(params.get()));

            // Non-parameter module state (e.g. the Sampler's loaded file). Emitted only when the
            // module has some, so every other node's JSON is byte-identical to before.
            if (auto* mb = dynamic_cast<ModuleBase*>(processor)) {
                juce::var extraState = mb->getExtraState();
                if (!extraState.isVoid())
                    n->setProperty("state", extraState);
            }

            // Position
            juce::DynamicObject::Ptr pos = new juce::DynamicObject();
            pos->setProperty("x", node->properties["x"]);
            pos->setProperty("y", node->properties["y"]);
            n->setProperty("position", juce::var(pos.get()));

            nodes.add(juce::var(n.get()));
        }
    }
    root->setProperty("nodes", nodes);

    juce::Array<juce::var> connections;
    for (const auto& conn : graph.getConnections()) {
        juce::DynamicObject::Ptr c = new juce::DynamicObject();
        c->setProperty("src", (int)conn.source.nodeID.uid);

        int srcPort = conn.source.channelIndex;
        int dstPort = conn.destination.channelIndex;
        if (conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
            srcPort = -1;
        if (conn.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
            dstPort = -1;

        c->setProperty("srcPort", srcPort);
        c->setProperty("dst", (int)conn.destination.nodeID.uid);
        c->setProperty("dstPort", dstPort);
        c->setProperty("isMidi", conn.source.isMIDI());
        connections.add(juce::var(c.get()));
    }
    root->setProperty("connections", connections);

    // Scan for AttenuverterModule nodes and emit modulations array
    juce::Array<juce::var> modulations;
    for (auto* node : graph.getNodes()) {
        if (auto* attenverter = dynamic_cast<AttenuverterModule*>(node->getProcessor())) {
            // Find source connection (input to attenuverter channel 0)
            bool hasSource = false;
            juce::AudioProcessorGraph::NodeID sourceNodeID;
            int sourceChannel = 0;
            for (const auto& conn : graph.getConnections()) {
                if (conn.destination.nodeID == node->nodeID && conn.destination.channelIndex == 0) {
                    sourceNodeID = conn.source.nodeID;
                    sourceChannel = conn.source.channelIndex;
                    hasSource = true;
                    break;
                }
            }

            // Find destination connection (output from attenuverter channel 0)
            bool hasDest = false;
            juce::AudioProcessorGraph::NodeID destNodeID;
            int destChannel = 0;
            for (const auto& conn : graph.getConnections()) {
                if (conn.source.nodeID == node->nodeID && conn.source.channelIndex == 0) {
                    destNodeID = conn.destination.nodeID;
                    destChannel = conn.destination.channelIndex;
                    hasDest = true;
                    break;
                }
            }

            // Only create modulation entry if both source and dest connections exist
            if (hasSource && hasDest) {
                juce::DynamicObject::Ptr modEntry = new juce::DynamicObject();
                modEntry->setProperty("source", (int)sourceNodeID.uid);
                modEntry->setProperty("sourcePort", sourceChannel);
                modEntry->setProperty("dest", (int)destNodeID.uid);
                modEntry->setProperty("destPort", destChannel);

                if (auto* param = findParameterByID(attenverter, "amount")) {
                    float amount = param->getNormalisableRange().convertFrom0to1(param->getValue());
                    modEntry->setProperty("amount", amount);
                }

                if (auto* param = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(attenverter, "bypassed"))) {
                    modEntry->setProperty("bypass", param->get());
                }

                modulations.add(juce::var(modEntry.get()));
            }
        }
    }
    root->setProperty("modulations", modulations);

    return juce::var(root.get());
}

juce::String AIStateMapper::getModuleSchema() {
    juce::String schema = "### Available Modules and Parameters\n\n";

    for (const auto& entry : moduleFactory) {
        // Hide non-authorable modules from AI — modulation uses the "modulations" array instead
        if (isInternalOnlyModule(entry.first))
            continue;

        auto processor = entry.second();
        if (!processor)
            continue;

        schema += "#### " + entry.first + "\n";
        schema += "| Parameter ID | Name | Range / Options | Default |\n";
        schema += "| :--- | :--- | :--- | :--- |\n";

        for (auto* param : processor->getParameters()) {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param)) {
                juce::String rangeStr;
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                    rangeStr = "Choice: [" + choice->choices.joinIntoString(", ") + "]";
                } else if (dynamic_cast<juce::AudioParameterBool*>(param)) {
                    rangeStr = "Boolean (0 or 1)";
                } else {
                    auto range = p->getNormalisableRange();
                    rangeStr = juce::String(range.start) + " to " + juce::String(range.end);
                }

                schema += "| `" + p->paramID + "` | " + p->name + " | " + rangeStr + " | " +
                          juce::String(p->getDefaultValue()) + " |\n";
            }
        }
        schema += "\n";
    }

    // Modulation targets section
    schema += "### Modulation Targets\n\n";
    schema += "Use the `modulations` array to route modulation sources to these targets.\n\n";
    schema += "| Module | Target | Port |\n";
    schema += "| :--- | :--- | :--- |\n";

    for (const auto& entry : moduleFactory) {
        auto processor = entry.second();
        if (!processor)
            continue;
        if (auto* mb = dynamic_cast<ModuleBase*>(processor.get())) {
            auto targets = mb->getModulationTargets();
            for (const auto& t : targets) {
                schema += "| " + entry.first + " | " + t.name + " | " + juce::String(t.channelIndex) + " |\n";
            }
        }
    }

    schema += "\n**Modulation Sources**: LFO, ADSR, Amp Env, Filter Env, Oscillator, Sequencer\n\n";

    return schema;
}

int AIStateMapper::findChoiceIndex(juce::AudioParameterChoice* p, const juce::String& choiceText) {
    // 1. Exact match
    int index = p->choices.indexOf(choiceText);
    if (index >= 0)
        return index;

    // 2. Case-insensitive match
    for (int i = 0; i < p->choices.size(); ++i) {
        if (p->choices[i].equalsIgnoreCase(choiceText))
            return i;
    }

    return -1;
}

void AIStateMapper::applyParamsToProcessor(juce::AudioProcessor* processor, const juce::DynamicObject* paramsObj,
                                           bool trusted) {
    for (auto* param : processor->getParameters()) {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            if (paramsObj->hasProperty(p->paramID)) {
                auto jsonValue = paramsObj->getProperty(p->paramID);

                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) {
                    if (jsonValue.isString()) {
                        int index = findChoiceIndex(choice, jsonValue.toString());
                        if (index >= 0) {
                            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float)index));
                        }
                    } else {
                        auto choiceRange = p->getNormalisableRange();
                        float val = choiceRange.snapToLegalValue((float)jsonValue);
                        p->setValueNotifyingHost(choiceRange.convertTo0to1(val));
                    }
                } else if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p)) {
                    b->setValueNotifyingHost((bool)jsonValue ? 1.0f : 0.0f);
                } else {
                    float val = (float)jsonValue;
                    auto range = p->getNormalisableRange();

                    // Detect likely normalized 0-1 values from AI models that ignore range instructions.
                    // If the actual range extends beyond [0,1] but the value is within [0,1],
                    // the AI probably sent a normalized value — convert it to the actual range.
                    // Skip this heuristic for integer params — small values like 0 or 1 are
                    // almost always valid denormalized values, not normalized.
                    bool isIntParam = (dynamic_cast<juce::AudioParameterInt*>(p) != nullptr);
                    bool rangeIsUnitInterval = (range.start >= 0.0f && range.end <= 1.0f);
                    if (!trusted && !isIntParam && !rangeIsUnitInterval && val >= 0.0f && val <= 1.0f) {
                        val = range.convertFrom0to1(val);
                    }

                    val = range.snapToLegalValue(val);
                    float normalizedValue = range.convertTo0to1(val);
                    p->setValueNotifyingHost(normalizedValue);
                }
            }
        }
    }
}

void AIStateMapper::applyExtraStateToProcessor(juce::AudioProcessor* processor, const juce::DynamicObject* nodeObj,
                                               bool trusted) {
    // Untrusted (model-authored) JSON never reaches setExtraState: a module may read this as a
    // filename (SamplerModule does), so honouring it for remote output would let a patch suggestion
    // name an arbitrary file for the app to open. Our own snapshots and presets are trusted.
    if (!trusted || !nodeObj->hasProperty("state"))
        return;
    if (auto* mb = dynamic_cast<ModuleBase*>(processor))
        mb->setExtraState(nodeObj->getProperty("state"));
}

bool AIStateMapper::applyJSONToGraph(const juce::var& json, juce::AudioProcessorGraph& graph, bool clearExisting,
                                     bool trusted, bool autoConnectNewNodes) {
    if (!json.isObject()) {
        juce::Logger::writeToLog("applyJSONToGraph: JSON is not an object.");
        return false;
    }
    auto* rootObj = json.getDynamicObject();
    if (!rootObj) {
        juce::Logger::writeToLog("applyJSONToGraph: JSON dynamic object is null.");
        return false;
    }

    // Validate the entire patch before making any changes — a rejected patch must never be
    // partially applied.
    auto validation = validatePatch(json, graph, clearExisting, trusted);
    if (!validation.ok) {
        juce::Logger::writeToLog("applyJSONToGraph: JSON patch validation failed: " + validation.message);
        return false;
    }

    const juce::ScopedLock sl(graph.getCallbackLock());

    if (clearExisting) {
        graph.clear();
    }

    std::map<int, juce::AudioProcessorGraph::NodeID> idMap;
    std::set<juce::AudioProcessorGraph::NodeID> newlyCreatedNodes;

    // Pre-populate idMap with existing nodes when merging
    if (!clearExisting) {
        for (auto* node : graph.getNodes()) {
            idMap[(int)node->nodeID.uid] = node->nodeID;
        }
    }

    // Process removals before adding new nodes
    if (rootObj->hasProperty("remove")) {
        auto* removeList = rootObj->getProperty("remove").getArray();
        if (removeList) {
            for (const auto& idVar : *removeList) {
                int nodeIdToRemove = (int)idVar;
                auto juceNodeId = juce::AudioProcessorGraph::NodeID((juce::uint32)nodeIdToRemove);
                if (graph.getNodeForId(juceNodeId) != nullptr) {
                    graph.removeNode(juceNodeId);
                }
                idMap.erase(nodeIdToRemove);
            }
        }
    }

    // Process removeModulations before adding new modulations
    if (rootObj->hasProperty("removeModulations")) {
        auto* rmModList = rootObj->getProperty("removeModulations").getArray();
        if (rmModList) {
            for (const auto& rmModVar : *rmModList) {
                if (auto* rmModObj = rmModVar.getDynamicObject()) {
                    int sourceId = (int)rmModObj->getProperty("source");
                    int destId = (int)rmModObj->getProperty("dest");
                    int destPort = (int)rmModObj->getProperty("destPort");

                    // Find and remove the matching attenuverter node
                    auto mappedSource = idMap.count(sourceId)
                                            ? idMap[sourceId]
                                            : juce::AudioProcessorGraph::NodeID((juce::uint32)sourceId);
                    auto mappedDest =
                        idMap.count(destId) ? idMap[destId] : juce::AudioProcessorGraph::NodeID((juce::uint32)destId);

                    juce::AudioProcessorGraph::NodeID nodeToRemove;
                    bool found = false;
                    for (auto* node : graph.getNodes()) {
                        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) == nullptr)
                            continue;

                        bool sourceMatch = false;
                        bool destMatch = false;
                        for (const auto& conn : graph.getConnections()) {
                            if (conn.destination.nodeID == node->nodeID && conn.destination.channelIndex == 0 &&
                                conn.source.nodeID == mappedSource)
                                sourceMatch = true;
                            if (conn.source.nodeID == node->nodeID && conn.source.channelIndex == 0 &&
                                conn.destination.nodeID == mappedDest && conn.destination.channelIndex == destPort)
                                destMatch = true;
                        }

                        if (sourceMatch && destMatch) {
                            nodeToRemove = node->nodeID;
                            found = true;
                            break;
                        }
                    }
                    if (found)
                        graph.removeNode(nodeToRemove);
                }
            }
        }
    }

    // 1. Create Nodes
    if (rootObj->hasProperty("nodes")) {
        auto* nodesList = rootObj->getProperty("nodes").getArray();
        if (nodesList) {
            for (const auto& nVar : *nodesList) {
                if (auto* nObj = nVar.getDynamicObject()) {
                    int oldId = nObj->getProperty("id");
                    juce::String type = nObj->getProperty("type");

                    // In merge mode, check if this node already exists
                    if (!clearExisting && idMap.count(oldId)) {
                        auto existingNodeId = idMap[oldId];
                        if (auto* existingNode = graph.getNodeForId(existingNodeId)) {
                            if (patchTypeMatchesProcessor(existingNode->getProcessor(), type)) {
                                // Update parameters on existing node
                                if (nObj->hasProperty("params")) {
                                    if (auto* pObj = nObj->getProperty("params").getDynamicObject()) {
                                        applyParamsToProcessor(existingNode->getProcessor(), pObj, trusted);
                                    }
                                }
                                applyExtraStateToProcessor(existingNode->getProcessor(), nObj, trusted);
                                adoptUuidIfTrusted(existingNode, nObj, trusted);
                                // Update position if provided
                                if (nObj->hasProperty("position")) {
                                    if (auto* posObj = nObj->getProperty("position").getDynamicObject()) {
                                        existingNode->properties.set("x", posObj->getProperty("x"));
                                        existingNode->properties.set("y", posObj->getProperty("y"));
                                    }
                                }
                                continue; // Skip node creation
                            }
                        }
                    }

                    auto processor = createModule(type);
                    if (processor) {
                        // Set parameters using helper
                        if (nObj->hasProperty("params")) {
                            if (auto* pObj = nObj->getProperty("params").getDynamicObject()) {
                                applyParamsToProcessor(processor.get(), pObj, trusted);
                            }
                        }
                        applyExtraStateToProcessor(processor.get(), nObj, trusted);

                        // Preserve node identity when restoring OUR OWN snapshot (undo/redo, preset load).
                        // graphToJSON writes the live uid as "id", so replaying it with the same NodeID
                        // keeps ids stable across an undo — without this the graph is renumbered and
                        // anything holding an id across the restore (most visibly a merge-mode patch card,
                        // which addresses existing nodes by uid) silently stops resolving.
                        // Only on the trusted path: untrusted AI JSON must never dictate node ids.
                        std::optional<juce::AudioProcessorGraph::NodeID> preservedId;
                        if (trusted && clearExisting && oldId > 0)
                            preservedId = juce::AudioProcessorGraph::NodeID((juce::uint32)oldId);

                        auto node = graph.addNode(std::move(processor), preservedId);
                        if (node) {
                            idMap[oldId] = node->nodeID;
                            newlyCreatedNodes.insert(node->nodeID);
                            adoptUuidIfTrusted(node.get(), nObj, trusted);
                            if (nObj->hasProperty("position")) {
                                if (auto* posObj = nObj->getProperty("position").getDynamicObject()) {
                                    node->properties.set("x", posObj->getProperty("x"));
                                    node->properties.set("y", posObj->getProperty("y"));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Connections
    if (rootObj->hasProperty("connections")) {
        auto* connList = rootObj->getProperty("connections").getArray();
        if (connList) {
            for (const auto& cVar : *connList) {
                if (auto* cObj = cVar.getDynamicObject()) {
                    int srcOld = cObj->getProperty("src");
                    int dstOld = cObj->getProperty("dst");
                    int srcPort = cObj->getProperty("srcPort");
                    int dstPort = cObj->getProperty("dstPort");

                    // Map -1 back to MIDI channel index
                    if (srcPort == -1)
                        srcPort = juce::AudioProcessorGraph::midiChannelIndex;
                    if (dstPort == -1)
                        dstPort = juce::AudioProcessorGraph::midiChannelIndex;

                    auto* srcNode = graph.getNodeForId(idMap[srcOld]);
                    auto* dstNode = graph.getNodeForId(idMap[dstOld]);
                    if (srcNode && dstNode) {
                        int srcPorts = srcNode->getProcessor()->getTotalNumOutputChannels();
                        int dstPorts = dstNode->getProcessor()->getTotalNumInputChannels();
                        bool isMidiConnection = (srcPort == juce::AudioProcessorGraph::midiChannelIndex);

                        // Auto-detect modulation targets: if the destination port is a
                        // modulation target, route through an attenuverter automatically
                        // (same logic as GraphEditor::endConnectionDrag).
                        // Skip if source is already an AttenuverterModule (existing routing).
                        bool isModTarget = false;
                        if (!isMidiConnection &&
                            dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) == nullptr) {
                            if (auto* modBase = dynamic_cast<ModuleBase*>(dstNode->getProcessor()))
                                isModTarget = modBase->isAutoPromotableModTarget(dstPort);
                        }

                        if (isModTarget) {
                            // Create attenuverter chain: source -> attenuverter -> dest
                            auto attenNode = graph.addNode(std::make_unique<AttenuverterModule>());
                            if (attenNode) {
                                if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(
                                        findParameterByID(attenNode->getProcessor(), "amount")))
                                    param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(1.0f));
                                graph.addConnection({{idMap[srcOld], srcPort}, {attenNode->nodeID, 0}});
                                graph.addConnection({{attenNode->nodeID, 0}, {idMap[dstOld], dstPort}});
                            }
                        } else if (isMidiConnection || (srcPort < srcPorts && dstPort < dstPorts)) {
                            graph.addConnection({{idMap[srcOld], srcPort}, {idMap[dstOld], dstPort}});
                        }
                    }
                }
            }
        }
    }

    // 3. Modulations
    if (rootObj->hasProperty("modulations")) {
        auto* modList = rootObj->getProperty("modulations").getArray();
        if (modList) {
            for (const auto& modVar : *modList) {
                if (auto* modObj = modVar.getDynamicObject()) {
                    int sourceId = (int)modObj->getProperty("source");
                    int destId = (int)modObj->getProperty("dest");
                    int sourcePort = modObj->hasProperty("sourcePort") ? (int)modObj->getProperty("sourcePort") : 0;
                    int destPort = (int)modObj->getProperty("destPort");
                    float amount = modObj->hasProperty("amount") ? (float)modObj->getProperty("amount") : 1.0f;
                    bool bypass = modObj->hasProperty("bypass") ? (bool)modObj->getProperty("bypass") : false;

                    // Get mapped node IDs
                    if (idMap.count(sourceId) && idMap.count(destId)) {
                        auto mappedSource = idMap[sourceId];
                        auto mappedDest = idMap[destId];

                        // Skip if an attenuverter already exists for this routing
                        // (e.g., from nodes/connections arrays in the same JSON)
                        bool alreadyExists = false;
                        for (auto* existingNode : graph.getNodes()) {
                            if (dynamic_cast<AttenuverterModule*>(existingNode->getProcessor()) == nullptr)
                                continue;
                            bool srcMatch = false, dstMatch = false;
                            for (const auto& conn : graph.getConnections()) {
                                if (conn.destination.nodeID == existingNode->nodeID &&
                                    conn.destination.channelIndex == 0 && conn.source.nodeID == mappedSource &&
                                    conn.source.channelIndex == sourcePort)
                                    srcMatch = true;
                                if (conn.source.nodeID == existingNode->nodeID && conn.source.channelIndex == 0 &&
                                    conn.destination.nodeID == mappedDest && conn.destination.channelIndex == destPort)
                                    dstMatch = true;
                            }
                            if (srcMatch && dstMatch) {
                                alreadyExists = true;
                                break;
                            }
                        }
                        if (alreadyExists)
                            continue;

                        // Create attenuverter node
                        auto attenNode = graph.addNode(std::make_unique<AttenuverterModule>());
                        if (attenNode) {
                            if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(
                                    findParameterByID(attenNode->getProcessor(), "amount"))) {
                                param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(amount));
                            }

                            if (bypass) {
                                if (auto* bp = dynamic_cast<juce::AudioParameterBool*>(
                                        findParameterByID(attenNode->getProcessor(), "bypassed"))) {
                                    bp->setValueNotifyingHost(1.0f);
                                }
                            }

                            // Add connections
                            graph.addConnection({{mappedSource, sourcePort}, {attenNode->nodeID, 0}});
                            graph.addConnection({{attenNode->nodeID, 0}, {mappedDest, destPort}});
                        }
                    }
                }
            }
        }
    }

    // 4. Auto-connect: in merge mode, connect new unconnected audio nodes to Audio Output.
    //
    // This is a convenience for AI-authored merge patches — a model that adds an Oscillator to an
    // existing patch means for it to be heard. It is WRONG for any caller reproducing an exact
    // sub-graph, where a missing wire is deliberate: snippet insertion opts out via
    // autoConnectNewNodes=false, otherwise every leaf module in the inserted group would be spliced
    // into the surrounding patch's output (and its MIDI source).
    if (autoConnectNewNodes && !clearExisting && !newlyCreatedNodes.empty()) {
        // Find the Audio Output node
        juce::AudioProcessorGraph::Node* audioOutputNode = nullptr;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor()->getName() == "Audio Output") {
                audioOutputNode = node;
                break;
            }
        }

        if (audioOutputNode != nullptr) {
            // Types that produce audio and should auto-connect to output
            static const std::set<juce::String> audioNodeTypes = {
                "Oscillator", "Noise",   "Sampler",    "Wavetable",     "Filter",       "VCA",    "Distortion",
                "Delay",      "Reverb",  "Amp Env",    "Filter Env",    "Chorus",       "Phaser", "Compressor",
                "Flanger",    "Limiter", "Bitcrusher", "Pitch Shifter", "Parametric EQ"};

            for (auto newNodeId : newlyCreatedNodes) {
                auto* node = graph.getNodeForId(newNodeId);
                if (node == nullptr)
                    continue;

                juce::String typeName = node->getProcessor()->getName();
                if (audioNodeTypes.find(typeName) == audioNodeTypes.end())
                    continue;

                bool hasOutgoing = false;
                for (const auto& conn : graph.getConnections()) {
                    if (conn.source.nodeID == newNodeId && !conn.source.isMIDI()) {
                        hasOutgoing = true;
                        break;
                    }
                }

                if (!hasOutgoing && node->getProcessor()->getTotalNumOutputChannels() > 0) {
                    graph.addConnection({{newNodeId, 0}, {audioOutputNode->nodeID, 0}});
                }
            }
        }

        // Auto-connect MIDI: find existing MIDI sources and connect to new MIDI-accepting nodes
        // Types that accept MIDI input
        // Sampler is here because a note-on retriggers it and transposes it against rootNote — the
        // same reason Oscillator is.
        static const std::set<juce::String> midiAcceptingTypes = {"Oscillator", "Sampler", "Sequencer",
                                                                  "Poly Sequencer", "Poly MIDI"};

        // Find all existing MIDI source nodes (nodes that have outgoing MIDI connections)
        std::set<juce::AudioProcessorGraph::NodeID> midiSources;
        for (const auto& conn : graph.getConnections()) {
            if (conn.source.isMIDI() && newlyCreatedNodes.find(conn.source.nodeID) == newlyCreatedNodes.end()) {
                midiSources.insert(conn.source.nodeID);
            }
        }

        if (!midiSources.empty()) {
            auto midiSourceId = *midiSources.begin(); // Use the first MIDI source found
            for (auto newNodeId : newlyCreatedNodes) {
                auto* node = graph.getNodeForId(newNodeId);
                if (node == nullptr)
                    continue;

                juce::String typeName = node->getProcessor()->getName();
                if (midiAcceptingTypes.find(typeName) == midiAcceptingTypes.end())
                    continue;

                // Check if this node already has incoming MIDI
                bool hasMidiInput = false;
                for (const auto& conn : graph.getConnections()) {
                    if (conn.destination.nodeID == newNodeId && conn.destination.isMIDI()) {
                        hasMidiInput = true;
                        break;
                    }
                }

                if (!hasMidiInput && node->getProcessor()->acceptsMidi()) {
                    graph.addConnection({{midiSourceId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {newNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                }
            }
        }
    }

    return true;
}

namespace {

// The module types the model may emit, taken from the factory itself rather than a hand-kept
// list. The previous literal had silently drifted: "Voice Mixer" existed in the factory but was
// absent from the schema, so a constrained decoder could never produce one.
juce::Array<juce::var> authorableModuleTypeEnum() {
    juce::Array<juce::var> types;
    for (const auto& name : AIStateMapper::authorableModuleTypes())
        types.add(name);
    return types;
}

// Choice parameters, keyed by parameter id, gathered across every authorable module.
//
// This is what closes the gap that dominates rejections on smaller models: `params` used to be
// an unconstrained {"type":"object"}, so nothing stopped a model writing "waveform":"White
// Noise". Emitting the real enum makes the decoder itself unable to produce an illegal choice.
//
// Ids are shared across modules only when they mean the same thing; if two modules ever declare
// the same id with *different* option lists, constraining it globally would forbid values that
// are legal for one of them, so such an id is deliberately left unconstrained (and
// SchemaChoiceParamIdsAreUnambiguous fails, to make the collision a decision rather than a
// silent loss of enforcement).
juce::var choiceParamProperties() {
    std::map<juce::String, juce::StringArray> byParamId;
    std::set<juce::String> ambiguous;

    for (const auto& entry : moduleFactory) {
        if (isInternalOnlyModule(entry.first))
            continue;
        auto processor = entry.second();
        if (!processor)
            continue;

        for (auto* param : processor->getParameters()) {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                auto existing = byParamId.find(choice->paramID);
                if (existing == byParamId.end())
                    byParamId[choice->paramID] = choice->choices;
                else if (existing->second != choice->choices)
                    ambiguous.insert(choice->paramID);
            }
        }
    }

    juce::DynamicObject::Ptr properties = new juce::DynamicObject();
    for (const auto& [paramId, choices] : byParamId) {
        if (ambiguous.count(paramId) > 0)
            continue;

        juce::Array<juce::var> options;
        for (const auto& option : choices)
            options.add(option);

        juce::DynamicObject::Ptr definition = new juce::DynamicObject();
        definition->setProperty("type", "string");
        definition->setProperty("enum", juce::var(options));
        properties->setProperty(paramId, juce::var(definition.get()));
    }
    return juce::var(properties.get());
}

} // namespace

juce::var AIStateMapper::getPatchSchema() {
    // Reserved fields are DELIBERATELY absent from this schema: "schemaVersion", node "uuid" and
    // the root "timeline" key. This document is the output contract handed to the provider as
    // `format` — every property in it is an invitation to emit that property, and all three are
    // ours to write, never the model's (uuid is identity, timeline is refused outright by
    // validatePatch). Pinned by AIStateMapperTest.SchemaOmitsReservedFields.
    juce::DynamicObject::Ptr schema = new juce::DynamicObject();
    schema->setProperty("type", "object");

    juce::DynamicObject::Ptr properties = new juce::DynamicObject();

    // 1. Nodes
    juce::DynamicObject::Ptr nodes = new juce::DynamicObject();
    nodes->setProperty("type", "array");
    juce::DynamicObject::Ptr nodeItems = new juce::DynamicObject();
    nodeItems->setProperty("type", "object");
    juce::DynamicObject::Ptr nodeProperties = new juce::DynamicObject();

    nodeProperties->setProperty("id", juce::JSON::parse("{\"type\": \"integer\"}"));

    juce::DynamicObject::Ptr typeDef = new juce::DynamicObject();
    typeDef->setProperty("type", "string");
    typeDef->setProperty("enum", juce::var(authorableModuleTypeEnum()));
    nodeProperties->setProperty("type", juce::var(typeDef.get()));

    // `additionalProperties` stays open on purpose: only choice parameters can be enumerated,
    // and numeric ones (cutoff, rateHz, …) must still be expressible.
    juce::DynamicObject::Ptr paramsDef = new juce::DynamicObject();
    paramsDef->setProperty("type", "object");
    paramsDef->setProperty("properties", choiceParamProperties());
    paramsDef->setProperty("additionalProperties", true);
    nodeProperties->setProperty("params", juce::var(paramsDef.get()));

    nodeItems->setProperty("properties", juce::var(nodeProperties.get()));
    nodeItems->setProperty("required", juce::Array<juce::var>({"id", "type"}));
    nodes->setProperty("items", juce::var(nodeItems.get()));
    properties->setProperty("nodes", juce::var(nodes.get()));

    // 2. Connections
    juce::DynamicObject::Ptr connections = new juce::DynamicObject();
    connections->setProperty("type", "array");
    juce::DynamicObject::Ptr connItems = new juce::DynamicObject();
    connItems->setProperty("type", "object");
    juce::DynamicObject::Ptr connProperties = new juce::DynamicObject();

    connProperties->setProperty("src", juce::JSON::parse("{\"type\": \"integer\"}"));
    connProperties->setProperty("srcPort", juce::JSON::parse("{\"type\": \"integer\"}"));
    connProperties->setProperty("dst", juce::JSON::parse("{\"type\": \"integer\"}"));
    connProperties->setProperty("dstPort", juce::JSON::parse("{\"type\": \"integer\"}"));

    connItems->setProperty("properties", juce::var(connProperties.get()));
    connItems->setProperty("required", juce::Array<juce::var>({"src", "srcPort", "dst", "dstPort"}));
    connections->setProperty("items", juce::var(connItems.get()));
    properties->setProperty("connections", juce::var(connections.get()));

    // 3. Mode (optional)
    properties->setProperty("mode", juce::JSON::parse("{\"type\": \"string\", \"enum\": [\"replace\", \"merge\"]}"));

    // 4. Remove (optional)
    juce::DynamicObject::Ptr removeArr = new juce::DynamicObject();
    removeArr->setProperty("type", "array");
    removeArr->setProperty("items", juce::JSON::parse("{\"type\": \"integer\"}"));
    properties->setProperty("remove", juce::var(removeArr.get()));

    // 5. Modulations (optional)
    juce::DynamicObject::Ptr modulations = new juce::DynamicObject();
    modulations->setProperty("type", "array");
    juce::DynamicObject::Ptr modItems = new juce::DynamicObject();
    modItems->setProperty("type", "object");
    juce::DynamicObject::Ptr modProperties = new juce::DynamicObject();
    modProperties->setProperty("source", juce::JSON::parse("{\"type\": \"integer\"}"));
    modProperties->setProperty("sourcePort", juce::JSON::parse("{\"type\": \"integer\"}"));
    modProperties->setProperty("dest", juce::JSON::parse("{\"type\": \"integer\"}"));
    modProperties->setProperty("destPort", juce::JSON::parse("{\"type\": \"integer\"}"));
    modProperties->setProperty("amount", juce::JSON::parse("{\"type\": \"number\"}"));
    modProperties->setProperty("bypass", juce::JSON::parse("{\"type\": \"boolean\"}"));
    modItems->setProperty("properties", juce::var(modProperties.get()));
    modItems->setProperty("required", juce::Array<juce::var>({"source", "dest", "destPort"}));
    modulations->setProperty("items", juce::var(modItems.get()));
    properties->setProperty("modulations", juce::var(modulations.get()));

    // 6. RemoveModulations (optional)
    juce::DynamicObject::Ptr removeModulations = new juce::DynamicObject();
    removeModulations->setProperty("type", "array");
    juce::DynamicObject::Ptr rmModItems = new juce::DynamicObject();
    rmModItems->setProperty("type", "object");
    juce::DynamicObject::Ptr rmModProperties = new juce::DynamicObject();
    rmModProperties->setProperty("source", juce::JSON::parse("{\"type\": \"integer\"}"));
    rmModProperties->setProperty("dest", juce::JSON::parse("{\"type\": \"integer\"}"));
    rmModProperties->setProperty("destPort", juce::JSON::parse("{\"type\": \"integer\"}"));
    rmModItems->setProperty("properties", juce::var(rmModProperties.get()));
    rmModItems->setProperty("required", juce::Array<juce::var>({"source", "dest", "destPort"}));
    removeModulations->setProperty("items", juce::var(rmModItems.get()));
    properties->setProperty("removeModulations", juce::var(removeModulations.get()));

    // Note: there is deliberately no "parameterChoices" property here. It used to be carried in
    // this schema "for AI reference", but this schema is the *output* contract handed to the
    // provider as `format` — every property in it is something the model is invited to emit, not
    // documentation it can read. The choice lists now do their real work as enums inside
    // node.params (above), and remain human-readable in the system prompt via getModuleSchema().
    schema->setProperty("properties", juce::var(properties.get()));
    schema->setProperty("required", juce::Array<juce::var>({"nodes", "connections"}));

    return juce::var(schema.get());
}

} // namespace synth
