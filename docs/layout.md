# Layout System Reference

This document describes the soft-grid layout model: how modules snap to an 8 px grid, how the
anti-overlap spiral search resolves collisions on drop and drag-release, how `autoArrange`
computes a topological signal-flow layout, and the full `LayoutUtil` API reference.

---

## 1. Soft-Grid Model

Agent Synth uses a **soft grid** — the same approach taken by Max/MSP, Bitwig Grid, and Blender's
node editor. Modules are free-form (you can place them anywhere on the 10000×10000 canvas), but
two layout rules are always enforced:

1. **Snap on drag-release.** When you finish dragging a module, its top-left corner rounds to the
   nearest 8 px multiple.
2. **Anti-overlap on drop/drag.** When a module lands on top of another one, the engine performs
   a spiral search to find the nearest clear slot, then places the module there instead.

These rules fire only at the moment of release, not on every drag tick. Live-tick snapping would
stair-step the position and fight `ModuleComponent`'s buffered-image compositing; the soft-grid
style avoids that entirely.

### Coordinate space

All layout and collision logic operates in **canvas coordinates** — the `content` child component,
bounded `0, 0, 10000, 10000`. The zoom/pan transform lives on `content.setTransform(...)` and is
invisible to `LayoutUtil`. Never pass screen-space coordinates into layout functions.

Module positions are persisted on the JUCE audio-graph node's property bag as integer keys `"x"`
and `"y"`. `GraphEditor::updateComponents()` reconciles those values back to
`setTopLeftPosition(x, y)` after every state change (preset load, undo/redo, auto-arrange).

---

## 2. kGridSize = 8 — Rationale

The grid quantum is 8 px for several complementary reasons:

- **Port jack vertical spacing is 20 px; module header height is 30 px.** 8 px is the smallest
  quantum that feels snappy and invisible — coarser grids (16, 20) can misalign jack-to-jack
  connections visually at high zoom.
- **All auto-arrange spacing constants are multiples of 8.** `kLayerGapX = 80`,
  `kIntraLayerGapY = 40`. Auto-arranged modules therefore land on-grid with no rounding residual.
- **The spiral step equals kGridSize.** Every candidate position the spiral search tries is
  already on-grid, so a module placed by anti-overlap is guaranteed to be grid-aligned.
- **Standard module width 280 = 35 × 8.** Flush tiling: a row of standard modules has no
  inter-module remainder when laid out at the grid quantum.

---

## 3. Anti-Overlap Spiral Search

When a module is placed (library drop or drag-release), the engine:

