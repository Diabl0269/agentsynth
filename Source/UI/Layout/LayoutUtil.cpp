#include "LayoutUtil.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ModuleBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace synth::LayoutUtil {

//==============================================================================
// Module width buckets
//==============================================================================

ModuleWidthBucket getModuleWidthBucket(ModuleType t) {
    switch (t) {
    case ModuleType::Sequencer:
    case ModuleType::PolySequencer:
    case ModuleType::MidiKeyboard:
    // Parametric EQ needs the extra width for a readable response curve plus four band rows of
    // On / Freq / Gain / Q laid out side by side.
    case ModuleType::ParametricEQ:
        return ModuleWidthBucket::Double;
    case ModuleType::Attenuverter:
        return ModuleWidthBucket::Narrow;
    default:
        return ModuleWidthBucket::Single;
    }
}

int moduleWidth(ModuleWidthBucket b) {
    switch (b) {
    case ModuleWidthBucket::Narrow:
        return kNarrowWidth;
    case ModuleWidthBucket::Double:
        return kDoubleWidth;
    default:
        return kSingleWidth;
    }
}

int moduleWidth(ModuleType t) { return moduleWidth(getModuleWidthBucket(t)); }

//==============================================================================
// snap
//==============================================================================
int snap(int v) { return (int)(std::lround(v / (double)kGridSize) * kGridSize); }

juce::Point<int> snap(juce::Point<int> p) { return {snap(p.x), snap(p.y)}; }

//==============================================================================
// intersectsAny
//==============================================================================
bool intersectsAny(const juce::Rectangle<int>& candidate, const std::vector<Box>& others, NodeID selfId, int gap) {
    // Enforce a minimum clear gap of `gap` by inflating ONLY the candidate and testing against the raw
    // other boxes. Inflating BOTH would double the enforced clearance to 2*gap, which wrongly rejects
    // layouts that are intentionally only `gap`+ apart (e.g. preset columns ~20px apart get bumped).
    auto inflated = candidate.expanded(gap);
    for (const auto& box : others) {
        if (box.id == selfId)
            continue;
        if (inflated.intersects(box.rect))
            return true;
    }
    return false;
}

//==============================================================================
// findFreeSlot
//==============================================================================
juce::Point<int> findFreeSlot(juce::Point<int> desired, int w, int h, const std::vector<Box>& others, NodeID selfId,
                              int gap) {
    auto clamp = [&](juce::Point<int> p) -> juce::Point<int> {
        return {juce::jlimit(0, juce::jmax(0, kCanvasMax - w), p.x),
                juce::jlimit(0, juce::jmax(0, kCanvasMax - h), p.y)};
    };

    auto snapped = snap(desired);
    snapped = clamp(snapped);

    auto candidate = juce::Rectangle<int>{snapped.x, snapped.y, w, h};
    if (!intersectsAny(candidate, others, selfId, gap))
        return snapped;

    // Square spiral search around the desired point
    for (int ring = 1; ring <= kSpiralMaxRings; ++ring) {
        int step = kSpiralStep * ring;

        // Walk the perimeter of the ring: top, right, bottom, left sides
        // Top side: y = -step, x from -step to +step
        for (int dx = -step; dx <= step; dx += kSpiralStep) {
            auto pt = clamp(snap(juce::Point<int>{snapped.x + dx, snapped.y - step}));
            auto rect = juce::Rectangle<int>{pt.x, pt.y, w, h};
            if (!intersectsAny(rect, others, selfId, gap))
                return pt;
        }
        // Right side: x = +step, y from -step+kSpiralStep to +step
        for (int dy = -step + kSpiralStep; dy <= step; dy += kSpiralStep) {
            auto pt = clamp(snap(juce::Point<int>{snapped.x + step, snapped.y + dy}));
            auto rect = juce::Rectangle<int>{pt.x, pt.y, w, h};
            if (!intersectsAny(rect, others, selfId, gap))
                return pt;
        }
        // Bottom side: y = +step, x from +step-kSpiralStep to -step
        for (int dx = step - kSpiralStep; dx >= -step; dx -= kSpiralStep) {
            auto pt = clamp(snap(juce::Point<int>{snapped.x + dx, snapped.y + step}));
            auto rect = juce::Rectangle<int>{pt.x, pt.y, w, h};
            if (!intersectsAny(rect, others, selfId, gap))
                return pt;
        }
        // Left side: x = -step, y from +step-kSpiralStep to -step+kSpiralStep
        for (int dy = step - kSpiralStep; dy >= -step + kSpiralStep; dy -= kSpiralStep) {
            auto pt = clamp(snap(juce::Point<int>{snapped.x - step, snapped.y + dy}));
            auto rect = juce::Rectangle<int>{pt.x, pt.y, w, h};
            if (!intersectsAny(rect, others, selfId, gap))
                return pt;
        }
    }

    // Give up: return snapped+clamped desired
    return snapped;
}

