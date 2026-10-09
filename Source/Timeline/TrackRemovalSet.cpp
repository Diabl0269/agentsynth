#include "TrackRemovalSet.h"

#include <map>

namespace synth {

namespace {

using Neighbours = std::map<std::uint32_t, std::set<std::uint32_t>>;

// The largest set of modules, outside `gone` and `keep`, whose every cable ends in `gone` or in the set itself, and
// that touches `gone` somewhere. Start from every module that could go, then drop any with a cable to something that
// stays, until nothing changes: each drop can only cost its neighbours.
std::set<std::uint32_t> closedAround(const Neighbours& neighbours, const std::set<std::uint32_t>& gone,
                                     const std::set<std::uint32_t>& keep) {
    std::set<std::uint32_t> candidates;
    for (const auto& [node, ignored] : neighbours)
        if (gone.count(node) == 0 && keep.count(node) == 0)
            candidates.insert(node);
    for (bool dropped = true; dropped;) {
        dropped = false;
        for (auto it = candidates.begin(); it != candidates.end();) {
            bool allInside = true;
            for (auto other : neighbours.at(*it))
                allInside = allInside && (gone.count(other) != 0 || candidates.count(other) != 0);
            if (allInside) {
                ++it;
            } else {
                it = candidates.erase(it);
                dropped = true;
            }
        }
    }

    // A cluster that never touches `gone` (two modules wired only to each other) is not this track's.
    std::set<std::uint32_t> result;
    std::set<std::uint32_t> visited;
    for (auto start : candidates) {
        if (visited.count(start) != 0)
            continue;
        std::vector<std::uint32_t> cluster{start};
        bool touchesGone = false;
        visited.insert(start);
        for (std::size_t i = 0; i < cluster.size(); ++i)
            for (auto other : neighbours.at(cluster[i])) {
                if (gone.count(other) != 0)
                    touchesGone = true;
                else if (visited.insert(other).second)
                    cluster.push_back(other);
            }
        if (touchesGone)
            result.insert(cluster.begin(), cluster.end());
    }
    return result;
}

} // namespace

std::set<std::uint32_t> modulesOnlyUsedBy(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& cables,
                                          const std::set<std::uint32_t>& seed, const std::set<std::uint32_t>& keep,
                                          const std::set<std::uint32_t>& relays) {
    Neighbours neighbours;
    std::map<std::uint32_t, std::set<std::uint32_t>> feeds; // node -> what it feeds
    for (const auto& [from, to] : cables) {
        if (from == to)
            continue;
        neighbours[from].insert(to);
        neighbours[to].insert(from);
        feeds[from].insert(to);
    }

    // Removing modules can leave a relay feeding nothing that stays, and a relay going can free its source to follow:
    // alternate the two until neither adds anything.
    std::set<std::uint32_t> gone = seed;
    std::set<std::uint32_t> result;
    for (bool grew = true; grew;) {
        grew = false;
        result = closedAround(neighbours, gone, keep);
        gone.insert(result.begin(), result.end());
        for (auto relay : relays) {
            const auto it = feeds.find(relay);
            if (gone.count(relay) != 0 || keep.count(relay) != 0 || it == feeds.end())
                continue;
            bool feedsOnlyGone = true;
            for (auto dest : it->second)
                feedsOnlyGone = feedsOnlyGone && gone.count(dest) != 0;
            if (feedsOnlyGone) {
                gone.insert(relay);
                grew = true;
            }
        }
    }
    for (auto node : seed)
        gone.erase(node);
    return gone;
}

} // namespace synth
