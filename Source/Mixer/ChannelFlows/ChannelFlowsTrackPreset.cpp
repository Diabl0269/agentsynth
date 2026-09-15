// Concern: FRO13 (P9-7, docs/mixer.md §5.7) -- the track-preset-specific queries:
// collectOutsideModulatorsForTrackPreset (the transitive upstream walk that finds every module
// outside a channel macro that feeds it through a port, so saving a track also captures a shared
// LFO) and isChannelMacro (the "is this macro a mixer channel" predicate both the walk's own stop
// rule and the macro's right-click menu gate on).
#include "ChannelFlows.h"

#include "ChannelFlowsInternal.h"
#include "MacroSet.h"
#include <algorithm>

namespace synth {

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

bool containsId(const std::vector<NodeID>& ids, NodeID id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void addUniqueId(std::vector<NodeID>& ids, NodeID id) {
    if (!containsId(ids, id))
        ids.push_back(id);
}

// Resolves a macro's uuid-keyed membership to live NodeIDs, dropping any member that no longer
// resolves (the same "skip what's gone" posture reachFrom/absorbSideInputs take elsewhere).
std::vector<NodeID> resolveMembers(juce::AudioProcessorGraph& graph, const Macro& macro) {
    std::vector<NodeID> members;
    for (auto* node : graph.getNodes()) {
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty() && macro.hasMember(uuid))
            addUniqueId(members, node->nodeID);
    }
    return members;
}

} // namespace

bool isChannelMacro(const Macro& macro, juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes()) {
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty() && macro.hasMember(uuid) && isStrip(node->getProcessor()))
            return true;
    }
    return false;
}

std::vector<NodeID> collectOutsideModulatorsForTrackPreset(juce::AudioProcessorGraph& graph, const MacroSet& macros,
                                                           const juce::String& channelMacroId) {
    std::vector<NodeID> result;
    const auto* macro = macros.find(channelMacroId);
    if (macro == nullptr)
        return result;

    const auto members = resolveMembers(graph, *macro);
    if (members.empty())
        return result;

    const auto connections = graph.getConnections();

    // Step 1 (seed set): every connection landing on a member whose SOURCE is not itself a
    // member — see the class comment for why this scans every member's incoming edges rather
    // than only the macro's own ports.
    std::vector<NodeID> frontier;
    for (const auto& c : connections)
        if (containsId(members, c.destination.nodeID) && !containsId(members, c.source.nodeID))
            addUniqueId(frontier, c.source.nodeID);

    // Steps 2-4: walk upstream to a fixpoint, entering (but never adding) an Attenuverter, and
    // stopping at another channel's strip or at a member of a different channel macro.
    std::vector<NodeID> visited;
    for (size_t i = 0; i < frontier.size(); ++i) {
        const auto id = frontier[i];
        if (containsId(members, id) || containsId(visited, id))
            continue;

        auto* processor = processorFor(graph, id);
        if (processor == nullptr)
            continue;

        if (isStrip(processor))
            continue; // another channel's own strip: that channel's business, not this preset's

        auto* node = graph.getNodeForId(id);
        const juce::String uuid = node != nullptr ? node->properties["uuid"].toString() : juce::String();
        if (uuid.isNotEmpty()) {
            if (const auto* otherMacro = macros.findByMember(uuid))
                if (otherMacro->id != channelMacroId && isChannelMacro(*otherMacro, graph))
                    continue; // a member of a DIFFERENT channel macro: also that channel's business
        }

        const bool isAtten = isAttenuverter(processor);
        if (!isAtten)
            addUniqueId(visited, id);

        for (const auto& c : connections)
            if (c.destination.nodeID == id)
                addUniqueId(frontier, c.source.nodeID);
    }

    result = visited;
    return result;
}

} // namespace synth
