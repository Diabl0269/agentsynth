// AIStateMapper — module factory access and graph <-> JSON mapping.
//
// Owns createModule/the type-name lookups, graphToJSON, and applyJSONToGraph (the merge-patch
// applier). The class itself is declared in AIStateMapper.h; validatePatch (the untrusted-input
// security boundary) lives in AIStateMapperValidation.cpp, snapshot restore in
// AIStateMapperSnapshots.cpp, and AI-facing schema generation in AIStateMapperSchema.cpp.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

#include <map>
#include <optional>
#include <set>

namespace synth {

namespace {

// Adopts a patch node's "uuid" onto the live node — trusted callers only. Untrusted input never
// dictates identity: a model could otherwise hand two nodes the same uuid, or claim the uuid of a
// node that automation lanes and track bindings already point at. Nodes created from untrusted
// JSON simply have no uuid until graphToJSON generates a fresh one for them.
void adoptUuidIfTrusted(juce::AudioProcessorGraph::Node* node, const juce::DynamicObject* nObj, bool trusted) {
    if (!trusted || node == nullptr || !nObj->hasProperty("uuid"))
        return;
    const juce::var uuidVar = nObj->getProperty("uuid");
    if (uuidVar.isString() && uuidVar.toString().isNotEmpty()) {
        node->properties.set("uuid", uuidVar.toString());
        detail::mirrorUuidIntoProcessor(node, uuidVar.toString());
    }
}

} // namespace

std::unique_ptr<juce::AudioProcessor> AIStateMapper::createModule(const juce::String& type) {
    auto it = detail::moduleFactory().find(type);
    if (it != detail::moduleFactory().end()) {
        return it->second();
    }

    // Strip trailing number suffix for backwards compatibility (e.g., "Oscillator 1" → "Oscillator")
    juce::String baseName = type;
    int lastSpace = baseName.lastIndexOf(" ");
    if (lastSpace != -1 && baseName.substring(lastSpace + 1).containsOnly("0123456789"))
        baseName = baseName.substring(0, lastSpace);

    it = detail::moduleFactory().find(baseName);
    if (it != detail::moduleFactory().end())
        return it->second();

    // Handle ADSR variants with custom names (e.g., "Amp Env", "Filter Env")
    if (baseName.containsIgnoreCase("Env") || baseName.containsIgnoreCase("ADSR"))
        return std::make_unique<ADSRModule>(baseName);

    if (baseName == "MidiKeyboard")
        return std::make_unique<MidiKeyboardModule>();

    // JUCE's display name for the graph's MIDI input node, which older saves emitted as the node
    // type before getFactoryTypeName mapped it back to the factory key.
    if (baseName == "MIDI Input")
        return std::make_unique<detail::AudioGraphIOProcessor>(detail::AudioGraphIOProcessor::midiInputNode);

    juce::Logger::writeToLog("AIStateMapper: Unknown module type: " + type);
    return nullptr;
}

juce::StringArray AIStateMapper::moduleFactoryTypeNames() {
    juce::StringArray names;
    for (const auto& entry : detail::moduleFactory())
        names.add(entry.first);
    names.sort(false); // moduleFactory is unordered; callers want a stable order
    return names;
}

const juce::StringArray& AIStateMapper::dualIOCapableModuleTypes() {
    // Probed from the factory rather than hand-listed: a module that gains the Dual I/O toggle has
    // to show up in the Preferences per-module popup without anyone remembering to add it there.
    // One instance per factory key, built once — cheap enough for a lazily-initialised static
    // (every module here is default-constructible with no device, no file and no plugin scan), and
    // the alternative is the stale list this replaces.
    static const juce::StringArray types = [] {
        juce::StringArray result;
        for (const auto& name : moduleFactoryTypeNames()) {
            auto probe = createModule(name);
            auto* mb = dynamic_cast<ModuleBase*>(probe.get());
            if (mb != nullptr && mb->hasDualIOParameter())
                result.add(name);
        }
        return result;
    }();
    return types;
}

juce::StringArray AIStateMapper::authorableModuleTypes() {
    juce::StringArray names;
    for (const auto& name : moduleFactoryTypeNames())
        if (!detail::isInternalOnlyModule(name))
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
        case ModuleType::Gate:
            return "Gate";
        case ModuleType::ParametricEQ:
            return "Parametric EQ";
        case ModuleType::VoiceMixer:
            return "Voice Mixer";
        case ModuleType::Bitcrusher:
            return "Bitcrusher";
        case ModuleType::PitchShifter:
            return "Pitch Shifter";
        case ModuleType::RingModulator:
            return "Ring Modulator";
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
        case ModuleType::Comparator:
            return "Comparator";
        case ModuleType::ExternalMidi:
            return "External MIDI";
        case ModuleType::TimelineMidiSource:
            return "Track In";
        case ModuleType::RecordTap:
            return "Rec Tap";
        case ModuleType::TimelineAudioSource:
            return "Track Audio";
        case ModuleType::AudioInput:
            // Deliberately the same string JUCE's audioInputNode reported, so old and new saves
            // are interchangeable on disk.
            return "Audio Input";
        case ModuleType::HostedPlugin:
            // The factory key, NOT the hosted plugin's own name: the type identifies the host
            // module, and which plugin it hosts is carried by the node's "state".
            return "Hosted Plugin";
        case ModuleType::MacroInlet:
            return "Macro In";
        case ModuleType::MacroOutlet:
            return "Macro Out";
        case ModuleType::MacroMidiInlet:
            return "Macro MIDI In";
        case ModuleType::MacroMidiOutlet:
            return "Macro MIDI Out";
        case ModuleType::ChannelStrip:
            return "Channel Strip";
        case ModuleType::Master:
            return "Master";
        }
    }

    // JUCE names the graph's MIDI input node "MIDI Input", which is NOT the factory key
    // ("Midi Input"): falling through to getName() below would emit a type string createModule
    // cannot resolve, and the node would silently vanish on the next load. The audio I/O nodes'
    // names already match their keys exactly, so only this one needs mapping.
    if (auto* io = dynamic_cast<detail::AudioGraphIOProcessor*>(processor))
        if (io->getType() == detail::AudioGraphIOProcessor::midiInputNode)
            return "Midi Input";

    return processor->getName();
}

