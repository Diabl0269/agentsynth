// Concern: building a TrackRenderPlan from the graph: JUCE's render order, its fan-in summing order and latency
// delays, per-node output slots, and the job dependency graph.
#include "AudioEngine/TrackRenderPlan.h"
#include "AudioEngine/TrackRenderPlanSteps.h"
#include <map>
#include <set>

namespace synth {

// Reproduces what juce::AudioProcessorGraph's RenderSequenceBuilder decides, from the graph's public nodes and
// connections, without its buffer recycling: every output channel another node reads later gets a slot of its own
// (so jobs on different threads never share a buffer), and everything else lives in the job's scratch.
//
// Same samples as JUCE needs three things kept exactly:
//  - the node order (createOrderedNodeList below is JUCE's, line for line): it decides which cable is a feedback
//    cable (its source renders later, so the input reads silence) and which inputs are summed in which order;
//  - the summing order of an input fed by several outputs: float addition is not associative, and JUCE starts
//    from the first source (in connection order) whose buffer no later node needs, then adds the rest in order;
//  - the latency delay of each source (maxInputLatency - its own), applied to that source before it is summed.
struct TrackRenderPlanBuilder {
    using Node = juce::AudioProcessorGraph::Node;
    using NodeID = juce::AudioProcessorGraph::NodeID;
    using NodeAndChannel = juce::AudioProcessorGraph::NodeAndChannel;
    static constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

    const juce::AudioProcessorGraph& graph;
    TrackRenderPlan& plan;
    std::map<NodeAndChannel, std::set<NodeAndChannel>> sourcesFor, destsFor;
    std::map<NodeID, std::set<NodeID>> sourceNodesFor;
    std::vector<Node*> ordered;
    std::map<NodeID, int> orderOf, delayOf, jobOf;
    std::map<NodeAndChannel, float*> slotOf;
    std::map<NodeID, TrackRenderPlan::Step*> stepOf;

    void indexConnections() {
        for (const auto& c : graph.getConnections()) {
            sourcesFor[c.destination].insert(c.source);
            destsFor[c.source].insert(c.destination);
            sourceNodesFor[c.destination.nodeID].insert(c.source.nodeID);
        }
    }

    const std::set<NodeID>& sourceNodes(NodeID node) const {
        static const std::set<NodeID> none;
        const auto it = sourceNodesFor.find(node);
        return it == sourceNodesFor.end() ? none : it->second;
    }

    void getAllParentsOfNode(NodeID child, std::set<NodeID>& parents,
                             const std::map<NodeID, std::set<NodeID>>& otherParents) const {
        for (const auto& parentNode : sourceNodes(child)) {
            if (parentNode == child)
                continue;
            if (parents.insert(parentNode).second) {
                const auto parentParents = otherParents.find(parentNode);
                if (parentParents != otherParents.end()) {
                    parents.insert(parentParents->second.begin(), parentParents->second.end());
                    continue;
                }
                getAllParentsOfNode(parentNode, parents, otherParents);
            }
        }
    }

    void createOrderedNodeList() {
        std::map<NodeID, std::set<NodeID>> nodeParents;
        for (auto* node : graph.getNodes()) {
            const auto nodeID = node->nodeID;
            size_t insertionIndex = 0;
            for (; insertionIndex < ordered.size(); ++insertionIndex) {
                auto& parents = nodeParents[ordered[insertionIndex]->nodeID];
                if (parents.find(nodeID) != parents.end())
                    break;
            }
            ordered.insert(ordered.begin() + (std::ptrdiff_t)insertionIndex, node);
            getAllParentsOfNode(nodeID, nodeParents[node->nodeID], nodeParents);
        }
        for (size_t i = 0; i < ordered.size(); ++i)
            orderOf[ordered[i]->nodeID] = (int)i;
    }

    bool renderedBefore(NodeAndChannel source, int step) const { return orderOf.at(source.nodeID) < step; }

    // JUCE's isBufferNeededLater: read by the node at `step` on another channel, or by any node after it.
    bool neededLater(int step, int inputChannelToIgnore, NodeAndChannel output) const {
        if ((int)ordered.size() <= step)
            return false;
        const auto it = destsFor.find(output);
        if (it == destsFor.end())
            return false;
        const NodeID here = ordered[(size_t)step]->nodeID;
        for (const auto& dest : it->second) {
            if (dest.nodeID == here && dest.channelIndex != inputChannelToIgnore)
                return true;
            if (orderOf.at(dest.nodeID) > step)
                return true;
        }
        return false;
    }

    int delayOfNode(NodeID node) const {
        const auto it = delayOf.find(node);
        return it == delayOf.end() ? 0 : it->second;
    }

    StepFeed feed(NodeAndChannel source, int step, int maxLatency) {
        StepFeed f;
        if (!renderedBefore(source, step))
            return f; // a feedback cable: JUCE reads silence
        f.source = slotOf.at(source);
        const int nodeDelay = delayOfNode(source.nodeID);
        if (nodeDelay < maxLatency) {
            f.delay = (int)plan.delays_.size();
            plan.delays_.push_back(std::make_unique<TrackRenderPlan::DelayLine>(maxLatency - nodeDelay));
        }
        return f;
    }

