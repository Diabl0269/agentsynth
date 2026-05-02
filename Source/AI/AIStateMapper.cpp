#include "AIStateMapper.h"
#include "../Modules/ADSRModule.h"
#include "../Modules/AttenuverterModule.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/FX/ChorusModule.h"

#include "../Modules/FX/CompressorModule.h"
#include "../Modules/FX/DelayModule.h"
#include "../Modules/FX/DistortionModule.h"
#include "../Modules/FX/FlangerModule.h"
#include "../Modules/FX/LimiterModule.h"
#include "../Modules/FX/PhaserModule.h"
#include "../Modules/FX/ReverbModule.h"
#include "../Modules/FilterModule.h"
#include "../Modules/LFOModule.h"
#include "../Modules/MidiKeyboardModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/OscillatorModule.h"
#include "../Modules/PolyMidiModule.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SequencerModule.h"
#include "../Modules/VCAModule.h"
#include "../Modules/VoiceMixerModule.h"
#include <functional> // For std::function
#include <map>
#include <set>
#include <unordered_map> // For the factory map

namespace gsynth {

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
    {"Voice Mixer", []() { return std::make_unique<VoiceMixerModule>(); }},
    {"External MIDI", []() { return std::make_unique<ExternalMidiModule>(); }}};

bool AIStateMapper::validatePatchJSON(const juce::var& json) {
    if (!json.isObject()) {
        juce::Logger::writeToLog("validatePatchJSON: Root is not an object.");
        return false;
    }
    auto* rootObj = json.getDynamicObject();
    if (!rootObj) {
        juce::Logger::writeToLog("validatePatchJSON: Root dynamic object is null.");
        return false;
    }

    // Check for "nodes" array
    if (rootObj->hasProperty("nodes")) {
        auto* nodesList = rootObj->getProperty("nodes").getArray();
        if (nodesList == nullptr) {
            juce::Logger::writeToLog("validatePatchJSON: 'nodes' property is not an array.");
            return false;
        }
        // Further validation of each node could go here (e.g., checking for id and type)
    } else if (!rootObj->hasProperty("remove")) {
        juce::Logger::writeToLog("validatePatchJSON: 'nodes' and 'remove' properties are both missing.");
        return false;
    }

    // Check for "connections" array (optional, a patch can exist without connections)
    if (rootObj->hasProperty("connections")) {
        auto* connList = rootObj->getProperty("connections").getArray();
        if (connList == nullptr) {
            juce::Logger::writeToLog("validatePatchJSON: 'connections' property is not an array.");
            return false;
        }
        // Further validation of each connection could go here
    }

    return true;
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

    juce::Logger::writeToLog("AIStateMapper: Unknown module type: " + type);
    return nullptr;
}

static juce::String getFactoryTypeName(juce::AudioProcessor* processor) {
    if (auto* mb = dynamic_cast<ModuleBase*>(processor)) {
        switch (mb->getModuleType()) {
        case ModuleType::Oscillator:
            return "Oscillator";
        case ModuleType::Filter:
            return "Filter";
        case ModuleType::VCA:
            return "VCA";
        case ModuleType::ADSR:
            return "ADSR";
        case ModuleType::LFO:
            return "LFO";
        case ModuleType::Sequencer:
            return "Sequencer";
        case ModuleType::PolySequencer:
            return "Sequencer";
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
        case ModuleType::VoiceMixer:
            return "Voice Mixer";
        case ModuleType::ExternalMidi:
            return "External MIDI";
        }
    }
    return processor->getName();
}

juce::var AIStateMapper::graphToJSON(juce::AudioProcessorGraph& graph) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();

    juce::Array<juce::var> nodes;
    for (auto* node : graph.getNodes()) {
        if (auto* processor = node->getProcessor()) {
            juce::DynamicObject::Ptr n = new juce::DynamicObject();
            n->setProperty("id", (int)node->nodeID.uid);
            n->setProperty("type", getFactoryTypeName(processor));

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

                // Get amount parameter (param[1], after bypassedParam at [0])
                if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(attenverter->getParameters()[1])) {
                    float amount = param->getNormalisableRange().convertFrom0to1(param->getValue());
                    modEntry->setProperty("amount", amount);
                }

                // Get bypass parameter (param[2], after bypassedParam at [0])
                if (auto* param = dynamic_cast<juce::AudioParameterBool*>(attenverter->getParameters()[2])) {
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
        // Hide internal modules from AI — modulation uses the "modulations" array instead
        if (entry.first == "Attenuverter" || entry.first == "Mod Slot")
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
                        float val = (float)jsonValue;
                        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(val));
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
                    bool wasConverted = false;
                    if (!trusted && !isIntParam && !rangeIsUnitInterval && val >= 0.0f && val <= 1.0f) {
                        float originalVal = val;
                        val = range.convertFrom0to1(val);
                        wasConverted = true;
                        juce::Logger::writeToLog("AIStateMapper: param '" + p->paramID + "' value " +
                                                 juce::String(originalVal) + " detected as normalized, converted to " +
                                                 juce::String(val) + " (range " + juce::String(range.start) + " to " +
                                                 juce::String(range.end) + ")");
                    }

                    val = range.snapToLegalValue(val);
                    float normalizedValue = range.convertTo0to1(val);
                    juce::Logger::writeToLog("AIStateMapper: setting '" + p->paramID + "' = " + juce::String(val) +
                                             " (normalized: " + juce::String(normalizedValue) + ")" +
                                             (wasConverted ? " [auto-corrected]" : ""));
                    p->setValueNotifyingHost(normalizedValue);
                }
            }
        }
    }
}