juce::String AIStateMapper::ensureNodeUuid(juce::AudioProcessorGraph::Node* node) {
    if (node == nullptr)
        return {};
    juce::String uuid = node->properties["uuid"].toString();
    if (uuid.isEmpty()) {
        uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        detail::mirrorUuidIntoProcessor(node, uuid);
    }
    return uuid;
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
            n->setProperty("uuid", ensureNodeUuid(node));

            // User's custom card title, when they renamed it. Deliberately a SEPARATE field from the
            // processor's own name: "type" carries the factory type, and the processor's getName()
            // is the auto-numbered "Chorus 2" that AudioEngine::updateModuleNames() recomputes
            // wholesale on every graph change (it even strips trailing digits to renumber). Storing
            // a custom name there would be clobbered on the next node add. Emitted only when set, so
            // every un-renamed node's JSON stays byte-identical to before.
            const auto displayName = node->properties["displayName"].toString();
            if (displayName.isNotEmpty())
                n->setProperty("displayName", displayName);

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

    for (const auto& entry : detail::moduleFactory()) {
        // Hide non-authorable modules from AI — modulation uses the "modulations" array instead
        if (detail::isInternalOnlyModule(entry.first))
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

    for (const auto& entry : detail::moduleFactory()) {
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
                                           bool trusted, bool skipUnchanged) {
    // Writing a parameter that already holds the target value is not a no-op: setValueNotifyingHost
    // notifies its listeners unconditionally, and one of ours re-anchors a module's cables when
    // "poly" changes — i.e. it mutates the graph. A snapshot restore re-applies every parameter of
    // every surviving node, so for that caller the redundant writes are the overwhelming majority.
    // The tolerance only has to absorb the float round-trip through the JSON's denormalized value;
    // it is orders of magnitude below any parameter step a user or a model can express.
    auto setNormalised = [skipUnchanged](juce::RangedAudioParameter* p, float normalised) {
        if (skipUnchanged && std::abs(p->getValue() - normalised) <= 1.0e-6f)
            return;
        p->setValueNotifyingHost(normalised);
    };

    for (auto* param : processor->getParameters()) {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            if (paramsObj->hasProperty(p->paramID)) {
                auto jsonValue = paramsObj->getProperty(p->paramID);

                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) {
                    if (jsonValue.isString()) {
                        int index = findChoiceIndex(choice, jsonValue.toString());
                        if (index >= 0) {
                            setNormalised(p, p->getNormalisableRange().convertTo0to1((float)index));
                        }
                    } else {
                        auto choiceRange = p->getNormalisableRange();
                        float val = choiceRange.snapToLegalValue((float)jsonValue);
                        setNormalised(p, choiceRange.convertTo0to1(val));
                    }
                } else if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p)) {
                    setNormalised(b, (bool)jsonValue ? 1.0f : 0.0f);
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
                    setNormalised(p, range.convertTo0to1(val));
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
                                     bool trusted, bool autoConnectNewNodes,
                                     std::map<int, juce::AudioProcessorGraph::NodeID>* outIdMap) {
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

                    // Audio Output is a per-graph singleton (like Audio Input): on a MERGE into an
                    // existing graph, adopt the graph's current output instead of spawning a second
                    // one. The loaded patch's feed then re-points to it (the connection phase resolves
                    // this node's id to the existing output via idMap), so the surrounding sound is
                    // not orphaned to a dead, unconnected output. On a full clear (REPLACE) no output
                    // exists yet, so this is a no-op and the patch's own output is created as usual.
                    if (!clearExisting && type == "Audio Output") {
                        juce::AudioProcessorGraph::Node* existingOutput = nullptr;
                        for (auto* node : graph.getNodes())
                            if (node->getProcessor() != nullptr && node->getProcessor()->getName() == "Audio Output") {
                                existingOutput = node;
                                break;
                            }
                        if (existingOutput != nullptr) {
                            idMap[oldId] = existingOutput->nodeID;
                            continue; // reuse the existing output; skip creating a second one
                        }
                    }

                    // In merge mode, check if this node already exists
                    if (!clearExisting && idMap.count(oldId)) {
                        auto existingNodeId = idMap[oldId];
                        if (auto* existingNode = graph.getNodeForId(existingNodeId)) {
                            if (detail::patchTypeMatchesProcessor(existingNode->getProcessor(), type)) {
                                // Update parameters on existing node
                                if (nObj->hasProperty("params")) {
                                    if (auto* pObj = nObj->getProperty("params").getDynamicObject()) {
                                        applyParamsToProcessor(existingNode->getProcessor(), pObj, trusted);
                                    }
                                }
                                applyExtraStateToProcessor(existingNode->getProcessor(), nObj, trusted);
                                adoptUuidIfTrusted(existingNode, nObj, trusted);
                                detail::applyDisplayNameToNode(existingNode, nObj);
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
                            detail::applyDisplayNameToNode(node.get(), nObj);
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
                "Oscillator",    "Noise",          "Sampler", "Wavetable", "Filter",     "VCA",
                "Distortion",    "Delay",          "Reverb",  "Amp Env",   "Filter Env", "Chorus",
                "Phaser",        "Compressor",     "Flanger", "Limiter",   "Bitcrusher", "Pitch Shifter",
                "Parametric EQ", "Ring Modulator", "Gate"};

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

    if (outIdMap != nullptr)
        *outIdMap = idMap;

    return true;
}

} // namespace synth