    StepInputMix audioMix(int step, int channel, int maxLatency) {
        StepInputMix mix;
        mix.channel = channel;
        const auto found = sourcesFor.find({ordered[(size_t)step]->nodeID, channel});
        if (found == sourcesFor.end() || found->second.empty())
            return mix;
        const auto& sources = found->second;
        if (sources.size() == 1) {
            mix.first = feed(*sources.begin(), step, maxLatency);
            return mix;
        }
        int base = -1, i = 0;
        for (const auto& src : sources) {
            if (renderedBefore(src, step) && !neededLater(step, channel, src)) {
                base = i;
                break;
            }
            ++i;
        }
        const int firstIndex = base >= 0 ? base : 0;
        i = 0;
        for (const auto& src : sources) {
            if (i == firstIndex)
                mix.first = feed(src, step, maxLatency);
            else if (renderedBefore(src, step))
                mix.rest.push_back(feed(src, step, maxLatency));
            ++i;
        }
        return mix;
    }

    void midiMix(TrackRenderPlan::Step& s, int step) {
        const auto found = sourcesFor.find({ordered[(size_t)step]->nodeID, kMidi});
        if (found == sourcesFor.end() || found->second.empty())
            return;
        const auto& sources = found->second;
        int base = -1, i = 0;
        if (sources.size() > 1)
            for (const auto& src : sources) {
                if (renderedBefore(src, step) && !neededLater(step, kMidi, src)) {
                    base = i;
                    break;
                }
                ++i;
            }
        const int firstIndex = base >= 0 ? base : 0;
        i = 0;
        for (const auto& src : sources) {
            if (renderedBefore(src, step))
                (i == firstIndex ? s.midiFirst : s.midiRest.emplace_back()) = &stepOf.at(src.nodeID)->midi;
            ++i;
        }
    }

    bool assignJobs(const std::vector<std::vector<NodeID>>& groups) {
        for (size_t g = 0; g < groups.size(); ++g)
            for (const auto& id : groups[g])
                if (!jobOf.emplace(id, (int)g).second) {
                    plan.problem_ = "a node is in two jobs";
                    return false;
                }
        for (auto* node : ordered)
            if (jobOf.count(node->nodeID) == 0) {
                plan.problem_ = "a node is in no job";
                return false;
            }
        return true;
    }

    // Every output channel that a node rendered after it reads gets its own slot.
    void allocateSlots(int maxBlockSize) {
        std::vector<NodeAndChannel> needed;
        for (const auto& [source, dests] : destsFor) {
            if (source.channelIndex == kMidi)
                continue;
            const int at = orderOf.at(source.nodeID);
            for (const auto& d : dests)
                if (orderOf.at(d.nodeID) > at) {
                    needed.push_back(source);
                    break;
                }
        }
        plan.slots_.setSize(std::max(1, (int)needed.size()), maxBlockSize);
        plan.slots_.clear();
        for (size_t i = 0; i < needed.size(); ++i)
            slotOf[needed[i]] = plan.slots_.getWritePointer((int)i);
    }

    static StepIo ioKind(juce::AudioProcessor* processor) {
        using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
        const auto* io = dynamic_cast<IO*>(processor);
        if (io == nullptr)
            return StepIo::None;
        switch (io->getType()) {
        case IO::audioInputNode:
            return StepIo::AudioIn;
        case IO::audioOutputNode:
            return StepIo::AudioOut;
        case IO::midiInputNode:
            return StepIo::MidiIn;
        case IO::midiOutputNode:
            return StepIo::MidiOut;
        }
        return StepIo::None;
    }

    void makeJobs(int groupCount, int maxBlockSize) {
        std::vector<int> width((size_t)groupCount, 1);
        for (auto* node : ordered) {
            auto* p = node->getProcessor();
            auto& w = width[(size_t)jobOf.at(node->nodeID)];
            w = std::max(w, std::max(p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()));
        }
        for (int g = 0; g < groupCount; ++g) {
            auto job = std::make_unique<TrackRenderPlan::Job>();
            job->scratch.setSize(width[(size_t)g] + 1, maxBlockSize);
            job->scratch.clear();
            job->delayScratch = job->scratch.getWritePointer(width[(size_t)g]);
            plan.jobs_.push_back(std::move(job));
        }
    }

