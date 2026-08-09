#pragma once
#include "../Modules/ModuleBase.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth::LayoutUtil {

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
// ---- Module width buckets ----
inline constexpr int kNarrowWidth = 40;  // Attenuverter
inline constexpr int kSingleWidth = 280; // standard module
inline constexpr int kDoubleWidth = 560; // Sequencer / PolySequencer / MidiKeyboard (= 2 × kSingleWidth)
// Note: kColumnStride = kSingleWidth + kLayerGapX = 280 + 80 = 360 (no duplicate constant needed)

// ---- Macro Control bank geometry ----
// The Macro bank is the one module whose footprint changes at runtime (its "Knobs" parameter
// picks how many macros are exposed). Its geometry lives here, not in ModuleComponent, so the
// three places that must agree on it — the component layout, the output-jack hit test, and the
// drag-preview size estimate — all read the same numbers, and so the growth maths stays
// headless-testable.
inline constexpr int kMacroHeaderH = 94;   // title bar + the Knobs / Bipolar row
inline constexpr int kMacroRowH = 44;      // one macro knob and its output jack
inline constexpr int kMacroBottomPad = 12; // padding below the last row

// Total component height for a bank showing `count` macros.
inline constexpr int macroBankHeight(int count) { return kMacroHeaderH + count * kMacroRowH + kMacroBottomPad; }

// Vertical centre of macro row `index` — where both the knob and its output jack sit.
inline constexpr int macroRowCentreY(int index) { return kMacroHeaderH + index * kMacroRowH + kMacroRowH / 2; }

enum class ModuleWidthBucket { Narrow, Single, Double };

// Maps a ModuleType to its width bucket.
ModuleWidthBucket getModuleWidthBucket(ModuleType t);

// Returns the pixel width for a given bucket or module type.
int moduleWidth(ModuleWidthBucket b);
int moduleWidth(ModuleType t);

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

// A module has just changed footprint in place (only the Macro bank does this today, when its
// "Knobs" count changes). `boxes` is every module box INCLUDING the resized one, already carrying
// its new rect. Returns the new top-left for each OTHER box that had to move to stay clear —
// boxes that do not move are not returned, so an empty result means the growth fitted as-is.
//
// The resized module never moves: it is the one the user is interacting with, and teleporting it
// out from under the cursor is worse than nudging its neighbours. Displaced boxes are pushed
// straight down past whatever they collided with and then run through findFreeSlot, so the result
// is on-grid and gap-respecting. Deterministic: boxes are processed top-to-bottom, then
// left-to-right, then by id, and the cascade is capped at kResolveMaxRounds passes.
inline constexpr int kResolveMaxRounds = 4;

std::vector<ArrangeResult> resolveOverlapsAfterResize(NodeID resizedId, const std::vector<Box>& boxes,
                                                      int gap = kCollisionGap);

// Topological signal-flow layout. sizeOf returns (w,h) footprint for a node id. extraEdges carries
// modulation routing edges (src->dst) so envelope->VCA etc. influence layering depth.
std::vector<ArrangeResult> computeAutoArrange(juce::AudioProcessorGraph& graph,
                                              const std::function<juce::Point<int>(NodeID)>& sizeOf,
                                              const std::vector<std::pair<NodeID, NodeID>>& extraEdges);

} // namespace synth::LayoutUtil
