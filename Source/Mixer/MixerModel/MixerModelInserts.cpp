// Concern: (docs/mixer/mixer.md#inserts-in-a-free-form-graph) -- a column's insert list: the chain between
// its feeding track's source and the strip, in signal order, plus linear-vs-branching classification, plus the three
// insert-list mutation primitives (splice out / splice in / reorder). Also (docs/mixer/sends-and-buses.md): a bus
// has no feeding track, so its own EQ/Compressor chain is discovered by walking BACKWARD from the strip instead
// (buildBusInsertsForColumn). And (docs/mixer/mixer.md#master-inserts): Master's own post-fader chain, walked
// FORWARD from Master's output to the Rec Tap / Audio Output terminator (buildMasterInsertsForColumn).
#include "MixerModel.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelMacroLookup.h"
#include "MixerModelInternal.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>
#include <optional>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

juce::AudioProcessor* processorFor(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

// Distinct NODES, never a raw connection count -- a stereo pair is TWO connections (ch0 and the
// right leg) to the very same next node, which must count as one successor/predecessor, not two,
// or every ordinary stereo chain would misreport as branching.
int signalSuccessorCount(const MixerGraphView& view, NodeID node) {
    std::vector<NodeID> destinations;
    for (const auto& c : view.cables.outOf(node))
        if (isSignalEdge(view.graph, view.cables, c))
            if (std::find(destinations.begin(), destinations.end(), c.destination.nodeID) == destinations.end())
                destinations.push_back(c.destination.nodeID);
    return (int)destinations.size();
}

int signalPredecessorCount(const MixerGraphView& view, NodeID node) {
    std::vector<NodeID> sources;
    for (const auto& c : view.cables.into(node))
        if (isSignalEdge(view.graph, view.cables, c))
            if (std::find(sources.begin(), sources.end(), c.source.nodeID) == sources.end())
                sources.push_back(c.source.nodeID);
    return (int)sources.size();
}

// Builds one MixerInsertEntry off a live node -- shared by the forward (track-anchored) and
// backward (bus-anchored) chain walks below.
MixerInsertEntry entryFor(juce::AudioProcessorGraph& graph, NodeID nodeId) {
    auto* processor = processorFor(graph, nodeId);
    auto* module = dynamic_cast<ModuleBase*>(processor);
    MixerInsertEntry entry;
    entry.nodeId = nodeId;
    entry.uuid = module != nullptr ? juce::String(module->getNodeUuid()) : juce::String();
    entry.name = processor != nullptr ? processor->getName() : juce::String("Module");
    entry.bypassed = module != nullptr && module->isBypassed();
    return entry;
}

// "Edit on canvas" points at the owning macro of the first boxed node in `chain`, else `chain`'s
// own first node -- shared by both walks' branching case.
void resolveEditOnCanvasTarget(const MixerGraphView& view, const std::vector<NodeID>& chain, MixerColumn& column) {
    auto& graph = view.graph;
    for (auto nodeId : chain) {
        auto* module = dynamic_cast<ModuleBase*>(processorFor(graph, nodeId));
        if (module == nullptr)
            continue;
        if (const auto* macro = view.channelMacros.nearest(module->getNodeUuid())) {
            column.editOnCanvasTargetUuid = macro->id;
            return;
        }
    }
    if (auto* module = dynamic_cast<ModuleBase*>(processorFor(graph, chain.front())))
        column.editOnCanvasTargetUuid = module->getNodeUuid();
}

// A bus has no feeding track (feedingTracks is empty by construction -- nothing in the timeline plays into
// it), so there is no source node to walk FORWARD from. Its own EQ/Compressor chain -- built by "Add
// bus"/buildBusChannel, or rearranged since -- instead sits immediately upstream of the strip, so walk
// BACKWARD from the strip along signal predecessors (see docs/mixer/sends-and-buses.md).
//
// A send into a bus lands on the SAME strip input channels (ch0/kRightBase) an insert's own
// output would, and IS a signal edge (isSignalEdge has no send-vs-insert concept) -- so a
// ChannelStripModule predecessor is excluded here exactly like findStripsFeedingStrip excludes one
// walking the other direction: "that strip IS a source, not something to expand through", never an
// insert and never itself grounds to call the bus's own chain "branching".
void buildBusInsertsForColumn(const MixerGraphView& view, MixerColumn& column) {
    auto& graph = view.graph;
    bool branching = false;
    std::vector<NodeID> reverseChain;
    std::vector<NodeID> visited{column.nodeId};
    NodeID current = column.nodeId;

    for (;;) {
        std::vector<NodeID> preds;
        for (const auto& c : view.cables.into(current)) {
            if (!isSignalEdge(graph, view.cables, c))
                continue;
            if (dynamic_cast<ChannelStripModule*>(processorFor(graph, c.source.nodeID)) != nullptr)
                continue; // a feeding strip is a send, never an insert
            if (std::find(preds.begin(), preds.end(), c.source.nodeID) == preds.end())
                preds.push_back(c.source.nodeID);
        }
        if (preds.empty())
            break; // reached the head of the bus's own chain -- nothing feeds it from outside
        if (preds.size() > 1)
            branching = true;

        const NodeID prev = preds.front();
        if (signalSuccessorCount(view, prev) > 1)
            branching = true;
        if (std::find(visited.begin(), visited.end(), prev) != visited.end())
            break; // cycle guard -- should not happen in a real patch, never hang if it does
        reverseChain.push_back(prev);
        visited.push_back(prev);
        current = prev;
        if (reverseChain.size() > 64)
            break; // sane upper bound
    }
    std::reverse(reverseChain.begin(), reverseChain.end());

    column.insertChainIsLinear = !branching;
    for (auto nodeId : reverseChain)
        column.inserts.push_back(entryFor(graph, nodeId));

    if (!branching || reverseChain.empty())
        return;
    resolveEditOnCanvasTarget(view, reverseChain, column);
}

// The two nodes Master's chain can end at -- the master Rec Tap when one is spliced in (it sits in front of
// Audio Output, see MasterSplice.h), else the graph's Audio Output. Neither is ever an insert.
bool isMasterChainTerminator(juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return false;
    if (dynamic_cast<RecordTapModule*>(processor) != nullptr)
        return true;
    auto* io = dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(processor);
    return io != nullptr && io->getType() == juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode;
}

// Master's inserts sit AFTER its fader, so the chain is Master's own output -> [inserts] -> terminator. Walked
// FORWARD from Master along signal edges (a strip's forward walk from its track source, with Master standing in as
// the source and the Rec Tap/Audio Output as the sink instead of the strip) (see docs/mixer/mixer.md#master-inserts).
//
// Deliberately not "branching" when the TERMINATOR has other feeders: a hand-wired audio track can land on the Rec
// Tap or Audio Output directly, and that shared sink is unaffected by splicing an insert in front of Master's own
// connection -- unlike an insert, which reordering/removing would drag those other signals through.
//
// Master unwired, or wired into something that never reaches a terminator: nothing to splice against, so the list
// stays empty and read-only with "Edit on canvas" pointing at Master itself.
void buildMasterInsertsForColumn(const MixerGraphView& view, MixerColumn& column) {
    auto& graph = view.graph;
    column.sourceNodeId = column.nodeId;

    bool branching = false;
    std::vector<NodeID> chain;
    std::vector<NodeID> visited{column.nodeId};
    NodeID current = column.nodeId;
    NodeID terminator;

    for (;;) {
        if (signalSuccessorCount(view, current) > 1)
            branching = true;

        NodeID next;
        bool haveNext = false;
        for (const auto& c : view.cables.outOf(current)) {
            if (isSignalEdge(graph, view.cables, c)) {
                next = c.destination.nodeID;
                haveNext = true;
                break;
            }
        }
        if (!haveNext)
            break; // Master's reach ends without arriving at a terminator

        if (isMasterChainTerminator(processorFor(graph, next))) {
            terminator = next;
            break;
        }
        if (signalPredecessorCount(view, next) > 1)
            branching = true;
        if (std::find(visited.begin(), visited.end(), next) != visited.end())
            break; // cycle guard -- should not happen in a real patch, never hang if it does
        chain.push_back(next);
        visited.push_back(next);
        current = next;
        if (chain.size() > 64)
            break; // sane upper bound
    }

    if (terminator == NodeID{}) {
        column.insertChainIsLinear = false;
        column.editOnCanvasTargetUuid = column.uuid;
        return;
    }

    column.chainEndNodeId = terminator;
    column.insertChainIsLinear = !branching;
    for (auto nodeId : chain)
        column.inserts.push_back(entryFor(graph, nodeId));

    if (!branching)
        return;
    if (chain.empty())
        column.editOnCanvasTargetUuid = column.uuid;
    else
        resolveEditOnCanvasTarget(view, chain, column);
}
// ---- Parallel paths (docs/mixer/mixer.md#parallel-paths) ----
// A strip's chain may fan out into parallel paths that all meet again at one node (an insert, or the strip itself)
// before the strip. That is a series-parallel shape the mixer can list row by row: each fork becomes a Split row
// followed by its paths' rows one level deeper. Anything else (a path that leaves the reach, a merge fed from
// outside the chain, paths meeting at different nodes) does not parse, and the caller keeps today's read-only list.

// A chain link: a signal edge that does not land on a Pitch or Gate jack. Those play the voice rather than carry its
// sound (an instrument's Poly MIDI feeds its oscillator's pitch and its envelope's gate), like the ModCV cables
// isSignalEdge already leaves out.
bool isChainEdge(const MixerGraphView& view, const Connection& c) {
    if (!isSignalEdge(view.graph, view.cables, c))
        return false;
    if (c.source.isMIDI())
        return true;
    const auto pin = resolveThroughPorts(view.graph, view.cables, c.destination);
    auto* module = dynamic_cast<ModuleBase*>(processorFor(view.graph, pin.nodeID));
    if (module == nullptr)
        return true;
    const auto role = module->mapInputChannel(pin.channelIndex).role;
    return role != PortRole::Pitch && role != PortRole::Gate;
}

int chainPredecessorCount(const MixerGraphView& view, NodeID node) {
    std::vector<NodeID> sources;
    for (const auto& c : view.cables.into(node))
        if (isChainEdge(view, c))
            if (std::find(sources.begin(), sources.end(), c.source.nodeID) == sources.end())
                sources.push_back(c.source.nodeID);
    return (int)sources.size();
}

// Distinct chain successors, top to bottom as their cards sit on the canvas (then by NodeID), so Path 1 is the upper
// path there and the order (and so the column's equality) stays put when a module moves within a path.
std::vector<NodeID> chainLinkSuccessors(const MixerGraphView& view, NodeID node) {
    std::vector<NodeID> destinations;
    for (const auto& c : view.cables.outOf(node))
        if (isChainEdge(view, c))
            if (std::find(destinations.begin(), destinations.end(), c.destination.nodeID) == destinations.end())
                destinations.push_back(c.destination.nodeID);
    auto y = [&](NodeID id) {
        auto* n = view.graph.getNodeForId(id);
        return n != nullptr ? (int)n->properties.getWithDefault("y", 0) : 0;
    };
    std::sort(destinations.begin(), destinations.end(),
              [&](NodeID a, NodeID b) { return y(a) != y(b) ? y(a) < y(b) : a.uid < b.uid; });
    return destinations;
}

class SplitChainParser {
public:
    SplitChainParser(const MixerGraphView& view, NodeID end)
        : view_(view)
        , end_(end) {}

    /** Rows for the chain after `source`, or nullopt when it is not a clean series-parallel chain ending at `end`. */
    std::optional<std::vector<MixerInsertEntry>> parse(NodeID source) {
        visited_.push_back(source);
        NodeID stop;
        if (!continueFrom(source, 0, -1, stop) || stop != end_)
            return std::nullopt;
        return std::move(rows_);
    }

private:
    static constexpr size_t kMaxRows = 64; // sane upper bound, as the flat walks use

    bool isStop(NodeID node) const { return node == end_ || chainPredecessorCount(view_, node) > 1; }

    // `node`'s signal successors, less the ones it only sends MIDI to that never reach the strip: an instrument
    // track's Track In feeds its ADSR as well as its instrument, and that envelope ends in the VCA's Gain CV, which is
    // modulation, not a second path of audio.
    std::vector<NodeID> chainSuccessors(NodeID node) {
        auto next = chainLinkSuccessors(view_, node);
        next.erase(std::remove_if(next.begin(), next.end(),
                                  [&](NodeID to) { return onlyMidiTo(node, to) && !reachesEnd(to); }),
                   next.end());
        return next;
    }

    bool onlyMidiTo(NodeID from, NodeID to) const {
        for (const auto& c : view_.cables.outOf(from))
            if (c.destination.nodeID == to && !c.source.isMIDI() && isChainEdge(view_, c))
                return false;
        return true;
    }

    bool reachesEnd(NodeID from) {
        std::vector<NodeID> seen{from};
        std::vector<NodeID> todo{from};
        while (!todo.empty()) {
            const auto node = todo.back();
            todo.pop_back();
            if (node == end_)
                return true;
            for (auto next : chainLinkSuccessors(view_, node))
                if (std::find(seen.begin(), seen.end(), next) == seen.end()) {
                    seen.push_back(next);
                    todo.push_back(next);
                }
        }
        return false;
    }

    bool pushModule(NodeID node, int depth, int path) {
        if (std::find(visited_.begin(), visited_.end(), node) != visited_.end() || rows_.size() >= kMaxRows)
            return false; // a cycle, or a runaway patch
        visited_.push_back(node);
        auto entry = entryFor(view_.graph, node);
        entry.depth = depth;
        entry.path = path;
        rows_.push_back(entry);
        return true;
    }

    // Walks on from `node` (already listed) to the first stop: the end, or a merge this run did not open. A fork on
    // the way is parsed whole, and the walk carries on past its merge.
    bool continueFrom(NodeID node, int depth, int path, NodeID& stop) {
        for (;;) {
            const auto next = chainSuccessors(node);
            if (next.empty())
                return false; // the reach ends before the strip
            NodeID after;
            if (next.size() == 1) {
                after = next.front();
                if (isStop(after)) {
                    stop = after;
                    return true;
                }
            } else {
                if (!parseFork(node, next, depth, path, after))
                    return false;
                if (after == end_) {
                    stop = after;
                    return true;
                }
            }
            if (!pushModule(after, depth, path))
                return false;
            node = after;
        }
    }

    // One Split row, then each path's rows one level deeper. Every path must stop at the same node, fed by exactly
    // these paths and nothing else.
    bool parseFork(NodeID fork, const std::vector<NodeID>& starts, int depth, int path, NodeID& merge) {
        MixerInsertEntry split;
        split.kind = MixerInsertEntry::Kind::Split;
        if (auto* module = dynamic_cast<ModuleBase*>(processorFor(view_.graph, fork)))
            split.uuid = module->getNodeUuid();
        split.name = juce::String((int)starts.size()) + " paths";
        split.depth = depth;
        split.path = path;
        split.pathCount = (int)starts.size();
        rows_.push_back(split);

        for (int i = 0; i < (int)starts.size(); ++i) {
            const auto start = starts[(size_t)i];
            NodeID stop = start; // a path wired straight to the merge is a dry path with no rows
            if (!isStop(start)) {
                if (!pushModule(start, depth + 1, i) || !continueFrom(start, depth + 1, i, stop))
                    return false;
            }
            if (i == 0)
                merge = stop;
            else if (stop != merge)
                return false;
        }
        return chainPredecessorCount(view_, merge) == (int)starts.size();
    }

    const MixerGraphView& view_;
    NodeID end_;
    std::vector<NodeID> visited_;
    std::vector<MixerInsertEntry> rows_;
};
} // namespace

