// MainComponentModulators.cpp -- the TrackHeaderHost side of a timeline lane's modulators: the timeline
// has no graph, so this is where a lane's (node, parameter) is turned into the routings into its CV jack,
// where "Add modulator..." / "Remove modulator" reach the GraphEditor, and where a modulator row's
// controls read and write live parameters through the canvas knobs' own undo path; and the load-time migration
// of a project's retired LFO sections lanes to amount lanes.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MacroOwnerIndex.h"
#include "MainComponent.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"
#include "UI/Timeline/AutomationLanes/Modulators/RemoveLfoConfirm.h"
#include <algorithm>
#include <map>
#include <set>

namespace {
juce::String uuidOf(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
}

// The macro port nodes, found once per pass by MacroGroupController::nodeIsMacroPort's rule (a node is a port when the
// first macro holding it lists it as one). Asking the controller per hop searched every macro's members each time, so
// each timeline refresh cost routings x macros x members (docs/architecture/graph-queries.md).
class PortNodes {
public:
    PortNodes(juce::AudioProcessorGraph& graph, const synth::MacroSet& macros) {
        static const juce::Identifier uuidKey("uuid");
        const synth::MacroOwnerIndex owners(macros);
        for (auto* node : graph.getNodes()) {
            const auto uuid = node->properties[uuidKey].toString();
            const auto* owner = uuid.isNotEmpty() ? owners.ownerOf(uuid) : nullptr;
            if (owner != nullptr && owner->memberIsPort(uuid))
                ids_.insert(node->nodeID.uid);
        }
    }
    bool contains(juce::AudioProcessorGraph::NodeID id) const { return ids_.count(id.uid) != 0; }

private:
    std::set<juce::uint32> ids_;
};

// A routing with the macro ports between its modulator and its parameter looked through, so a row names the
// real LFO and the real parameter however the cable crosses macro boundaries.
synth::ui::ResolvedRouting resolveThroughPorts(const synth::ui::ConnectionIndex& cables, const PortNodes& ports,
                                               const AudioEngine::ModulationRouting& routing) {
    return synth::ui::resolveRouting(cables, routing,
                                     [&ports](juce::AudioProcessorGraph::NodeID id) { return ports.contains(id); });
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
// What the lane's "Change source..." can re-point a routing at: an LFO or an envelope, decided by the shared source
// catalogue's grouping so a new modulator module needs no edit here. `channel` < 0 asks about the module as a whole.
bool isLaneModulatorSource(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node, int channel = -1) {
    for (const auto& item : synth::ui::enumerateModSources(graph))
        if (item.node == node && (channel < 0 || item.channel == channel))
            return item.group == synth::ui::ModSourceGroup::Lfos || item.group == synth::ui::ModSourceGroup::Envelopes;
    return false;
}

// One picker entry for the source output (`node`, `channel`; channel < 0 = any output): where it lives (the innermost
// macro it sits in), what it already moves (each routing's real destination, macro ports looked through) and whether
// it already moves (`target`, `raw`).
synth::ui::TrackHeaderHost::LfoChoice describeSource(AudioEngine& engine, const synth::MacroSet& macros,
                                                     const synth::ui::ConnectionIndex& cables, const PortNodes& ports,
                                                     juce::AudioProcessorGraph::Node* node, int channel,
                                                     juce::AudioProcessorGraph::Node* target, int raw) {
    auto& graph = engine.getGraph();
    synth::ui::TrackHeaderHost::LfoChoice choice;
    choice.uuid = uuidOf(node);
    choice.name = synth::moduleTitle(*node);
    choice.sourceChannel = std::max(channel, 0);
    if (const auto* macro = macros.findByMember(choice.uuid))
        choice.macroName = macro->name;
    for (const auto& r : engine.getModulationRoutings()) {
        if (!r.hasSource || !r.hasDest || r.role != PortRole::ModCV)
            continue;
        const auto real = resolveThroughPorts(cables, ports, r);
        if (real.source.node != node->nodeID || (channel >= 0 && real.source.channel != channel))
            continue;
        if (target != nullptr && real.dest.node == target->nodeID && real.dest.channel == raw)
            choice.movesThisParameter = true;
        choice.targets.push_back(destinationName(graph, real.dest));
    }
    return choice;
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
    return getModulatorsForLanes({{nodeUuid, paramId}}).front();
}

// One pass for every lane: the nodes indexed by uuid, the cables indexed once, and each ModCV routing resolved once
// and filed under its real destination, so each lane is a lookup (a lane by lane walk made every timeline refresh
// cost lanes x routings x cables).
std::vector<std::vector<synth::ui::ModulatorInfo>>
MainComponent::getModulatorsForLanes(const std::vector<std::pair<juce::String, juce::String>>& lanes) {
    auto& graph = audioEngine.getGraph();
    std::map<juce::String, juce::AudioProcessorGraph::Node*> byUuid;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString().isNotEmpty())
            byUuid.emplace(node->properties["uuid"].toString(), node); // the first, as findNodeByUuid answers

    using Resolved = std::pair<AudioEngine::ModulationRouting, synth::ui::ResolvedRouting>;
    std::map<std::pair<juce::uint32, int>, std::vector<Resolved>> byDest;
    const synth::ui::ConnectionIndex cables(graph);
    const PortNodes ports(graph, graphEditor.getMacros());
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasSource || !r.hasDest || r.role != PortRole::ModCV)
            continue;
        const auto real = resolveThroughPorts(cables, ports, r);
        byDest[{real.dest.node.uid, real.dest.channel}].emplace_back(r, real);
    }

    std::vector<std::vector<synth::ui::ModulatorInfo>> out(lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i) {
        const auto& [nodeUuid, paramId] = lanes[i];
        const auto found = nodeUuid.isNotEmpty() ? byUuid.find(nodeUuid) : byUuid.end();
        auto* target = found != byUuid.end() ? found->second : nullptr;
        const int raw = target != nullptr ? graphEditor.modulationChannelFor(target->nodeID, paramId) : -1;
        const auto bucket = raw >= 0 ? byDest.find({target->nodeID.uid, raw}) : byDest.end();
        if (bucket == byDest.end())
            continue;
        for (const auto& [r, real] : bucket->second) {
            auto* source = graph.getNodeForId(real.source.node);
            if (source == nullptr)
                continue;
            synth::ui::ModulatorInfo info;
            info.sourceUuid = uuidOf(source);
            info.sourceTitle = synth::moduleTitle(*source);
            info.sourceChannel = real.source.channel;
            info.isLfo = dynamic_cast<LFOModule*>(source->getProcessor()) != nullptr;
            if (const auto* module = dynamic_cast<ModuleBase*>(source->getProcessor()))
                info.isEnvelope = module->getModulationCategory() == ModulationCategory::Envelope;
            if (r.kind == AudioEngine::RoutingKind::AttenuverterChain)
                info.attenuverterUuid = uuidOf(graph.getNodeForId(r.attenuverterNodeID));
            info.targetUuid = nodeUuid;
            info.paramId = paramId;
            info.targetChannel = raw;
            info.colour = graphEditor.modulationWireColour(real.source.node);
            out[i].push_back(std::move(info));
        }
    }
    return out;
}

