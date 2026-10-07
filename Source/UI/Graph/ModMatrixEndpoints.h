#pragma once

#include "AudioEngine/ConnectionIndex.h"
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

/** The Core cable index (synth::ConnectionIndex) that also counts itself as one cable scan in the canvas's work
 *  counters (graph_editor_paint::workCounters), so a test can hold a tick to one index. */
class ConnectionIndex : public synth::ConnectionIndex {
public:
    ConnectionIndex() = default; // no cables
    explicit ConnectionIndex(const juce::AudioProcessorGraph& graph);
};
/** The attenuverter's channel-0 edge in (`incoming`) or out, if it has one. */
std::optional<juce::AudioProcessorGraph::Connection>
attenuverterEdge(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID atten, bool incoming);
std::optional<juce::AudioProcessorGraph::Connection>
attenuverterEdge(const synth::ConnectionIndex& cables, juce::AudioProcessorGraph::NodeID atten, bool incoming);

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
RoutingEndpoint realEndpointBehindPorts(const synth::ConnectionIndex& cables, juce::AudioProcessorGraph::NodeID atten,
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
ResolvedRouting resolveRouting(const synth::ConnectionIndex& cables, const ModulationRouting& routing,
                               const std::function<bool(juce::AudioProcessorGraph::NodeID)>& isPort);

} // namespace synth::ui
