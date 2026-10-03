// MainComponentModulators.cpp -- the TrackHeaderHost side of a timeline lane's modulators: the timeline
// has no graph, so this is where a lane's (node, parameter) is turned into the routings into its CV jack,
// where "Add modulator..." / "Remove modulator" reach the GraphEditor, and where a modulator row's
// controls read and write live parameters through the canvas knobs' own undo path; and the load-time migration
// of a project's retired LFO sections lanes to amount lanes.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MainComponent.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"
#include "UI/Timeline/AutomationLanes/Modulators/RemoveLfoConfirm.h"

namespace {
juce::String uuidOf(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
}

// A routing with the macro ports between its modulator and its parameter looked through, so a row names the
// real LFO and the real parameter however the cable crosses macro boundaries.
synth::ui::ResolvedRouting resolveThroughPorts(AudioEngine& engine, GraphEditor& editor,
                                               const AudioEngine::ModulationRouting& routing) {
    return synth::ui::resolveRouting(engine.getGraph(), routing, [&editor](juce::AudioProcessorGraph::NodeID id) {
        return editor.getMacroController().nodeIsMacroPort(id);
    });
}

// "<module title> <parameter>" for a routing's real destination ("Pad oscillator detune").
juce::String destinationName(juce::AudioProcessorGraph& graph, const synth::ui::RoutingEndpoint& dest) {
    auto* node = graph.getNodeForId(dest.node);
    if (node == nullptr)
        return {};
    auto title = synth::moduleTitle(*node);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        for (const auto& t : module->getModulationTargets())
            if (t.channelIndex == dest.channel && t.name.isNotEmpty())
                return title + " " + t.name;
    return title;
}
// The routing a row stands for, looked up again in the live graph by its Attenuverter's uuid (a copy: the engine's
// list is rebuilt by the edits that follow).
std::optional<AudioEngine::ModulationRouting> findAttenuverterRouting(AudioEngine& engine,
                                                                      const synth::ui::ModulatorInfo& modulator) {
    auto& graph = engine.getGraph();
    for (const auto& r : engine.getModulationRoutings())
        if (r.kind == AudioEngine::RoutingKind::AttenuverterChain &&
            uuidOf(graph.getNodeForId(r.attenuverterNodeID)) == modulator.attenuverterUuid)
            return r;
    return std::nullopt;
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
        if (!r.hasSource || !r.hasDest || r.role != PortRole::ModCV)
            continue;
        const auto real = resolveThroughPorts(audioEngine, graphEditor, r);
        if (real.dest.node != target->nodeID || real.dest.channel != raw)
            continue;
        auto* source = graph.getNodeForId(real.source.node);
        if (source == nullptr)
            continue;
        synth::ui::ModulatorInfo info;
        info.sourceUuid = uuidOf(source);
        info.sourceTitle = synth::moduleTitle(*source);
        info.sourceChannel = real.source.channel;
        info.isLfo = dynamic_cast<LFOModule*>(source->getProcessor()) != nullptr;
        if (r.kind == AudioEngine::RoutingKind::AttenuverterChain)
            info.attenuverterUuid = uuidOf(graph.getNodeForId(r.attenuverterNodeID));
        info.targetUuid = nodeUuid;
        info.paramId = paramId;
        info.targetChannel = raw;
        info.colour = graphEditor.modulationWireColour(real.source.node);
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

// Every LFO in the project for the "Add modulator..." picker, in graph order: where it lives (the innermost macro
// it sits in), what it already moves (each routing's real destination, macro ports looked through) and whether it
// already moves (`nodeUuid`, `paramId`) itself. Names and uuids are read from the live graph, never cached.
std::vector<synth::ui::TrackHeaderHost::LfoChoice> MainComponent::getLfoChoices(const juce::String& nodeUuid,
                                                                                const juce::String& paramId) {
    std::vector<synth::ui::TrackHeaderHost::LfoChoice> result;
    auto& graph = audioEngine.getGraph();
    auto* target = findNodeByUuid(nodeUuid);
    const int raw = target != nullptr ? graphEditor.modulationChannelFor(target->nodeID, paramId) : -1;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<LFOModule*>(node->getProcessor()) == nullptr)
            continue;
        synth::ui::TrackHeaderHost::LfoChoice choice;
        choice.uuid = uuidOf(node);
        choice.name = synth::moduleTitle(*node);
        if (const auto* macro = graphEditor.getMacros().findByMember(choice.uuid))
            choice.macroName = macro->name;
        for (const auto& r : audioEngine.getModulationRoutings()) {
            if (!r.hasSource || !r.hasDest || r.role != PortRole::ModCV)
                continue;
            const auto real = resolveThroughPorts(audioEngine, graphEditor, r);
            if (real.source.node != node->nodeID)
                continue;
            if (target != nullptr && real.dest.node == target->nodeID && real.dest.channel == raw)
                choice.movesThisParameter = true;
            choice.targets.push_back(destinationName(graph, real.dest));
        }
        result.push_back(std::move(choice));
    }
    return result;
}

