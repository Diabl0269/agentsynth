// MainComponentModulators.cpp -- the TrackHeaderHost side of a timeline lane's modulators: the timeline
// has no graph, so this is where a lane's (node, parameter) is turned into the routings into its CV jack,
// where "Add LFO modulator" / "Remove modulator" reach the GraphEditor, and where a modulator row's
// controls read and write live parameters through the canvas knobs' own undo path.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MainComponent.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace {
juce::String uuidOf(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
}
} // namespace

// Every routing whose destination is the lane's node and whose destination channel is the parameter's
// CV channel, however it got there (the lane menu or a cable patched by hand). Only ModCV routings
// count: a poly pitch or gate fan into the same node is signal distribution, not modulation. Every node
// a row will name is given a uuid here -- graphToJSON assigns the same lazily, so this changes nothing
// an undo snapshot would see.
std::vector<synth::ui::ModulatorInfo> MainComponent::getModulators(const juce::String& nodeUuid,
                                                                   const juce::String& paramId) {
    std::vector<synth::ui::ModulatorInfo> result;
    auto* target = findNodeByUuid(nodeUuid);
    const int raw = target != nullptr ? graphEditor.modulationChannelFor(target->nodeID, paramId) : -1;
    if (raw < 0)
        return result;
    auto& graph = audioEngine.getGraph();
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasSource || !r.hasDest || r.destNodeID != target->nodeID || r.destChannelIndex != raw ||
            r.role != PortRole::ModCV)
            continue;
        auto* source = graph.getNodeForId(r.sourceNodeID);
        if (source == nullptr)
            continue;
        synth::ui::ModulatorInfo info;
        info.sourceUuid = uuidOf(source);
        info.sourceTitle = synth::moduleTitle(*source);
        info.sourceChannel = r.sourceChannelIndex;
        info.isLfo = dynamic_cast<LFOModule*>(source->getProcessor()) != nullptr;
        if (r.kind == AudioEngine::RoutingKind::AttenuverterChain)
            info.attenuverterUuid = uuidOf(graph.getNodeForId(r.attenuverterNodeID));
        info.targetUuid = nodeUuid;
        info.paramId = paramId;
        info.targetChannel = raw;
        info.colour = graphEditor.modulationWireColour(r.sourceNodeID);
        result.push_back(std::move(info));
    }
    return result;
}

bool MainComponent::canModulate(const juce::String& nodeUuid, const juce::String& paramId) {
    auto* target = findNodeByUuid(nodeUuid);
    return target != nullptr && graphEditor.modulationChannelFor(target->nodeID, paramId) >= 0;
}

juce::String MainComponent::addLfoModulator(const juce::String& nodeUuid, const juce::String& paramId) {
    auto* target = findNodeByUuid(nodeUuid);
    if (target == nullptr)
        return {};
    const auto lfoId = graphEditor.addLfoModulator(target->nodeID, paramId);
    return uuidOf(audioEngine.getGraph().getNodeForId(lfoId));
}

// The row names its routing by uuids (node ids do not survive an undo restore), so it is looked up
// again in the live graph. Only an LFO row offers Remove, and only an LFO source is taken with it.
void MainComponent::removeModulator(const synth::ui::ModulatorInfo& modulator) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    if (target == nullptr)
        return;
    auto& graph = audioEngine.getGraph();
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.destNodeID != target->nodeID || r.destChannelIndex != modulator.targetChannel)
            continue;
        const bool sameChain = modulator.attenuverterUuid.isNotEmpty() &&
                               r.kind == AudioEngine::RoutingKind::AttenuverterChain &&
                               uuidOf(graph.getNodeForId(r.attenuverterNodeID)) == modulator.attenuverterUuid;
        const bool sameDirect = modulator.attenuverterUuid.isEmpty() &&
                                r.kind != AudioEngine::RoutingKind::AttenuverterChain &&
                                r.sourceChannelIndex == modulator.sourceChannel &&
                                uuidOf(graph.getNodeForId(r.sourceNodeID)) == modulator.sourceUuid;
        if (sameChain || sameDirect) {
            graphEditor.removeModulator(r, modulator.isLfo);
            return;
        }
    }
}

float MainComponent::getNodeParameter(const juce::String& uuid, const juce::String& paramId) {
    auto* node = findNodeByUuid(uuid);
    auto* param = node != nullptr ? findParameterByID(node->getProcessor(), paramId) : nullptr;
    return param != nullptr ? param->convertFrom0to1(param->getValue()) : 0.0f;
}

// The canvas knobs' own undo idiom (captureBeforeState at the gesture's start, pushSnapshotFromCapture
// at its end), driven here directly rather than through begin/endChangeGesture: the parameter's card is
// a gesture listener with its own capture into the same single slot, and two owners of that slot in
// one gesture would interleave. The write itself still notifies, so the card follows live.
void MainComponent::setNodeParameter(const juce::String& uuid, const juce::String& paramId, float value,
                                     synth::ui::ParameterEditPhase phase) {
    auto* node = findNodeByUuid(uuid);
    auto* param = node != nullptr ? findParameterByID(node->getProcessor(), paramId) : nullptr;
    if (param == nullptr)
        return;
    using Phase = synth::ui::ParameterEditPhase;
    auto& graph = audioEngine.getGraph();
    if (phase == Phase::Begin || phase == Phase::Once)
        undoManager.captureBeforeState(graph);
    param->setValueNotifyingHost(param->convertTo0to1(value));
    if (phase == Phase::End || phase == Phase::Once)
        undoManager.pushSnapshotFromCapture(graph);
}

// Select, then centre the canvas on the card, so a modulator off screen is found as well as highlighted.
void MainComponent::showNodeOnCanvas(const juce::String& uuid) {
    auto* node = findNodeByUuid(uuid);
    if (node == nullptr)
        return;
    selectNodeInGraph(uuid);
    for (auto* comp : graphEditor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == node->nodeID)
            graphEditor.centreViewOn(comp->getBounds().toFloat().getCentre());
}