1. Snaps the desired top-left position to the nearest grid multiple.
2. Checks whether the snapped rectangle (at the module's actual pixel dimensions) overlaps any
   other module, using a minimum clear gap of `kCollisionGap = 12 px` between bounding boxes.
3. If the slot is clear, places the module there — done.
4. Otherwise, walks an **expanding square spiral** outward from the desired position, testing each
   grid-aligned candidate, until it finds a clear slot or exhausts
   `kSpiralMaxRings = 256` rings (256 × 8 = 2048 px search radius). If no clear slot is found
   within the radius the snapped-desired position is returned as a fallback — always a valid
   on-canvas coordinate.

The spiral visits positions in ring order (innermost first) so the module lands as close to the
intended drop point as possible.

### Collision inflaton rule

Two boxes A and B collide (with gap `g`) when:

```
A.inflated(g/2).intersects(B.inflated(g/2))
```

which is equivalent to requiring at least `g` px of clear space on all sides between the edges.
The `selfId` parameter lets a module exclude its own box from the occupied set — used during
drag so a module does not collide with its own pre-drag position.

---

## 4. Auto-Arrange (Cmd+L / "Auto Arrange" button)

`GraphEditor::autoArrange()` rearranges all visible modules into a left-to-right
**topological signal-flow layout** in one undo step.

### 4.1 Algorithm

**Step 1 — Collect arrangeable nodes.**
All graph nodes whose processor is a `ModuleBase` subclass, plus the two
`AudioGraphIOProcessor` IO nodes. `AttenuverterModule` nodes are skipped entirely — they are
implementation details of the modulation graph and do not appear as visible module cards.

**Step 2 — Build directed edges.**
Edges are gathered from `AudioProcessorGraph::getConnections()`, skipping any edge that touches
an `AttenuverterModule` node. Additionally, modulation routing edges from
`AudioEngine::getModulationRoutings()` (source → destination, collapsing attenuverter chains to
logical endpoints) are added as `extraEdges`. Duplicate edges and self-loops are removed.

**Step 3 — Assign signal-flow depth via longest-path.**
A Kahn-style topological traversal assigns each node a depth equal to the length of the longest
incoming path. Nodes with no incoming edges get depth 0. Any cycle is broken by ignoring
back-edges to already-visited nodes so the traversal always terminates. The Audio Output IO node
is forced to the maximum depth (rightmost column).

**Step 4 — Group nodes into layers.**
All nodes at the same depth form one layer (column). Within a layer, nodes are sorted by
**role rank** for a stable, readable ordering:

| Rank | Module types |
|------|-------------|
| 0 | Oscillator, Sequencer, MIDI Keyboard |
| 1 | Filter, VCA |
| 2 | FX modules (Delay, Reverb, Distortion, …) |
| 3 | ADSR, LFO |

Nodes at equal rank are then sorted by node UID for full determinism across runs.

**Step 5 — Assign pixel coordinates.**

```
x = kArrangeOriginX          // 40 px left margin
for d in 0..maxDepth:
    layerWidth = max(sizeOf(n).x for n in layers[d])
    y = kArrangeOriginY      // 40 px top margin per column
    for n in layers[d]:
        nodeX = x + (layerWidth - w) / 2   // centre narrow cards in wide column
        emit { n, snap({nodeX, y}) }
        y += h + kIntraLayerGapY            // 40 px between cards
    x += layerWidth + kLayerGapX            // 80 px between columns
```

Every emitted position is passed through `snap()` and clamped to
`[0, kCanvasMax − w] × [0, kCanvasMax − h]`.

### 4.2 Spacing constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `kLayerGapX` | 80 px | Horizontal gap between adjacent layer columns |
| `kIntraLayerGapY` | 40 px | Vertical gap between stacked modules in the same layer |
| `kArrangeOriginX` | 40 px | Left margin — where the first layer column starts |
| `kArrangeOriginY` | 40 px | Top margin — where every layer column's first module starts |

All four constants are multiples of `kGridSize`, so auto-arranged positions are always on-grid.

### 4.3 Undo integration

`autoArrange()` wraps all position writes in a single undo snapshot:
`undoManager->captureBeforeState(graph)` before computing the layout,
`undoManager->pushSnapshotFromCapture(graph)` after calling `updateComponents()`. Pressing
Cmd+Z once restores every module to its pre-arrange position.

---

## 5. Toolbar & Status Bar Layout

The application chrome is carved out of `MainComponent::resized()` — the single canonical layout method. It slices top→bottom:

```
┌─────────────────────────────────┐  ← toolbar strip  (height: Metrics::toolbarHeight = 36 px)
│  [Lib] [Save][Load][Cfg][⟲][⟳][⬜]   ···   [Matrix][AI]  │
├──────────┬──────────────────────┤
│  Library │                      │  ← library sidebar  (width: Metrics::librarySidebarWidth = 200 px, or 0 when hidden)
│ (200 px) │    Graph canvas      │
│          │                      │  ← AI panel clips right (width: Metrics::aiPanelWidth = 300 px, or 0 when hidden)
├──────────┴──────────────────────┤
│  [Patch Name]  CPU  Voices  [🔇] │  ← status bar  (height: Metrics::statusBarHeight = 24 px)
└─────────────────────────────────┘
```

### ToolbarComponent `paint()`

`paint()` fills the toolbar background with `theme.colors.bg0` via a `dynamic_cast<AppLookAndFeel*>`. When the cast returns null (headless tests or non-themed context), it falls back to the hardcoded colour `0xff0B0D10`.

### Toolbar FlexBox layout (`ToolbarComponent`)

`ToolbarComponent::layoutButtons(bounds)` runs a single `juce::FlexBox` (row, align-center):
- Left group: Library, Save, Load, Settings, Undo, Redo, AutoArrange
- Flex spacer (`withFlex(1.0f)`) — fills available gap
- Right group: ToggleModMatrix, ToggleAiPanel

**Narrow mode** fires when `bounds.getWidth() <= Metrics::minWindowWidth` (480 px). In narrow mode, all button `prefWidth` = 32 (icon-only). In wide mode each button has a labelled preferred width (Library 96, Save 112, Load 116, Settings 96, Undo 72, Redo 72, AutoArrange 120, ToggleModMatrix 104, ToggleAiPanel 92).

### Narrow-mode gate in `MainComponent::resized()`

`applyToolbarIcons()` clones `Drawable` objects from the icon cache to set button images. To avoid a clone storm on every resize, the call is **gated to narrow-mode transitions only**:

```cpp
bool prevNarrow = toolbarNarrowMode_;
toolbar.layoutButtons(toolbarBounds);       // updates toolbar.isNarrowMode()
toolbarNarrowMode_ = toolbar.isNarrowMode();
if (toolbarNarrowMode_ != prevNarrow)
    applyToolbarIcons();                    // re-clone only on mode flip
```

`applyToolbarIcons()` is also called unconditionally once at the end of `initialiseCommon()` and after every theme switch (via `changeListenerCallback`).

### Metrics layout tokens (code-only)

The following `Metrics` struct fields govern chrome layout. They are **not parsed from user JSON** — a user theme may not override them. `ThemeLoader` silently ignores them (unknown-key forward-compatibility). Their values come from the C++ struct defaults only.

| Token | Value | Meaning |
|---|---|---|
| `toolbarHeight` | 36 | Toolbar strip height (px) |
| `statusBarHeight` | 24 | Status bar strip height (px) |
| `controlPadding` | 4 | Inset around toolbar buttons (px) |
| `minWindowWidth` | 480 | Narrow-mode breakpoint (= minimum window width) |
| `minWindowHeight` | 400 | Minimum window height reference |
| `sidebarCollapsedWidth` | 0 | Library width when hidden (px) |
| `librarySidebarWidth` | 200 | Library width when visible (px) |
| `aiPanelWidth` | 300 | AI panel width when visible (px) |
| `iconSize` | 16 | Icon render size in library/status bar contexts (px) |

### Minimum window size

`Main.cpp` `MainWindow` ctor calls `setResizeLimits(480, 400, 8192, 8192)` — a hard platform floor on the `DocumentWindow` before `centreWithSize(1600, 900)`. This prevents the window from shrinking below the minimum where toolbar buttons could clip to zero width. `Metrics::minWindowWidth`/`minWindowHeight` carry the same values as layout constants for use in `ToolbarComponent`'s narrow threshold and tests.

### StatusBarComponent

The status bar (`Source/UI/StatusBarComponent.h/.cpp`) is a 24 px high strip rendered at the bottom of `MainComponent`.

**Layout:**
- Patch name: left-aligned (padded 6 px from left edge)
- CPU %: centre section, drawn in `theme.colors.warning` when above 80 %, otherwise `textMuted`
- Voice count: right-aligned before the mute button slot
- `masterMuteButton_` (`DrawableButton`): positioned in `resized()` at `(w-28, 2, 20, h-4)`

**Update contract:**
`update(float cpuPct, int voices, const juce::String& patch)` is gated — it only calls `repaint()` when any value changes by a visible amount (cpu delta > 0.5 %, voice count changed, or patch name changed). It contains **zero `writeToLog` calls**.

**Polling rate:**
The status bar polls at 5 Hz, driven by `MainComponent`'s 10 Hz timer via an every-other-tick guard (`statusBarTickCount_`).

**Static format helpers** (headless-testable, no JUCE GUI deps):
- `formatCpu(float fraction)` — fraction is 0..1; `0.756f → "75.6%"`
- `formatVoices(int n)` — `0 → "0 voices"`, `1 → "1 voice"`, `8 → "8 voices"`
- `formatPatch(const juce::String& s)` — empty or whitespace-only → `"Untitled"`

### ModuleLibraryComponent section headers

Each category section-header entry in `ModuleLibraryComponent::paint()` draws a 16×16 category icon at `x=10` using `lf->peekIcon(catIcon)`, then shifts the header text to `x=30`. This is null-guarded: when the `AppLookAndFeel` cast returns null (headless tests or assets absent), no icon is drawn and header text falls back to the original `x=10` position.

### ModMatrixComponent chrome

`Source/UI/ModMatrixComponent.h/.cpp` paints rows with the following visual rules:

- **Row height**: `static constexpr int kRowHeight = 48` (was 40)
- **Zebra striping**: odd rows (`isZebraRow(rowIndex)` — `rowIndex % 2 == 1`) are tinted with `theme.colors.surfaceHi.withAlpha(0.45f)`; even rows are transparent (parent background shows through)
- **Hover highlight**: the currently hovered row is tinted with `theme.colors.accent.withAlpha(0.10f)`, overriding the zebra base
- **Hover tracking**: `ModRow::mouseEnter` calls `owner.setHoveredRow(rowIndex)`; `ModRow::mouseExit` calls `owner.setHoveredRow(-1)` only when that row still owns the hover, avoiding races when the cursor moves between rows. The `hoveredRow_` member defaults to `-1` (no hover)
- **Static helper**: `static bool isZebraRow(int rowIndex) noexcept` — exposed for unit tests

### ModuleComponent header button layout

The header area of each module card (`Source/UI/ModuleComponent.cpp`) contains three `DrawableButton` instances (not `TextButton`), positioned in `resized()`:

| Button | Bounds | Action |
|---|---|---|
| `deleteButton` | `(w-26, 2, 22, 20)` | Calls `owner.requestDeleteModule(nodeId)` |
| `bypassButton` | `(w-50, 2, 22, 20)` | Toggles bypass state |
| `muteButton` | `(w-74, 2, 22, 20)` | Toggles mute state |

`requestDeleteModule(NodeID)` is the canonical delete entry point — `deleteButton.onClick` delegates here.

`applyHeaderButtonIcons()` retints all three buttons from the active `AppLookAndFeel`. It is null-guarded: when the LnF cast fails (headless tests), the function returns early and buttons remain imageless but functional. `lookAndFeelChanged()` calls `applyHeaderButtonIcons()` so icons update on theme switch.

### Panel collapse and persistence

Library sidebar and AI panel can each be fully hidden (width = 0). State persists across launches via `ApplicationProperties`:

| Key | Default | Component |
|---|---|---|
| `librarySidebarVisible` | `"1"` (true) | `moduleLibrary` left panel |
| `aiPanelVisible` | `"0"` (false) | `aiChatComponent` right panel |

Both keys are read at the top of `initialiseCommon()` before any `setVisible()` or `addAndMakeVisible()` call. Cmd+B toggles the library sidebar (wired via `ShortcutManager`).

---

## 6. Module Width Buckets

Phase 3 standardizes module card widths into three named buckets defined in `LayoutUtil.h`:

| Bucket | Constant | Width | Modules |
|---|---|---|---|
| `Narrow` | `kNarrowWidth` | 40 px | AttenuverterModule |
| `Single` | `kSingleWidth` | 280 px | Oscillator, Filter, VCA, ADSR, LFO, FX modules, VoiceMixer, PolyMidi, all others |
| `Double` | `kDoubleWidth` | 560 px | Sequencer, PolySequencer, MidiKeyboard |

`kDoubleWidth == 2 × kSingleWidth` — a Double module occupies exactly two standard column slots. Attenuverter modules are excluded from the visible module card grid (they do not appear as `ModuleComponent` cards and are skipped by auto-arrange).

### Module body layout (`ModuleComponent::layoutDefaultContent`)

Every module that does not have a bespoke layout (Sequencer, PolySequencer, MidiKeyboard, ADSR,
Attenuverter) is laid out by one function, `layoutDefaultContent(bool apply)`. It runs twice per
size change — once with `apply = false` to measure the height, once with `apply = true` from
`resized()` to position the children — so the measured height and the real positions cannot drift
apart. They previously *did* drift: two hand-maintained copies of the geometry disagreed, and body
content was drawn on top of the lowest port labels.

| Constant | Value | Meaning |
|---|---|---|
| `kKnobColumns` | 3 | knobs per row |
| `kContentMargin` | 12 | left/right gutter for body content |
| `kNarrowContentWidth` | 200 | combos / toggles / the Sampler load row, centred |
| `kLabelHeight` | 18 | label above a knob or combo |
| `kRowHeight` | 24 | combo box, toggle, button |
| `kKnobHeight` | 58 | rotary + its text box |
| `kWaveformHeight` | 72 | Sampler waveform overview |
| `kPortLabelClearance` | 15 | gap below the lowest jack before body content starts |

**Body content always starts below every jack.** `getContentTopY()` derives that y from
`getPortCenter()` — the same function that anchors wires — rather than recomputing the port
geometry, so content can never land on a port label again. Because the body is below the ports, it
does not need the narrow gutters that used to keep it clear of the port labels, which is what makes
three knobs per row fit inside the 280 px card.

Three columns instead of two removes a knob row from most modules. Measured heights before → after:
Oscillator 530 → 449, Filter 570 → 487, LFO 440 → 353, Sampler 750 → 657. A few short modules got
*taller* (VCA 200 → 245, Noise 250 → 293, Poly MIDI 100 → 123) — those are the overlap fix, not
padding: their content used to start at y=60 while the first jack sits at y=70.

`GraphEditor::estimateModuleSize()` mirrors these heights for the library drag ghost.
`ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents` constructs every library-offered
type and fails if the table drifts, so the ghost cannot lie about where a module will land.

### Modules that resize at runtime

Widths are static, but one module's **height** is not: the Macro bank (`MacroControlModule`) grows
and shrinks with its `Knobs` parameter. Its geometry lives in `LayoutUtil.h` so the component
layout, the output-jack hit test and `estimateModuleSize` all read the same numbers:

```cpp
kMacroHeaderH  = 94   // title bar + Knobs / Bipolar row
kMacroRowH     = 44   // one macro knob and its output jack
kMacroBottomPad = 12

macroBankHeight(count) == kMacroHeaderH + count * kMacroRowH + kMacroBottomPad
macroRowCentreY(index) == kMacroHeaderH + index * kMacroRowH + kMacroRowH / 2
```

Growth is **anchored at the top-left and pushes neighbours, never itself**. Moving the module the
user is currently interacting with would teleport it out from under the cursor, so
`GraphEditor::handleModuleResized` instead feeds every module box to
`LayoutUtil::resolveOverlapsAfterResize` and applies the displacements it returns. Shrinking
returns an empty result set — nothing is pulled back up, the canvas just gains space.

### Column stride derivation

```
kColumnStride = kSingleWidth + kLayerGapX = 280 + 80 = 360 px
```

`kColumnStride` is not a named constant; it is the natural result of the auto-arrange algorithm. DOUBLE-width modules advance the column cursor by `kDoubleWidth + kLayerGapX = 640 px`.

### Auto-arrange with DOUBLE modules

`computeAutoArrange` uses the `sizeOf` callback to query each module's pixel width. For a Sequencer node the callback returns `{kDoubleWidth, 380}`, so `layerWidth` for that column becomes 560 and the next column starts at `x += 560 + 80 = 640`. Downstream modules are pushed rightward correctly without overlap.

### Preset column/row model (authoritative: `Source/PresetManager.cpp` lines 38–81)

Factory preset positions follow a fixed column/row grid. The **column x-positions** are not uniformly strided — they reflect actual module widths plus a ≥12 px collision gap:

| Column | x | Contents |
|---|---|---|
| Col 0 | 10 | IO nodes, Sequencer, MIDI Keyboard |
| Col 1 | 350 | Oscillator |
| Col 2 | 650 | Filter |
| Col 3 | 950 | VCA |
| Col 4 | 1250 | FX chain (Distortion / Delay / Reverb) |
| Col 5 | 1560 | Audio Output |

The stride between columns is ~300 px and variable — **not** the uniform 360 px value. (`kColumnStride = 360` is the auto-arrange algorithm's column advance for single-width modules, a separate concept.)

**Row y-positions:**

| Row | y | Contents |
|---|---|---|
| Signal row | 10 | Osc, Filter, VCA, FX chain |
| Sequencer row | 560 | Sequencer (bottom edge = 940) |
| Modulator row | 600 | AmpEnv, FilterEnv, LFO |
| Keyboard row | 960 | MIDI Keyboard |

### Preset position rebake (presets 0, 1, 5)

Factory presets 0, 1, and 5 contain a Sequencer at x=10 (right edge = 570). After switching the Sequencer to `kDoubleWidth = 560`, the AmpEnv and FilterEnv positions were rebaked to avoid overlap:

- **AmpEnv**: x=560 → x=**584** (570 + 12 gap + 2 grid ceil)
- **FilterEnv**: x=870 → x=**880** (584 + 280 + 12 gap + 4 grid ceil)

Presets 2, 3, 4, 6 have no Sequencer-adjacent envelopes and required no rebake. The `AllFactoryPresetsLoadWithoutOverlap` test and the `estimateModuleSize` mirror in `Tests/PresetManagerTests.cpp` are updated atomically with the preset data change to keep the test green.

### Poly Pad preset routing (case 6)

The Poly Pad factory preset routes **Amp Env → VCA per-voice CV** (PolyBus, VCA ports 8–15) only. There is **no** Amp Env → Osc Level (ch12) DirectCV connection in this preset.

---

## 7. LayoutUtil API Reference

`Source/UI/LayoutUtil.h` / `Source/UI/LayoutUtil.cpp` — no JUCE GUI component dependencies;
testable headlessly.

### Constants

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

// Module width buckets (see section 6)
inline constexpr int kNarrowWidth  = 40;   // Attenuverter
inline constexpr int kSingleWidth  = 280;  // standard module
inline constexpr int kDoubleWidth  = 560;  // Sequencer / PolySequencer / MidiKeyboard

enum class ModuleWidthBucket { Narrow, Single, Double };
ModuleWidthBucket getModuleWidthBucket(ModuleType t);  // defined in LayoutUtil.cpp; requires ModuleBase.h in caller
int moduleWidth(ModuleWidthBucket b);
int moduleWidth(ModuleType t);

} // namespace synth::LayoutUtil
```

### `snap`

```cpp
int snap(int v);
juce::Point<int> snap(juce::Point<int> p);
```

Rounds `v` to the nearest multiple of `kGridSize`. Negative-safe (uses `std::lround`).
The two-argument overload snaps both components of a point independently.

### `Box`

```cpp
struct Box {
    NodeID              id;
    juce::Rectangle<int> rect;
};
```

Represents one occupied slot: the JUCE graph `NodeID` plus the module's pixel bounding rectangle
in canvas coordinates. Build this list from the live module components for collision testing.

### `intersectsAny`

```cpp
bool intersectsAny(const juce::Rectangle<int>& candidate,
                   const std::vector<Box>& others,
                   NodeID selfId,
                   int gap = kCollisionGap);
```

Returns `true` if `candidate` (inflated by `gap`) overlaps any box in `others` whose `id` is not
`selfId`. Pass the dragged module's own `NodeID` as `selfId` to exclude it from its own
collision set during a drag.

### `findFreeSlot`

```cpp
juce::Point<int> findFreeSlot(juce::Point<int> desired, int w, int h,
                              const std::vector<Box>& others,
                              NodeID selfId,
                              int gap = kCollisionGap);
```

Finds the nearest grid-aligned top-left position where a `w × h` box does not overlap any entry
in `others` (excluding `selfId`). Starts at `snap(desired)`, then walks an expanding square
spiral at step `kSpiralStep` for up to `kSpiralMaxRings` rings. Returns `snap(desired)` (clamped
to canvas) if no clear slot is found within the search radius — never returns an out-of-bounds
position.

### `resolveOverlapsAfterResize`

```cpp
inline constexpr int kResolveMaxRounds = 4;

std::vector<ArrangeResult>
resolveOverlapsAfterResize(NodeID resizedId,
                           const std::vector<Box>& boxes,
                           int gap = kCollisionGap);
```

Called after a module changes footprint in place. `boxes` is every module box **including** the
resized one, already carrying its new rect. Returns a new top-left for each *other* box that had
to move; boxes that stayed put are not returned, so an empty result means the new footprint fitted
as-is.

The resized module is never returned and never moves. Displaced boxes are pushed straight down
past the lowest thing they collided with, then run through `findFreeSlot`, so results are on-grid
and gap-respecting. The sweep is deterministic (top-to-bottom, then left-to-right, then id) and
the cascade is capped at `kResolveMaxRounds` passes.

---

### `computeAutoArrange`

```cpp
struct ArrangeResult { NodeID id; juce::Point<int> pos; };

std::vector<ArrangeResult>
computeAutoArrange(juce::AudioProcessorGraph& graph,
                   const std::function<juce::Point<int>(NodeID)>& sizeOf,
                   const std::vector<std::pair<NodeID, NodeID>>& extraEdges);
```

Computes the topological signal-flow layout described in section 4. Returns one `ArrangeResult`
per arrangeable node (AttenuverterModule nodes are excluded). The caller is responsible for
writing `pos.x` / `pos.y` back to `node->properties` and calling `updateComponents()`.

`sizeOf` is a callback that returns the pixel footprint `{width, height}` for a given `NodeID`.
Typically backed by the live `ModuleComponent` dimensions; falls back to `{kSingleWidth, 300}` (280×300) for nodes
without a visible component.

`extraEdges` carries additional directed edges (e.g. from `getModulationRoutings()`) that are
merged into the graph edge set before computing depths. This ensures that envelope → VCA
modulation connections influence column ordering even though the attenuverter nodes they pass
through are excluded from the arrangeable set.

---

## 8. Drag Affordance — Grid Dots + Landing Ghost

During any module drag (moving an existing module or dragging one in from the library sidebar),
Agent Synth draws two visual cues over the canvas so placement is predictable and beautiful.

### Grid dots

While a drag is in progress the canvas shows subtle **dots at 40 px spacing** (5 × `kGridSize`).
Each dot is drawn in the `textPrimary` theme colour at ~8 % alpha — barely visible, but enough to
communicate "there is a grid here." Dots are computed only over the *visible* clip region of the
canvas (not all 10 000 × 10 000 px), so the cost is negligible even at low zoom.

### Landing ghost

As the user drags, a **translucent rounded rectangle** tracks the exact position the module will
land — the result of `resolvePlacement()`, which snaps to the grid *and* performs the anti-overlap
spiral search in real time. The ghost fill uses the theme `accent` colour at ~18 % alpha; the
outline uses `accent` at ~70 % alpha with a 1.5 px stroke, using the theme `cornerRadius`.

This makes placement fully predictable: you can see the exact slot *before* you release.

### API

```cpp
// Begin a drag preview for an existing module (selfId = its NodeID) or a new
// library module (selfId = {} / default-constructed NodeID).
void GraphEditor::beginDragPreview(int w, int h,
                                   juce::AudioProcessorGraph::NodeID selfId);

// Update ghost position. Call on every drag tick with the current canvas top-left.
// Internally calls resolvePlacement() so the ghost is always the true landing rect.
void GraphEditor::updateDragPreview(juce::Point<int> desiredTopLeftCanvas);

// Clear the preview (both dots and ghost). Call on mouseUp / drag exit / drop.
void GraphEditor::endDragPreview();
```

`ModuleComponent` calls these three methods from `mouseDown` / `mouseDrag` / `mouseUp` for
existing-module drags. `GraphEditor::itemDragEnter` / `itemDragMove` / `itemDragExit` do the
same for library-sidebar drag-ins, using `estimateModuleSize()` for the ghost footprint
(approximate preview only — the final placement always uses the real component size).

### Accurate final drop placement

`itemDropped` now calls `finalizeModuleDrag(newComp)` on the newly created `ModuleComponent`
after `updateComponents()` builds it. This means the final position uses the module's **real
component size** (not the size estimate) for the anti-overlap collision test, so tall modules
like Oscillator (530 px) or Filter (570 px) always land correctly even if the ghost preview used
a slightly different footprint.

---

## 9. Visualizer Components

Several in-module visualizer components provide real-time signal display inside module cards; one of them (`EQCurveComponent`) is also an editor.

### FrequencyGrid (`Source/UI/FrequencyGrid.h`)

Not a component — the pure log-frequency / dB coordinate maths shared by the two frequency-domain views (`FrequencyResponseComponent` for Filter, `EQCurveComponent` for Parametric EQ). Both plot over the same 20 Hz – 20 kHz log axis, so `freqToX` / `xToFreq` / `indexToFreq` / `formatHzLabel` / `findPeakBin` live here once. The dB axis is *not* fixed — `dbToY` / `yToDb` take `minDb` and `maxDb` per call, because the filter view needs an asymmetric −40…+50 dB window (resonance peaks overshoot a long way) while the EQ view uses a symmetric ±30 dB one. Each component still paints its own grid: they differ in dB step, label set, and whether the 0 dB line is emphasised.

### FrequencyResponseComponent (`Source/UI/FrequencyResponseComponent.h`)

Serum-style frequency-response curve with an optional FFT spectrum overlay, used by `FilterModule` cards.

**Phase 4 paint additions:**
- Hz axis labels at 100 Hz, 1 kHz, 10 kHz — drawn 3 px right of each vertical frequency gridline (10 kHz label is right-justified to stay within bounds at narrow widths)
- dB axis labels at −20, 0, +20 dB — drawn at the left edge of each horizontal dB gridline
- Resonance peak marker: filled dot (accent cyan `0xff00b4d8`) with a dark outline ring, plus a small text callout label using `formatHzLabel` at the magnitude peak; callout is skipped when component width < 44 px

**Public static helpers** (headless-testable, no component state needed):

| Helper | Signature | Notes |
|---|---|---|
| `findPeakBin` | `static int findPeakBin(const float* mags, int numBins)` | Returns index of maximum value; returns -1 for null/empty |
| `formatHzLabel` | `static juce::String formatHzLabel(float hz)` | `100 → "100Hz"`, `1000 → "1kHz"`, `10000 → "10kHz"` |
| `freqToXStatic` | `static float freqToXStatic(float freq, float width)` | Log-scaled freq → x pixel; mirrors private `freqToX` (minFreq=20, maxFreq=20000) |
| `dbToYStatic` | `static float dbToYStatic(float db, float height)` | dB → y pixel; mirrors private `dbToY` (minDb=−40, maxDb=50) |

All four now forward to `synth::ui::FrequencyGrid`; they stay as the component's public API so existing callers and tests are unaffected.

### EQCurveComponent (`Source/UI/EQCurveComponent.h`)

Interactive response curve for `ParametricEQModule`, in the traditional DAW idiom. Used twice over the same module: inline on the card and inside the pop-out `EQWindow`. Both views stay in sync automatically — each one's timer picks the other's edits up on its next tick.

On the card the curve is 150 px tall and laid out *beside* the port labels (`x` from 88 to `width − 88`, starting at `y = 60`) rather than below them. The labels only occupy a narrow gutter down each edge, so on a six-input module this reclaims ~125 px of otherwise dead space at the top of the card.

**Gestures.** All four bands start disabled, so the curve starts empty with a "Double-click to add an EQ point" hint.

| Gesture | Effect |
|---|---|
| Double-click empty space | Adds a point, enabling the slot that best fits that frequency (`findBandForNewPoint`) |
| Double-click a point | Removes it (disables that band; its settings are kept) |
| Drag a point | Sets frequency (x) and gain (y) |
| Scroll over a point | Widens / narrows it (Q), multiplicatively |
| Hover | Halos the handle and shows a `freq / gain / Q` readout bottom-right |

The mouse handlers are deliberately thin wrappers over public `addPointAt` / `removeBand` / `dragBandTo` / `nudgeBandQ` / `hitTestBand`, so the interaction is unit-tested without synthesising `juce::MouseEvent`s (`EQCurveInteraction.*`).

**Undo.** `onGestureStart` / `onGestureEnd` bracket every parameter-changing gesture; `ModuleComponent::wireEqGestureCallbacks` binds them to `AppUndoManager::captureBeforeState` / `pushSnapshotFromCapture`, so one drag is one undo step. Deliberately *not* opened on `mouseDown` — a click that never becomes a drag would otherwise push an empty undo entry. `dragBandTo` itself does no bracketing since it is called repeatedly during a drag. Both are wired through a `Component::SafePointer`, so a pop-out window that outlives its card becomes a no-op rather than a dangling call.

- **dB window**: symmetric ±30 dB, so 0 dB sits at the exact vertical centre and boosts read as the mirror of cuts. dB gridlines at ±12 and ±24; the 0 dB line is drawn brighter as the reference the curve is read against. `gainAtY` clamps to the parameter's ±24 dB, since the view is deliberately wider than the range.
- **Curve source**: `ParametricEQModule::responseDb()` — the analytic prototypes the module's biquad coefficients are derived from, so the drawing and the DSP cannot drift apart (locked by `ParametricEQAudio.MeasuredResponseTracksTheAnalyticCurve`). Sampled at 512 log-spaced points; the gradient fill hangs off the 0 dB line rather than the bottom edge, so a cut fills downward and a boost upward.
- **Band handles**: one numbered ring per *enabled* band at (centre freq, band gain) — the parameter pair, so vertical drag maps 1:1 to gain. As in Cubase/Pro-Q, a shelf's handle sits at its corner frequency at the full shelf gain, which is above the curve there (a shelf reaches its full gain only well past the corner).
- **Spectrum overlay**: 1024-point FFT of the module's `VisualBuffer`, drawn *behind* the curve over its own −80…0 dB window, smoothed with an exponential moving average. **On by default** here (unlike the Filter card) because the curve is meant to be read against it.
- **Repaint discipline**: 30 Hz timer that repaints when a band setting or the output trim changed, when hover/selection changed, or while the spectrum has actual signal. The analyser gates on peak < 1e-5 and repaints one final frame on the transition to silence, so a default-on spectrum still settles to **zero repaints on an idle patch** — that gate is what keeps this compliant with §10–11. Never make it unconditional.

### EQWindow (`Source/UI/EQWindow.h`)

Pop-out editor for a Parametric EQ, opened from the card's "Open EQ Window" button. Content-only `juce::Component` (same pattern as `SettingsWindow`); the caller wraps it in a `juce::DialogWindow` via `LaunchOptions::launchAsync()`. Hosts a second `EQCurveComponent` over the same module at 720×420 (resizable), plus a spectrum toggle and a gesture hint.

`ModuleComponent` holds the dialog as a `Component::SafePointer` and **deletes it in `detachFromProcessor()`** — the window references the module, so leaving it open across a graph rebuild would dangle. Re-clicking the button brings the existing window to front rather than opening a second one.

### ScopeComponent (`Source/UI/ScopeComponent.h`)

Oscilloscope waveform display used by all modules that have a `VisualBuffer`.

**Phase 4 paint additions:**
- Horizontal amplitude grid lines at ±0.5 and ±1.0 (drawn in `theme.colors.border` at 0.6 alpha)
- Centre (0.0) grid line — slightly brighter (`border.brighter(0.3f)`) to serve as the waveform baseline
- Centred "No Signal" empty-state text (in `textDisabled` colour) when the buffer peak ≤ 0.02 — replaces the waveform path draw entirely; `return` is early so no waveform is drawn on silence

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `isNoSignal` | `static bool isNoSignal(float peak) noexcept` | Returns `true` when `peak <= 0.02f` |
| `amplitudeToY` | `static float amplitudeToY(float amp, juce::Rectangle<float> bounds) noexcept` | Maps amplitude [-1,1] → y pixel; amp=+1 → top, amp=-1 → bottom, amp=0 → centre; uses 45% height per side |

**Themed colours**: resolved via `dynamic_cast<AppLookAndFeel*>` — `border` for grid, `textDisabled` for no-signal label, `accent` for the waveform. When the cast fails (headless), hardcoded fallbacks are used (`0xff2A2F38`, `0xff5C6470`, limegreen).

### TriggerMeterComponent (`Source/UI/TriggerMeterComponent.h`)

Live Trigger-jack level readout used by `SampleHoldModule` cards, so the module's `Threshold` can be
set by eye against the actual incoming signal instead of guessed.

Draws a bipolar bar (-1 left, 0 centre, +1 right) that fills from the centre toward the signal's
sign, a marker line at the **effective** threshold (knob + Threshold CV), and a pip that flashes for
`getFlashFrames()` ticks on each capture. The bar switches to the `gateWire` colour while the Schmitt
trigger is armed. Reads four atomics off the module: `getTriggerLevel()`, `getEffectiveThreshold()`,
`isTriggerHigh()`, `getTriggerCount()`.

**Why it is a separate component, not something `ModuleComponent::paint` draws:** `ModuleComponent`
is `setBufferedToImage(true)`, so painting a live meter inside its `paint()` would invalidate that
cached image and re-run the module's text layout on every meter tick — exactly the repaint storm
§10 prohibits. Instead this owns a 20 Hz timer and repaints *itself* only when a displayed value
moves past a visible amount (see `needsRepaint`), leaving the parent's cached image untouched.

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `valueToX` | `static float valueToX(float value, float x, float width) noexcept` | Maps bipolar [-1,1] → x offset within a width; clamps out-of-range input |
| `needsRepaint` | `static bool needsRepaint(...) noexcept` | Mirrors the `timerCallback` repaint gate; lets the "no repaint while idle" rule be tested without a message loop |
| `getFlashFrames` | `static constexpr int getFlashFrames() noexcept` | Ticks the fired-flash stays lit |

### WavetableDisplayComponent (`Source/UI/WavetableDisplayComponent.h`)

Wavetable frame view used by the `Wavetable` module card. Draws the frame currently under the scan position as a solid trace, with `kGhostFrames` (3) receding low-alpha traces sampled slightly further along the stack so the scan direction and the table's depth read as three-dimensional. Captioned with the table name and `frame/total`.

The trace is drawn **warped** — `getDisplayWaveformAt()` runs the same `readWarped()` the audio path does. A Warp knob whose effect is invisible is a knob users do not trust.

**Repaints are gated** — the 15 Hz timer compares a change signature (quantised scan position, table name, frame count, and `getWarpSignature()` — warp mode + quantised amount + interpolation mode) and calls `repaint()` only when it differs. This is required by §10: the display is always visible inside a `setBufferedToImage(true)` module card, so an unconditional per-tick repaint would invalidate that cache 15 times a second.

#### Wavetable card chrome

`ModuleComponent` builds the following for `Wavetable` modules:

- **"Load Wavetable..."** `TextButton` — opens an async `juce::FileChooser` (starting in the module's browser folder), and on success calls `loadWavetableFile()` and switches the module's `table` choice to `Loaded File`.
- **Folder browser row** — `[Folder...] [<] [>] [caption]`. `Folder...` opens an async directory chooser and calls `setWavetableFolder()`; `<` / `>` step the folder cursor; the caption shows the loaded file name and `index/total`.

Both completion lambdas hold a `Component::SafePointer` and re-derive the module via `dynamic_cast`, since the card can be destroyed while the dialog is open. The card also implements `FileDragAndDropTarget` for audio files, routing drops through the same import path as the Load button.

The card is **double-width** (issue #180): 15 knobs, 8 combos and a 16-jack port stack. Laid out flat, that came to 560×869 — technically correct and genuinely unusable, a wall of identical knobs with no hierarchy. Three mechanisms bring it to **560×554** and, more importantly, give it a reading order:

**1. Two-column jack gutter, both columns on the LEFT.** `getInputPortColumns()` returns 2 for a card with more than 10 visible input jacks at double width; `getPortCenter()` then lays the inputs out **column-major** (jack 0 top-left, running down then over) at `kPortColumnStride` (100 px) apart. Sixteen jacks in one column set a ~390 px floor on the card height before a single control is placed.

**Inputs stay on the left, outputs on the right — that convention is not negotiable for a saving in height.** It is what makes signal flow read left to right across a patch. Spilling the overflow down the right edge was tried and reverted: it reads as an output, and inputs and outputs are drawn identically, so the side is the only cue there is.

The real objection to an interior column — the module covers the lower half of a cable you are dragging towards it — is answered by not aiming at the gutter at all. Release the cable **on the destination knob** instead; see [`modulation.md` § Drag-to-Knob Modulation](modulation.md).

Everything that touches jack geometry — wire drawing, hit-testing (`getPortForPoint`), painting — reads `getPortCenter`, so this is the only place that changes. One consequence worth knowing: `getContentTopY()` takes the **maximum** y over all inputs rather than the last one's, because with more than one column an odd jack count leaves the second column a row short, so the last jack is not the lowest.

**2. Tabbed control body.** The 23 controls are grouped into five pages — **Tune / Unison / Phase / Sub / File** — by `kWavetablePages` in `ModuleComponent.cpp`, which maps parameter *display names* to pages. Three controls sit outside the strip: `Position` and `Warp` / `Warp Amt` are **pinned** above it (they are what you actually perform with), and `Table` is laid out in the chrome band beside the display it selects.

- **Jacks are never tabbed.** All 16 CV inputs stay on the card at all times, so a cable can never point at a hidden port. Only knobs and combos page.
- **Anything that paints from a knob's bounds must check visibility.** A hidden knob keeps the bounds it had when its page was last laid out. Modulation rings go through `getModRingSliderIndex()` and drop targeting through `getModTargetPortForPoint()`; both return "none" for a knob whose page is hidden, so a ring cannot paint over empty card and a hidden knob cannot swallow a cable drop.
- The card is sized to the **tallest** page, not the active one. A card that grew and shrank would shove its neighbours around the canvas on every tab click.
- Page knob rows are **centred**, and page combos run three across — most pages carry fewer than the 6 available knob columns, and left-aligning them stranded half the card's width.
- `createWavetableTabs()` must run **after** `createControls()` (it groups the controls that call creates) and must end by calling `updateLayout()` — `createControls()` already sized the card, so without a second pass the card keeps its flat-grid height and the tabbed layout never applies.

**3. Chrome beside the ports.** The display / Table / button row / caption sit **beside** the jack stack, starting just under the header — the same reclaim the Parametric EQ card makes for its response curve (see §9). The body still starts below the lowest jack.

The final size is pinned by `EstimatedModuleSizesMatchTheRealComponents`; `WavetableCardSplitsItsJackGutterIntoTwoColumns` and `WavetableTabsSwitchContentWithoutResizingTheCard` guard the two mechanisms above (including that every knob is reachable from some page — a control on no page is unusable).

**Public static helper** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `quantisePosition` | `static int quantisePosition(float position, int steps = 200)` | Clamps to [0,1] and quantises the scan position into `steps` buckets — the repaint gate's change signature |

**Themed colours**: resolved via `dynamic_cast<AppLookAndFeel*>` — `bg1` for the panel, `border` for the frame and zero line, `accent` for the traces, `textMuted` for the caption. Falls back to hardcoded colours when the cast fails (headless).

---

## 10. UI Rendering Performance

Guidelines to preserve smooth frame rates:

- **`ModuleComponent` uses `setBufferedToImage(true)`**. Each module card is composited as a cached image. The `GraphEditor` 30 Hz connection animation blits these cached images rather than re-running JUCE text layout and parameter read on every animation frame. **Do not reintroduce unconditional `repaint()` calls in `timerCallback` on module components or their always-visible children.**
- **Gated 15 Hz repaint**: `ModuleComponent::timerCallback` repaints only when the display needs to change — specifically when RMS level changes, active modulation routing changes, or the sequencer step index changes. The timer runs at 15 Hz (`startTimerHz(15)`).
- **Single re-skin pass on theme switch**: `AppLookAndFeel::applyTheme()` → `sendLookAndFeelChangeMessage()` → one `repaint()`. No timer is started and no continuous repaint is added during or after a theme switch.
- **`applyToolbarIcons()` is gated**: cloning `Drawable` objects is expensive. The call is restricted to narrow-mode transitions in `MainComponent::resized()` — not executed on every resize frame. See §5 for the gate logic.
- **Status bar polls at 5 Hz** and repaints only itself (`repaint()` on `StatusBarComponent` only). There are zero `writeToLog` calls in the status-polling path.
- **TL4-5 automation → UI reflection adds no new timer.** `GraphEditor::timerCallback()`'s existing 30 Hz tick drains `AudioEngine::getAutomationUiFeed()` and calls `ModuleComponent::reflectParameterValue()`, which only ever calls `slider->setValue(..., juce::dontSendNotification)` — no direct `repaint()`; the card's own gated 15 Hz timer and buffered-image cache pick the change up on their own schedule, same as any other control change.
- **All UI animations are time-bounded** (see §11). They run for a finite transition duration and stop when settled. Never add continuous / per-frame animations outside the loading-spinner exception defined in §11.

---

## 11. Animation System (Phase 5)

Phase 5 introduces a shared animation infrastructure in `Source/UI/UIAnimation.h` (namespace `synth::ui`). All motion in the app uses this helper. The `juce_animation` module is linked into both `Core` and the `AgentSynth` app target.

### Easing functions

Pure, stateless helpers — no component state required:

| Function | Signature | Curve |
|---|---|---|
| `easeOutCubic` | `float easeOutCubic(float t)` | Fast-out deceleration |
| `easeInOutCubic` | `float easeInOutCubic(float t)` | Smooth in + out |
| `easeOutBack` | `float easeOutBack(float t)` | Overshoots slightly then settles |

All take `t` in `[0, 1]` and return a mapped `[0, 1]` value (may exceed 1 briefly for `easeOutBack`).

### `AnimationDriver`

VBlank-driven, **time-bounded** tween: animates a progress value from `0` to `1` over a caller-specified duration, then auto-stops. It never ticks beyond `t = 1.0`.

**Usage contract:**

```cpp
// Members in the owning component:
juce::VBlankAnimatorUpdater vblankUpdater_{ this };
synth::ui::AnimationDriver driver_;

// Start a 200 ms transition:
driver_.start(0.20);   // seconds

// In timerCallback / VBlank callback:
float t = driver_.getValue();
auto bounds = AnimationDriver::lerpBounds(startRect, endRect, t);
setBounds(bounds);
if (!driver_.isRunning())
    ; // settled — no more repaints fired
```

**Key properties:**
- Callers must hold both a `juce::VBlankAnimatorUpdater` and an `AnimationDriver` as members (VBlank updater keeps the driver alive and ticking).
- `isRunning()` returns `false` once `t` reaches `1.0` — the driver stops itself and fires no further repaints.
- `AnimationDriver::lerpBounds(from, to, t)` is a static helper that interpolates a `juce::Rectangle<int>` linearly between two positions.

### `formatShortcutHint`

```cpp
juce::String formatShortcutHint(const juce::String& base,
                                const juce::String& shortcutDisplay);
```

Composes tooltip text with an optional keyboard shortcut hint appended in `[brackets]`. Pass an empty `shortcutDisplay` to omit the hint. Used by feature controls to produce consistent `setTooltip()` strings.

### Phase 5 micro-interactions

| Feature | Animation | Note |
|---|---|---|
| **Module drop landing** | Eased tween from drop position → snapped + anti-overlapped final position (`easeOutBack`); `computeDropFinalPosition` is a pure helper | `GraphEditor` |
| **Mod-matrix show/hide** | Bounds tween, `easeInOutCubic` | `GraphEditor` |
| **Library sidebar show/hide** | Bounds tween, `easeInOutCubic` | `MainComponent` |
| **Library section collapse/expand** | Band-height fold (150 ms), `easeInOutCubic` | `ModuleLibraryComponent` |
| **AI panel show/hide** | Bounds tween, `easeInOutCubic` | `MainComponent` |
| **Empty-canvas first-run hint** | Static drawn text; no animation — drawn only when `isCanvasEmpty(nodeCount)` returns `true` | `GraphEditor` |
| **ModuleLibraryComponent rows** | Row hover-highlight; grab/dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()` | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` → `StatusBarComponent` |
| **AI request Cancel + spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner (time-bounded — stops on completion or cancel, confined to its region) | `AIChatComponent` |

### Time-bounded animation rule

**All animations MUST be time-bounded.** An `AnimationDriver` runs for a finite duration and stops at `t = 1.0`. The single permitted exception is the AI thinking spinner — it pulses only while a network request is in flight and stops immediately on completion or cancel. Its repaint is confined to its own component region.

Never add:
- Continuous `timerCallback` repaints on `ModuleComponent` or its children outside the existing gated 15 Hz gate.
- A free-running `AnimationDriver` (no duration, or duration far longer than the visible transition).
- Per-frame `repaint()` calls in any path that is always active (not gated to an active transition).

---

## 6. Alignment Guides (UI Phase 7 - Item 4)

Agent Synth provides **Figma-style smart alignment guides** while dragging modules in the graph editor. These visual cues help you align new modules with existing ones at edges and centers.

### What they are

- **Edge-to-edge snap lines** — appear when a module edge gets within 8 px of another module's edge (left/right/top/bottom)
- **Center alignment lines** — appear when dragging module centers align vertically or horizontally
- **Visual style** — muted-colour lines at ~70% opacity, ~1.5 px stroke width

### How they work

1. When you begin dragging a module, `GraphEditor::updateDragPreview()` scans all existing module rectangles in canvas space.
2. For each edge of the dragged ghost and each neighbor, it computes:
   - Edge-to-edge snap (distance < `Metrics.gridSize` = 8 px)
   - Center alignment (module centerX/centerY matches neighbor's centerX/centerY)
3. Matching positions are stored as `AlignmentGuide` structs and rendered in `paintOverChildren()` using the current theme's `textMuted` colour.
4. Guides are **visual-only** — they do not alter snapping behavior (`findFreeSlot()` still uses the soft 8 px grid).

### Configuration

A toggle is available in `Settings → Appearance → Show Alignment Guides`. It persists as `alignmentGuidesEnabled` in `juce::ApplicationProperties`.

| Toggle State | Effect |
|---|---|
| **ON** (default) | Guide lines appear during drag when alignment candidates are detected |
| **OFF** | No guide lines appear; only the module ghost is drawn |

### Implementation details

- **Grid threshold**: `Metrics.gridSize = 8` px — snap detection window radius
- **Guide opacity**: `Metrics.guideAlpha = 0.7` (70%)
- **Stroke width**: `Metrics.guideLineWidth = 1.5` px
- **Deduplication**: Only the closest guide per type (left/right/top/bottom.centerX/centerY) is shown

### Visual examples

**When dragging:**

- Move module left edge near another's right edge → vertical grey line appears
- Move module top edge near another's bottom edge → horizontal grey line appears  
- Align centerX with neighbor's centerX → vertical centre line appears
- Align centerY with neighbor's centerY → horizontal centre line appears

**Theme switching:**

When you change themes, alignment guides update to the new `textMuted` colour automatically.

---

## 12. Multi-Select, Group Drag, Snippets & Clipboard (issue #156)

### 12.1 Why the gesture is modifier-gated

Drag on empty canvas already meant **pan**, and that predates selection. Rebinding it to
marquee-select (the Figma/Blender/VCV convention) would have retrained every existing pan habit, so
the marquee is gated behind **Shift** instead and pan is untouched.

| Gesture | Result |
|---|---|
| Drag on empty canvas | Pan — unchanged |
| **Shift** + drag on empty canvas | Marquee-select, *replacing* the selection |
| **Cmd/Ctrl + Shift** + drag | Marquee-select, *adding* to the selection |
| Click a module body | Select just that module |
| **Shift**/**Cmd** + click a module | Toggle that module's membership (does **not** start a drag) |
| Drag any selected module | Move the entire selection together |
| Click empty canvas (no drag) | Clear the selection |
| Right-click a module | Select it if it wasn't, then open the menu |
| Right-click empty canvas | Open the canvas menu (Paste Here / Select All) — the selection is **kept**, so the menu can still act on it |

Two details that are easy to get wrong:

- **Deselect-on-click is deferred to mouse-up.** `GraphEditor::mouseDown` only *arms*
  `pendingEmptyCanvasClick`; the first `mouseDrag` clears it. If the clear happened on mouse-down,
  every pan would wipe the selection.
- **A modifier-click never arms the dragger.** `ModuleComponent::bodyDragActive` gates
  `mouseDrag`/`mouseUp`, because `juce::ComponentDragger::dragComponent` must not run when
  `startDraggingComponent` was never called.

### 12.2 SelectionModel — `Source/UI/SelectionModel.h`

Header-only, no Component or graph dependency, so the selection rules are testable headlessly
(`Tests/SelectionModelTests.cpp`).

- `SelectionModel` wraps a `std::set<NodeID>`: `add` / `remove` / `toggle` / `setSelection` /
  `contains` / `retainOnly`. `NodeID{0}` (the graph's invalid-node sentinel) is rejected outright.
- `getSelected()` returns **ascending uid order**, never click order — snippet extraction walks that
  order, and a snippet's node list must not depend on how the user happened to click.
- `retainOnly(alive)` is the staleness guard. `GraphEditor::pruneSelection()` calls it from
  `updateComponents()`, which every node-removing path (delete, undo/redo, preset load) already
  funnels through, so a selection can never name a freed node.
- `hitTestMarquee` uses **intersection, not containment** — clipping a module's edge selects it.
  Requiring full enclosure makes a 560 px `kDoubleWidth` Sequencer practically unselectable when
  zoomed out. A degenerate (zero-area) band selects nothing, so Shift-click on empty canvas
  deselects rather than grabbing whatever is under the cursor.

The marquee band is stored and painted in **canvas coordinates** (in
`GraphContentComponent::paintOverChildren`), so it stays locked to the modules it is selecting while
zoomed or panned.

### 12.3 Selection repaints stay bounded

`ModuleComponent` is `setBufferedToImage(true)`, so repainting every card on every marquee frame
would re-rasterize the whole canvas — exactly what §10 forbids.
`GraphEditor::applySelectionChange()` diffs the old and new selection and repaints **only the cards
whose state flipped**. During a marquee drag that is zero repaints until the band actually crosses a
module boundary.

The selected treatment itself is not new drawing code: `AppLookAndFeel::drawModulePanel()` always had
a `selected` parameter (accent border + themed glow) that was hard-coded to `false` pending a
selection model. `ModuleComponent::paint` now passes `owner.isNodeSelected(nodeId)`.

### 12.4 Group drag resolves as one rigid body

Followers are placed from **their own recorded origin plus the initiator's delta**
(`selectionDragStartPositions`), never by accumulating per-frame deltas — incremental application
drifts at non-1.0 zoom and shears the group apart.

`finalizeSelectionDrag()` then treats the selection as a single rigid body:

1. Union every member's bounds into one group box.
2. `LayoutUtil::snap()` its top-left.
3. `LayoutUtil::findFreeSlot()` for the whole box against **unselected modules only** — members are
   moving together and must not be obstacles to one another.
4. Apply that one offset to every member, and `updateModulePosition()` each so positions reach the
   node properties and survive a save/reload.

Calling the single-module `finalizeModuleDrag()` per member instead would spiral them away from each
other and destroy the arrangement the user just made.

`cancelSelectionDrag()` exists for the zero-delta case (a click that never moved). Preset positions
are not necessarily grid-aligned, so running the finalize path on a drag that did not happen would
visibly nudge the group.

### 12.5 Snippets — `Source/SnippetManager.{h,cpp}`

A snippet is the **same JSON dialect as a preset** (`AIStateMapper::graphToJSON`) narrowed to a subset
of nodes. Stored one file per snippet as
`<userAppData>/<settingsFolder>/Snippets/<name>.agsnip`, mirroring
`ThemeManager::getUserThemesFolder()`.

Three rules define what a snippet contains:

| Rule | Reason |
|---|---|
| Only connections with **both** endpoints selected | A snippet must mean the same thing in every patch it lands in; it cannot assume the destination has the node on the other end. Wires to Audio Output are dropped — re-patch the output after dropping. |
| **No Attenuverter nodes**; modulation is stored in the `modulations` array | Same representation the AI patch format uses. `applyJSONToGraph` rebuilds the attenuverter chain on insert. |
| Positions normalised so the selection's top-left is `(0,0)` | `prepareForInsert()` re-offsets by the drop point, so internal layout is preserved wherever it lands. |

Graph I/O nodes (Audio In/Out, MIDI In) are never captured — they are singletons that already exist in
the target patch.

Extraction **filters `graphToJSON` output** rather than re-deriving the per-node JSON shape, so the
params encoding, MIDI port sentinel and attenuverter→modulations folding keep exactly one owner.

#### Id renumbering is mandatory

`prepareForInsert()` renumbers snippet ids to a fresh range starting at `nextFreeIdBase(graph)` (max
existing uid + 1). This is not cosmetic: `applyJSONToGraph` in **merge mode** treats an incoming node
id that already exists as *"update that node"*, so a colliding id would silently retune an existing
module of the same type instead of adding a copy.

#### Validate strictly, apply faithfully

`insertSnippet()` deliberately splits the two:

```cpp
validatePatch(prepared, graph, /*clearExisting=*/false, /*trusted=*/false);   // strict — reject whole
applyJSONToGraph(prepared, graph, /*clearExisting=*/false, /*trusted=*/true,  // faithful — no rescale
                 /*autoConnectNewNodes=*/false);                              // exact — no extra wires
```

- **Strict validation** because a snippet is a file on disk and can be hand-edited, truncated or
  copied in from elsewhere. A malformed snippet is rejected whole, never applied halfway. This *adds*
  a validation gate the trusted path would otherwise skip — it does not relax `validatePatch` (see the
  invariant in `CLAUDE.md`).
- **Trusted apply** because the untrusted apply path carries an AI-output heuristic: when a
  parameter's range extends beyond `[0,1]` but the incoming value sits inside it, the value is treated
  as normalised and rescaled. Snippet values come from `graphToJSON` already denormalised, so that
  heuristic would corrupt legitimate small values — an LFO `rateHz` of 0.5 Hz (range 0.01–20) would
  land at roughly 5 Hz. Guarded by
  `SnippetInsert.PreservesParameterValuesThatLookNormalised`.
- **`autoConnectNewNodes=false`**, and this one is easy to miss. Merge mode otherwise runs a
  convenience pass that wires every newly created audio node with no outgoing wire straight to
  Audio Output, and every new MIDI-accepting node to an existing MIDI source. That is right for an
  AI merge patch (a model that adds an Oscillator means it to be audible) and **wrong** for a
  snippet, where the absent wires are as deliberate as the present ones — without the opt-out,
  dropping a snippet into the patch it came from splices the copy's leaf modules into the live
  output. Guarded by `SnippetInsert.DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch` and
  `…DoesNotAttachInsertedModulesToAnExistingMidiSource`; the AI-side behaviour it preserves is
  locked by `AIStateMapperTest.MergeAutoConnectsNewAudioNodesToOutputByDefault`.

A snippet is therefore **exactly** its internal wiring: what you selected, nothing the surrounding
patch happened to be connected to, and nothing added for convenience on the way back in.

#### Wiring

`GraphEditor` owns no file dialogs and `ModuleLibraryComponent` owns no filesystem access;
`MainComponent` brokers between them via `GraphEditor::onSaveSnippetRequested` /
`GraphEditor::snippetProvider` and `ModuleLibraryComponent::onSnippetDeleteRequested`.

Snippet drags reuse the **existing** DragAndDrop channel between sidebar and canvas. The payload is
prefixed (`snippet:<name>`, `SnippetManager::kPayloadPrefix`) so `itemDropped` can tell a group drop
from a plain module drop. `itemDragEnter` sizes the landing ghost from the snippet's own bounding box
rather than the single-module estimate table.

Insert is one undoable change (`recordStructuralChange`), and the newly landed group is left
selected — it is what the user will want to move next.

### 12.6 Copy / Paste / Duplicate — `Source/UI/ModuleClipboard.h`

Cmd+C / Cmd+V / Cmd+D, plus the same three actions on the module and canvas context menus.

**They are snippets that never reach disk.** `copySelection()` calls the same
`SnippetManager::extractSnippet` the Save-as-Snippet path calls and parks the result in a
`ModuleClipboard`; paste and duplicate hand it to the same `insertSnippet`. Nothing about the wiring
rules is re-derived, which is the point — the three §12.5 rules give a paste its behaviour for free:

| Snippet rule | What it means for a paste |
|---|---|
| Only connections with **both** endpoints selected | The copies wire to **each other**, never back to the originals. A wire that ran into the group from outside is dropped rather than duplicated onto the copy. |
| Modulation stored as intent | An LFO→Filter routing between two copied modules comes back as a rebuilt attenuverter chain, not as a second wire onto the original attenuverter. |
| Origin-relative positions + fresh id range | The group keeps its internal layout, and `applyJSONToGraph`'s merge mode cannot mistake a copy's id for an existing node and silently retune it. |

`autoConnectNewNodes=false` carries over too, so a pasted module never wires itself to Audio Output.
Guarded by `ClipboardPaste.RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals`,
`…DropsConnectionsThatLeftTheCopiedSelection`, `…DoesNotSpliceTheCopiesIntoTheSurroundingPatch` and
`…RebuildsModulationChainsBetweenTheCopies`.

**Non-parameter state is the one difference from a saved snippet.** `extractSnippet` /
`prepareForInsert` / `insertSnippet` take an `includeExtraState` flag, **off by default**. A `.agsnip`
is a hand-editable file that inserts on the *trusted* apply path, and `applyExtraStateToProcessor`
reads `state` as a filename for `SamplerModule` — so a snippet file must not be able to carry one.
The clipboard has no such exposure: its payload comes straight from the live graph and never leaves
the process, so it opts in and a duplicated Sampler keeps its sample.
(`SnippetExtraState.*`, `ClipboardCopy.CarriesNonParameterModuleStateSoADuplicatedSamplerKeepsItsSample`.)

**Placement.** `SnippetManager::selectionOrigin()` returns the top-left corner extraction normalises
against — the clipboard's anchor. Both paste and duplicate offset from it by
`ModuleClipboard::kOffsetStep` (40 px = 5 grid units, so an offset copy stays on-grid):

- **Cmd+V** cascades: each successive paste steps one more offset down-right, so repeated pastes fan
  out instead of stacking on one pixel and looking like nothing happened.
- **"Paste Here"** (canvas right-click) drops at the click point and re-anchors the cascade there.
- **Cmd+D** offsets one step from the *current* selection and leaves the clipboard untouched —
  Cmd+D must not cost the user what they had copied. Since a duplicate leaves the copies selected,
  repeating it walks a chain across the canvas rather than piling up on one spot.

Neither runs the group through `findFreeSlot`: a fixed offset is predictable (the Figma/Illustrator
convention), the copy is visibly its own card, and it arrives selected and ready to drag.

Both are one undoable change for the whole group, on the same `recordStructuralChange` path as a
snippet drop.

**The clipboard is in-app and in-memory only.** It is deliberately not the system clipboard: Cmd+C on
the canvas must not silently destroy whatever text the user had copied, and text fields keep their
own copy/paste because JUCE's `TextEditor` consumes Cmd+C/Cmd+V before the key reaches
`MainComponent::keyPressed`.

---

## 13. Collapsible Library Sections

With a Snippets section added on top of eight module categories, the sidebar overflows its height.
Every section header is now a disclosure toggle, plus a **COLLAPSE ALL / EXPAND ALL** strip in the top
24 px (`kTopStripHeight`).

- **One layout pass.** `buildRows()` returns `{entryIndex, y, height}` for the currently visible rows
  and is used by *both* `paint()` and `getEntryIndexAt()`. These previously duplicated the y-advance
  arithmetic in two places — a standing invitation for paint and hit-testing to disagree. Guarded by
  `ModuleLibraryStructure.HitTestingAgreesWithTheRowLayout`.
- **Rows carry a `RowKind`** (`Header` / `Module` / `Snippet` / `EmptyHint`) and their owning
  `section`, so collapsing is just "skip rows whose section is collapsed". Headers stay visible.
- **The Snippets section stays visible when empty**, showing a "No snippets yet" hint, so the feature
  is discoverable before the first snippet exists.
- **Chevrons are `juce::Path` triangles, not glyphs** — `▾`/`▸` coverage is not guaranteed across the
  embedded typefaces (see the font limitation in [`theming.md`](theming.md)). The triangle is drawn
  once (pointing down) and rotated by `-90° × progress`, so it turns with the fold; for a square box
  the two endpoints are exactly the shapes the old two-state version switched between.

### Fold animation

Collapsing and expanding tween over `kCollapseAnimMs` (150 ms, `easeInOutCubic`) via
`AnimationDriver` — no free-running repaints, per the animation invariant.

- **`sectionProgress` is purely visual** (0 = open .. 1 = shut). The logical state stays in
  `collapsedSections` and flips *instantly*, so `isSectionCollapsed()`, `areAllSectionsCollapsed()`,
  persistence and `onCollapseStateChanged` never lag a frame behind what the user clicked.
- **One driver for all sections**, so COLLAPSE ALL folds them together instead of racing nine
  animators. Retargeting mid-flight eases on from the current value rather than snapping back.
- **Rows are truncated, not squashed.** `buildRows()` gives each section a band of
  `naturalHeight × (1 - progress)`; rows keep their natural spacing inside it and are clipped at the
  band's bottom edge (`row.height < kItemHeight` marks a partly clipped row; rows past the band are
  dropped, so they stop hit-testing). `juce::Graphics::drawText` does not clip on its own, hence the
  explicit `reduceClipRegion` in `paint()`.
- **It snaps when not `isShowing()`** — there is no VBlank off screen, so a hidden component would
  otherwise freeze mid-fold. This is also what keeps the headless tests deterministic.
  `setCollapsedSections()` (the launch-time restore) always snaps: animating there would look like
  the sidebar folding itself up on startup.
- **Collapse state persists** as newline-joined section names under `libraryCollapsedSections` in
  `juce::ApplicationProperties`. `setCollapsedSections()` skips blank entries, because an unset
  preference arrives from `StringArray::fromLines("")` as a single empty string, and
  `onCollapseStateChanged` deliberately does *not* fire from it — that is the restore path, and
  re-notifying would write back what was just read.

### Scrolling

With every section expanded the rows exceed any realistic panel height, so the sidebar scrolls.

- **No `juce::Viewport`.** The library is a single painted component: rows come from one
  `buildRows()` pass, and it is also the tooltip client and the drag source. A viewport would split
  all three across an outer wrapper and an inner content component — and, because
  `findParentDragContainerFor()` walks to the *nearest* container ancestor, an inner component would
  bind drags to the sidebar instead of `MainComponent`, breaking drops onto the canvas. Instead a
  `juce::ScrollBar` drives a `scrollOffset` that `paint()` and hit-testing both apply.
- **The COLLAPSE ALL strip stays pinned** in the top `kTopStripHeight` px — the one control that
  shortens an overflowing list must never scroll out of reach. `paint()` therefore clips the rows to
  below the strip and `setOrigin(0, -scrollOffset)`s them, then draws the strip last over its own
  background fill.
- **Two coordinate spaces.** `buildRows()`, `getRowCentreY()` and `getEntryIndexAt()` are all
  *content*-space; mouse handlers go through `getEntryIndexAtComponentY()`, which rejects the pinned
  strip and then adds `scrollOffset`. Mixing them up is the failure mode this split exists to
  prevent — guarded by `ModuleLibraryScroll.HitTestingFollowsTheScrollOffset`.
- **`updateScrollBar()` runs after anything that changes content height** — resize, collapse,
  snippet refresh, theme change (the bar's width is the `kScrollbarWidth` token). It shows/hides the
  bar and re-clamps the offset, so a shrinking list can never leave the view scrolled past its end.
- **Rows lose the bar's width** (`getRowContentWidth()`) while it is visible, so row text and the
  snippet count never run under the thumb.

---

## 14. Cable Interaction

### Cables are not graph edges

A **cable** is one wire as the user sees it, which is not the same thing as a
`juce::AudioProcessorGraph::Connection`:

| Drawn as | Backed by |
|---|---|
| Audio / MIDI wire | one graph edge |
| Attenuverter chain | two edges plus a hidden `AttenuverterModule` node |
| Poly bus | `voiceCount` parallel edges |

Anything that identifies, hit-tests, colours or removes a cable therefore keys on the logical
view, not on the raw edge. `GraphEditor::CableId` is that identity
(`srcUid`/`srcPort`/`dstUid`/`dstPort` plus `attenUid`, non-zero only for an attenuverter chain).

### One enumeration for paint and mouse

`GraphEditor::buildVisibleCables()` returns every drawn cable, in paint order, as
`VisibleCable` records carrying geometry, signal kind, source category, activity and bypass
state. **Both** `GraphContentComponent::paint()` and hit-testing consume that one list.

This is load-bearing: computing the drawn curve and the clickable curve separately means they
drift apart the first time either is tweaked, and clicks silently miss the wire. For the same
reason the bezier lives in exactly one place, `GraphEditor::buildCablePath()`, which must stay
identical to `AppLookAndFeel::drawConnectionWire`'s default curve.

### Hit-testing

`GraphEditor::getCableAt(canvasPos, tolerance)` returns the topmost cable within `tolerance`
canvas px (default `kCableHitTolerance` = 7 px, wider than the wire so thin cables stay
grabbable). Distance is the perpendicular distance to the bezier, via `juce::Path::getNearestPoint`
— note that method *returns* the distance **along** the path and writes the nearest point out by
reference, so the perpendicular distance is `pos.getDistanceFrom(nearestPoint)`.

Ties go to the later cable, matching paint order (mod wires draw over audio wires).

### Hover

`mouseMove` resolves the cable under the cursor and stores only its `CableId`. The canvas
repaints **only when the hovered cable changes** — never on every mouse move. Hovered cables are
drawn brighter and one pixel wider by `AppLookAndFeel::drawConnectionWire`'s existing `hovered`
parameter, and the cursor becomes a pointing hand.

This does not violate the no-continuous-repaint invariant: `GraphEditor` already runs a 30 Hz
timer that calls `content.repaint()` for the wire-flow animation, so a hover change only marks
the next frame dirty rather than adding a new repaint source.

Only the `CableId` is retained between frames — geometry is rebuilt each paint anyway, and
holding a stale `VisibleCable` across a graph edit would dangle conceptually (ports move, nodes
disappear).

### Right-click menu

Right-clicking a cable opens a menu with **Disconnect Cable**. Before this, the only way to
remove a connection was to right-click one of its *ports*, which is not where users aim.

`GraphEditor::disconnectCable()` removes every graph edge behind the cable as **one** undoable
action — the whole attenuverter chain via `AudioEngine::removeModRouting()`, or all `voiceCount`
parallel edges of a poly bus. One visible wire, one undo step.

### Colouring

See [`docs/theming.md` §11](theming.md#11-cable-colours) for cable colour modes, the
`cableCategory` palette and the user override layer. `GraphEditor` renders whatever mode and
overrides it is handed via `setCableColourMode()` / `setCableColourOverrides()`; it never reads
`ApplicationProperties` itself.

---

## 15. Minimap Overlay (issue #159)

`Source/UI/MinimapComponent.h/.cpp` — `synth::ui::MinimapComponent`, a small always-current
overview of the graph.

### What it is

An untransformed sibling overlay on `GraphEditor`, the same pattern as `ModMatrixComponent` — it
does not live inside the panned/zoomed `GraphContentComponent`, so its own bounds are plain screen
space. Everything it draws is expressed in **canvas coordinates** (the space `ModuleComponent`s and
cables already live in) and mapped down to the small map area with `computeWorldToMap()`.

### Placement, sizing, auto-hide

Positioned **bottom-left** with a 12 px margin, sized `min(220, w/4) × min(150, h/4)`. Bottom-left
is deliberate: the mod-matrix panel occupies the right-hand 600 px (§5). Below a 480×360 editor the
minimap auto-hides — `GraphEditor::resized()` recomputes `minimapVisible && fits` on every layout
pass against absolute floors (a fraction-of-self test like `w/4 * 2 <= w` is always true, so it
would never actually hide anything). This never clobbers the user's preference: `minimapVisible`
still records what they asked for, and reappears the moment the window grows back past the floor.

### What it draws

Renders from a `MinimapModel` snapshot — nodes, cables, and the current viewport rect, all in
canvas coordinates:

- **Nodes** — filled rounded rects in the module's per-category theme colour
  (`themeColourForCategory`), clamped to a `kMinNodeSize` (2 px) floor so zoomed-out modules stay
  visible; selected nodes get an additional `accent` stroke.
- **Cables** — thin straight lines in the cable's colour at reduced alpha, not the bezier the
  canvas draws — this is a thumbnail, not a second connection view.
- **Viewport** — the area outside the currently visible canvas rect is dimmed with a translucent
  `bg0` wash (an even-odd path fill punches a "hole" over the viewport rect rather than drawing
  four separate bars); the viewport itself is stroked in `accent`.

`computeWorldBounds()` derives what the map actually shows: the union of every node's bounds and
the viewport, inflated by an 80 px margin (`kWorldMargin`), and never narrower/shorter than 1200 px
(`kMinWorldSpan`) per axis, so a single module doesn't blow up to fill the map. All three drawing
and hit-testing helpers (`computeWorldBounds`, `computeWorldToMap`, `mapToWorld`) are pure static
functions with no `Component` state — unit-tested directly (`Tests/MinimapComponentTests.cpp`).

### Interaction

Click or drag on the map converts the event position to a canvas point (`mapToWorld`) and fires
`onNavigate`, which `GraphEditor` wires to `centreViewOn()` — pans so that point is centred; zoom is
unchanged. Scroll wheel fires `onZoom` with the wheel's `deltaY`, wired to `zoomAroundCentre()`.
Both the minimap's wheel-zoom and the canvas's own wheel-zoom (`GraphEditor::mouseWheelMove`) share
one private helper, `applyZoomAt(wheelDelta, screenAnchor)` — the canvas anchors on the mouse
position, the minimap anchors on the visible area's centre, but the zoom curve and the `[0.1, 2.0]`
clamp are identical.

Hovering the map shows a tooltip that leads with the show/hide shortcut, the same way the toolbar
buttons read (`"Hide Minimap  (Cmd+K)  - click or drag to navigate, scroll to zoom"`). The binding
is rebindable, so `MinimapComponent` does **not** depend on `ShortcutManager`: it exposes
`setShortcutHint(displayString)` and `MainComponent::applyToolbarIcons()` resolves the current
binding and pushes it down alongside the toolbar tooltips. Because tooltips embed the resolved
keypress, `MainComponent::updateCommandShortcuts()` (the `ShortcutManager::onBindingsChanged` hook)
re-runs `applyToolbarIcons()` — without that, every toolbar hint *and* the minimap's keeps
advertising the pre-rebind key until some unrelated toggle happens to refresh it.

### Repaint discipline

Follows §10. `setModel()` and `setViewport()` only `repaint()` when the incoming data actually
differs from the current model (`MinimapModel::operator==`, a field-wise comparison over nodes,
cables and viewport). `GraphEditor::timerCallback()` — the existing 30 Hz tick that already drives
the connection-flow animation — pushes a freshly built model via `buildMinimapModel()` **only while
the minimap is visible**, so a hidden minimap costs nothing: no graph walk, no model diffing.
`updateTransform()` (called on every pan/zoom frame) pushes **only the viewport rect** via
`setViewport()`, because panning/zooming changes what's visible, not where modules or cables are —
rebuilding the full model on every drag frame would re-walk every graph edge for nothing.

### GraphEditor API

| Member | Purpose |
|---|---|
| `setMinimapVisible(bool)` | Sets the user preference and re-runs `resized()`'s fits check; also seeds a full model immediately so the map isn't blank until the next 30 Hz tick |
| `toggleMinimapVisibility()` | `setMinimapVisible(!minimapVisible)` |
| `isMinimapVisible()` | The user preference (not the fits-adjusted effective visibility) |
| `getMinimap()` | Direct access to the `MinimapComponent` |
| `getVisibleCanvasRect()` | The canvas rect currently visible — inverse of the content transform applied to `getLocalBounds()` |
| `centreViewOn(canvasPoint)` | Pans so `canvasPoint` is centred; zoom unchanged |
| `zoomAroundCentre(wheelDelta)` | `applyZoomAt` anchored on the visible area's centre |
| `buildMinimapModel()` | Walks every module and `buildVisibleCables()` into a `MinimapModel` snapshot |

### Toolbar, shortcut, persistence

A toolbar toggle (`ToggleMinimap`, right-hand group, before `ToggleModMatrix`) and the **Cmd+K**
shortcut (action id `toggleMinimap`; see [`shortcuts.md`](shortcuts.md)) both call
`GraphEditor::toggleMinimapVisibility()`. Visibility persists under the `minimapVisible` key in
`juce::ApplicationProperties`, default `true`.

## 16. Timeline panel (TL5-1)

`Source/UI/TimelinePanelComponent.h/.cpp` — `synth::ui::TimelinePanelComponent`, a bottom-docked
panel owned by `MainComponent`.

### What it is

For TL5-1 this is only the SHELL later tasks (TL5-2+) build content into: a themed background
(`theme.colors.bg0`-family, same fallback pattern as the toolbar/status-bar/sidebar panels), a
thin top border separating it from the graph editor above, and three placeholder child regions
already laid out in `resized()`:

- **Transport bar** (top strip, `Metrics::timelineTransportBarHeight`)
- **Track-header column** (left, `Metrics::timelineTrackHeaderWidth`)
- **Lanes/ruler area** (remainder) — the only region that currently paints anything, a centred
  "Timeline" placeholder

All three are exposed as public rect getters (`getTransportBarBounds()`, `getTrackHeaderBounds()`,
`getLanesBounds()`) so later tasks and tests build on the same arithmetic instead of re-deriving
it. The component owns no timer and no animation of its own.

### Docking, toggle, shortcut

`MainComponent` carves the panel full-width, directly above the status bar: `resized()` and its
pure-geometry twin `computePanelBounds()` (§ see `PanelBoundsResult::timelineBounds`) remove it
from the bottom AFTER the status bar and BEFORE the AI-panel/library removals, so it spans the
whole window width regardless of which side panels are open.

A toolbar toggle (`ToolbarComponent::Slot::ToggleTimeline`, right-hand group, immediately before
`ToggleTheme`) and the **Cmd+T** shortcut (action id `toggleTimelinePanel`; see
[`shortcuts.md`](shortcuts.md)) both flip `MainComponent::isTimelineVisible`. Visibility persists
under the `timelinePanelVisible` key in `juce::ApplicationProperties`, default `false`.

### Animation

The slide in/out reuses the **same** coordinated `AnimationDriver` that already animates the
library and AI panels (`MainComponent::animatePanelTransition()`, ~190 ms ease-in-out-cubic, one
shared `VBlankAnimatorUpdater`) — no new animator or timer was added. The transition lambda was
extended to also lerp the timeline panel's bounds; showing the panel starts it from a zero-height
rect pinned at its final bottom edge (so it grows upward into place), and hiding it calls
`setVisible(false)` in `onComplete`, same as the sibling panels.

### Build flag

Everything past the always-present `TimelinePanelComponent` member and `PanelBoundsResult`/
`computePanelBounds()` plumbing — the toolbar button, the `toggleTimelinePanel` command, and the
carve itself — is gated `#if SYNTH_ENABLE_TIMELINE`, so a `-DSYNTH_ENABLE_TIMELINE=OFF` build has
no button, no shortcut effect, and never carves the panel into the layout (see the CMake option's
own comment in the root `CMakeLists.txt`).

Contents (transport controls, tracks, clips, ruler) arrive in TL5-2 and later tasks.