bool MainComponent::canModulate(const juce::String& nodeUuid, const juce::String& paramId) {
    auto* target = findNodeByUuid(nodeUuid);
    if (target == nullptr)
        return false;
    // A plugin parameter has no CV input until a cable claims one; any parameter can, while a slot is free.
    if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(target->getProcessor()))
        return hosted->canModulateParam(paramId);
    return graphEditor.modulationChannelFor(target->nodeID, paramId) >= 0;
}

// A new LFO or envelope beside the target card, cabled in: create, cable, hidden attenuverter and macro join are one
// undo step. Only an LFO or an envelope can be added from a lane.
juce::String MainComponent::addNewModulator(const juce::String& nodeUuid, const juce::String& paramId,
                                            const juce::String& typeName, int sourceChannel) {
    auto* target = findNodeByUuid(nodeUuid);
    if (target == nullptr || typeName.isEmpty())
        return {};
    if (typeName != "LFO" && typeName != "ADSR")
        return {};
    const auto source = graphEditor.addNewModulatorTo(typeName, sourceChannel, target->nodeID, paramId);
    return source.uid == 0 ? juce::String() : uuidOf(audioEngine.getGraph().getNodeForId(source));
}

// Every LFO and envelope output in the project for the "Add modulator..." and "Change source..." pickers, in the source
// catalogue's order, each described with where it lives (the innermost macro it sits in), what it already moves (each
// routing's real destination, macro ports looked through) and whether it already moves (`nodeUuid`, `paramId`) itself;
// a macro's port nodes are skipped (they are plumbing, not sources).
std::vector<synth::ui::TrackHeaderHost::LfoChoice> MainComponent::getChangeSourceChoices(const juce::String& nodeUuid,
                                                                                         const juce::String& paramId) {
    std::vector<synth::ui::TrackHeaderHost::LfoChoice> result;
    auto& graph = audioEngine.getGraph();
    auto* target = findNodeByUuid(nodeUuid);
    const int raw = target != nullptr ? graphEditor.modulationChannelFor(target->nodeID, paramId) : -1;
    const synth::ui::ConnectionIndex cables(graph);
    const PortNodes ports(graph, graphEditor.getMacros());
    for (const auto& item : synth::ui::enumerateModSources(graph)) {
        if (item.group != synth::ui::ModSourceGroup::Lfos && item.group != synth::ui::ModSourceGroup::Envelopes)
            continue;
        auto* node = graph.getNodeForId(item.node);
        if (node == nullptr || ports.contains(item.node))
            continue;
        auto choice =
            describeSource(audioEngine, graphEditor.getMacros(), cables, ports, node, item.channel, target, raw);
        choice.name = item.label();
        choice.isEnvelope = item.group == synth::ui::ModSourceGroup::Envelopes;
        result.push_back(std::move(choice));
    }
    return result;
}

