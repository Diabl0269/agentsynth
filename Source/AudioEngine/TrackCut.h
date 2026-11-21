#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <utility>
#include <vector>

namespace synth {

/** One unit of the read-only track cut: every node that feeds a single Channel Strip (and the strip), or the nodes
 *  that feed more than one strip (Shared), or the ones no strip is downstream of (Post). */
struct TrackJob {
    enum class Kind { Strip, Shared, Post };
    Kind kind = Kind::Post;
    juce::AudioProcessorGraph::NodeID strip{}; // the strip node of a Strip job; unused otherwise
    std::vector<juce::AudioProcessorGraph::NodeID> nodes;
};

/** The graph cut into jobs by Channel Strip: every node lands in exactly one job. `edges` are job index pairs
 *  (from, to), one per ordered pair that a cable joins; `order` lists the jobs so each follows every job it reads
 *  from, and is empty when the job graph has a cycle (`acyclic` false). */
struct TrackCut {
    std::vector<TrackJob> jobs;
    std::vector<std::pair<int, int>> edges;
    bool acyclic = true;
    std::vector<int> order;
};

/** Cuts `graph` into track jobs. Message thread only; nothing in the render calls it. */
TrackCut cutIntoTrackJobs(const juce::AudioProcessorGraph& graph);

} // namespace synth