bool AIStateMapper::applyJSONToGraph(const juce::var& json, juce::AudioProcessorGraph& graph, bool clearExisting,
                                     bool trusted) {
    if (!json.isObject()) {
        juce::Logger::writeToLog("applyJSONToGraph: JSON is not an object.");
        return false;
    }
    auto* rootObj = json.getDynamicObject();
    if (!rootObj) {
        juce::Logger::writeToLog("applyJSONToGraph: JSON dynamic object is null.");
        return false;
    }

    // Validate JSON structure before making any changes
    if (!validatePatchJSON(json)) {
        juce::Logger::writeToLog("applyJSONToGraph: JSON patch validation failed.");
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
                            if (existingNode->getProcessor()->getName() == type) {
                                // Update parameters on existing node
                                if (nObj->hasProperty("params")) {
                                    if (auto* pObj = nObj->getProperty("params").getDynamicObject()) {
                                        applyParamsToProcessor(existingNode->getProcessor(), pObj, trusted);
                                    }
                                }
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

                        auto node = graph.addNode(std::move(processor));
                        if (node) {
                            idMap[oldId] = node->nodeID;
                            newlyCreatedNodes.insert(node->nodeID);
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
                            if (auto* modBase = dynamic_cast<ModuleBase*>(dstNode->getProcessor())) {
                                for (const auto& t : modBase->getModulationTargets()) {
                                    if (t.channelIndex == dstPort) {
                                        isModTarget = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (isModTarget) {
                            // Create attenuverter chain: source -> attenuverter -> dest
                            auto attenNode = graph.addNode(std::make_unique<AttenuverterModule>());
                            if (attenNode) {
                                if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(
                                        attenNode->getProcessor()->getParameters()[1]))
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
                            // Set amount parameter (param[1], after bypassedParam at [0])
                            if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(
                                    attenNode->getProcessor()->getParameters()[1])) {
                                param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(amount));
                            }

                            // Set bypass parameter if true (param[2], after bypassedParam at [0])
                            if (bypass) {
                                if (auto* bp = dynamic_cast<juce::AudioParameterBool*>(
                                        attenNode->getProcessor()->getParameters()[2])) {
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

    // 4. Auto-connect: in merge mode, connect new unconnected audio nodes to Audio Output
    if (!clearExisting && !newlyCreatedNodes.empty()) {
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
                "Oscillator", "Filter", "VCA",    "Distortion", "Delay",   "Reverb", "Amp Env",
                "Filter Env", "Chorus", "Phaser", "Compressor", "Flanger", "Limiter"};

            for (auto newNodeId : newlyCreatedNodes) {
                auto* node = graph.getNodeForId(newNodeId);
                if (node == nullptr)
                    continue;

                juce::String typeName = node->getProcessor()->getName();
                if (audioNodeTypes.find(typeName) == audioNodeTypes.end())
                    continue;

                // Check if this node already has outgoing audio connections
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
        static const std::set<juce::String> midiAcceptingTypes = {"Oscillator", "Sequencer", "Poly Sequencer",
                                                                  "Poly MIDI"};

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

juce::var AIStateMapper::getPatchSchema() {
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
    nodeProperties->setProperty(
        "type",
        juce::JSON::parse("{\"type\": \"string\", \"enum\": [\"Audio Input\", \"Audio Output\", \"Midi "
                          "Input\", \"Oscillator\", \"Filter\", \"VCA\", \"ADSR\", \"Sequencer\", \"LFO\", "
                          "\"Distortion\", \"Delay\", \"Reverb\", \"MIDI Keyboard\", \"Amp Env\", \"Filter Env\", "
                          "\"Poly MIDI\", \"Poly Sequencer\", \"Chorus\", \"Phaser\", "
                          "\"Compressor\", \"Flanger\", \"Limiter\", \"External MIDI\"]}"));
    nodeProperties->setProperty("params", juce::JSON::parse("{\"type\": \"object\"}"));

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

    schema->setProperty("properties", juce::var(properties.get()));
    schema->setProperty("required", juce::Array<juce::var>({"nodes", "connections"}));

    return juce::var(schema.get());
}

} // namespace gsynth