//==============================================================================
// resolveOverlapsAfterResize
//==============================================================================
std::vector<ArrangeResult> resolveOverlapsAfterResize(NodeID resizedId, const std::vector<Box>& boxes, int gap) {
    std::vector<Box> working = boxes;

    // Deterministic sweep order: top-to-bottom, then left-to-right, then id. Without a fixed
    // order the same growth could displace a different neighbour run-to-run.
    std::vector<size_t> order;
    order.reserve(working.size());
    for (size_t i = 0; i < working.size(); ++i)
        if (working[i].id != resizedId)
            order.push_back(i);

    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto& ra = working[a].rect;
        const auto& rb = working[b].rect;
        if (ra.getY() != rb.getY())
            return ra.getY() < rb.getY();
        if (ra.getX() != rb.getX())
            return ra.getX() < rb.getX();
        return working[a].id.uid < working[b].id.uid;
    });

    for (int round = 0; round < kResolveMaxRounds; ++round) {
        bool movedAny = false;

        for (size_t idx : order) {
            auto& box = working[idx];
            if (!intersectsAny(box.rect, working, box.id, gap))
                continue;

            // Push straight down past the lowest thing it collides with, then let findFreeSlot
            // settle it on-grid and clear of everything else.
            int pushedY = box.rect.getY();
            auto inflated = box.rect.expanded(gap);
            for (const auto& other : working) {
                if (other.id == box.id)
                    continue;
                if (inflated.intersects(other.rect))
                    pushedY = std::max(pushedY, other.rect.getBottom() + gap);
            }

            auto placed = findFreeSlot({box.rect.getX(), pushedY}, box.rect.getWidth(), box.rect.getHeight(), working,
                                       box.id, gap);
            if (placed != box.rect.getPosition()) {
                box.rect.setPosition(placed);
                movedAny = true;
            }
        }

        if (!movedAny)
            break;
    }

    std::vector<ArrangeResult> moved;
    for (size_t i = 0; i < working.size(); ++i)
        if (working[i].rect.getPosition() != boxes[i].rect.getPosition())
            moved.push_back({working[i].id, working[i].rect.getPosition()});

    return moved;
}

//==============================================================================
// resolveDisplacement
//==============================================================================
namespace {

enum class Dir { Down, Right, Up, Left, None }; // declaration order == tie-break order

struct Push {
    Dir dir = Dir::None;
    int amount = 0; // penetration, before grid snapping
};

int snapUp(int v) { return ((v + kGridSize - 1) / kGridSize) * kGridSize; }

juce::Point<int> deltaFor(Dir d, int amount) {
    const int a = snapUp(amount);
    switch (d) {
    case Dir::Down:
        return {0, a};
    case Dir::Right:
        return {a, 0};
    case Dir::Up:
        return {0, -a};
    case Dir::Left:
        return {-a, 0};
    case Dir::None:
        break;
    }
    return {};
}

// How far `u` must travel in direction `d` to sit `gap` clear of `p`.
int penetration(Dir d, const juce::Rectangle<int>& p, const juce::Rectangle<int>& u, int gap) {
    switch (d) {
    case Dir::Down:
        return p.getBottom() + gap - u.getY();
    case Dir::Right:
        return p.getRight() + gap - u.getX();
    case Dir::Up:
        return u.getBottom() - (p.getY() - gap);
    case Dir::Left:
        return u.getRight() - (p.getX() - gap);
    case Dir::None:
        break;
    }
    return 0;
}

// The wall (canvas top-left) and pinned units both block a destination.
bool blocked(const juce::Rectangle<int>& dest, const std::vector<LayoutUnit>& units, const std::vector<bool>& isMoved,
             const std::vector<juce::Rectangle<int>>& cur, size_t self) {
    if (dest.getX() < 0 || dest.getY() < 0)
        return true;
    for (size_t i = 0; i < units.size(); ++i)
        if (i != self && units[i].pinned && !isMoved[i] && cur[i].intersects(dest))
            return true;
    return false;
}

} // namespace

