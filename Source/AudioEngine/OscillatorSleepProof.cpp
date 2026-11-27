// OscillatorSleepProof.cpp -- the message-thread half of an Oscillator's sleep: walks the cables out of each
// Oscillator and proves which nodes decide whether it is heard. The render half: OscillatorModuleSleep.cpp.
// docs/architecture/audio-engine.md#oscillator-sleep.
#include "OscillatorSleepProof.h"

#include "ConnectionIndex.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <set>
#include <vector>

namespace {

using Graph = juce::AudioProcessorGraph;
using NodeID = Graph::NodeID;

constexpr int kMaxDepth = 16;    // a modulation chain longer than this is not proven
constexpr int kEnvelopeOuts = 8; // an ADSR's per-voice envelope outputs; an idle voice's is 0

// Collects the cone while proving it. Each proven node is kept alive by the cone, so the render can read it after the
// graph dropped it, until the next proof replaces the cone.
struct ConeBuilder {
    const Graph& graph;
    const synth::ConnectionIndex& cables;
    std::set<juce::uint32> midiSources; // the Oscillator's MIDI sources
    synth::OscillatorSleepCone cone;
    std::set<juce::uint32> kept;

    void keep(Graph::Node* node) {
        if (kept.insert(node->nodeID.uid).second)
            cone.keepAlive.push_back(node);
    }

    template <class T>
    static void addOnce(std::vector<T*>& list, T* item) {
        if (std::find(list.begin(), list.end(), item) == list.end())
            list.push_back(item);
    }

    // Every audio cable into `node` on `channel` (any channel when below 0) carries zeros while the cone rests.
    bool inputsZero(NodeID node, int channel, int depth) {
        for (const auto& cable : cables.into(node)) {
            if (cable.destination.isMIDI() || (channel >= 0 && cable.destination.channelIndex != channel))
                continue;
            if (!outputZero(cable.source, depth + 1))
                return false;
        }
        return true;
    }

    // An envelope with no gate cable and notes only from the Oscillator's own MIDI sources is silent whenever the
    // Oscillator gets no MIDI and the envelope rested (atRest). An Attenuverter scales zeros to zeros while its curve
    // is linear and its Amount is not a NaN, so both its inputs must be proven too. A Macro In/Out copies a channel.
    bool outputZero(Graph::NodeAndChannel source, int depth) {
        auto* node = depth <= kMaxDepth && !source.isMIDI() ? graph.getNodeForId(source.nodeID) : nullptr;
        if (node == nullptr)
            return false;
        auto* processor = node->getProcessor();
        if (auto* envelope = dynamic_cast<ADSRModule*>(processor)) {
            if (source.channelIndex >= kEnvelopeOuts)
                return false;
            for (const auto& cable : cables.into(source.nodeID)) {
                if (cable.destination.isMIDI() ? midiSources.count(cable.source.nodeID.uid) == 0
                                               : cable.destination.channelIndex < kEnvelopeOuts)
                    return false;
            }
            addOnce(cone.envelopes, envelope);
        } else if (auto* attenuverter = dynamic_cast<AttenuverterModule*>(processor)) {
            if (source.channelIndex != 0 || !inputsZero(source.nodeID, -1, depth))
                return false;
            addOnce(cone.attenuverters, attenuverter);
        } else if (auto* module = dynamic_cast<ModuleBase*>(processor);
                   module != nullptr && (module->getModuleType() == ModuleType::MacroInlet ||
                                         module->getModuleType() == ModuleType::MacroOutlet)) {
            if (!inputsZero(source.nodeID, source.channelIndex, depth))
                return false;
        } else {
            return false;
        }
        keep(node);
        return true;
    }

    // A latency anywhere upstream makes the graph delay the VCA's other inputs, so its gain CV this block would be the
    // envelope of an earlier one; a cable loop back into the VCA has no well-defined block either.
    bool upstreamHasNoDelay(NodeID vca) const {
        std::vector<NodeID> todo{vca};
        std::set<juce::uint32> seen{vca.uid};
        while (!todo.empty()) {
            const NodeID at = todo.back();
            todo.pop_back();
            for (const auto& cable : cables.into(at)) {
                const auto from = cable.source.nodeID;
                if (from == vca)
                    return false;
                if (!seen.insert(from.uid).second)
                    continue;
                auto* node = graph.getNodeForId(from);
                if (node == nullptr || node->getProcessor()->getLatencySamples() != 0)
                    return false;
                todo.push_back(from);
            }
        }
        return true;
    }

    // Every cable out of the Oscillator is its Audio L or R into a VCA's audio input, whose gain CV (ch1, and ch8 the
    // VCA falls back to) is proven zero; no audio cable enters the Oscillator, so its CV and Gate are zeros.
    bool prove(Graph::Node& oscillator) {
        for (const auto& cable : cables.into(oscillator.nodeID)) {
            if (!cable.destination.isMIDI())
                return false;
            midiSources.insert(cable.source.nodeID.uid);
        }
        for (const auto& cable : cables.outOf(oscillator.nodeID)) {
            const int from = cable.source.channelIndex;
            const int to = cable.destination.channelIndex;
            auto* node = graph.getNodeForId(cable.destination.nodeID);
            auto* vca = node != nullptr ? dynamic_cast<VCAModule*>(node->getProcessor()) : nullptr;
            if (cable.source.isMIDI() || (from != 0 && from != OscillatorModule::kRightBase) || vca == nullptr ||
                (to != 0 && to != VCAModule::kRightBase))
                return false;
            if (std::find(cone.vcas.begin(), cone.vcas.end(), vca) != cone.vcas.end())
                continue;
            if (!inputsZero(node->nodeID, 1, 0) || !inputsZero(node->nodeID, VCAModule::kPolyCVBase, 0) ||
                !upstreamHasNoDelay(node->nodeID))
                return false;
            cone.vcas.push_back(vca);
            keep(node);
        }
        return !cone.vcas.empty() && oscillator.getProcessor()->getLatencySamples() == 0;
    }
};

} // namespace

std::unique_ptr<synth::OscillatorSleepCone>
synth::buildOscillatorSleepCone(const juce::AudioProcessorGraph& graph, const ConnectionIndex& cables,
                                juce::AudioProcessorGraph::NodeID oscillator) {
    auto* node = graph.getNodeForId(oscillator);
    if (node == nullptr || dynamic_cast<OscillatorModule*>(node->getProcessor()) == nullptr)
        return std::make_unique<OscillatorSleepCone>();
    ConeBuilder builder{graph, cables, {}, {}, {}};
    if (!builder.prove(*node))
        return std::make_unique<OscillatorSleepCone>();
    return std::make_unique<OscillatorSleepCone>(std::move(builder.cone));
}

void synth::publishOscillatorSleepCones(juce::AudioProcessorGraph& graph) {
    const ConnectionIndex cables(graph);
    for (auto* node : graph.getNodes())
        if (auto* oscillator = dynamic_cast<OscillatorModule*>(node->getProcessor()))
            oscillator->publishSleepCone(buildOscillatorSleepCone(graph, cables, node->nodeID));
}

void synth::collectOscillatorSleepCones(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* oscillator = dynamic_cast<OscillatorModule*>(node->getProcessor()))
            oscillator->collectSleepCones();
}
