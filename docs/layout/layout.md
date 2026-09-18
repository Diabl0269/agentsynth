# Canvas Layout

`Source/UI/Layout/LayoutUtil.h` / `.cpp` (`synth::LayoutUtil`) is the headless, JUCE-GUI-free
geometry behind every module placement on the patch canvas: the grid a module snaps to, the search
that keeps two cards from overlapping, the topological auto-arrange, and the ghost drawn while a
drag is in flight.

This folder holds the rest of the editor's presentation layer:

- [chrome](chrome.md) — toolbar, status bar, the dockable panels and the mod-matrix overlay
- [module-card](module-card.md) — a module card's own geometry, header buttons and width buckets
- [module-library](module-library.md) — the library sidebar's rows, folds, search and help popover
- [preset-positions](preset-positions.md) — where factory presets place their modules
- [selection](selection.md) · [snippets-clipboard](snippets-clipboard.md) ·
  [macro-cards](macro-cards.md) — multi-select, group drag, snippets, macro containers
- [cables](cables.md) · [smart-connections](smart-connections.md) · [minimap](minimap.md) —
  wires, the suggestions offered while dragging, the overview map
- [rendering](rendering.md) · [animation](animation.md) · [visualizers](visualizers.md) — repaint
  discipline, motion, in-card signal displays
- [theming](theming.md) · [theme-authoring](theme-authoring.md) · [icons](icons.md) ·
  [colour-overrides](colour-overrides.md) — tokens, user themes, SVG icons, user colour overrides

## The soft grid

Modules are free-form — placeable anywhere on the 10000x10000 canvas — but two rules are always
enforced:

1. **Snap on drag-release.** A module's top-left corner rounds to the nearest `kGridSize` (8 px)
   multiple when the drag ends.
2. **Anti-overlap on drop and drag-release.** A module landing on top of another is moved to the
   nearest clear slot found by the spiral search below.

Both fire at the moment of release, never on a drag tick. Live-tick snapping stair-steps the
position and fights the card's buffered-image compositing.

**Why the quantum is 8 px.** Port jacks sit 20 px apart vertically and the card header is 30 px
high; 8 is the smallest quantum that feels snappy and invisible, where a coarser 16 or 20 can
misalign jack-to-jack connections visually at high zoom. Every auto-arrange spacing constant is a
multiple of 8, so an arranged module lands on-grid with no rounding residual, and the spiral step
equals the grid size, so an anti-overlap placement is grid-aligned by construction. The standard
280 px card width is 35 x 8, so a row of standard cards tiles flush.

## Canvas coordinates

All layout and collision logic runs in **canvas coordinates** — the `content` child component,
bounded `0, 0, 10000, 10000`. The zoom/pan transform lives on `content.setTransform(...)` and is
invisible to `LayoutUtil`. Never pass screen-space coordinates into a layout function.

Module positions persist on the JUCE audio-graph node's property bag as integer `"x"` and `"y"`
keys. `GraphEditor::updateComponents()` reconciles those back to `setTopLeftPosition(x, y)` after
every state change (preset load, undo/redo, auto-arrange).

## Anti-overlap search

When a module is placed (library drop or drag-release) the engine:

1. Snaps the desired top-left to the nearest grid multiple.
2. Tests the snapped rectangle, at the module's real pixel dimensions, against every other module
   with a minimum clear gap of `kCollisionGap` = 12 px between bounding boxes.
3. Places the module there if the slot is clear.
4. Otherwise walks an **expanding square spiral** outward from the desired position, testing each
   grid-aligned candidate, until it finds a clear slot or exhausts `kSpiralMaxRings` = 256 rings
   (256 x 8 = a 2048 px search radius). With no clear slot inside that radius the snapped-desired
   position is returned as a fallback — always a valid on-canvas coordinate.

The spiral visits positions in ring order, innermost first, so the module lands as close to the
intended drop point as possible.