std::vector<UnitMove> resolveDisplacement(const juce::String& growerKey, const std::vector<LayoutUnit>& units,
                                          int gap) {
    const size_t n = units.size();
    std::vector<juce::Rectangle<int>> cur(n);
    std::vector<bool> isMoved(n, false);
    std::optional<size_t> grower;
    for (size_t i = 0; i < n; ++i) {
        cur[i] = units[i].rect;
        if (units[i].key == growerKey)
            grower = i;
    }
    if (!grower)
        return {};

    struct Pusher {
        size_t idx;
        Dir inherited;
    };
    auto overlaps = [&](size_t pusher, size_t u) { return cur[pusher].expanded(gap).intersects(cur[u]); };

    std::vector<Pusher> frontier{{*grower, Dir::None}};
    int rounds = 0;
    bool regrow = true;
    while (regrow && rounds < kDisplacementMaxRounds) {
        regrow = false;
        while (!frontier.empty() && rounds++ < kDisplacementMaxRounds) {
            std::vector<Pusher> next;
            for (const auto& pusher : frontier) {
                // Everything this pusher currently overlaps, most-penetrated first (deterministic ties).
                struct Hit {
                    size_t idx;
                    int pen;
                };
                std::vector<Hit> hits;
                for (size_t u = 0; u < n; ++u) {
                    if (u == pusher.idx || u == *grower || units[u].pinned || !overlaps(pusher.idx, u))
                        continue;
                    int least = std::numeric_limits<int>::max();
                    for (auto d : {Dir::Down, Dir::Right, Dir::Up, Dir::Left})
                        least = std::min(least, penetration(d, cur[pusher.idx], cur[u], gap));
                    hits.push_back({u, least});
                }
                std::sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
                    if (a.pen != b.pen)
                        return a.pen > b.pen;
                    if (cur[a.idx].getY() != cur[b.idx].getY())
                        return cur[a.idx].getY() < cur[b.idx].getY();
                    if (cur[a.idx].getX() != cur[b.idx].getX())
                        return cur[a.idx].getX() < cur[b.idx].getX();
                    return units[a.idx].key < units[b.idx].key;
                });

                for (const auto& hit : hits) {
                    const size_t u = hit.idx;
                    if (!overlaps(pusher.idx, u))
                        continue; // an earlier push in this pass already cleared it
                    auto canGo = [&](Dir d) {
                        const int pen = penetration(d, cur[pusher.idx], cur[u], gap);
                        return pen > 0 && !blocked(cur[u].translated(deltaFor(d, pen).x, deltaFor(d, pen).y), units,
                                                   isMoved, cur, u);
                    };

                    Dir chosen = Dir::None;
                    if (pusher.inherited != Dir::None) {
                        if (canGo(pusher.inherited))
                            chosen = pusher.inherited;
                    } else {
                        Dir best = Dir::None;
                        int bestPen = std::numeric_limits<int>::max();
                        for (auto d : {Dir::Down, Dir::Right, Dir::Up, Dir::Left}) {
                            const int pen = penetration(d, cur[pusher.idx], cur[u], gap);
                            if (pen < bestPen) {
                                bestPen = pen;
                                best = d;
                            }
                        }
                        if (canGo(best))
                            chosen = best;
                    }
                    if (chosen == Dir::None) {
                        // Blocked: the smaller of the two positive directions (tie: down).
                        const int penD = penetration(Dir::Down, cur[pusher.idx], cur[u], gap);
                        const int penR = penetration(Dir::Right, cur[pusher.idx], cur[u], gap);
                        const std::array<Dir, 2> order = penD <= penR ? std::array<Dir, 2>{Dir::Down, Dir::Right}
                                                                      : std::array<Dir, 2>{Dir::Right, Dir::Down};
                        for (auto d : order)
                            if (canGo(d)) {
                                chosen = d;
                                break;
                            }
                    }
                    if (chosen == Dir::None)
                        continue; // boxed in on both sides: leave it

                    const auto delta = deltaFor(chosen, penetration(chosen, cur[pusher.idx], cur[u], gap));
                    cur[u].translate(delta.x, delta.y);
                    isMoved[u] = true;
                    next.push_back({u, chosen});
                }
            }
            frontier = std::move(next);
        }

        // A pushed unit may have been carried back over the grower: run the grower again until it is clear.
        for (size_t u = 0; u < n; ++u)
            if (u != *grower && !units[u].pinned && overlaps(*grower, u) && isMoved[u]) {
                regrow = true;
                break;
            }
        if (regrow)
            frontier = {{*grower, Dir::None}};
    }

    std::vector<UnitMove> moves;
    for (size_t i = 0; i < n; ++i) {
        const auto delta = cur[i].getPosition() - units[i].rect.getPosition();
        if (isMoved[i] && (delta.x != 0 || delta.y != 0))
            moves.push_back({units[i].key, delta});
    }
    return moves;
}