// Both nodes are found again by uuid; the host call is one undo step and adds no card.
bool MainComponent::connectModulator(const juce::String& lfoUuid, const juce::String& nodeUuid,
                                     const juce::String& paramId) {
    auto* lfo = findNodeByUuid(lfoUuid);
    auto* target = findNodeByUuid(nodeUuid);
    return lfo != nullptr && target != nullptr &&
           graphEditor.connectExistingLfoModulator(lfo->nodeID, target->nodeID, paramId);
}

// Only an LFO routed through an Attenuverter can be re-pointed: that Attenuverter is what the amount lane is keyed by,
// and a hand-patched direct cable has neither.
bool MainComponent::canChangeModulatorSource(const synth::ui::ModulatorInfo& modulator) {
    return modulator.isLfo && modulator.attenuverterUuid.isNotEmpty() &&
           findNodeByUuid(modulator.targetUuid) != nullptr && findNodeByUuid(modulator.sourceUuid) != nullptr;
}

// "Change source...": the new LFO is cabled in at the old routing's depth, the amount lane (keyed by the routing's
// hidden Attenuverter) is re-keyed to the new routing's Attenuverter with its points untouched, and the old routing
// is cut -- all in ONE graph + timeline undo step, with no confirm dialog. The old LFO stays on the canvas, whatever
// else it drives (or nothing): nothing a person built is deleted by a re-point.
bool MainComponent::changeModulatorSource(const synth::ui::ModulatorInfo& modulator, const juce::String& lfoUuid) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    auto* lfo = findNodeByUuid(lfoUuid);
    const auto old = findAttenuverterRouting(audioEngine, modulator);
    if (!canChangeModulatorSource(modulator) || target == nullptr || lfo == nullptr || !old.has_value() ||
        lfoUuid == modulator.sourceUuid || dynamic_cast<LFOModule*>(lfo->getProcessor()) == nullptr)
        return false;
    auto& graph = audioEngine.getGraph();
    float depth = 0.5f;
    if (auto* oldAtten = graph.getNodeForId(old->attenuverterNodeID))
        if (auto* amount = findParameterByID(oldAtten->getProcessor(), synth::ui::kAmountParamId))
            depth = amount->convertFrom0to1(amount->getValue());
    const auto* amountLane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid);
    const bool hasLane = amountLane != nullptr;
    const auto laneId = hasLane ? amountLane->id : synth::LaneId{};
    const auto oldRouting = *old;
    bool changed = false;
    undoManager.recordGraphTimelineAndMacroChange(graph, timelineDoc, graphEditor.getMacros(), [&, this] {
        const auto created = graphEditor.connectModulationSource(lfo->nodeID, /*sourceChannel=*/0, target->nodeID,
                                                                 modulator.targetChannel, depth,
                                                                 /*recordUndo=*/false);
        if (created.uid == 0)
            return;
        if (hasLane)
            timelineDoc.rebindLane(laneId, uuidOf(graph.getNodeForId(created)));
        graphEditor.removeModulator(oldRouting, /*removeLonelySource=*/false, /*recordUndo=*/false);
        changed = true;
    });
    return changed;
}