Two boxes A and B collide at gap `g` when `A.inflated(g/2).intersects(B.inflated(g/2))`, which is
equivalent to requiring at least `g` px of clear space on all sides between the edges. The `selfId`
parameter excludes a module's own box from the occupied set — used during a drag so a module does
not collide with its own pre-drag position.

## Auto-arrange

`GraphEditor::autoArrange()` (Cmd+L, or the toolbar button) rearranges every visible module into a
left-to-right topological signal-flow layout in one undo step.

**Collect arrangeable nodes.** Every graph node whose processor is a `ModuleBase` subclass (which
includes Audio Input), plus the `AudioGraphIOProcessor` IO nodes — Audio Output and, in a patch
still holding one, a raw `audioInputNode`. `AttenuverterModule` nodes are skipped entirely: they
are implementation details of the modulation graph and never appear as visible cards.

**Build directed edges.** Edges come from `AudioProcessorGraph::getConnections()`, skipping any
edge touching an `AttenuverterModule` node. Modulation routings from
`AudioEngine::getModulationRoutings()` (source to destination, collapsing attenuverter chains to
logical endpoints) are added as `extraEdges`. Duplicate edges and self-loops are removed.

**Assign depth by longest path.** A Kahn-style topological traversal gives each node a depth equal
to the length of its longest incoming path; nodes with no incoming edges get depth 0. Any cycle is
broken by ignoring back-edges to already-visited nodes, so the traversal always terminates. The
Audio Output IO node is forced to the maximum depth, the rightmost column.

**Group into layers.** All nodes at one depth form a column. Within a column, nodes sort by role
rank for a stable, readable ordering, then by node UID for full determinism across runs:

| Rank | Module types |
|------|-------------|
| 0 | Oscillator, Sequencer, MIDI Keyboard |
| 1 | Filter, VCA |
| 2 | FX modules (Delay, Reverb, Distortion, ...) |
| 3 | ADSR, LFO |

**Assign pixel coordinates.**

```
x = kArrangeOriginX          // 40 px left margin
for d in 0..maxDepth:
    layerWidth = max(sizeOf(n).x for n in layers[d])
    y = kArrangeOriginY      // 40 px top margin per column
    for n in layers[d]:
        nodeX = x + (layerWidth - w) / 2   // centre narrow cards in a wide column
        emit { n, snap({nodeX, y}) }
        y += h + kIntraLayerGapY            // 40 px between cards
    x += layerWidth + kLayerGapX            // 80 px between columns
```

Every emitted position passes through `snap()` and is clamped to
`[0, kCanvasMax - w] x [0, kCanvasMax - h]`.

| Constant | Value | Meaning |
|----------|-------|---------|
| `kLayerGapX` | 80 px | Horizontal gap between adjacent layer columns |
| `kIntraLayerGapY` | 40 px | Vertical gap between stacked modules in one layer |
| `kArrangeOriginX` | 40 px | Left margin — where the first layer column starts |
| `kArrangeOriginY` | 40 px | Top margin — where each column's first module starts |

All four are multiples of `kGridSize`, so arranged positions are always on-grid.

