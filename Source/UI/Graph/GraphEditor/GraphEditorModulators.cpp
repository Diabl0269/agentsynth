// GraphEditorModulators.cpp -- the canvas side of a timeline lane's modulators: which CV jack drives a
// parameter, adding an LFO card beside a module and cabling it into that jack, removing one routing (and
// an LFO left with nothing to drive), and the wire colour a modulator row shows. GraphEditor is declared
// in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

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
void settleNewRouting(AudioEngine& engine, NodeID lfoId, NodeID targetId, int raw) {
    for (const auto& r : engine.getModulationRoutings()) {
        if (r.kind != AudioEngine::RoutingKind::AttenuverterChain || r.sourceNodeID != lfoId ||
            r.destNodeID != targetId || r.destChannelIndex != raw)
            continue;
        auto* atten = engine.getGraph().getNodeForId(r.attenuverterNodeID);
        if (atten == nullptr)
            continue;
        synth::AIStateMapper::ensureNodeUuid(atten);
        if (auto* amount = findParameterByID(atten->getProcessor(), "amount"))
            amount->setValueNotifyingHost(amount->convertTo0to1(kDefaultModulatorDepth));
    }
}
} // namespace

// A module names each CV jack by the parameter it drives. A target declared without a paramId is
// matched by its name against the parameter's display name instead (the knob-binding fallback).
int GraphEditor::modulationChannelFor(NodeID nodeId, const juce::String& paramId) const {
    auto* node = audioEngine.getGraph().getNodeForId(nodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr || paramId.isEmpty())
        return -1;
    const auto* param = findParameterByID(module, paramId);
    const juce::String displayName = param != nullptr ? param->getName(64) : juce::String();
    for (const auto& target : module->getModulationTargets()) {
        if (target.paramId == paramId)
            return target.channelIndex;
        if (target.paramId.isEmpty() && displayName.isNotEmpty() && target.name == displayName)
            return target.channelIndex;
    }
    return -1;
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
        placeNewModulator(*node, targetId);

        auto* lfo = moduleOf(graph, lfoId);
        auto* dst = moduleOf(graph, targetId);
        if (lfo == nullptr || dst == nullptr)
            return;
        connectPorts(lfoId, lfo->mapOutputChannel(0).visibleJackIndex, targetId,
                     dst->mapInputChannel(raw).visibleJackIndex, /*isMidi=*/false, /*recordUndo=*/false);
        settleNewRouting(audioEngine, lfoId, targetId, raw);

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
    auto& graph = audioEngine.getGraph();
    auto* lfo = moduleOf(graph, lfoId);
    auto* dst = moduleOf(graph, targetId);
    const int raw = modulationChannelFor(targetId, paramId);
    if (lfo == nullptr || dst == nullptr || raw < 0 || dynamic_cast<LFOModule*>(lfo) == nullptr || lfoId == targetId)
        return false;
    const auto isPort = [this](NodeID id) { return macroController_.nodeIsMacroPort(id); };
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasSource || !r.hasDest)
            continue;
        const auto real = synth::ui::resolveRouting(graph, r, isPort);
        if (real.source.node == lfoId && real.dest.node == targetId && real.dest.channel == raw)
            return false;
    }

    auto mutation = [this, &graph, lfo, dst, lfoId, targetId, raw] {
        connectPorts(lfoId, lfo->mapOutputChannel(0).visibleJackIndex, targetId,
                     dst->mapInputChannel(raw).visibleJackIndex, /*isMidi=*/false, /*recordUndo=*/false);
        settleNewRouting(audioEngine, lfoId, targetId, raw);
        return true;
    };
    auto step = [this, &mutation] {
        macroController_.applyProgrammaticConnectionChange(autoCreateMacroPortsOnDragEnabled, mutation);
        updateComponents();
    };
    if (undoManager != nullptr)
        undoManager->recordGraphAndMacroChange(graph, macros, step);
    else
        step();
    repaintCanvas();
    return true;
}

// Two passes, like a library drop: an estimated size places the node before its card exists, then the
// real card's size re-resolves it. Written straight to the final spot (no landing tween): nothing was
// dragged, and the position must be final before the cable and the macro join read it.
void GraphEditor::placeNewModulator(juce::AudioProcessorGraph::Node& node, NodeID targetId) {
    const auto estimate = estimateModuleSize("LFO");
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
}

// Resolved exactly as the canvas resolves a mod wire from that source (mode, user overrides, theme), so
// a modulator row is the colour of the cable it stands for.
juce::Colour GraphEditor::modulationWireColour(NodeID sourceId) const {
    VisibleCable cable;
    cable.signal = synth::ui::CableSignal::ModCV;
    cable.sourceCategory = detail::categoryForNode(audioEngine.getGraph().getNodeForId(sourceId));
    return colourForCable(cable);
}
