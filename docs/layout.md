# Layout System Reference

This document describes the soft-grid layout model: how modules snap to an 8 px grid, how the
anti-overlap spiral search resolves collisions on drop and drag-release, how `autoArrange`
computes a topological signal-flow layout, and the full `LayoutUtil` API reference.

---

## 1. Soft-Grid Model

Gravisynth uses a **soft grid** — the same approach taken by Max/MSP, Bitwig Grid, and Blender's
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

`paint()` fills the toolbar background with `theme.colors.bg0` via a `dynamic_cast<GravisynthLookAndFeel*>`. When the cast returns null (headless tests or non-themed context), it falls back to the hardcoded colour `0xff0B0D10`.

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

Each category section-header entry in `ModuleLibraryComponent::paint()` draws a 16×16 category icon at `x=10` using `lf->peekIcon(catIcon)`, then shifts the header text to `x=30`. This is null-guarded: when the `GravisynthLookAndFeel` cast returns null (headless tests or assets absent), no icon is drawn and header text falls back to the original `x=10` position.

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

`applyHeaderButtonIcons()` retints all three buttons from the active `GravisynthLookAndFeel`. It is null-guarded: when the LnF cast fails (headless tests), the function returns early and buttons remain imageless but functional. `lookAndFeelChanged()` calls `applyHeaderButtonIcons()` so icons update on theme switch.

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
namespace gsynth::LayoutUtil {

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

} // namespace gsynth::LayoutUtil
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
Gravisynth draws two visual cues over the canvas so placement is predictable and beautiful.

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

Two in-module visualizer components provide real-time signal display inside module cards.

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

**Themed colours**: resolved via `dynamic_cast<GravisynthLookAndFeel*>` — `border` for grid, `textDisabled` for no-signal label, `accent` for the waveform. When the cast fails (headless), hardcoded fallbacks are used (`0xff2A2F38`, `0xff5C6470`, limegreen).

---

## 10. UI Rendering Performance

Guidelines to preserve smooth frame rates:

- **`ModuleComponent` uses `setBufferedToImage(true)`**. Each module card is composited as a cached image. The `GraphEditor` 30 Hz connection animation blits these cached images rather than re-running JUCE text layout and parameter read on every animation frame. **Do not reintroduce unconditional `repaint()` calls in `timerCallback` on module components or their always-visible children.**
- **Gated 15 Hz repaint**: `ModuleComponent::timerCallback` repaints only when the display needs to change — specifically when RMS level changes, active modulation routing changes, or the sequencer step index changes. The timer runs at 15 Hz (`startTimerHz(15)`).
- **Single re-skin pass on theme switch**: `GravisynthLookAndFeel::applyTheme()` → `sendLookAndFeelChangeMessage()` → one `repaint()`. No timer is started and no continuous repaint is added during or after a theme switch.
- **`applyToolbarIcons()` is gated**: cloning `Drawable` objects is expensive. The call is restricted to narrow-mode transitions in `MainComponent::resized()` — not executed on every resize frame. See §5 for the gate logic.
- **Status bar polls at 5 Hz** and repaints only itself (`repaint()` on `StatusBarComponent` only). There are zero `writeToLog` calls in the status-polling path.
- **All UI animations are time-bounded** (see §11). They run for a finite transition duration and stop when settled. Never add continuous / per-frame animations outside the loading-spinner exception defined in §11.

---

## 11. Animation System (Phase 5)

Phase 5 introduces a shared animation infrastructure in `Source/UI/UIAnimation.h` (namespace `gravisynth::ui`). All motion in the app uses this helper. The `juce_animation` module is linked into both `GravisynthCore` and the `Gravisynth` app target.

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
gravisynth::ui::AnimationDriver driver_;

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
