#pragma once
#include "Modules/ModuleBase.h"
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
inline constexpr int kLayerGapX = 80;          // horizontal gap between adjacent layer columns
inline constexpr int kIntraLayerGapY = 40;     // vertical gap between stacked modules in the same layer
inline constexpr int kArrangeOriginX = 40;     // left margin where layer 0 starts
inline constexpr int kArrangeOriginY = 40;     // top margin where each layer column starts
inline constexpr int kOutputDockCardGapX = 40; // gap between neighbouring output-dock cards (Master, Rec Tap, Output)
// Horizontal reach of an OPEN macro's hull past its member union, on each side: the hull margin (14) plus the
// fixed port strip (96). Anything that must sit clear of an open hull (a track head's x floor, Master right of a
// channel's Strip) budgets this. MacroGroupControllerGeometry.cpp static_asserts it equals margin + strip width.
inline constexpr int kMacroHullSideOutset = 110;
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

// ---- Making room when something grows (docs/layout/layout.md#making-room-when-something-grows) ----
//
// One sibling on the canvas (a loose module, or a whole macro as ONE rigid unit) has grown; every
// unit it now overlaps is pushed clear, and pushed units push what they land on in the same direction.
struct LayoutUnit {
    juce::String key;
    juce::Rectangle<int> rect;
    bool pinned = false; // never moves, and pushed units are redirected around it
};

struct UnitMove {
    juce::String key;
    juce::Point<int> delta;
};

// Backstop on cascade rounds (kResolveMaxRounds above belongs to the older single-resize sweep). Displacements
// only ever grow, so the cascade settles well before this.
inline constexpr int kDisplacementMaxRounds = 64;

// Pure and deterministic. The grower never moves. Returns only units whose delta is non-zero; a unit blocked on
// every side (canvas wall, pinned neighbours) is left where it is. Deltas are multiples of kGridSize.
std::vector<UnitMove> resolveDisplacement(const juce::String& growerKey, const std::vector<LayoutUnit>& units,
                                          int gap = kCollisionGap);

// The output dock (Master, Rec Tap, Audio Output -- always the rightmost cards). Pure: `content` is every layout
// unit that is NOT a dock card, `dockSizes` the dock cards' (w,h) in chain order, `dockTopY` the shared top y.
// Returns one snapped top-left per dock card, in the same order: left edge = snapUp(rightmost content edge) +
// kLayerGapX (kArrangeOriginX when there is no content), cards kOutputDockCardGapX apart, all on snap(dockTopY).
std::vector<juce::Point<int>> computeOutputDock(const std::vector<LayoutUnit>& content,
                                                const std::vector<juce::Point<int>>& dockSizes, int dockTopY);

} // namespace synth::LayoutUtil