// Both nodes are found again by uuid; the host call is one undo step and adds no card.
// Only an LFO or an envelope output can be taken (the same set the picker lists).
bool MainComponent::connectModulator(const juce::String& sourceUuid, int sourceChannel, const juce::String& nodeUuid,
                                     const juce::String& paramId) {
    auto* source = findNodeByUuid(sourceUuid);
    auto* target = findNodeByUuid(nodeUuid);
    return source != nullptr && target != nullptr &&
           isLaneModulatorSource(audioEngine.getGraph(), source->nodeID, sourceChannel) &&
           graphEditor.connectExistingModulator(source->nodeID, sourceChannel, target->nodeID, paramId);
}

// Only an LFO or an envelope routed through an Attenuverter can be re-pointed: that Attenuverter is what the amount
// lane is keyed by, and a hand-patched direct cable has neither.
bool MainComponent::canChangeModulatorSource(const synth::ui::ModulatorInfo& modulator) {
    auto* source = findNodeByUuid(modulator.sourceUuid);
    return modulator.attenuverterUuid.isNotEmpty() && findNodeByUuid(modulator.targetUuid) != nullptr &&
           source != nullptr && isLaneModulatorSource(audioEngine.getGraph(), source->nodeID);
}

// "Change source...": the new source (an LFO, or an envelope output) is cabled in at the old routing's depth, the
// amount lane (keyed by the routing's hidden Attenuverter) is re-keyed to the new routing's Attenuverter with its
// points untouched, and the old routing is cut -- all in ONE graph + timeline undo step, with no confirm dialog. The
// old source stays on the canvas, whatever else it drives (or nothing): nothing a person built is deleted by a
// re-point.
bool MainComponent::changeModulatorSource(const synth::ui::ModulatorInfo& modulator, const juce::String& sourceUuid,
                                          int sourceChannel) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    auto* source = findNodeByUuid(sourceUuid);
    if (target == nullptr || source == nullptr ||
        (sourceUuid == modulator.sourceUuid && sourceChannel == modulator.sourceChannel) ||
        !isLaneModulatorSource(audioEngine.getGraph(), source->nodeID, sourceChannel))
        return false;
    return repointModulator(modulator, [this, source, sourceChannel, target, &modulator](float depth) {
        return graphEditor.connectModulationSource(source->nodeID, sourceChannel, target->nodeID,
                                                   modulator.targetChannel, depth, /*recordUndo=*/false);
    });
}

// "Change source" -> "New LFO" / "New envelope": the same re-point, with the new source module created beside the
// target card and cabled in -- create, cable, amount-lane re-key and cut of the old routing are ONE undo step.
bool MainComponent::changeModulatorSourceToNew(const synth::ui::ModulatorInfo& modulator, const juce::String& typeName,
                                               int sourceChannel) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    if (target == nullptr || typeName.isEmpty())
        return false;
    return repointModulator(modulator, [this, &typeName, sourceChannel, target, &modulator](float depth) {
        return graphEditor.addModulationSourceModule(typeName, sourceChannel, target->nodeID, modulator.targetChannel,
                                                     depth, /*recordUndo=*/false);
    });
}

