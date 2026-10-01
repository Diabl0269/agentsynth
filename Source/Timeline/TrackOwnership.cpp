#include "Timeline/TrackOwnership.h"
#include <set>

namespace synth {

namespace {

using Adjacency = std::map<juce::String, std::vector<juce::String>>;

constexpr int kNoOwner = -1;

// Index of the single track in `owners`, or kNoOwner when `owners` is empty or names two tracks.
int soleOwner(const std::set<int>& owners) { return owners.size() == 1 ? *owners.begin() : kNoOwner; }

} // namespace

std::map<juce::String, TrackId> resolveTrackOwners(const OwnershipGraph& graph,
                                                   const std::vector<std::pair<TrackId, juce::String>>& trackStarts) {
    Adjacency flowOut, flowNeighbours, modOut;
    std::set<juce::String> hasFlowOut, hasModIn;
    for (const auto& [from, to] : graph.flowEdges) {
        flowOut[from].push_back(to);
        hasFlowOut.insert(from);
    }
    for (const auto& [from, to] : graph.modEdges) {
        modOut[from].push_back(to);
        hasModIn.insert(to);
    }

    // Same test as assignRows: modulation consumers, no modulation input, no signal leaving.
    std::set<juce::String> modulators;
    for (const auto& [id, consumers] : modOut)
        if (!consumers.empty() && hasFlowOut.count(id) == 0 && hasModIn.count(id) == 0)
            modulators.insert(id);

    // Flow neighbours with the modulators cut out of the picture, so a MIDI cable into one never claims it.
    for (const auto& [from, to] : graph.flowEdges) {
        if (modulators.count(from) != 0 || modulators.count(to) != 0)
            continue;
        flowNeighbours[from].push_back(to);
        flowNeighbours[to].push_back(from);
    }

    // 1. Reach per track, then exclusive ownership.
    std::map<juce::String, std::set<int>> reachedBy;
    for (size_t t = 0; t < trackStarts.size(); ++t) {
        const int track = static_cast<int>(t);
        std::vector<juce::String> stack{trackStarts[t].second};
        reachedBy[trackStarts[t].second].insert(track);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            const auto next = flowOut.find(node);
            if (next == flowOut.end())
                continue;
            for (const auto& to : next->second)
                if (modulators.count(to) == 0 && reachedBy[to].insert(track).second)
                    stack.push_back(to);
        }
    }

    std::map<juce::String, int> owner;
    for (const auto& [node, tracks] : reachedBy)
        if (soleOwner(tracks) != kNoOwner)
            owner[node] = soleOwner(tracks);

    // 2. Feeders: unreached nodes adopt the one owner their owned flow neighbours agree on.
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& node : graph.nodes) {
            if (owner.count(node) != 0 || reachedBy.count(node) != 0 || modulators.count(node) != 0)
                continue;
            const auto neighbours = flowNeighbours.find(node);
            if (neighbours == flowNeighbours.end())
                continue;
            std::set<int> owners;
            for (const auto& other : neighbours->second) {
                const auto found = owner.find(other);
                if (found != owner.end())
                    owners.insert(found->second);
            }
            if (soleOwner(owners) != kNoOwner) {
                owner[node] = soleOwner(owners);
                changed = true;
            }
        }
    }

    // 3. Modulators follow what they modulate. Unowned consumers are ignored, as arrange ignores row-less ones.
    for (const auto& node : modulators) {
        std::set<int> owners;
        for (const auto& consumer : modOut[node]) {
            const auto found = owner.find(consumer);
            if (found != owner.end())
                owners.insert(found->second);
        }
        if (soleOwner(owners) != kNoOwner)
            owner[node] = soleOwner(owners);
    }

    std::map<juce::String, TrackId> result;
    for (const auto& [node, track] : owner)
        result[node] = trackStarts[static_cast<size_t>(track)].first;
    return result;
}

} // namespace synth
