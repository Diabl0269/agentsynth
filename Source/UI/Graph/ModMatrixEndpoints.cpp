// ModMatrixEndpoints.cpp -- what the Mod Matrix's source and destination lists offer per module.
#include "ModMatrixEndpoints.h"

#include "Modules/MacroInletModule.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include <algorithm>

namespace synth::ui {

namespace {
juce::String roleLabel(PortRole role) {
    switch (role) {
    case PortRole::Pitch:
        return "Pitch";
    case PortRole::Gate:
        return "Gate";
    case PortRole::ModCV:
        return "CV";
    case PortRole::Midi:
        return "MIDI";
    case PortRole::Sidechain:
        return "Sidechain";
    case PortRole::Audio:
        return "Audio";
    default:
        return {};
    }
}
} // namespace

std::vector<ModSourceOutput> modSourceOutputs(const ModuleBase& module) {
    std::vector<ModSourceOutput> out;
    const int numRaw = module.getTotalNumOutputChannels();
    for (int jack = 0; jack < module.getVisibleOutputPortCount(); ++jack) {
        const auto targets = module.getJackTargets(jack, /*isInput=*/false);
        for (const auto& t : targets) {
            if (t.rawHeadChannel < 0 || t.rawHeadChannel >= numRaw)
                continue;
            if (std::any_of(out.begin(), out.end(), [&](const auto& o) { return o.channel == t.rawHeadChannel; }))
                continue;
            auto label = module.getOutputPortLabel(jack);
            if (targets.size() > 1 && roleLabel(t.role).isNotEmpty())
                label += " " + roleLabel(t.role);
            out.push_back({t.rawHeadChannel, label});
        }
    }
    // A single entry reads as just the module's name.
    if (out.size() == 1)
        out.front().label = {};
    return out;
}

// MacroInletModule deliberately declares NO getModulationTargets() — GraphEditor::connectPorts()
// relies on that empty list to keep a plain cable drop onto a Macro In's jack a plain connection,
// never auto-wrapped in a fresh attenuverter (Tests/Macros/MacroPortFlowTests.cpp's drop-a-cable tests
// pin that). A modulation cable through an attenuverter
// (docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter) can still
// splice a MacroInletModule in as the DESTINATION of an EXISTING AttenuverterChain crossing a macro boundary, so a
// matrix row's destination combo needs something to show and match against — without changing what the module declares
// globally (which would resurrect the auto-wrap problem for ordinary drops). Display-only: this never becomes a real
// ModulationTarget the module advertises anywhere else. A spliced port is always Mono with its one active raw channel
// at 0 (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed /
// docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter — the internal jack an AttenuverterChain lands
// on is never poly-fanned), so channel 0 is the only candidate.
std::vector<ModulationTarget> modDestinationCandidates(ModuleBase* module) {
    auto targets = module->getModulationTargets();
    if (targets.empty() && dynamic_cast<MacroInletModule*>(module) != nullptr)
        targets.push_back({"In", 0});
    return targets;
}

ConnectionIndex::ConnectionIndex(const juce::AudioProcessorGraph& graph) {
    ++graph_editor_paint::workCounters().cableScans;
    for (const auto& c : graph.getConnections()) {
        in_[c.destination.nodeID.uid].push_back(c);
        out_[c.source.nodeID.uid].push_back(c);
        touching_[c.source.nodeID.uid].push_back(c);
        if (c.destination.nodeID != c.source.nodeID)
            touching_[c.destination.nodeID.uid].push_back(c);
    }
}

const std::vector<ConnectionIndex::Connection>&
ConnectionIndex::touching(juce::AudioProcessorGraph::NodeID node) const {
    static const std::vector<Connection> none;
    const auto it = touching_.find(node.uid);
    return it != touching_.end() ? it->second : none;
}

const std::vector<ConnectionIndex::Connection>& ConnectionIndex::into(juce::AudioProcessorGraph::NodeID node) const {
    static const std::vector<Connection> none;
    const auto it = in_.find(node.uid);
    return it != in_.end() ? it->second : none;
}

const std::vector<ConnectionIndex::Connection>& ConnectionIndex::outOf(juce::AudioProcessorGraph::NodeID node) const {
    static const std::vector<Connection> none;
    const auto it = out_.find(node.uid);
    return it != out_.end() ? it->second : none;
}

std::optional<juce::AudioProcessorGraph::Connection>
attenuverterEdge(const ConnectionIndex& cables, juce::AudioProcessorGraph::NodeID atten, bool incoming) {
    for (const auto& c : incoming ? cables.into(atten) : cables.outOf(atten))
        if ((incoming ? c.destination.channelIndex : c.source.channelIndex) == 0)
            return c;
    return std::nullopt;
}

std::optional<juce::AudioProcessorGraph::Connection>
attenuverterEdge(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID atten, bool incoming) {
    return attenuverterEdge(ConnectionIndex(graph), atten, incoming);
}

void edgesAround(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                 std::vector<juce::AudioProcessorGraph::Connection>& in,
                 std::vector<juce::AudioProcessorGraph::Connection>& out) {
    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID == node)
            in.push_back(c);
        if (c.source.nodeID == node)
            out.push_back(c);
    }
}