    void makeStep(int step) {
        auto* node = ordered[(size_t)step];
        auto* processor = node->getProcessor();
        const int numIns = processor->getTotalNumInputChannels();
        const int numOuts = processor->getTotalNumOutputChannels();
        auto& job = *plan.jobs_[(size_t)jobOf.at(node->nodeID)];

        auto s = std::make_unique<TrackRenderPlan::Step>();
        s->node = node;
        s->processor = processor;
        s->io = ioKind(processor);
        s->numAudioChannels = (numIns == 0 && numOuts == 0) ? 0 : std::max(numIns, numOuts);
        const int total = std::max(1, std::max(numIns, numOuts));
        for (int ch = 0; ch < total; ++ch) {
            const auto slot = ch < numOuts ? slotOf.find({node->nodeID, ch}) : slotOf.end();
            s->channels.push_back(slot != slotOf.end() ? slot->second : job.scratch.getWritePointer(ch));
        }
        int maxLatency = 0;
        for (const auto& src : sourceNodes(node->nodeID))
            maxLatency = std::max(maxLatency, delayOfNode(src));
        for (int ch = 0; ch < numIns; ++ch)
            s->mixes.push_back(audioMix(step, ch, maxLatency));
        for (int ch = numIns; ch < numOuts; ++ch)
            s->clears.push_back(ch);
        midiMix(*s, step);
        if (processor->acceptsMidi() || processor->producesMidi())
            s->midi.ensureSize(4096);
        delayOf[node->nodeID] = maxLatency + processor->getLatencySamples();

        stepOf[node->nodeID] = s.get();
        job.steps.push_back(s.get());
        plan.steps_.push_back(std::move(s));
    }

    // Job edges are the cables a job really reads (source rendered first); a feedback cable reads silence, so it
    // orders nothing. The jobs are then renumbered in dependency order, so the runner's scan meets ready jobs first.
    bool linkJobs() {
        const int count = (int)plan.jobs_.size();
        std::set<std::pair<int, int>> edges;
        for (const auto& [dest, sources] : sourcesFor)
            for (const auto& src : sources) {
                const int from = jobOf.at(src.nodeID), to = jobOf.at(dest.nodeID);
                if (from != to && orderOf.at(src.nodeID) < orderOf.at(dest.nodeID))
                    edges.insert({from, to});
            }
        std::vector<int> indegree((size_t)count, 0);
        std::vector<std::vector<int>> next((size_t)count);
        for (const auto& [from, to] : edges) {
            next[(size_t)from].push_back(to);
            ++indegree[(size_t)to];
        }
        std::vector<int> waits = indegree, order;
        std::set<int> ready;
        for (int j = 0; j < count; ++j)
            if (indegree[(size_t)j] == 0)
                ready.insert(j);
        while (!ready.empty()) {
            const int j = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(j);
            for (const int to : next[(size_t)j])
                if (--indegree[(size_t)to] == 0)
                    ready.insert(to);
        }
        if ((int)order.size() != count) {
            plan.problem_ = "the jobs depend on each other in a loop";
            return false;
        }
        std::vector<int> renumber((size_t)count);
        for (int i = 0; i < count; ++i)
            renumber[(size_t)order[(size_t)i]] = i;
        std::vector<std::unique_ptr<TrackRenderPlan::Job>> sorted((size_t)count);
        for (int j = 0; j < count; ++j) {
            auto& job = plan.jobs_[(size_t)j];
            job->waits = waits[(size_t)j];
            for (const int to : next[(size_t)j])
                job->successors.push_back(renumber[(size_t)to]);
            sorted[(size_t)renumber[(size_t)j]] = std::move(job);
        }
        plan.jobs_ = std::move(sorted);
        return true;
    }

    // The graph's audio and MIDI output buffers are summed into without atomics, so every node that writes them
    // must render on one thread.
    bool outputsInOneJob() {
        int outJob = -1;
        for (const auto& job : plan.jobs_)
            for (auto* step : job->steps)
                if (step->io == StepIo::AudioOut || step->io == StepIo::MidiOut) {
                    if (outJob >= 0 && outJob != (int)(&job - plan.jobs_.data())) {
                        plan.problem_ = "the graph's outputs are in different jobs";
                        return false;
                    }
                    outJob = (int)(&job - plan.jobs_.data());
                }
        return true;
    }
};

std::unique_ptr<TrackRenderPlan> TrackRenderPlan::build(const juce::AudioProcessorGraph& graph,
                                                        const std::vector<std::vector<NodeID>>& groups,
                                                        int maxBlockSize, int maxIoChannels) {
    std::unique_ptr<TrackRenderPlan> plan(new TrackRenderPlan());
    plan->maxBlockSize_ = std::max(1, maxBlockSize);
    plan->maxIoChannels_ = std::max(1, maxIoChannels);
    if (graph.isUsingDoublePrecision()) {
        plan->problem_ = "the graph renders in double precision";
        return plan;
    }
    if ((int)groups.size() > TrackJobRunner::kMaxJobs) {
        plan->problem_ = "too many jobs";
        return plan;
    }
    TrackRenderPlanBuilder b{graph, *plan, {}, {}, {}, {}, {}, {}, {}, {}, {}};
    b.indexConnections();
    b.createOrderedNodeList();
    if (!b.assignJobs(groups))
        return plan;
    b.allocateSlots(plan->maxBlockSize_);
    b.makeJobs((int)groups.size(), plan->maxBlockSize_);
    for (int i = 0; i < (int)b.ordered.size(); ++i)
        b.makeStep(i);
    if (!b.linkJobs() || !b.outputsInOneJob())
        return plan;
    plan->audioOut_.setSize(plan->maxIoChannels_, plan->maxBlockSize_);
    plan->audioOut_.clear();
    plan->midiOut_.ensureSize(4096);
    return plan;
}

} // namespace synth