void buildInsertsForColumn(const MixerGraphView& view, const TimelineDoc& doc, MixerColumn& column) {
    auto& graph = view.graph;
    if (column.kind == MixerColumn::Kind::Master) {
        buildMasterInsertsForColumn(view, column);
        return;
    }
    if (column.feedingTracks.empty()) {
        if (column.kind == MixerColumn::Kind::Bus)
            buildBusInsertsForColumn(view, column);
        return; // a non-bus orphan strip has no track-anchored chain to walk
    }

    const auto* track = doc.getTrack(column.feedingTracks.front());
    const auto sourceId = track != nullptr ? view.trackSource(*track) : NodeID{};
    if (sourceId == NodeID{})
        return;
    column.sourceNodeId = sourceId;

    // A chain that runs straight, or splits into parallel paths that meet again, before the strip lists row by row.
    if (auto rows = SplitChainParser(view, column.nodeId).parse(sourceId)) {
        const bool hasSplits = std::any_of(rows->begin(), rows->end(), [](const MixerInsertEntry& row) {
            return row.kind == MixerInsertEntry::Kind::Split;
        });
        column.insertChainHasSplits = hasSplits;
        column.insertChainIsLinear = !hasSplits;
        column.inserts = std::move(*rows);
        return;
    }

    // Forward walk from the source along signal edges, collecting the chain up to (not including)
    // the strip. A node with more than one signal predecessor or successor within the reach is a
    // merge/fan-out -- the whole chain is then read-only ("branching"), per
    // docs/mixer/mixer.md#inserts-in-a-free-form-graph.
    bool branching = false;
    std::vector<NodeID> chain;
    std::vector<NodeID> visited{sourceId};
    NodeID current = sourceId;

    while (current != column.nodeId) {
        if (signalSuccessorCount(view, current) > 1)
            branching = true;

        NodeID next;
        bool haveNext = false;
        for (const auto& c : view.cables.outOf(current)) {
            if (isSignalEdge(graph, view.cables, c)) {
                next = c.destination.nodeID;
                haveNext = true;
                break;
            }
        }
        if (!haveNext)
            return; // this track's reach never arrives at the strip -- leave the list empty

        if (next != column.nodeId) {
            if (signalPredecessorCount(view, next) > 1)
                branching = true;
            if (std::find(visited.begin(), visited.end(), next) != visited.end())
                break; // cycle guard -- should not happen in a real patch, never hang if it does
            chain.push_back(next);
            visited.push_back(next);
        }
        current = next;
        if (chain.size() > 64)
            break; // sane upper bound
    }

    column.insertChainIsLinear = !branching;
    for (auto nodeId : chain)
        column.inserts.push_back(entryFor(graph, nodeId));

    if (!branching || chain.empty())
        return;
    resolveEditOnCanvasTarget(view, chain, column);
}