//==============================================================================
// computeAutoArrange
//==============================================================================
namespace {

// Role rank for intra-layer ordering: lower == appears first (top of column)
int roleRank(ModuleType t) {
    switch (t) {
    case ModuleType::Oscillator:
    case ModuleType::Sequencer:
    case ModuleType::PolySequencer:
    case ModuleType::MidiKeyboard:
    case ModuleType::PolyMidi:
    case ModuleType::ExternalMidi:
    case ModuleType::Noise:
    case ModuleType::Sampler:
    case ModuleType::Wavetable:
        return 0;
    case ModuleType::Filter:
    case ModuleType::ParametricEQ:
    case ModuleType::VCA:
    case ModuleType::VoiceMixer:
        return 1;
    case ModuleType::Delay:
    case ModuleType::Distortion:
    case ModuleType::Reverb:
    case ModuleType::Chorus:
    case ModuleType::Phaser:
    case ModuleType::Compressor:
    case ModuleType::Flanger:
    case ModuleType::Limiter:
    case ModuleType::Gate:
    case ModuleType::PitchShifter:
    case ModuleType::RingModulator:
        return 2;
    case ModuleType::ADSR:
    case ModuleType::LFO:
    case ModuleType::Math:
    case ModuleType::MacroControl:
    case ModuleType::SampleHold:
    case ModuleType::Comparator:
    case ModuleType::EnvelopeFollower:
        return 3;
    default:
        return 4;
    }
}

} // anonymous namespace

