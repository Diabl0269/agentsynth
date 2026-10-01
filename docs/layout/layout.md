# Canvas Layout

`Source/UI/Layout/LayoutUtil.h` / `.cpp` (`synth::LayoutUtil`) is the headless, JUCE-GUI-free
geometry behind every module placement on the patch canvas: the grid a module snaps to, the search
that keeps two cards from overlapping, the row-and-column auto-arrange, and the ghost drawn while a
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

## Making room when something grows

When a macro's hull or card gets bigger, or any module card does (a loose card or a macro member),
the things beside it move aside instead of being covered. The pure geometry is `LayoutUtil::resolveDisplacement`; the canvas
glue (`buildLayoutUnits`, `moveUnitBy`, `makeRoomFor`) lives in `MacroGroupControllerDisplacement.cpp`.

- **Units.** Neighbours are compared one nesting level at a time. At the top level a unit is a loose
  module, or a whole macro as ONE rectangle (its hull when open, its card when collapsed); inside a
  macro the units are its direct members and its child macros. A module hidden inside a collapsed
  macro is never a unit, and macro port widgets never are either (they live inside their hull).
  Moving a macro moves everything drawn inside it rigidly, hidden members and nested collapsed
  cards included, so their offsets to the card never change.
- **Direction.** Only units the grower overlaps (with the usual 12 px clearance) move. Each goes the
  way that needs the least travel (left, right, up or down; a tie goes down, then right), rounded up
  to the 8 px grid. The grower itself never moves.
- **Cascade.** A pushed unit pushes what it now overlaps in the same direction, so a row of
  neighbours shifts together. A unit reached twice ends up as far as the further push needs.
  Nothing ever moves back, so the cascade always settles.
- **Canvas edge and pinned units.** The top-left of the canvas is a wall: a move that would leave
  it is not taken, and the unit instead goes the smaller of right or down. The same rule applies
  to a move that would land on a pinned unit (which never moves). A unit that is blocked both
  ways is left where it is.
- **Inside out.** After the grower's own level is settled, its enclosing macro may have a bigger
  hull, so that macro is treated as the grower one level up, all the way to the top level.
- **When.** Only on discrete events, never while dragging: grouping (including nesting), adding
  modules to a macro (menu, Cmd-drag, library drop into a hull), expanding a macro, adding a port
  (which lengthens the hull), and a module card changing size in place (loose or macro member: the Macros knob count, the Scope / Response / envelope graph folds, an LFO's Draw section, a poly or Dual I/O toggle; the list is in [module-card.md](module-card.md)).
- **Undo.** It runs inside the undo step of whatever caused the growth, so one undo puts the
  neighbours back with the macro. Positions are written at once, so hulls, port strips and cables
  are correct immediately.
- **Shrinking a card.** A module card keeps the same kind of record for what its own growth pushed (in
  `MacroGroupController`, keyed by the card, never saved). Each time the card changes size in place
  (`reflowForResizedModule`) the neighbours it pushed first get their way back by the rule below, and only then does the
  new footprint push what it covers. A neighbour whose old spot the card's new rect still covers stays blocked and keeps
  its record, so grow, shrink, grow settles the same way every time. The records are dropped on undo/redo, Auto Arrange,
  any graph replacement (project load, new patch, preset) and when the card is deleted.
- **Shrinking.** Whatever a macro's growth pushed is remembered on that macro (`Macro::displaced`: which unit, how
  far, where it landed; never saved to a project file, and lost when an undo or redo restores a snapshot). Collapsing the
  macro, deleting one of its members or ports, or a member leaving it (drag or menu), offers each pushed unit its way back, newest push first
  (`returnDisplacedNeighbours`), repeating over the still-blocked ones until a pass returns nothing so a cascade unwinds fully: a unit returns only if the user has not moved it since (it is still exactly where the
  push left it) and its old spot is clear, with the usual 12 px clearance, of every other unit at that level, the
  collapsed card included. A unit that cannot return stays put. Collapse clears the record; a delete on a macro that
  stays open keeps the pushes that only lacked room. It runs inside the same undo step as the collapse or delete. Before neighbours return, a collapse first undoes the
  expand's canvas nudge (`restoreCardAfterCollapse`) when the members have not moved since and the card's old spot is
  clear, so the card sits where it was and the neighbours' old spots are clear of it again.
