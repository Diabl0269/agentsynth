// Concern: the read-only cut of the graph into per-Channel-Strip jobs (TrackCut.h).
#include "AudioEngine/TrackCut.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Modules/ChannelStripModule.h"
#include <algorithm>
#include <map>
#include <set>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

bool isStrip(const juce::AudioProcessorGraph::Node& node) {
    return dynamic_cast<ChannelStripModule*>(node.getProcessor()) != nullptr;
}

// Kahn's algorithm over the job edges; smallest ready index first so the order is stable.
bool topologicalOrder(int jobCount, const std::vector<std::pair<int, int>>& edges, std::vector<int>& order) {
    std::vector<int> indegree((size_t)jobCount, 0);
    std::vector<std::vector<int>> next((size_t)jobCount);
    for (const auto& [from, to] : edges) {
        next[(size_t)from].push_back(to);
        ++indegree[(size_t)to];
    }
    std::set<int> ready;
    for (int i = 0; i < jobCount; ++i)
        if (indegree[(size_t)i] == 0)
            ready.insert(i);
    while (!ready.empty()) {
        const int job = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(job);
        for (const int to : next[(size_t)job])
            if (--indegree[(size_t)to] == 0)
                ready.insert(to);
    }
    return (int)order.size() == jobCount;
}
} // namespace

// A node's strips are found backwards from each strip: walk the cables into it, never through another strip (a strip
// ends a job, it does not belong to the strips it feeds), and every non-strip node reached feeds that strip.
TrackCut cutIntoTrackJobs(const juce::AudioProcessorGraph& graph) {
    const ConnectionIndex cables(graph);
    TrackCut cut;
    std::map<juce::uint32, int> jobOf;
    std::map<juce::uint32, std::set<juce::uint32>> stripsOf;

    std::vector<NodeID> strips;
    for (auto* node : graph.getNodes())
        if (isStrip(*node))
            strips.push_back(node->nodeID);
    for (const auto& id : strips) {
        jobOf[id.uid] = (int)cut.jobs.size();
        TrackJob job;
        job.kind = TrackJob::Kind::Strip;
        job.strip = id;
        job.nodes.push_back(id);
        cut.jobs.push_back(std::move(job));
    }
    for (const auto& strip : strips) {
        std::set<juce::uint32> seen{strip.uid};
        std::vector<NodeID> stack{strip};
        while (!stack.empty()) {
            const NodeID at = stack.back();
            stack.pop_back();
            for (const auto& c : cables.into(at)) {
                const NodeID from = c.source.nodeID;
                if (!seen.insert(from.uid).second || jobOf.count(from.uid) != 0)
                    continue;
                stripsOf[from.uid].insert(strip.uid);
                stack.push_back(from);
            }
        }
    }

    int sharedJob = -1, postJob = -1;
    const auto jobFor = [&](TrackJob::Kind kind, int& slot) {
        if (slot < 0) {
            slot = (int)cut.jobs.size();
            TrackJob job;
            job.kind = kind;
            cut.jobs.push_back(std::move(job));
        }
        return slot;
    };
    for (auto* node : graph.getNodes()) {
        const auto id = node->nodeID;
        if (jobOf.count(id.uid) != 0)
            continue;
        const auto found = stripsOf.find(id.uid);
        const size_t count = found == stripsOf.end() ? 0 : found->second.size();
        const int job = count == 1   ? jobOf[*found->second.begin()]
                        : count >= 2 ? jobFor(TrackJob::Kind::Shared, sharedJob)
                                     : jobFor(TrackJob::Kind::Post, postJob);
        jobOf[id.uid] = job;
        cut.jobs[(size_t)job].nodes.push_back(id);
    }

    std::set<std::pair<int, int>> edgeSet;
    for (const auto& c : cables.all()) {
        const int from = jobOf[c.source.nodeID.uid], to = jobOf[c.destination.nodeID.uid];
        if (from != to)
            edgeSet.insert({from, to});
    }
    cut.edges.assign(edgeSet.begin(), edgeSet.end());
    cut.acyclic = topologicalOrder((int)cut.jobs.size(), cut.edges, cut.order);
    if (!cut.acyclic)
        cut.order.clear();
    return cut;
}

} // namespace synth