// The row names its routing by uuids (node ids do not survive an undo restore), so it is looked up
// again in the live graph, by the real modulator and parameter (ports looked through). Only an LFO source is
// taken with the routing. When that would delete the LFO (this routing is its last destination) the person is
// asked first, unless they switched the question off; the removal itself runs from the answer.
void MainComponent::removeModulator(const synth::ui::ModulatorInfo& modulator) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    auto* lfo = modulator.isLfo ? findNodeByUuid(modulator.sourceUuid) : nullptr;
    const auto* settings = appProperties.getUserSettings();
    const bool ask = settings == nullptr || settings->getBoolValue(synth::ui::kAskBeforeRemovingLfoKey, true);
    if (target == nullptr || lfo == nullptr || !ask || lfoMovesMoreThanOneRouting(lfo->nodeID)) {
        performRemoveModulator(modulator);
        return;
    }
    auto* param = findParameterByID(target->getProcessor(), modulator.paramId);
    const auto targetName = param != nullptr ? param->getName(64) : modulator.paramId;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    synth::ui::confirmRemoveLfo(synth::ui::removeLfoConfirmText(synth::moduleTitle(*lfo), targetName),
                                [safeThis, modulator](bool confirmed, bool dontAskAgain) {
                                    auto* self = safeThis.getComponent();
                                    if (self == nullptr || !confirmed)
                                        return;
                                    if (dontAskAgain)
                                        if (auto* userSettings = self->appProperties.getUserSettings()) {
                                            userSettings->setValue(synth::ui::kAskBeforeRemovingLfoKey, "0");
                                            userSettings->saveIfNeeded();
                                        }
                                    self->performRemoveModulator(modulator);
                                });
}

// True when `lfoId` drives more than one routing, so removing one of them leaves the LFO in place.
bool MainComponent::lfoMovesMoreThanOneRouting(juce::AudioProcessorGraph::NodeID lfoId) {
    int count = 0;
    for (const auto& r : audioEngine.getModulationRoutings())
        if (r.hasSource && r.hasDest && resolveThroughPorts(audioEngine, graphEditor, r).source.node == lfoId)
            ++count;
    return count > 1;
}

void MainComponent::performRemoveModulator(const synth::ui::ModulatorInfo& modulator) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    if (target == nullptr)
        return;
    auto& graph = audioEngine.getGraph();
    // The routing's amount lane goes with it, in the same undo step: one Cmd+Z brings back the LFO, the cable
    // and the amount lane together. It belongs to the routing alone, so it goes even when the LFO stays to
    // drive another jack. A routing with no amount lane keeps the plain graph-only step.
    const bool hasAmountLane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid) != nullptr;
    const auto remove = [this, hasAmountLane, &modulator](const AudioEngine::ModulationRouting& routing) {
        if (!hasAmountLane) {
            graphEditor.removeModulator(routing, modulator.isLfo);
            return;
        }
        undoManager.recordGraphTimelineAndMacroChange(
            audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [this, &routing, &modulator] {
                graphEditor.removeModulator(routing, modulator.isLfo, /*recordUndo=*/false);
                if (const auto* lane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid))
                    timelineDoc.removeLane(lane->id);
            });
    };
    for (const auto& r : audioEngine.getModulationRoutings()) {
        const auto real = resolveThroughPorts(audioEngine, graphEditor, r);
        if (real.dest.node != target->nodeID || real.dest.channel != modulator.targetChannel)
            continue;
        const bool sameChain = modulator.attenuverterUuid.isNotEmpty() &&
                               r.kind == AudioEngine::RoutingKind::AttenuverterChain &&
                               uuidOf(graph.getNodeForId(r.attenuverterNodeID)) == modulator.attenuverterUuid;
        const bool sameDirect = modulator.attenuverterUuid.isEmpty() &&
                                r.kind != AudioEngine::RoutingKind::AttenuverterChain &&
                                real.source.channel == modulator.sourceChannel &&
                                uuidOf(graph.getNodeForId(real.source.node)) == modulator.sourceUuid;
        if (sameChain || sameDirect) {
            remove(r);
            return;
        }
    }
}

