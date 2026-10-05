// GraphEditorModulators.cpp -- the canvas side of a timeline lane's modulators: which CV jack drives a
// parameter, adding an LFO card beside a module and cabling it into that jack, removing one routing (and
// an LFO left with nothing to drive), and the wire colour a modulator row shows. GraphEditor is declared
// in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <set>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

// A new routing's depth: half way, so the first thing a person hears is clearly modulated but leaves
// room both ways (the attenuverter's own default after AudioEngine::addModRouting is full scale).
constexpr float kDefaultModulatorDepth = 0.5f;
constexpr int kBesideGap = 24; // px between the target card and the LFO card

ModuleBase* moduleOf(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
}

// Left of the target, where a modulator reads as feeding it; right of it when the left edge of the
// canvas is too close. resolvePlacement then moves it to the nearest free slot either way.
juce::Point<int> besideTarget(juce::Rectangle<int> target, juce::Point<int> size) {
    if (target.isEmpty())
        return {synth::LayoutUtil::kArrangeOriginX, synth::LayoutUtil::kArrangeOriginY};
    const int left = target.getX() - kBesideGap - size.x;
    if (left >= synth::LayoutUtil::kArrangeOriginX)
        return {left, target.getY()};
    return {target.getRight() + kBesideGap, target.getY()};
}

bool hasOutgoingConnection(juce::AudioProcessorGraph& graph, NodeID id) {
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == id)
            return true;
    return false;
}

// The routing connectPorts just made into (target, raw): its hidden attenuverter gets a uuid (a node
// id does not survive an undo restore, and the timeline row names it by uuid) and the default depth.
// `existing` are attenuverters that were there before the cable, so a second routing between the same two nodes
// keeps its own depth.
NodeID settleNewRouting(AudioEngine& engine, NodeID lfoId, NodeID targetId, int raw, float depth,
                        const std::set<juce::uint32>& existing = {}) {
    NodeID settled;
    for (const auto& r : engine.getModulationRoutings()) {
        if (r.kind != AudioEngine::RoutingKind::AttenuverterChain || r.sourceNodeID != lfoId ||
            r.destNodeID != targetId || r.destChannelIndex != raw || existing.count(r.attenuverterNodeID.uid) > 0)
            continue;
        auto* atten = engine.getGraph().getNodeForId(r.attenuverterNodeID);
        if (atten == nullptr)
            continue;
        synth::AIStateMapper::ensureNodeUuid(atten);
        if (auto* amount = findParameterByID(atten->getProcessor(), "amount"))
            amount->setValueNotifyingHost(amount->convertTo0to1(depth));
        settled = r.attenuverterNodeID;
    }
    return settled;
}
} // namespace

