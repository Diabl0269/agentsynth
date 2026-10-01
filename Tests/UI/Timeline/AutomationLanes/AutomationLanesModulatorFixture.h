#pragma once

// AutomationLanesModulatorFixture.h -- a real MainComponent with a MIDI track, a module on it and an open
// automation lane for one of its parameters, plus the graph queries the modulator tests assert with.
// Header-only; not registered in Tests/CMakeLists.txt.

#include "../TimelinePanel/TimelinePanelTestFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AutomationLanesTestFixture.h"
#include "MacroSet.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

namespace modulator_test {

using NodePtr = juce::AudioProcessorGraph::Node::Ptr;

inline NodePtr addNodeWithUuid(MainComponent& mc, const juce::String& type) {
    auto node = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule(type));
    if (node == nullptr)
        return node;
    const auto uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node;
}

// `type`'s `paramId` automated on a MIDI track, the timeline open with its lane row. Close the audio device so a render
// in the test is the only thing processing the graph.
struct Scene {
    MainComponent mc{std::make_unique<MockProviderTL>()};
    synth::TrackId track;
    NodePtr target;
    juce::String targetUuid;
    synth::LaneId lane;

    explicit Scene(const juce::String& type = "Filter", const juce::String& paramId = "cutoff") {
        mc.setSize(1600, 1000);
        mc.simulateToggleBottomPanelClick(); // the timeline open and laid out, as a person would see it
        mc.getAudioEngine().getDeviceManager().closeAudioDevice();
        mc.simulateAddMidiTrackClick();
        track = doc().getTracks().front().id;
        target = addNodeWithUuid(mc, type);
        targetUuid = target->properties["uuid"].toString();
        mc.getGraphEditor().updateComponents();
        lane = doc().addLane(track, targetUuid, paramId, {0.0f, 1.0f, 0.5f});
        panel().setTrackAutomationExpanded(track, true);
        baseAttenuverters = nodesOf<AttenuverterModule>().size();
    }

    size_t baseAttenuverters = 0; // the track's own chain may already hold some

    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }
    synth::ui::TimelinePanelComponent& panel() { return mc.getTimelinePanel(); }
    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }
    AppUndoManager& undo() { return mc.getUndoManager(); }

    void addLfoFromLaneMenu() {
        auto* header = panel().laneHeaderForTest(lane);
        ASSERT_NE(header, nullptr);
        header->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kAddLfoModulatorMenuId);
    }

    synth::ui::ModulatorRow* row(int index = 0) { return panel().modulatorRowForTest(lane, index); }

    template <typename T>
    std::vector<juce::AudioProcessorGraph::Node*> nodesOf() {
        std::vector<juce::AudioProcessorGraph::Node*> found;
        for (auto* node : graph().getNodes())
            if (dynamic_cast<T*>(node->getProcessor()) != nullptr)
                found.push_back(node);
        return found;
    }

    juce::AudioProcessorGraph::Node* byUuid(const juce::String& uuid) {
        for (auto* node : graph().getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return node;
        return nullptr;
    }

    float parameter(const juce::String& uuid, const juce::String& paramId) {
        auto* node = byUuid(uuid);
        auto* p = node != nullptr ? findParameterByID(node->getProcessor(), paramId) : nullptr;
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : -999.0f;
    }

    // The target's CV channel for `paramId` (-1 when it has none).
    int channelFor(const juce::String& paramId) {
        auto* t = byUuid(targetUuid);
        return t != nullptr ? mc.getGraphEditor().modulationChannelFor(t->nodeID, paramId) : -1;
    }

    // The attenuverter chains from the node `sourceUuid` into the target's CV channel `raw`. Looked up by
    // uuid: an undo restore rebuilds the graph, and node ids need not survive it.
    std::vector<ModulationRouting> chainsInto(const juce::String& sourceUuid, int raw) {
        std::vector<ModulationRouting> found;
        auto* source = byUuid(sourceUuid);
        auto* t = byUuid(targetUuid);
        if (source == nullptr || t == nullptr)
            return found;
        for (const auto& r : mc.getAudioEngine().getModulationRoutings())
            if (r.kind == ModulationRoutingKind::AttenuverterChain && r.sourceNodeID == source->nodeID &&
                r.destNodeID == t->nodeID && r.destChannelIndex == raw)
                found.push_back(r);
        return found;
    }

    float depthOf(const ModulationRouting& r) {
        auto* atten = graph().getNodeForId(r.attenuverterNodeID);
        auto* p = atten != nullptr ? findParameterByID(atten->getProcessor(), "amount") : nullptr;
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : -999.0f;
    }

    ModuleComponent* cardFor(juce::AudioProcessorGraph::NodeID id) {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                return comp;
        return nullptr;
    }
};

} // namespace modulator_test