// The shared recipe: `makeRouting(depth)` cables the new source into the old routing's CV jack at the old depth and
// returns the new routing's Attenuverter (invalid = nothing happened); the amount lane (keyed by the routing's hidden
// Attenuverter) is re-keyed to it with its points untouched, it takes the old routing's slot in the modulator order of
// every lane on the jack, and the old routing is cut -- all in the one recorded step, so an undo puts the order back.
bool MainComponent::repointModulator(const synth::ui::ModulatorInfo& modulator,
                                     const std::function<juce::AudioProcessorGraph::NodeID(float)>& makeRouting) {
    const auto old = findAttenuverterRouting(audioEngine, modulator);
    if (!canChangeModulatorSource(modulator) || !old.has_value())
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
    // Every lane on this jack, its rows as they show now: the new routing takes the old one's slot in each (the slot is
    // stored under the Attenuverter's uuid, which the re-point replaces), so the row keeps its place.
    std::vector<std::pair<synth::LaneId, std::vector<juce::String>>> orders;
    for (const auto& track : timelineDoc.getTracks())
        for (const auto& lane : track.lanes)
            if (lane.nodeUuid == modulator.targetUuid && lane.paramId == modulator.paramId)
                orders.emplace_back(lane.id, shownModulatorOrder(lane.id));
    const auto oldKey = synth::ui::modulatorOrderKey(modulator);
    // The modulator rows see the whole re-point as one change, so the row re-binds in place (it never fades out while
    // another fades in).
    const synth::ui::TimelineAutomationLanes::ModulatorHold hold(timelinePanel.getAutomationLanes());
    bool changed = false;
    undoManager.recordGraphTimelineAndMacroChange(graph, timelineDoc, graphEditor.getMacros(), [&, this] {
        const auto created = makeRouting(depth);
        if (created.uid == 0)
            return;
        const auto newAtten = uuidOf(graph.getNodeForId(created));
        if (hasLane)
            timelineDoc.rebindLane(laneId, newAtten);
        for (auto& [lane, order] : orders) {
            std::replace(order.begin(), order.end(), oldKey, newAtten);
            timelineDoc.setModulatorOrder(lane, std::move(order));
        }
        graphEditor.removeModulator(oldRouting, /*removeLonelySource=*/false, /*recordUndo=*/false);
        changed = true;
    });
    return changed;
}

// The row names its routing by uuids (node ids do not survive an undo restore), so it is looked up
// again in the live graph, by the real modulator and parameter (ports looked through). Every way a person removes a
// modulation (a timeline row, the mod dot's menu, a double-click on a cable or on the dot) ends here. Only an LFO
// source is taken with the routing. When that would delete the LFO (this routing is its last destination) the
// person is asked first -- remove the LFO too, or keep it -- unless they asked to be remembered; the removal itself
// runs from the answer.
void MainComponent::removeModulator(const synth::ui::ModulatorInfo& modulator) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    auto* lfo = modulator.isLfo ? findNodeByUuid(modulator.sourceUuid) : nullptr;
    auto* settings = appProperties.getUserSettings();
    if (target == nullptr || lfo == nullptr || lfoMovesMoreThanOneRouting(lfo->nodeID)) {
        performRemoveModulator(modulator);
        return;
    }
    if (settings != nullptr && !settings->getBoolValue(synth::ui::kAskBeforeRemovingLfoKey, true)) {
        performRemoveModulator(modulator, synth::ui::rememberedRemoveLfoChoice(*settings) ==
                                              synth::ui::RemoveLfoChoice::removeModule);
        return;
    }
    auto* param = findParameterByID(target->getProcessor(), modulator.paramId);
    const auto targetName = param != nullptr ? param->getName(64) : modulator.paramId;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    synth::ui::confirmRemoveLfo(synth::ui::removeLfoConfirmText(synth::moduleTitle(*lfo), targetName),
                                [safeThis, modulator](synth::ui::RemoveLfoChoice choice, bool remember) {
                                    auto* self = safeThis.getComponent();
                                    if (self == nullptr || choice == synth::ui::RemoveLfoChoice::cancel)
                                        return;
                                    if (remember)
                                        if (auto* userSettings = self->appProperties.getUserSettings())
                                            synth::ui::rememberRemoveLfoChoice(*userSettings, choice);
                                    self->performRemoveModulator(modulator,
                                                                 choice == synth::ui::RemoveLfoChoice::removeModule);
                                });
}