std::vector<ArrangeResult> computeAutoArrange(juce::AudioProcessorGraph& graph,
                                              const std::function<juce::Point<int>(NodeID)>& sizeOf,
                                              const std::vector<std::pair<NodeID, NodeID>>& extraEdges) {
    // ---- 1. Collect arrangeable nodes ----
    std::vector<NodeID> nodeIds;
    NodeID audioOutputId{};
    bool hasAudioOutput = false;

    for (auto* node : graph.getNodes()) {
        if (auto* ioProc = dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(node->getProcessor())) {
            // Include Audio Input and Audio Output IO nodes
            if (ioProc->getType() == juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode ||
                ioProc->getType() == juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode) {
                nodeIds.push_back(node->nodeID);
                if (ioProc->getType() == juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode) {
                    audioOutputId = node->nodeID;
                    hasAudioOutput = true;
                }
            }
        } else if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            // Skip AttenuverterModule
            if (dynamic_cast<AttenuverterModule*>(mb) == nullptr)
                nodeIds.push_back(node->nodeID);
        }
    }

    if (nodeIds.empty())
        return {};

    // Build a fast lookup set
    std::unordered_set<uint32_t> nodeSet;
    for (auto id : nodeIds)
        nodeSet.insert(id.uid);

    // ---- 2. Build directed edges (deduped, no self-loops) ----
    // Adjacency: src -> list of dsts
    std::unordered_map<uint32_t, std::vector<NodeID>> adj;
    std::unordered_map<uint32_t, int> indegree;
    for (auto id : nodeIds) {
        adj[id.uid]; // ensure entry exists
        indegree[id.uid] = 0;
    }

    // Track edges to deduplicate
    std::unordered_set<uint64_t> edgeSet;

    auto addEdge = [&](NodeID src, NodeID dst) {
        if (src.uid == dst.uid)
            return; // no self-loops
        if (nodeSet.find(src.uid) == nodeSet.end() || nodeSet.find(dst.uid) == nodeSet.end())
            return; // skip edges to/from excluded nodes (attenuverters)
        uint64_t key = ((uint64_t)src.uid << 32) | (uint64_t)dst.uid;
        if (edgeSet.insert(key).second) {
            adj[src.uid].push_back(dst);
            indegree[dst.uid]++;
        }
    };

    // From graph connections (skipping edges touching attenuverter nodes)
    for (const auto& conn : graph.getConnections()) {
        addEdge(conn.source.nodeID, conn.destination.nodeID);
    }

    // From extra edges (modulation routing logical endpoints)
    for (const auto& [src, dst] : extraEdges) {
        addEdge(src, dst);
    }

    // ---- 3. Kahn topological sort + longest-path depth ----
    std::unordered_map<uint32_t, int> depth;
    for (auto id : nodeIds)
        depth[id.uid] = 0;

    // Kahn's algorithm with cycle-break: process zero-indegree nodes first,
    // break cycles by skipping back-edges to already-visited nodes.
    std::unordered_set<uint32_t> visited;
    std::vector<NodeID> topoOrder;
    topoOrder.reserve(nodeIds.size());

    // Use a copy of indegree for the Kahn queue
    std::unordered_map<uint32_t, int> indegCopy = indegree;

    std::vector<NodeID> queue;
    for (auto id : nodeIds) {
        if (indegCopy[id.uid] == 0)
            queue.push_back(id);
    }

    while (!queue.empty()) {
        // Pick the next node (stable: sort by uid for determinism among zero-indegree)
        std::sort(queue.begin(), queue.end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
        NodeID cur = queue.front();
        queue.erase(queue.begin());

        if (visited.count(cur.uid))
            continue;
        visited.insert(cur.uid);
        topoOrder.push_back(cur);

        for (auto dst : adj[cur.uid]) {
            if (visited.count(dst.uid))
                continue; // back-edge: skip (cycle-break)
            // Update longest-path depth
            depth[dst.uid] = std::max(depth[dst.uid], depth[cur.uid] + 1);
            indegCopy[dst.uid]--;
            if (indegCopy[dst.uid] == 0)
                queue.push_back(dst);
        }
    }

    // Handle any nodes not reached (part of a cycle): append them at current depth
    for (auto id : nodeIds) {
        if (!visited.count(id.uid))
            topoOrder.push_back(id);
    }

    // ---- Force Audio Output to last layer ----
    int maxDepth = 0;
    for (auto& [uid, d] : depth)
        maxDepth = std::max(maxDepth, d);

    if (hasAudioOutput)
        depth[audioOutputId.uid] = maxDepth;

    // ---- 4. Group nodes by depth ----
    std::unordered_map<int, std::vector<NodeID>> layers;
    for (auto id : nodeIds)
        layers[depth[id.uid]].push_back(id);

    // ---- 5. Intra-layer stable order: role rank then UID ----
    for (auto& [d, ids] : layers) {
        std::stable_sort(ids.begin(), ids.end(), [&](NodeID a, NodeID b) {
            // Get module type for rank
            auto getRank = [&](NodeID nid) -> int {
                auto* node = graph.getNodeForId(nid);
                if (!node)
                    return 99;
                if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
                    return roleRank(mb->getModuleType());
                return 50; // IO nodes go in middle
            };
            int ra = getRank(a);
            int rb = getRank(b);
            if (ra != rb)
                return ra < rb;
            return a.uid < b.uid;
        });
    }

    // ---- 6. Assign coordinates ----
    std::vector<ArrangeResult> results;
    results.reserve(nodeIds.size());

    int x = kArrangeOriginX;
    for (int d = 0; d <= maxDepth; ++d) {
        auto it = layers.find(d);
        if (it == layers.end())
            continue;

        const auto& ids = it->second;

        // Determine layer width = max module width in this layer
        int layerWidth = 0;
        for (auto id : ids) {
            int w = sizeOf(id).x;
            layerWidth = std::max(layerWidth, w);
        }
        if (layerWidth == 0)
            layerWidth = 280;

        int y = kArrangeOriginY;
        for (auto id : ids) {
            auto sz = sizeOf(id);
            int w = sz.x;
            int h = sz.y;

            int nodeX = x + (layerWidth - w) / 2;
            auto snappedPos = snap(juce::Point<int>{nodeX, y});
            // Clamp to canvas bounds
            snappedPos.x = juce::jlimit(0, juce::jmax(0, kCanvasMax - w), snappedPos.x);
            snappedPos.y = juce::jlimit(0, juce::jmax(0, kCanvasMax - h), snappedPos.y);

            results.push_back({id, snappedPos});
            y += h + kIntraLayerGapY;
        }

        x += layerWidth + kLayerGapX;
    }

    return results;
}

} // namespace synth::LayoutUtil
