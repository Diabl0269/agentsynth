// MacroGroupControllerProgrammaticRouting.cpp
//
// Concern: auto macro ports for connections made by code rather than by a hand-drawn cable (a
// mixer send today). applyProgrammaticConnectionChange diffs the graph around a caller's mutation,
// routes every ADDED connection that crosses a macro boundary through a fresh port (the same rule a
// dragged cable follows), and splices out any port a REMOVED connection left carrying nothing
// (docs/macros/auto-ports.md#programmatic-connections).

#include "MacroGroupController.h"

#include "Modules/ModuleBase.h"
#include <algorithm>
#include <set>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;
using Group = MacroGroupController::MacroPortCrossingGroup;

std::set<Connection> connectionSet(juce::AudioProcessorGraph& graph) {
    const auto all = graph.getConnections();
    return {all.begin(), all.end()};
}

std::vector<Connection> minus(const std::set<Connection>& a, const std::set<Connection>& b) {
    std::vector<Connection> out;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

/** The raw graph edge a crossing-plan edge stands for, rebuilt from the group it sits in. */
Connection edgeConnection(const Group& group, const MacroGroupController::MacroPortCrossingEdge& edge) {
    const int midi = juce::AudioProcessorGraph::midiChannelIndex;
    const int external = group.isMidi ? midi : edge.externalRawChannel;
    const int internal = group.isMidi ? midi : edge.internalRawChannel;
    return group.isInput ? Connection{{edge.externalNodeId, external}, {group.internalNodeId, internal}}
                         : Connection{{group.internalNodeId, internal}, {edge.externalNodeId, external}};
}

/** True when `raw` is a channel no visible jack shows (a Mono Channel Strip's unused right input).
 *  The module never reads it, and a hand-drawn cable into a Mono strip wires only its one jack, so
 *  such a leg is dropped rather than ported -- the routed result matches what a drag would build. */
bool isHiddenRawChannel(juce::AudioProcessorGraph& graph, const Group& group, int raw) {
    if (group.isMidi || raw == group.headRawChannel)
        return false;
    auto* node = graph.getNodeForId(group.internalNodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr)
        return false;
    const auto port = group.isInput ? module->mapInputChannel(raw) : module->mapOutputChannel(raw);
    return port.role == PortRole::Other && !port.isPolyGroupHead && raw - group.headRawChannel >= group.voiceCount;
}

/** Folds a programmatic stereo pair that landed on two SEPARATE Mono jacks of one module (a
 *  Channel Strip's L and R inputs, or a send's L and R outputs) into one two-jack Stereo group, so
 *  the pair enters through ONE port rather than two. The grouping-time merge pass only does this for
 *  Dual-I/O modules; a strip has no Dual-I/O switch but the same split-jack layout. Pairs only when
 *  both legs come from the same outside node, one edge each, lower raw channel as the left leg. */
void mergeSplitStereoPairs(std::vector<Group>& groups) {
    for (size_t i = 0; i < groups.size(); ++i) {
        for (size_t j = i + 1; j < groups.size(); ++j) {
            const auto& a = groups[i];
            const auto& b = groups[j];
            const bool pair = !a.isMidi && !b.isMidi && a.shape == MacroPortShape::Mono &&
                              b.shape == MacroPortShape::Mono && a.internalNodeId == b.internalNodeId &&
                              a.isInput == b.isInput && a.edges.size() == 1 && b.edges.size() == 1 &&
                              a.edges[0].externalNodeId == b.edges[0].externalNodeId;
            if (!pair)
                continue;
            const size_t leftIndex = a.headRawChannel <= b.headRawChannel ? i : j;
            const size_t rightIndex = leftIndex == i ? j : i;
            auto rightEdge = groups[rightIndex].edges[0];
            auto& left = groups[leftIndex];
            left.shape = MacroPortShape::Stereo;
            left.voiceCount = 1;
            left.edges[0].legIndex = 0;
            rightEdge.legIndex = 1;
            left.edges.push_back(rightEdge);
            groups.erase(groups.begin() + (long)rightIndex);
            mergeSplitStereoPairs(groups); // indices shifted -- start over; each call removes one group
            return;
        }
    }
}
} // namespace

// Diff-based on purpose: a caller hands over an arbitrary Core mutation (MixerSends has no UI
// dependency and no idea macros have ports), and the only thing this seam needs from it is which
// edges appeared and which went away. Everything runs inside the CALLER's own
// recordGraphAndMacroChange, so the connection, every port it minted and every port it swept are
// one undo step. Routing precedes the caller's updateComponents(), which is what lays out and docks
// the new port widgets (the same splice-before-layout order groupSelectionIntoMacro keeps).
bool MacroGroupController::applyProgrammaticConnectionChange(bool autoCreatePorts,
                                                             const std::function<bool()>& mutation) {
    auto& graph = host_.graph();
    const auto before = connectionSet(graph);
    const bool result = mutation();
    const auto after = connectionSet(graph);

    const auto added = minus(after, before);
    const auto removed = minus(before, after);

    if (autoCreatePorts && !added.empty())
        routeFreshEdgesThroughMacroPorts({added.begin(), added.end()});

    std::vector<NodeID> touched;
    for (const auto& c : removed) {
        touched.push_back(c.source.nodeID);
        touched.push_back(c.destination.nodeID);
    }
    sweepOneSidedMacroPorts(touched);
    return result;
}