// True when `lfoId` drives more than one routing, so removing one of them leaves the LFO in place.
bool MainComponent::lfoMovesMoreThanOneRouting(juce::AudioProcessorGraph::NodeID lfoId) {
    int count = 0;
    const synth::ui::ConnectionIndex cables(audioEngine.getGraph());
    const PortNodes ports(audioEngine.getGraph(), graphEditor.getMacros());
    for (const auto& r : audioEngine.getModulationRoutings())
        if (r.hasSource && r.hasDest && resolveThroughPorts(cables, ports, r).source.node == lfoId)
            ++count;
    return count > 1;
}

// `deleteLonelyLfo` false takes only the cable, leaving an LFO that drives nothing else on the canvas.
void MainComponent::performRemoveModulator(const synth::ui::ModulatorInfo& modulator, bool deleteLonelyLfo) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    if (target == nullptr)
        return;
    auto& graph = audioEngine.getGraph();
    // The routing's amount lane goes with it, in the same undo step: one Cmd+Z brings back the LFO, the cable
    // and the amount lane together. It belongs to the routing alone, so it goes even when the LFO stays to
    // drive another jack. A routing with no amount lane keeps the plain graph-only step.
    const bool hasAmountLane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid) != nullptr;
    const bool takeLfo = modulator.isLfo && deleteLonelyLfo;
    const auto remove = [this, hasAmountLane, takeLfo, &modulator](const AudioEngine::ModulationRouting& routing) {
        if (!hasAmountLane) {
            graphEditor.removeModulator(routing, takeLfo);
            return;
        }
        undoManager.recordGraphTimelineAndMacroChange(
            audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [this, &routing, &modulator, takeLfo] {
                graphEditor.removeModulator(routing, takeLfo, /*recordUndo=*/false);
                if (const auto* lane = synth::ui::amountLaneFor(timelineDoc, modulator.attenuverterUuid))
                    timelineDoc.removeLane(lane->id);
            });
    };
    if (const auto routing = findModulatorRouting(modulator))
        remove(*routing);
}

// The live routing a row stands for (a copy: the engine's list is rebuilt by the edits that follow), found by its
// real source and parameter with macro ports looked through, or by its Attenuverter's uuid for a chain.
std::optional<AudioEngine::ModulationRouting>
MainComponent::findModulatorRouting(const synth::ui::ModulatorInfo& modulator) {
    auto* target = findNodeByUuid(modulator.targetUuid);
    if (target == nullptr)
        return std::nullopt;
    auto& graph = audioEngine.getGraph();
    const synth::ui::ConnectionIndex cables(graph);
    const PortNodes ports(graph, graphEditor.getMacros());
    for (const auto& r : audioEngine.getModulationRoutings()) {
        const auto real = resolveThroughPorts(cables, ports, r);
        if (real.dest.node != target->nodeID || real.dest.channel != modulator.targetChannel)
            continue;
        const bool sameChain = modulator.attenuverterUuid.isNotEmpty() &&
                               r.kind == AudioEngine::RoutingKind::AttenuverterChain &&
                               uuidOf(graph.getNodeForId(r.attenuverterNodeID)) == modulator.attenuverterUuid;
        const bool sameDirect = modulator.attenuverterUuid.isEmpty() &&
                                r.kind != AudioEngine::RoutingKind::AttenuverterChain &&
                                real.source.channel == modulator.sourceChannel &&
                                uuidOf(graph.getNodeForId(real.source.node)) == modulator.sourceUuid;
        if (sameChain || sameDirect)
            return r;
    }
    return std::nullopt;
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

// Select, then centre the canvas on the card, so a modulator off screen is found as well as highlighted. A node inside
// a collapsed macro has no card of its own on the canvas; the visible thing is the outermost collapsed macro's card,
// so that is what gets centred (the hidden member's own bounds sit wherever its macro was expanded).
void MainComponent::showNodeOnCanvas(const juce::String& uuid) {
    auto* node = findNodeByUuid(uuid);
    if (node == nullptr)
        return;
    ensureCanvasShowing();
    selectNodeInGraph(uuid);
    const synth::Macro* collapsedCard = nullptr;
    auto& macros = graphEditor.getMacros();
    for (const auto* macro = macros.findByMember(uuid); macro != nullptr;
         macro = macro->parentId.isEmpty() ? nullptr : macros.find(macro->parentId))
        if (macro->collapsed)
            collapsedCard = macro;
    if (collapsedCard != nullptr) {
        graphEditor.centreViewOn(collapsedCard->bounds.toFloat().getCentre());
        return;
    }
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
    const synth::ui::ConnectionIndex cables(graph);
    const PortNodes ports(graph, graphEditor.getMacros());
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (resolveThroughPorts(cables, ports, r).source.node != lfo->nodeID)
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