bool spliceOutInsert(juce::AudioProcessorGraph& graph, NodeID node) {
    const auto connections = graph.getConnections();
    std::vector<Connection> preds, succs;
    for (const auto& c : connections) {
        if (c.destination.nodeID == node && isSignalEdge(graph, connections, c))
            preds.push_back(c);
        if (c.source.nodeID == node && isSignalEdge(graph, connections, c))
            succs.push_back(c);
    }
    if (preds.empty() || succs.empty())
        return false;

    // Bridge by matching `node`'s own input channel index to its own output channel index -- true
    // for every ordinary passthrough FX module (see MixerModel.h's own contract note).
    for (const auto& p : preds)
        for (const auto& s : succs)
            if (p.destination.channelIndex == s.source.channelIndex)
                graph.addConnection({p.source, s.destination});

    for (const auto& c : preds)
        graph.removeConnection(c);
    for (const auto& c : succs)
        graph.removeConnection(c);
    return true;
}

bool spliceInInsert(juce::AudioProcessorGraph& graph, NodeID predecessor, NodeID successor, NodeID node) {
    const auto connections = graph.getConnections();
    std::vector<Connection> between;
    for (const auto& c : connections)
        if (c.source.nodeID == predecessor && c.destination.nodeID == successor && isSignalEdge(graph, connections, c))
            between.push_back(c);
    if (between.empty())
        return false;

    // `node`'s own L/R channel numbering need not match `successor`'s (an ordinary FX module's
    // right leg vs. e.g. ChannelStripModule's kRightBase) -- left is always ch0
    // (Source/Modules/CLAUDE.md), right is whichever channel `node` itself reports as its own
    // right leg, read off the connection being replaced only to decide WHICH leg (left vs. right)
    // this connection carries, never to pick node's channel number directly.
    auto* nodeModule = dynamic_cast<ModuleBase*>(processorFor(graph, node));
    const int nodeRight = nodeModule != nullptr ? nodeModule->rightAudioLegChannel() : -1;

    for (const auto& c : between) {
        const bool isRightLeg = c.destination.channelIndex != 0;
        const int nodeChannel = isRightLeg ? (nodeRight >= 0 ? nodeRight : c.destination.channelIndex) : 0;
        graph.removeConnection(c);
        graph.addConnection({c.source, {node, nodeChannel}});
        graph.addConnection({{node, nodeChannel}, c.destination});
    }
    return true;
}