- **Placement blockers.** A module being dropped or dragged lands through `findFreeSlot` against the same layout
  units, flattened over every level (`MacroGroupController::placementBlockers`): each visible module, each collapsed
  card, each open hull. The hidden members of a collapsed macro are not blockers (they still sit at their
  pre-collapse spots under and around the card, which is why a module set just below a card used to jump away).
  Left out are the placed module itself, every macro that contains it (its own hull and its ancestors'), the
  collapsed macros a group drag carries, and the macro the drop is about to join, whose hull is where the module is
  meant to land. Used by single drags and drops (`resolvePlacement`), the group drag finalize, a collapsed card's own drop (`finalizeMacroCardDrag`) and the add-track slot.
- **Loading never pushes.** A card that is still being constructed (opening a project, undo, paste)
  has not "grown": its saved position is authoritative and no neighbour moves for it. Only a card
  already on the canvas that changes size makes room. `updateComponents()` is not re-entrant, so
  anything that runs while it builds cards must not call it back.
- **Cards glide, geometry does not.** When a make-room push, a neighbour return (collapse, port or member delete)
  or an auto-arrange moves cards, each moved card slides from its old spot to its new one over 160 ms
  (`easeOutCubic`). Geometry stays synchronous: component bounds, node x/y, hulls, the cables' base geometry and
  hit-tests all hold the FINAL positions at once, so a click or drag during the slide acts on the card's final spot.
  The slide is a canvas-level overlay (`CardGlideAnimator`, `Source/UI/Graph/CardGlideAnimator/`): the real card is
  hidden (alpha 0; JUCE paints nothing for it but still routes the mouse to it) and a snapshot of it is drawn
  interpolating between the two rects over the canvas. Cables touching a gliding card follow it (the endpoint is
  offset by the card's drawn-minus-final position). Open-macro hulls and port strips land at once. Only cards visible
  both before and after glide; a card that just appeared or is being dragged does not. A second move mid-glide
  retargets from the drawn position. One `CardGlideAnimator::Scope` around the whole action (expand, collapse,
  `makeRoomFor`, `returnDisplacedNeighbours`, auto-arrange) arms once. Loading a project, undo/redo and paste never
  glide: they land at once.

## Output dock

Master (once any mixer channel exists), the Rec Tap (if the project has one) and Audio Output form the
**output dock**: the chain tail, always the rightmost cards on the canvas, left to right in signal order
(`Master -> Rec Tap -> Audio Output`, [`docs/mixer/mixer.md`](../mixer/mixer.md#node-types)). The pure rule is
`LayoutUtil::computeOutputDock`; the canvas glue is `GraphEditor::reflowOutputDock()`
(`GraphEditorOutputDock.cpp`).

- **x is derived, never user-set.** The dock's left edge is the rightmost edge of every other layout unit
  (`MacroGroupController::buildLayoutUnits("")` minus the dock: loose modules, whole macros as their hull or
  card), rounded up to the grid, plus `kLayerGapX`. On an empty canvas it is `kArrangeOriginX`. Dock cards follow
  each other `kOutputDockCardGapX` (40 px) apart. Nothing stores the x: it is recomputed, so it can never drift.
- **y is Audio Output's own stored `"y"`** (no extra persisted field), snapped. A new patch seeds it at
  `kArrangeOriginY`; when Master first appears it joins the dock on that same y.
- **Dragging** a dock card moves it, and the whole dock with it, vertically only: x is held while dragging and
  is not snapped back afterwards because it was never moved. Dock cards never travel with a multi-selection
  drag (they stay put), and never join or leave a macro.
- **Pinned.** Dock cards are `pinned` layout units, so make-room never pushes them; a grown macro pushes the
  dock (re-derived to its right) instead.
- **When it is re-derived**, all inside the causing undo step where there is one: every `updateComponents()` (which
  covers project/preset load, module add/remove, undo/redo, track creation and the Master splice, and
  auto-arrange), module and selection drag release, a collapsed-macro card drop, a library drop, and the end of
  `makeRoomFor`. It writes node x/y and the live component bounds together, never opens an undo record of its own
  and never marks the project dirty (dirtiness follows the undo edit serial), so opening an older project just
  shows the dock in its derived place.
- **Delete protection.** Audio Output can never be deleted; Master can't while any mixer channel exists
  (`synth::outputDockDeleteRefusal`, used by canvas delete, multi-delete, the card's Delete item, which is
  disabled with the reason in its label, and the AI patch "remove" list). Undoing the track deletion that
  removes the last channel still removes Master, as before.
- **Go to Output** (Cmd+Shift+M, canvas menu; command id `locateMaster`) selects Master (else Audio Output) and
  centres the view on the whole dock.

## Auto-arrange

`GraphEditor::autoArrange()` (Cmd+L, or the toolbar button) lays the whole canvas out in one undo step. The rule is
built for people who are not graph experts: things that belong together stay together, the result is predictable, and
running it twice changes nothing. The pure layout is `computeHierarchicalArrange`
(`Source/UI/Layout/HierarchicalArrange.{h,cpp}`: blocks, edges and track order in, positions out, no graph or
component types); `GraphEditorAutoArrange.cpp` flattens the live canvas into that input and writes the result back.

**Rows, top to bottom.**

1. **The shared-modulator row.** A modulator (a block with no signal cable at all and nothing feeding it, such as an
   LFO or an envelope) whose consumers sit in two or more rows goes in one row on top, ordered by consumer count
   (most first), then node id. A modulator whose consumers are all in one row joins that row instead. A block with modulation consumers and no signal cable leaving it (an LFO
   that only takes a MIDI retrigger cable in) counts as a modulator too, so that incoming cable never pulls it into the
   feeding track's row.
2. **One row per track, in track order.** Track order is the timeline's: `GraphEditor::trackSourceOrder` (set by
   `MainComponent`) returns each track's Track In / Track Audio node uuid in timeline order, the same order the mixer
   lists its channels in ([`docs/mixer/mixer.md`](../mixer/mixer.md#channels-follow-audio-not-tracks)). Without that
   callback (plugin, tests) track source nodes are ordered by node id. A track's row holds everything its source
   reaches through signal cables (the channel macro, instrument modules, effects, a bus it sends to) plus whatever is
   wired into that chain. A block reachable from two tracks belongs to the earlier one; a track whose source an
   earlier track already claimed gets no row of its own.
3. **One row per remaining connected component**, ordered by the id of the component's leftmost source (its first
   block with nothing feeding it). A loose module with no cables is a component of one.

Cables between rows are still drawn but never influence placement. Row gap is `kIntraLayerGapY`; a row is as tall as
its tallest column stack.

**Columns.** A block's column is its longest-path depth from the row's sources, over the edges inside the row (signal
and modulation alike). A source block (nothing feeding it inside its row) that is not the row's anchor (the track's
start, or a component's leftmost source) is then placed as late as possible: one column before its nearest consumer in
the row, and in the stack slot that consumer has, so a loose LFO sits right before the macro it modulates instead of
over column 0. Such a source is right-aligned in its column (everything else is left-aligned), so a wide block in
another row's cell of the same column never opens a gap between it and what it feeds. The pass runs per level, so it
also applies inside open macros.
A signal edge from a block into its own modulator (an LFO that takes MIDI from the macro it modulates) is left out of
the column count, so the pair is not a cycle: the modulator stays one column before the block and what follows the block
keeps its column.

**Columns are aligned across rows.** Inside a row a block's column is its longest-path depth from that row's sources
(signal cables and modulation routings both count; a cycle is broken by ignoring back-edges). Column k has ONE x for
every row and is as wide as the widest block at depth k in any row, so matching stages (Gate, EQ, Compressor, channel
strip) line up vertically down the tracks. Inside one column of one row blocks stack by role rank (sources, then
filters/EQ/VCA, then effects, then modulators), then node id; the shared-modulator row stacks by its own order.

| Rank | Module types |
|------|-------------|
| 0 | Oscillator, Sequencer, MIDI Keyboard and the other sources |
| 1 | Filter, EQ, VCA, Voice Mixer |
| 2 | FX modules (Delay, Reverb, Distortion, Gate, Compressor, ...) |
| 3 | ADSR, LFO and the other modulators |

**What is a block.**

- **Loose module** or **collapsed macro**: one card-sized block. Nothing inside a collapsed macro is arranged; its
  hidden members are translated rigidly by the card's delta (`MacroGroupController::moveUnitBy`), so their offsets to
  the card never change.
- **Open macro**: arranged recursively with the same rule (members and child macros are blocks; port widgets are
  excluded because they dock to the hull), then placed as ONE block whose size is its hull footprint. The hull is
  `LayoutUtil::openMacroHull(memberUnion, portRows)`, the same function `macroHullBounds` draws: the union grown by
  the margin, the chip row on top and `kMacroHullSideOutset` of port strip each side, so the outer hull contains the
  inner one and a hull never starts left of the canvas (the first column starts at `kArrangeOriginX`). A macro with
  nothing to arrange inside (only ports) stays one rigid rectangle.
- **The output dock is not arranged.** Master, Rec Tap and Audio Output are skipped; after positions are written
  `reflowOutputDock()` places them right of everything, on the first row's y (Audio Output's own stored y is set to it).
- A cable endpoint that resolves to nothing (the dock, a hidden helper node) contributes no edge; a macro port node or
  a member hidden inside a collapsed macro stands in as its macro's block.

**Pixel coordinates.** Everything is on `kGridSize`: columns and stacks advance by grid-rounded sizes, a card is
left-aligned in its column (a wider block elsewhere in the column never shifts it sideways), and an open macro's anchor (the origin its members' offsets are measured from) is
chosen so every member lands on the grid while the hull, which can sit a few pixels off-grid, stays within its slot.
The origin is `(kArrangeOriginX, kArrangeOriginY)`.

| Constant | Value | Meaning |
|----------|-------|---------|
| `kLayerGapX` | 80 px | Horizontal gap between adjacent columns |
| `kIntraLayerGapY` | 40 px | Gap between stacked blocks and between rows |
| `kArrangeOriginX` | 40 px | Left margin where the first column starts |
| `kArrangeOriginY` | 40 px | Top margin where the first row starts |

**Idempotent by construction.** Positions depend only on sizes, topology and row order, never on where anything
currently is, so a second Cmd+L moves nothing (and pushes no undo step: `recordGraphAndMacroChange` records nothing
when nothing changed). Collapsing or expanding one macro and arranging again changes only that block's footprint.

**Undo and "home".** The whole write (node x/y, collapsed cards' persisted bounds, the dock) is one
`recordGraphAndMacroChange` step, so one Cmd+Z restores every node and every collapsed card. Arranging also clears every
macro's transient make-room record (`Macro::displaced`, the expand nudge): the layout is the new "home", so a later
collapse never drags a neighbour back to a spot the layout has left.

**Synchronous geometry.** Node x/y, macro bounds and live component bounds are written together; then only the light
refresh runs (`syncMacroCards`, port widgets re-docked, `reflowOutputDock`, repaint). Auto-arrange never calls
`updateComponents()`, which is not re-entrant.

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

**`computeHierarchicalArrange`**

```cpp
ArrangeOutput computeHierarchicalArrange(const ArrangeInput& input);
```

The pure layout behind [Auto-arrange](#auto-arrange): `ArrangeInput` carries the blocks (module, collapsed macro, open
macro with children), the edges (signal and modulation, between any block ids), aliases (hidden members and port nodes
mapped to the block standing in for them) and the track starts in track order. `ArrangeOutput` carries a position per
block at every level (an open macro's position is its anchor) and the open macros' hulls. `openMacroHull` is the one
definition of the hull around a member union. The same header holds `arrangeRoleRank`.

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

A toggle lives at `Settings -> Preferences -> Graph -> Show Alignment Guides` (see [settings-preferences.md](settings-preferences.md)), persisted as
`alignmentGuidesEnabled` in `juce::ApplicationProperties` and on by default. Off, only the module
ghost is drawn.
