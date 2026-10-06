#pragma once

#include "AudioEngine/ModulationRoutingTypes.h"
#include "Modules/ModuleBase.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <optional>
#include <unordered_map>
#include <vector>

namespace synth::ui {

/** One Mod Matrix source entry: the raw output channel a routing reads, and the jack name shown
 *  after the module's title (empty when the module offers a single entry). */
struct ModSourceOutput {
    int channel = 0;
    juce::String label;
};

/** The sources a module offers the Mod Matrix: one per visible output jack, the ones its card draws.
 *  A raw channel that is no jack (an LFO's silent pass-throughs) is no source either. A jack that
 *  fronts two poly heads (Poly MIDI's Pitch and Gate) lists each, named by role. See
 *  docs/modules/modulation.md#smart-cables-poly-bus-wires-and-the-mod-matrix. */
std::vector<ModSourceOutput> modSourceOutputs(const ModuleBase& module);

/** The destinations a module offers the Mod Matrix: its modulation targets, plus channel 0 of a
 *  Macro In port spliced into an existing routing (display only, see the definition). */
std::vector<ModulationTarget> modDestinationCandidates(ModuleBase* module);

/** One end of a routing: a node and the channel the attenuverter's edge lands on or leaves from. */
struct RoutingEndpoint {
    juce::AudioProcessorGraph::NodeID node;
    int channel = 0;
    bool valid() const noexcept { return node.uid != 0; }
};

/** Every cable of a graph, indexed once by the node it enters and the node it leaves, in getConnections() order.
 *  getConnections() copies and sorts the whole cable set on every call, so a walk over many routings builds one
 *  index and asks it instead (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch). */
class ConnectionIndex {
public:
    using Connection = juce::AudioProcessorGraph::Connection;
    ConnectionIndex() = default; // no cables
    explicit ConnectionIndex(const juce::AudioProcessorGraph& graph);
    const std::vector<Connection>& into(juce::AudioProcessorGraph::NodeID node) const;
    const std::vector<Connection>& outOf(juce::AudioProcessorGraph::NodeID node) const;
    /** Every cable into or out of `node` (a self-cable once), in getConnections() order. */
    const std::vector<Connection>& touching(juce::AudioProcessorGraph::NodeID node) const;

private:
    std::unordered_map<juce::uint32, std::vector<Connection>> in_, out_, touching_;
};
/** The attenuverter's channel-0 edge in (`incoming`) or out, if it has one. */
std::optional<juce::AudioProcessorGraph::Connection>
attenuverterEdge(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID atten, bool incoming);
std::optional<juce::AudioProcessorGraph::Connection>
attenuverterEdge(const ConnectionIndex& cables, juce::AudioProcessorGraph::NodeID atten, bool incoming);

/** Appends every edge landing on `node` to `in` and every edge leaving it to `out`. */
void edgesAround(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                 std::vector<juce::AudioProcessorGraph::Connection>& in,
                 std::vector<juce::AudioProcessorGraph::Connection>& out);

/** The real module at one end of an attenuverter's routing, looking through macro ports (`isPort`).
 *  Invalid when the attenuverter has no edge on that side. Message thread only.
 *  `passedPorts`, when given, receives every port the walk went through. By default a port is passed
 *  only when this routing alone uses it (one edge in, one out); `allowFanOut` also passes a port the
 *  routing shares on the far side of the walk (an inlet feeding several attenuverters while the source
 *  side stays unambiguous, an outlet fed by several sources while the destination does). */
RoutingEndpoint realEndpointBehindPorts(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID atten,
                                        bool incoming,
                                        const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort,
                                        std::vector<juce::AudioProcessorGraph::NodeID>* passedPorts = nullptr,
                                        bool allowFanOut = false);
RoutingEndpoint realEndpointBehindPorts(const ConnectionIndex& cables, juce::AudioProcessorGraph::NodeID atten,
                                        bool incoming,
                                        const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort,
                                        std::vector<juce::AudioProcessorGraph::NodeID>* passedPorts = nullptr,
                                        bool allowFanOut = false);

/** A modulation routing with the macro ports between its modulator and its parameter looked through:
 *  the real source and destination, and every port on the chain (the ones a removal may leave empty).
 *  A direct cable has no attenuverter to walk from, so its own endpoints stand (and are the ports,
 *  when they are ones). */
struct ResolvedRouting {
    RoutingEndpoint source;
    RoutingEndpoint dest;
    std::vector<juce::AudioProcessorGraph::NodeID> ports;
};
ResolvedRouting resolveRouting(juce::AudioProcessorGraph& graph, const ModulationRouting& routing,
                               const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort);
/** The same, against an index built once for a walk over many routings. */
ResolvedRouting resolveRouting(const ConnectionIndex& cables, const ModulationRouting& routing,
                               const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort);

} // namespace synth::ui