// The rule itself (paramId match, or the display-name fallback for a jack with no paramId) lives
// in ModuleBase::modulationChannelForParam, shared with a patch modulation's "destParam".
int GraphEditor::modulationChannelFor(NodeID nodeId, const juce::String& paramId) const {
    auto* node = audioEngine.getGraph().getNodeForId(nodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    return module != nullptr ? module->modulationChannelForParam(paramId) : -1;
}

// Everything is ONE undo step: the node, its uuid, its placement (and any room made for it), the cable
// through the normal CV path (connectPorts -> addModRouting, hidden attenuverter included), the depth,
// and -- when the target is a macro member -- the LFO joining that macro. The join runs after the cable
// exists, so the crossing plan sees the cable as interior and mints no port for it. The final
// updateComponents() also fires onGraphStructureChanged with the routing already in place, which is
// what tells the timeline to show the new row.
NodeID GraphEditor::addLfoModulator(NodeID targetId, const juce::String& paramId) {
    auto& graph = audioEngine.getGraph();
    auto* target = graph.getNodeForId(targetId);
    const int raw = modulationChannelFor(targetId, paramId);
    if (target == nullptr || raw < 0)
        return {};
    const auto* macro = macros.findByMember(synth::AIStateMapper::ensureNodeUuid(target));
    const juce::String macroId = macro != nullptr ? macro->id : juce::String();

    NodeID lfoId;
    auto mutation = [this, &graph, &lfoId, targetId, raw, macroId] {
        // The hull the LFO is about to join is where it should land, not an obstacle (resolvePlacement).
        juce::ScopedValueSetter<juce::String> joinScope(macroDragJoinId_, macroId);
        auto node = graph.addNode(synth::AIStateMapper::createModule("LFO"));
        if (node == nullptr)
            return;
        lfoId = node->nodeID;
        const auto lfoUuid = synth::AIStateMapper::ensureNodeUuid(node.get());
        placeNewModulator(*node, targetId, "LFO");

        auto* lfo = moduleOf(graph, lfoId);
        auto* dst = moduleOf(graph, targetId);
        if (lfo == nullptr || dst == nullptr)
            return;
        connectPorts(lfoId, lfo->mapOutputChannel(0).visibleJackIndex, targetId,
                     dst->mapInputChannel(raw).visibleJackIndex, /*isMidi=*/false, /*recordUndo=*/false);
        settleNewRouting(audioEngine, lfoId, targetId, raw, kDefaultModulatorDepth);

        if (macroId.isNotEmpty())
            macroController_.addSelectionToMacro(macroId, {lfoUuid}, /*recordUndo=*/false);
        else
            macroController_.makeRoomFor("n:" + juce::String((juce::int64)lfoId.uid));
        reflowOutputDock();
        updateComponents();
    };
    if (undoManager != nullptr)
        undoManager->recordGraphAndMacroChange(graph, macros, mutation);
    else
        mutation();
    repaintCanvas();
    return lfoId;
}

// The cable an existing LFO gets is the same CV path addLfoModulator uses, but through the programmatic-
// connection seam, so a macro boundary between the LFO and the knob is crossed with ports (the mixer sends and
// the Mod Matrix do the same). Port creation, the cable and its depth are one undo step.
bool GraphEditor::connectExistingLfoModulator(NodeID lfoId, NodeID targetId, const juce::String& paramId) {
    auto* lfo = moduleOf(audioEngine.getGraph(), lfoId);
    const int raw = modulationChannelFor(targetId, paramId);
    if (lfo == nullptr || dynamic_cast<LFOModule*>(lfo) == nullptr || raw < 0)
        return false;
    return connectModulationSource(lfoId, /*sourceChannel=*/0, targetId, raw, kDefaultModulatorDepth).uid != 0;
}

// Any modulation source (an LFO, an envelope, a macro knob...) onto one CV jack: `sourceChannel` is the raw output
// channel a Mod Matrix source entry names. Returns the new routing's hidden attenuverter; invalid (nothing changed)
// when a node or the jack is missing, the source is the target, or it already drives that jack from that channel.
NodeID GraphEditor::connectModulationSource(NodeID sourceId, int sourceChannel, NodeID targetId, int destChannel,
                                            float depth, bool recordUndo) {
    auto& graph = audioEngine.getGraph();
    auto* src = moduleOf(graph, sourceId);
    auto* dst = moduleOf(graph, targetId);
    if (src == nullptr || dst == nullptr || destChannel < 0 || sourceId == targetId)
        return {};
    const auto isPort = [this](NodeID id) { return macroController_.nodeIsMacroPort(id); };
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasSource || !r.hasDest)
            continue;
        const auto real = synth::ui::resolveRouting(graph, r, isPort);
        if (real.source.node == sourceId && real.source.channel == sourceChannel && real.dest.node == targetId &&
            real.dest.channel == destChannel)
            return {};
    }

    std::set<juce::uint32> existing;
    for (const auto& r : audioEngine.getModulationRoutings())
        existing.insert(r.attenuverterNodeID.uid);
    NodeID created;
    auto mutation = [&, this] {
        connectPorts(sourceId, src->mapOutputChannel(sourceChannel).visibleJackIndex, targetId,
                     dst->mapInputChannel(destChannel).visibleJackIndex, /*isMidi=*/false, /*recordUndo=*/false);
        created = settleNewRouting(audioEngine, sourceId, targetId, destChannel, depth, existing);
        return true;
    };
    auto step = [&, this] {
        macroController_.applyProgrammaticConnectionChange(autoCreateMacroPortsOnDragEnabled, mutation);
        updateComponents();
    };
    if (recordUndo && undoManager != nullptr)
        undoManager->recordGraphAndMacroChange(graph, macros, step);
    else
        step();
    repaintCanvas();
    return created;
}