The column advance for single-width modules works out to `kSingleWidth + kLayerGapX` = 360 px; it
is a consequence of the algorithm, not a named constant. A double-width module advances the cursor
by `kDoubleWidth + kLayerGapX` = 640 px, because `sizeOf` reports its real 560 px width and
`layerWidth` follows — see [module-card](module-card.md#width-buckets).

`autoArrange()` wraps every position write in one undo snapshot:
`undoManager->captureBeforeState(graph)` before computing the layout,
`undoManager->pushSnapshotFromCapture(graph)` after `updateComponents()`. One Cmd+Z restores every
module to its pre-arrange position.

## LayoutUtil API

No JUCE GUI component dependencies; testable headlessly.

```cpp
namespace synth::LayoutUtil {

inline constexpr int kGridSize       = 8;    // snap quantum
inline constexpr int kCollisionGap   = 12;   // minimum clear gap between bounding boxes (px)
inline constexpr int kSpiralStep     = 8;    // spiral ring step — equals kGridSize
inline constexpr int kSpiralMaxRings = 256;  // hard cap: 256*8 = 2048 px search radius
inline constexpr int kCanvasMax      = 10000;
inline constexpr int kLayerGapX      = 80;
inline constexpr int kIntraLayerGapY = 40;
inline constexpr int kArrangeOriginX = 40;
inline constexpr int kArrangeOriginY = 40;

// Module width buckets — see module-card.md
inline constexpr int kNarrowWidth  = 40;   // Attenuverter
inline constexpr int kSingleWidth  = 280;  // standard module
inline constexpr int kDoubleWidth  = 560;  // Sequencer / PolySequencer / MidiKeyboard

enum class ModuleWidthBucket { Narrow, Single, Double };
ModuleWidthBucket getModuleWidthBucket(ModuleType t);  // in LayoutUtil.cpp; caller needs ModuleBase.h
int moduleWidth(ModuleWidthBucket b);
int moduleWidth(ModuleType t);

} // namespace synth::LayoutUtil
```

**`snap`**

```cpp
int snap(int v);
juce::Point<int> snap(juce::Point<int> p);
```

Rounds `v` to the nearest multiple of `kGridSize`. Negative-safe (uses `std::lround`). The point
overload snaps both components independently.

**`Box`**

```cpp
struct Box {
    NodeID              id;
    juce::Rectangle<int> rect;
};
```

One occupied slot: the JUCE graph `NodeID` plus the module's pixel bounding rectangle in canvas
coordinates. Build this list from the live module components for collision testing.

**`intersectsAny`**

```cpp
bool intersectsAny(const juce::Rectangle<int>& candidate,
                   const std::vector<Box>& others,
                   NodeID selfId,
                   int gap = kCollisionGap);
```

True when `candidate`, inflated by `gap`, overlaps any box in `others` whose `id` is not `selfId`.
Pass the dragged module's own `NodeID` as `selfId` to exclude it from its own collision set.

**`findFreeSlot`**

```cpp
juce::Point<int> findFreeSlot(juce::Point<int> desired, int w, int h,
                              const std::vector<Box>& others,
                              NodeID selfId,
                              int gap = kCollisionGap);
```

Finds the nearest grid-aligned top-left where a `w x h` box does not overlap any entry in `others`
(excluding `selfId`). Starts at `snap(desired)`, then walks the expanding spiral. Returns
`snap(desired)`, clamped to the canvas, when no clear slot is found inside the search radius — it
never returns an out-of-bounds position.

**`resolveOverlapsAfterResize`**

```cpp
inline constexpr int kResolveMaxRounds = 4;

std::vector<ArrangeResult>
resolveOverlapsAfterResize(NodeID resizedId,
                           const std::vector<Box>& boxes,
                           int gap = kCollisionGap);
```

Called after a module changes footprint in place. `boxes` is every module box **including** the
resized one, already carrying its new rect. Returns a new top-left for each *other* box that had to
move; boxes that stayed put are not returned, so an empty result means the new footprint fitted
as-is.

The resized module is never returned and never moves. Displaced boxes are pushed straight down past
the lowest thing they collided with, then run through `findFreeSlot`, so results are on-grid and
gap-respecting. The sweep is deterministic (top-to-bottom, then left-to-right, then id) and the
cascade is capped at `kResolveMaxRounds` passes.

**`computeAutoArrange`**

```cpp
struct ArrangeResult { NodeID id; juce::Point<int> pos; };

std::vector<ArrangeResult>
computeAutoArrange(juce::AudioProcessorGraph& graph,
                   const std::function<juce::Point<int>(NodeID)>& sizeOf,
                   const std::vector<std::pair<NodeID, NodeID>>& extraEdges);
```

Computes the topological layout above and returns one `ArrangeResult` per arrangeable node
(`AttenuverterModule` nodes excluded). The caller writes `pos.x` / `pos.y` back to
`node->properties` and calls `updateComponents()`.

`sizeOf` returns the pixel footprint `{width, height}` for a `NodeID`, typically backed by the live
`ModuleComponent` dimensions; it falls back to `{kSingleWidth, 300}` for a node with no visible
component. `extraEdges` carries additional directed edges (for example from
`getModulationRoutings()`) merged into the graph edge set before depths are computed, so an
envelope-to-VCA modulation influences column ordering even though the attenuverter nodes it passes
through are excluded from the arrangeable set.

## Drag affordance

During any module drag — moving an existing module, or dragging one in from the library sidebar —
two cues are drawn over the canvas so placement is predictable.

**Grid dots.** Subtle dots at 40 px spacing (5 x `kGridSize`), drawn in the `textPrimary` theme
colour at about 8% alpha. Dots are computed only over the canvas's *visible* clip region, never all
10000 x 10000 px, so the cost is negligible at low zoom.

**Landing ghost.** A translucent rounded rectangle tracks the exact position the module will land —
the result of `resolvePlacement()`, which snaps to the grid *and* runs the anti-overlap spiral in
real time. The fill is the theme `accent` at about 18% alpha; the outline is `accent` at about 70%
alpha with a 1.5 px stroke, using the theme `cornerRadius`. The user sees the exact slot before
releasing.

```cpp
// Begin a drag preview for an existing module (selfId = its NodeID) or a new
// library module (selfId = {} / default-constructed NodeID).
void GraphEditor::beginDragPreview(int w, int h,
                                   juce::AudioProcessorGraph::NodeID selfId);

// Update ghost position. Called on every drag tick with the current canvas top-left.
// Internally calls resolvePlacement() so the ghost is always the true landing rect.
void GraphEditor::updateDragPreview(juce::Point<int> desiredTopLeftCanvas);

// Clear the preview (dots and ghost). Called on mouseUp / drag exit / drop.
void GraphEditor::endDragPreview();
```

`ModuleComponent` calls these three from `mouseDown` / `mouseDrag` / `mouseUp` for existing-module
drags. `GraphEditor::itemDragEnter` / `itemDragMove` / `itemDragExit` do the same for
library-sidebar drag-ins, sizing the ghost from `estimateModuleSize()` — an approximate preview
only.

**The final drop uses the real component size.** `itemDropped` calls
`finalizeModuleDrag(newComp)` on the newly created `ModuleComponent` after `updateComponents()`
builds it, so the anti-overlap test runs against the module's real footprint. Tall modules such as
Oscillator (553 px) or Sampler (665 px) therefore land correctly even when the ghost preview used a
slightly different estimate.

## Alignment guides

Figma-style smart guides appear while a module is dragged, so a card can be lined up with its
neighbours by eye:

- **Edge-to-edge lines** when a dragged edge comes within `Metrics::gridSize` (8 px) of another
  module's left, right, top or bottom edge.
- **Centre lines** when the dragged module's centreX or centreY matches a neighbour's.
- Drawn in the theme's `textMuted` colour at `Metrics::guideAlpha` (0.7) with a
  `Metrics::guideLineWidth` (1.5 px) stroke.

`GraphEditor::updateDragPreview()` scans every existing module rectangle in canvas space, stores
matches as `AlignmentGuide` structs and renders them in `paintOverChildren()`. Only the closest
guide per type (left / right / top / bottom / centreX / centreY) is shown.

**Guides are visual only** — they never alter snapping. `findFreeSlot()` still governs where the
card lands, on the soft 8 px grid. A theme switch re-resolves the guide colour automatically.

A toggle lives at `Settings -> Preferences -> Show Alignment Guides`, persisted as
`alignmentGuidesEnabled` in `juce::ApplicationProperties` and on by default. Off, only the module
ghost is drawn.