// The modulator rows of a parameter sit under its automation lane, so showing the modulator is showing that lane:
// automateParameter finds the lane or makes it (one undo step), opens the Timeline tab and scrolls to the lane.
void MainComponent::revealModulatorInTimeline(const synth::ui::ModulatorInfo& modulator) {
    if (auto* target = findNodeByUuid(modulator.targetUuid))
        automateParameter(target->nodeID, modulator.paramId);
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

//==============================================================================
// Sections -> amount lanes (docs/timeline/automation.md#migration-from-sections)

// A `level` lane the sections UI drew as an LFO's band rather than as a lane row: the LFO modulates another
// lane on the same track, and nothing modulates the level lane itself.
bool MainComponent::wasSectionsLane(const synth::Track& track, const synth::AutomationLane& level) {
    if (level.paramId != synth::ui::kSectionsParamId || !getModulators(level.nodeUuid, level.paramId).empty())
        return false;
    for (const auto& lane : track.lanes)
        if (lane.id != level.id)
            for (const auto& info : getModulators(lane.nodeUuid, lane.paramId))
                if (info.isLfo && info.sourceUuid == level.nodeUuid)
                    return true;
    return false;
}

// Once per project load, with the graph already built, and never an undo step (the loaded document IS the
// migrated one; it is marked clean right after). For every sections lane of an LFO whose every routing runs
// through an Attenuverter: each routing gets an amount lane that holds its current amount inside the old
// blocks and 0 outside, on the same Hold edges; then the level lane goes and the LFO's level is set to 1, so
// the LFO is no longer silenced on its own. An LFO with any direct cable keeps its level lane (as an ordinary
// lane row now): a direct cable has no amount to carry the blocks.
void MainComponent::migrateSectionsToAmountLanes() {
    std::vector<std::pair<synth::LaneId, synth::TrackId>> candidates;
    for (const auto& track : timelineDoc.getTracks())
        for (const auto& lane : track.lanes)
            if (auto* node = findNodeByUuid(lane.nodeUuid); node != nullptr &&
                                                            dynamic_cast<LFOModule*>(node->getProcessor()) != nullptr &&
                                                            wasSectionsLane(track, lane))
                candidates.push_back({lane.id, track.id});
    for (const auto& [levelId, trackId] : candidates)
        migrateSectionsLane(levelId, trackId);
}

void MainComponent::migrateSectionsLane(synth::LaneId levelId, synth::TrackId trackId) {
    const auto* level = timelineDoc.getLane(levelId);
    auto* lfo = level != nullptr ? findNodeByUuid(level->nodeUuid) : nullptr;
    if (lfo == nullptr)
        return;
    auto& graph = audioEngine.getGraph();
    std::vector<juce::AudioProcessorGraph::Node*> attenuverters;
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (resolveThroughPorts(audioEngine, graphEditor, r).source.node != lfo->nodeID)
            continue;
        auto* atten =
            r.kind == AudioEngine::RoutingKind::AttenuverterChain ? graph.getNodeForId(r.attenuverterNodeID) : nullptr;
        if (atten == nullptr)
            return; // a direct cable: the level lane stays
        attenuverters.push_back(atten);
    }
    if (attenuverters.empty())
        return;

    const auto blocks = synth::ui::sectionsFromPoints(level->points);
    for (auto* atten : attenuverters) {
        const auto uuid = uuidOf(atten);
        auto* amount = findParameterByID(atten->getProcessor(), synth::ui::kAmountParamId);
        if (amount == nullptr || synth::ui::amountLaneFor(timelineDoc, uuid) != nullptr)
            continue;
        synth::ui::writeAmountLane(
            timelineDoc, trackId, uuid,
            synth::ui::amountPointsFromSections(blocks, amount->convertFrom0to1(amount->getValue())));
    }
    timelineDoc.removeLane(levelId);
    if (auto* levelParam = findParameterByID(lfo->getProcessor(), synth::ui::kSectionsParamId))
        levelParam->setValueNotifyingHost(levelParam->convertTo0to1(1.0f));
}