// Same single record as addLfoModulator, for any module type: the cable goes through connectModulationSource (which
// mints macro ports across a boundary), so the macro join, when the target sits in a macro, runs after the cable and
// the crossing plan sees it as interior.
NodeID GraphEditor::addModulationSourceModule(const juce::String& typeName, int sourceChannel, NodeID targetId,
                                              int destChannel, float depth) {
    auto& graph = audioEngine.getGraph();
    auto* target = graph.getNodeForId(targetId);
    if (target == nullptr || destChannel < 0 || isSingletonIOModule(typeName))
        return {};
    auto processor = synth::AIStateMapper::createModule(typeName);
    if (processor == nullptr)
        return {};
    applyDefaultDualIOForNewModule(*processor, typeName);
    const auto* macro = macros.findByMember(synth::AIStateMapper::ensureNodeUuid(target));
    const juce::String macroId = macro != nullptr ? macro->id : juce::String();

    NodeID attenuverter;
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(processor));
    auto mutation = [this, &graph, &attenuverter, proc, typeName, sourceChannel, targetId, destChannel, depth,
                     macroId] {
        juce::ScopedValueSetter<juce::String> joinScope(macroDragJoinId_, macroId);
        auto node = graph.addNode(std::move(*proc));
        if (node == nullptr)
            return;
        const auto sourceId = node->nodeID;
        const auto uuid = synth::AIStateMapper::ensureNodeUuid(node.get());
        placeNewModulator(*node, targetId, typeName);
        attenuverter = connectModulationSource(sourceId, sourceChannel, targetId, destChannel, depth, false);
        if (macroId.isNotEmpty())
            macroController_.addSelectionToMacro(macroId, {uuid}, /*recordUndo=*/false);
        else
            macroController_.makeRoomFor("n:" + juce::String((juce::int64)sourceId.uid));
        reflowOutputDock();
        updateComponents();
    };
    if (undoManager != nullptr)
        undoManager->recordGraphAndMacroChange(graph, macros, mutation);
    else
        mutation();
    repaintCanvas();
    return attenuverter;
}

// Two passes, like a library drop: an estimated size places the node before its card exists, then the
// real card's size re-resolves it. Written straight to the final spot (no landing tween): nothing was
// dragged, and the position must be final before the cable and the macro join read it.
void GraphEditor::placeNewModulator(juce::AudioProcessorGraph::Node& node, NodeID targetId,
                                    const juce::String& typeName) {
    const auto estimate = estimateModuleSize(typeName);
    auto* targetComp = moduleComponentFor(targetId);
    const auto desired =
        besideTarget(targetComp != nullptr ? targetComp->getBounds() : juce::Rectangle<int>{}, estimate);
    const auto initial = resolvePlacement(desired, estimate.x, estimate.y, NodeID{});
    node.properties.set("x", initial.x);
    node.properties.set("y", initial.y);
    updateComponents();
    if (auto* comp = moduleComponentFor(node.nodeID)) {
        comp->setTopLeftPosition(
            resolvePlacement(comp->getPosition(), comp->getWidth(), comp->getHeight(), node.nodeID));
        updateModulePosition(comp);
    }
}

// One undo step. An attenuverter chain goes as a whole (hidden node and both edges, what deleting the
// cable on the canvas does); a direct or poly cable loses its edges into the target. A macro port the
// cable crossed (on either side of the attenuverter, however many macros deep) is swept when nothing is left
// on one side of it. The source goes too when asked and it drives
// nothing else any more -- through the single-node removal path, so every pre-removal unbind runs.
// `recordUndo` false leaves the undo step to the caller (the timeline's Remove modulator, which takes the
// LFO's sections lane in the same step).
void GraphEditor::removeModulator(const ModulationRouting& routing, bool removeLonelySource, bool recordUndo) {
    auto& graph = audioEngine.getGraph();
    const auto cablesBefore = snapshotCablesForRetract();
    auto mutation = [this, &graph, routing, removeLonelySource] {
        // The chain is read before anything is cut: once the attenuverter is gone there is no edge to follow.
        const auto isPort = [this](NodeID id) { return macroController_.nodeIsMacroPort(id); };
        const auto chain = synth::ui::resolveRouting(graph, routing, isPort);
        if (routing.kind == AudioEngine::RoutingKind::AttenuverterChain) {
            audioEngine.removeModRouting(routing.attenuverterNodeID);
        } else {
            for (const auto& c : graph.getConnections())
                if (c.source.nodeID == routing.sourceNodeID && c.destination.nodeID == routing.destNodeID &&
                    c.destination.channelIndex >= routing.destChannelIndex &&
                    c.destination.channelIndex < routing.destChannelIndex + std::max(1, routing.voiceCount))
                    graph.removeConnection(c);
        }
        // Every port the cable crossed that now has nothing on one side goes, on both sides of the routing
        // and across nested macros, whatever the auto-delete preference says: the removal is the request.
        macroController_.sweepOneSidedMacroPorts(chain.ports, /*ignorePreference=*/true);
        // After the sweep, not before: the LFO's edge into a port counts as outgoing until the port is gone.
        if (removeLonelySource && chain.source.valid() && graph.getNodeForId(chain.source.node) != nullptr &&
            !hasOutgoingConnection(graph, chain.source.node))
            requestDeleteModule(chain.source.node, /*recordUndo=*/false); // ends in updateComponents()
        else
            updateComponents();
    };
    if (recordUndo && undoManager != nullptr)
        undoManager->recordGraphAndMacroChange(graph, macros, mutation);
    else
        mutation();
    repaintCanvas();
    retractCablesGoneSince(cablesBefore);
}