// One pass over every macro. For each, the ordinary crossing plan is computed (so shape inference,
// the attenuverter rule and jack grouping are the grouping path's own), then narrowed to the fresh
// edges only -- a crossing the user earlier chose to "leave as is" on the same jack is never ported
// behind their back -- and to groups whose inside end is an ORDINARY member: an edge landing on an
// existing port already enters properly (the drag path's "never mint a second port" rule). After
// each splice the fresh set is re-read from the graph, so a send between two macros gets an outlet
// on the source's macro and then an inlet on the target's, wired port to port, in either order.
void MacroGroupController::routeFreshEdgesThroughMacroPorts(std::set<Connection> fresh) {
    auto& graph = host_.graph();
    std::vector<juce::String> macroIds;
    for (const auto& m : host_.getMacros().getAll())
        macroIds.push_back(m.id);

    for (const auto& macroId : macroIds) {
        const auto* macro = host_.getMacros().find(macroId);
        if (macro == nullptr)
            continue;

        std::vector<Group> plan;
        std::vector<Connection> dropped; // fresh edges onto a hidden channel -- carry nothing
        for (auto group : buildMacroPortCrossingPlan(macro->members)) {
            if (macro->memberIsPort(group.internalUuid))
                continue;
            std::vector<MacroPortCrossingEdge> kept;
            for (const auto& edge : group.edges) {
                const auto conn = edgeConnection(group, edge);
                if (fresh.count(conn) == 0)
                    continue;
                if (isHiddenRawChannel(graph, group, edge.internalRawChannel))
                    dropped.push_back(conn);
                else
                    kept.push_back(edge);
            }
            group.edges = std::move(kept);
            if (!group.edges.empty())
                plan.push_back(std::move(group));
        }
        mergeSplitStereoPairs(plan);
        if (plan.empty() && dropped.empty())
            continue;

        const auto beforeSplice = connectionSet(graph);
        for (const auto& conn : dropped)
            graph.removeConnection(conn);
        spliceMacroPorts(macroId, plan);
        const auto afterSplice = connectionSet(graph);
        for (const auto& gone : minus(beforeSplice, afterSplice))
            fresh.erase(gone);
        for (const auto& born : minus(afterSplice, beforeSplice))
            fresh.insert(born);
    }
}

// The removal half. autoDeleteOrphanedMacroPort only fires at ZERO connections, which a removed
// send never reaches: its port keeps the interior leg into the member it fronts. So the test here
// is "nothing left on one side" -- an inlet nothing feeds, or an outlet that feeds nothing -- the
// same "only interior left" reading autoDeleteOrphanedAttenuverter already applies. Splicing out a
// port can strand the port it was wired to (a send between two macros runs outlet -> inlet), so
// every port neighbour of a spliced port is re-checked; the walk only ever visits port nodes.
// Gated on the same auto-delete preference as the cable-gesture sweep.
void MacroGroupController::sweepOneSidedMacroPorts(std::vector<NodeID> candidates) {
    if (!host_.getAutoDeleteMacroPortsOnLastCableEnabled())
        return;
    auto& graph = host_.graph();

    while (!candidates.empty()) {
        const auto nodeId = candidates.back();
        candidates.pop_back();
        if (!nodeIsMacroPort(nodeId))
            continue;

        bool hasIn = false, hasOut = false;
        std::vector<NodeID> neighbours;
        for (const auto& c : graph.getConnections()) {
            if (c.destination.nodeID == nodeId) {
                hasIn = true;
                neighbours.push_back(c.source.nodeID);
            }
            if (c.source.nodeID == nodeId) {
                hasOut = true;
                neighbours.push_back(c.destination.nodeID);
            }
        }
        if (hasIn && hasOut)
            continue;

        const auto uuid = nodeUuidFor(nodeId);
        auto* macro = host_.getMacros().findByMember(uuid);
        if (macro == nullptr)
            continue;
        const auto macroId = macro->id;
        spliceOutMacroPort(*macro, uuid);
        // MacroSet's cascade rule: a macro with no direct members AND no children dissolves, and a parent
        // emptied by losing its last child dissolves with it.
        auto& macros = host_.getMacros();
        for (juce::String id = macroId; id.isNotEmpty();) {
            const auto* left = macros.find(id);
            if (left == nullptr || !left->members.empty() || !macros.childrenOf(id).empty())
                break;
            const juce::String parentId = left->parentId;
            macros.remove(id);
            id = parentId;
        }
        candidates.insert(candidates.end(), neighbours.begin(), neighbours.end());
    }
}