bool reorderInsert(juce::AudioProcessorGraph& graph, NodeID node, NodeID newPredecessor, NodeID newSuccessor) {
    if (!spliceOutInsert(graph, node))
        return false;
    return spliceInInsert(graph, newPredecessor, newSuccessor, node);
}

// `node`'s one signal neighbour on the given side, or an invalid id when it has none or several.
NodeID soleSignalNeighbour(juce::AudioProcessorGraph& graph, NodeID node, bool upstream) {
    const auto connections = graph.getConnections();
    NodeID found;
    for (const auto& c : connections) {
        const auto& self = upstream ? c.destination : c.source;
        const auto& other = upstream ? c.source : c.destination;
        if (self.nodeID != node || !isSignalEdge(graph, connections, c) || other.nodeID == found)
            continue;
        if (found != NodeID{})
            return {};
        found = other.nodeID;
    }
    return found;
}

// The neighbour is read before `node` comes out: removing `node` (which sits on the far side of `before`/`after`)
// never changes it.
bool moveInsertBefore(juce::AudioProcessorGraph& graph, NodeID node, NodeID before) {
    const auto predecessor = soleSignalNeighbour(graph, before, true);
    if (predecessor == NodeID{} || predecessor == node)
        return false;
    return reorderInsert(graph, node, predecessor, before);
}

bool moveInsertAfter(juce::AudioProcessorGraph& graph, NodeID node, NodeID after) {
    const auto successor = soleSignalNeighbour(graph, after, false);
    if (successor == NodeID{} || successor == node)
        return false;
    return reorderInsert(graph, node, after, successor);
}

} // namespace synth
