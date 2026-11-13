// AIStateMapper — merge-mode MIDI auto-connect.
//
// A new note-driven node (an instrument, or an LFO/ADSR that retriggers on notes) with no MIDI input is cabled
// from the MIDI source of the track it belongs to. Which track that is comes from the cables: a node belongs to
// the track whose Track In reaches what the node feeds or modulates. Cabling every new node from the first MIDI
// source would make two tracks reach the same modules, and synth::resolveTrackOwners then leaves their automation
// lanes unassigned.

#include "AIStateMapperInternal.h"
#include "AudioEngine/ConnectionIndex.h"
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace synth {
namespace detail {

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using NodeSet = std::set<NodeID>;

// Every node reachable from `start` along cables of either kind (MIDI included), `start` itself too.
NodeSet reachFrom(const ConnectionIndex& cables, NodeID start) {
    NodeSet seen{start};
    std::vector<NodeID> stack{start};
    while (!stack.empty()) {
        const auto node = stack.back();
        stack.pop_back();
        for (const auto& conn : cables.outOf(node))
            if (seen.insert(conn.destination.nodeID).second)
                stack.push_back(conn.destination.nodeID);
    }
    return seen;
}

bool hasMidiInput(const ConnectionIndex& cables, NodeID node) {
    for (const auto& conn : cables.into(node))
        if (conn.destination.isMIDI())
            return true;
    return false;
}

// The one MIDI source whose track `node` belongs to: the source whose reach holds something `node` reaches that no
// other source reaches (the shared Audio Output and any bus are reached by every track, so they decide nothing).
std::optional<NodeID> owningSource(const ConnectionIndex& cables, const std::map<NodeID, NodeSet>& reachOfSource,
                                   NodeID node) {
    std::map<NodeID, int> reachedByCount;
    for (const auto& entry : reachOfSource)
        for (const auto& reached : entry.second)
            ++reachedByCount[reached];

    std::optional<NodeID> owner;
    for (const auto& reached : reachFrom(cables, node)) {
        if (reached == node || reachedByCount[reached] != 1)
            continue;
        for (const auto& [source, reach] : reachOfSource) {
            if (reach.count(reached) == 0)
                continue;
            if (owner.has_value() && *owner != source)
                return std::nullopt; // it feeds two tracks: no single owner
            owner = source;
        }
    }
    return owner;
}

} // namespace

void autoConnectNewNodesToMidi(juce::AudioProcessorGraph& graph, const ConnectionIndex& cables,
                               const std::set<juce::AudioProcessorGraph::NodeID>& newlyCreatedNodes) {
    // Sampler is here because a note-on retriggers it and transposes it against rootNote, the same reason Oscillator
    // is. ADSR is here because without notes it never opens: an envelope a model adds as a modulation source (e.g.
    // into a Custom filter's Morph X) would stay at zero. LFO is here so Retrig (and the Fade In it restarts) sees
    // the notes; with Retrig off it ignores MIDI.
    static const std::set<juce::String> midiAcceptingTypes = {"Oscillator", "Sampler",   "Drum Kit",       "Pluck",
                                                              "Mallet",     "Sequencer", "Poly Sequencer", "Poly MIDI",
                                                              "ADSR",       "LFO"};
    static const std::set<juce::String> modulatorTypes = {"ADSR", "LFO"};

    std::map<NodeID, NodeSet> reachOfSource; // existing MIDI sources only, never a node this patch created
    for (const auto& conn : cables.all())
        if (conn.source.isMIDI() && newlyCreatedNodes.count(conn.source.nodeID) == 0)
            reachOfSource.try_emplace(conn.source.nodeID, reachFrom(cables, conn.source.nodeID));
    if (reachOfSource.empty())
        return;

    for (auto newNodeId : newlyCreatedNodes) {
        auto* node = graph.getNodeForId(newNodeId);
        if (node == nullptr || !node->getProcessor()->acceptsMidi() || hasMidiInput(cables, newNodeId))
            continue;
        const juce::String typeName = node->getProcessor()->getName();
        if (midiAcceptingTypes.count(typeName) == 0)
            continue;

        std::optional<NodeID> source = owningSource(cables, reachOfSource, newNodeId);
        if (!source.has_value() && reachOfSource.size() == 1)
            source = reachOfSource.begin()->first; // one track: no other owner to confuse it with
        if (!source.has_value() && modulatorTypes.count(typeName) == 0)
            source = reachOfSource.begin()->first; // an instrument that feeds nothing yet still has to play
        if (source.has_value())
            graph.addConnection({{*source, juce::AudioProcessorGraph::midiChannelIndex},
                                 {newNodeId, juce::AudioProcessorGraph::midiChannelIndex}},
                                juce::AudioProcessorGraph::UpdateKind::none); // the caller rebuilds once
    }
}

} // namespace detail
} // namespace synth
