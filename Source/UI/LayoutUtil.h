#pragma once
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace gsynth::LayoutUtil {

// ---- Constants (canvas pixels) ----
inline constexpr int kGridSize = 8;         // snap quantum
inline constexpr int kCollisionGap = 12;    // min clear gap enforced between module bounding boxes
inline constexpr int kSpiralStep = 8;       // spiral ring step (== kGridSize so results stay on-grid)
inline constexpr int kSpiralMaxRings = 256; // hard cap; 256*8 = 2048px search radius before giving up
inline constexpr int kCanvasMax = 10000;
// ---- Auto-arrange spacing ----
inline constexpr int kLayerGapX = 80;      // horizontal gap between adjacent layer columns
inline constexpr int kIntraLayerGapY = 40; // vertical gap between stacked modules in the same layer
inline constexpr int kArrangeOriginX = 40; // left margin where layer 0 starts
inline constexpr int kArrangeOriginY = 40; // top margin where each layer column starts

using NodeID = juce::AudioProcessorGraph::NodeID;

int snap(int v); // round-to-nearest grid multiple, negative-safe
juce::Point<int> snap(juce::Point<int> p);

struct Box {
    NodeID id;
    juce::Rectangle<int> rect;
};

// True if candidate (inflated test against each other box inflated by gap) intersects any box in
// others except the one whose id == selfId.
bool intersectsAny(const juce::Rectangle<int>& candidate, const std::vector<Box>& others, NodeID selfId,
                   int gap = kCollisionGap);

// Starting at desired (top-left, snap it inside), find nearest snapped top-left whose (w x h) box does
// not intersect any others (inflated by gap). Returns desired-snapped if already clear. Square spiral on
// grid, step=kSpiralStep, up to kSpiralMaxRings rings. Clamp to [0, kCanvasMax-w] x [0, kCanvasMax-h].
juce::Point<int> findFreeSlot(juce::Point<int> desired, int w, int h, const std::vector<Box>& others, NodeID selfId,
                              int gap = kCollisionGap);

struct ArrangeResult {
    NodeID id;
    juce::Point<int> pos;
};

// Topological signal-flow layout. sizeOf returns (w,h) footprint for a node id. extraEdges carries
// modulation routing edges (src->dst) so envelope->VCA etc. influence layering depth.
std::vector<ArrangeResult> computeAutoArrange(juce::AudioProcessorGraph& graph,
                                              const std::function<juce::Point<int>(NodeID)>& sizeOf,
                                              const std::vector<std::pair<NodeID, NodeID>>& extraEdges);

} // namespace gsynth::LayoutUtil