// The walk steps from the attenuverter's edge to the next port along the signal's direction away from it.
// Passing a port is safe when the walk's own side of it is unambiguous: one edge on the side being followed.
// Without `allowFanOut` the other side must be single too, so the port belongs to this routing alone and a
// Mod Matrix re-route may move it.
RoutingEndpoint realEndpointBehindPorts(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID atten,
                                        bool incoming,
                                        const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort,
                                        std::vector<juce::AudioProcessorGraph::NodeID>* passedPorts, bool allowFanOut) {
    return realEndpointBehindPorts(ConnectionIndex(graph), atten, incoming, isPort, passedPorts, allowFanOut);
}

RoutingEndpoint realEndpointBehindPorts(const ConnectionIndex& cables, juce::AudioProcessorGraph::NodeID atten,
                                        bool incoming,
                                        const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort,
                                        std::vector<juce::AudioProcessorGraph::NodeID>* passedPorts, bool allowFanOut) {
    auto edge = attenuverterEdge(cables, atten, incoming);
    if (!edge)
        return {};
    for (;;) {
        const auto far = incoming ? edge->source.nodeID : edge->destination.nodeID;
        if (!isPort(far))
            break;
        const auto& followed = incoming ? cables.into(far) : cables.outOf(far);
        const auto& other = incoming ? cables.outOf(far) : cables.into(far);
        if (followed.size() != 1 || (!allowFanOut && other.size() != 1))
            break; // shared with another routing: it stays where it is
        if (passedPorts != nullptr)
            passedPorts->push_back(far);
        edge = followed.front();
    }
    return incoming ? RoutingEndpoint{edge->source.nodeID, edge->source.channelIndex}
                    : RoutingEndpoint{edge->destination.nodeID, edge->destination.channelIndex};
}

ResolvedRouting resolveRouting(juce::AudioProcessorGraph& graph, const ModulationRouting& routing,
                               const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort) {
    if (routing.kind != ModulationRoutingKind::AttenuverterChain) // no walk: skip building the index
        return resolveRouting(ConnectionIndex(), routing, isPort);
    return resolveRouting(ConnectionIndex(graph), routing, isPort);
}

ResolvedRouting resolveRouting(const ConnectionIndex& cables, const ModulationRouting& routing,
                               const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort) {
    ResolvedRouting resolved;
    resolved.source = {routing.sourceNodeID, routing.sourceChannelIndex};
    resolved.dest = {routing.destNodeID, routing.destChannelIndex};
    if (routing.kind == ModulationRoutingKind::AttenuverterChain) {
        const auto atten = routing.attenuverterNodeID;
        if (const auto real = realEndpointBehindPorts(cables, atten, true, isPort, &resolved.ports, true); real.valid())
            resolved.source = real;
        if (const auto real = realEndpointBehindPorts(cables, atten, false, isPort, &resolved.ports, true);
            real.valid())
            resolved.dest = real;
        return resolved;
    }
    for (const auto id : {routing.sourceNodeID, routing.destNodeID})
        if (isPort(id))
            resolved.ports.push_back(id);
    return resolved;
}

} // namespace synth::ui
