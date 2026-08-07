#include "LayoutUtil.h"
#include "../Modules/AttenuverterModule.h"
#include "../Modules/ModuleBase.h"
#include <algorithm>
#include <cmath>
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
    case ModuleType::Bitcrusher:
    case ModuleType::PitchShifter:
        return 2;
    case ModuleType::ADSR:
    case ModuleType::LFO:
    case ModuleType::MacroControl:
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