// Every canvas removal of a modulation goes through here (or reads the ports first): the chain is cut and the
// ports it crossed are swept in one step, so no inlet or outlet is left holding a cable that drives nothing.
// A chain no routing lists (a dangling end) is just cut.
void GraphEditor::removeModulationChain(NodeID attenId) {
    for (const auto& r : audioEngine.getModulationRoutings())
        if (r.kind == AudioEngine::RoutingKind::AttenuverterChain && r.attenuverterNodeID == attenId) {
            removeModulator(r, /*removeLonelySource=*/false);
            return;
        }
    auto cut = [this, attenId] { audioEngine.removeModRouting(attenId); };
    if (undoManager != nullptr)
        undoManager->recordStructuralChange(audioEngine.getGraph(), cut);
    else
        cut();
    repaintCanvas();
}

// A port the cut left with nothing on its outer side is dead weight: an inlet nothing feeds still carries its inside
// leg into the member, and when that leg is a modulation (inlet -> attenuverter -> knob) the knob stays modulated by
// silence. A plain one-sided port follows the auto-delete preference; a modulation hanging off one is cut whatever
// the preference says, because the cable that fed it was the one the person removed, and its ports go with it.
void GraphEditor::pruneMacroPortsAfterCut(const std::vector<NodeID>& touched, std::vector<NodeID> chainPorts) {
    auto& graph = audioEngine.getGraph();
    for (const auto id : touched)
        macroController_.autoDeleteOrphanedMacroPort(id);

    std::vector<NodeID> oneSided;
    for (const auto id : touched) {
        if (!macroController_.nodeIsMacroPort(id))
            continue;
        bool hasIn = false, hasOut = false;
        std::vector<NodeID> attenuverters;
        for (const auto& c : graph.getConnections()) {
            const bool in = c.destination.nodeID == id, out = c.source.nodeID == id;
            if (!in && !out)
                continue;
            hasIn = hasIn || in;
            hasOut = hasOut || out;
            const auto other = in ? c.source.nodeID : c.destination.nodeID;
            if (auto* n = graph.getNodeForId(other);
                n != nullptr && dynamic_cast<AttenuverterModule*>(n->getProcessor()))
                attenuverters.push_back(other);
        }
        // Only the side facing out of the macro may be the empty one: an inlet with nothing feeding it, an outlet
        // feeding nothing. A port wired outside but not yet inside is waiting to be patched, and stays.
        const auto uuid = macroController_.nodeUuidFor(id);
        const auto* macro = macros.findByMember(uuid);
        const auto port = macro != nullptr ? std::find_if(macro->ports.begin(), macro->ports.end(),
                                                          [&](const synth::MacroPort& p) { return p.nodeUuid == uuid; })
                                           : std::vector<synth::MacroPort>::const_iterator();
        if (macro == nullptr || port == macro->ports.end() || (port->isInput ? hasIn || !hasOut : hasOut || !hasIn))
            continue;
        if (attenuverters.empty()) {
            oneSided.push_back(id);
            continue;
        }
        for (const auto atten : attenuverters) {
            const auto ports = modulationChainPorts(atten);
            chainPorts.insert(chainPorts.end(), ports.begin(), ports.end());
            audioEngine.removeModRouting(atten);
        }
        chainPorts.push_back(id);
    }
    macroController_.sweepOneSidedMacroPorts(chainPorts, /*ignorePreference=*/true);
    macroController_.sweepOneSidedMacroPorts(oneSided, /*ignorePreference=*/false);
}

std::vector<NodeID> GraphEditor::modulationChainPorts(NodeID attenId) {
    const auto isPort = [this](NodeID id) { return macroController_.nodeIsMacroPort(id); };
    for (const auto& r : audioEngine.getModulationRoutings())
        if (r.kind == AudioEngine::RoutingKind::AttenuverterChain && r.attenuverterNodeID == attenId)
            return synth::ui::resolveRouting(audioEngine.getGraph(), r, isPort).ports;
    return {};
}

// Resolved exactly as the canvas resolves a mod wire from that source (mode, user overrides, theme), so
// a modulator row is the colour of the cable it stands for.
juce::Colour GraphEditor::modulationWireColour(NodeID sourceId) const {
    VisibleCable cable;
    cable.signal = synth::ui::CableSignal::ModCV;
    cable.sourceCategory = detail::categoryForNode(audioEngine.getGraph().getNodeForId(sourceId));
    return colourForCable(cable);
}
