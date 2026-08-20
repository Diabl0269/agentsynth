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
All graph nodes whose processor is a `ModuleBase` subclass (which since TL6-2 includes *Audio
Input*), plus the `AudioGraphIOProcessor` IO nodes — *Audio Output* and, in any patch still
holding one, a raw `audioInputNode`. `AttenuverterModule` nodes are skipped entirely — they are
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
│  [Patch Name]  CPU  RT  Voices  [🔇] │  ← status bar  (height: Metrics::statusBarHeight = 24 px)
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
- **Round trip (TL6-8)**: `RT <n> ms`, immediately after the CPU figure (`x = 236`, 90 px wide), `textMuted`. Drawn only while it *fits* before the voice-count slot — a cramped window drops the segment rather than overlapping two readings — and only once a first reading has arrived
- Voice count: right-aligned before the mute button slot
- `masterMuteButton_` (`DrawableButton`): positioned in `resized()` at `(w-28, 2, 20, h-4)`

**Update contract:**
`update(float cpuPct, int voices, const juce::String& patch)` is gated — it only calls `repaint()` when any value changes by a visible amount (cpu delta > 0.5 %, voice count changed, or patch name changed). It contains **zero `writeToLog` calls**.

`updateRoundTripLatency(double milliseconds, bool available)` is a **sibling** setter with its own gate, so a moving CPU figure never repaints on account of an unchanged latency or vice versa. Its diff is on the **rendered string**, which means a latency drifting below the printed resolution costs no repaint at all. It shows `AudioEngine::getRecordingLatencySamples()` (input device + graph + output device — the amount a recorded take is shifted back by; see [`architecture.md`](architecture.md)'s "Latency alignment (TL6-8)"), fed from the same 5 Hz poll. `available == false` — Hosted mode, where the host owns both ends — draws `RT —` rather than a made-up number.

**Polling rate:**
The status bar polls at 5 Hz, driven by `MainComponent`'s 10 Hz timer via an every-other-tick guard (`statusBarTickCount_`).

**Static format helpers** (headless-testable, no JUCE GUI deps):
- `formatCpu(float fraction)` — fraction is 0..1; `0.756f → "75.6%"`
- `formatVoices(int n)` — `0 → "0 voices"`, `1 → "1 voice"`, `8 → "8 voices"`
- `formatPatch(const juce::String& s)` — empty or whitespace-only → `"Untitled"`
- `formatRoundTrip(double ms, bool available)` — `(12.34, true) → "RT 12.3 ms"`; `(anything, false) → "RT —"`; negative input clamps to `0.0`

### ModuleLibraryComponent section headers

Each category section-header entry in `ModuleLibraryComponent::paint()` draws a 16×16 category icon at `x=10` using `lf->peekIcon(catIcon)`, then shifts the header text to `x=30`. This is null-guarded: when the `AppLookAndFeel` cast returns null (headless tests or assets absent), no icon is drawn and header text falls back to the original `x=10` position.

### ModuleLibraryComponent search

A `juce::TextEditor` is pinned at the top of the library (`kSearchHeight = 32`), above the COLLAPSE ALL strip. Together they form `kPinnedChromeHeight` — the scrollbar, row clip, and hit-testing all start below that band so a scrolled row cannot steal a click from the search field.

Typing a query (trimmed, case-insensitive substring) filters `buildRows()`:

- A module or snippet row is kept when its name contains the query, or when its section header contains the query (so searching `"Time"` shows Delay and Reverb).
- A section with no remaining children is omitted entirely.
- Matching sections are drawn fully open regardless of `collapsedSections`. The stored fold is not rewritten and `onCollapseStateChanged` does not fire, so clearing the field restores the user's collapse state.
- The matching substring is painted with the theme accent (fill + coloured glyphs) via `highlightSpansFor`. A query that matches nothing leaves the list empty and draws "No matching modules".
- Filtering is layout-only: `getDraggableModuleNames()` still returns every factory type.

### ModuleLibraryComponent search

A `juce::TextEditor` is pinned at the top of the library (`kSearchHeight = 32`), above the COLLAPSE ALL strip. The two together are `kPinnedChromeHeight`; rows and the scrollbar live below that band, so the field never scrolls away.

Typing a query (trimmed, case-insensitive substring):

- Hides module, snippet, and empty-hint rows that do not contain the query.
- A section stays visible when its header matches **or** any of its children match. A header match reveals every child in that section (searching "Time" shows Delay and Reverb).
- Matching sections layout as fully open. The stored collapse set is not rewritten and `onCollapseStateChanged` does not fire, so clearing the field restores the fold the user had.
- Matching runs in the visible label are highlighted (accent fill + accent text). `highlightSpansFor` is the pure helper paint uses; tests cover it directly.
- No hits: `buildRows()` is empty and the body draws "No matching modules".

Escape clears the field. `getDraggableModuleNames()` is unfiltered — callers that instantiate via the factory must not see a search-shrunk catalogue.

### ModMatrixComponent chrome

`Source/UI/ModMatrixComponent.h/.cpp` paints rows with the following visual rules:

- **Row height**: `static constexpr int kRowHeight = 48` (was 40)
- **Zebra striping**: odd rows (`isZebraRow(rowIndex)` — `rowIndex % 2 == 1`) are tinted with `theme.colors.surfaceHi.withAlpha(0.45f)`; even rows are transparent (parent background shows through)
- **Hover highlight**: the currently hovered row is tinted with `theme.colors.accent.withAlpha(0.10f)`, overriding the zebra base
- **Hover tracking**: `ModRow::mouseEnter` calls `owner.setHoveredRow(rowIndex)`; `ModRow::mouseExit` calls `owner.setHoveredRow(-1)` only when that row still owns the hover, avoiding races when the cursor moves between rows. The `hoveredRow_` member defaults to `-1` (no hover)
- **Static helper**: `static bool isZebraRow(int rowIndex) noexcept` — exposed for unit tests

### ModuleComponent header button layout

The header area of each module card (`Source/UI/ModuleComponent.cpp`) contains `DrawableButton` instances (not `TextButton`), positioned in `resized()`:

| Button | Bounds | Action |
|---|---|---|
| `deleteButton` | `(w-26, 2, 22, 20)` | Calls `owner.requestDeleteModule(nodeId)` — tooltip "Delete module" |
| `bypassButton` | `(w-50, 2, 22, 20)` | Toggles bypass state — tooltip "Bypass" |
| `muteButton` | `(w-74, 2, 22, 20)` | Toggles mute state — tooltip "Mute" |
| `dualIOButton` | `(w-98, 2, 22, 20)` | FX / Voice Mixer only — splits or merges the stereo jack pair. Tooltip names Dual I/O. |

`requestDeleteModule(NodeID)` is the canonical delete entry point — `deleteButton.onClick` delegates here.

`applyHeaderButtonIcons()` retints the header buttons from the active `AppLookAndFeel`. It is null-guarded: when the LnF cast fails (headless tests), the function returns early and buttons remain imageless but functional. `lookAndFeelChanged()` calls `applyHeaderButtonIcons()` so icons update on theme switch.

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

## 8b. Smart Connections

While placing a module, Agent Synth can **suggest logical cables** to nearby modules and auto-wire them on drop.

### Modes (`Settings → Preferences → Smart connections`)

Persisted as `smartConnectionMode` in `juce::ApplicationProperties`. Default: **When main I/O is free** (`NewAndUnwired`).

| Mode | Library drop | Reposition existing module |
|---|---|---|
| **Off** | Never | Never |
| **New modules only** | Yes | Never |
| **When main I/O is free** (default) | Yes | Yes, when the jacks that would be wired are still free (source output and dest input) |
| **All module moves** | Yes | Yes (dest input must be free; a source that already fans out may still tap a free dest) |

Group multi-select drags never smart-connect. Snippet drops are excluded.

### Behaviour

1. During `updateDragPreview()`, if the mode allows it, cull neighbors whose **module** rects are more than **96 px** edge-to-edge, then score **jack-to-jack** distance (same 96 px cap). A pair is rejected when the source jack sits to the right of the dest jack — that stops wrap-around cables from a neighbor on the right into the dragged module’s left inputs.
2. Score compatible jack pairs with `scoreJackPair` role matching and free destination jacks. In **When main I/O is free** the source output must also be unwired. Stereo requires explicit Left/Right (or Audio L/R) labels — two unlabeled ports (e.g. Math A/B) are never treated as L/R. Cap at the best neighbor’s audio group (stereo L→L / R→R, or mono↔stereo fan of both legs, both-or-neither when a stereo dest has a taken leg) plus one MIDI suggestion. Mod-matrix / attenuverter destinations are skipped in v1. MIDI suggestions are limited to known MIDI sources/destinations (Sequencer, Poly MIDI, MIDI Keyboard, Oscillator, …) because `ModuleBase` defaults `producesMidi()`/`acceptsMidi()` to true for almost every card.
3. Frosted preview cables are drawn in `paintOverChildren` (~40% alpha via `drawConnectionWire`).
4. On drop / `finalizeModuleDrag`, pending suggestions are applied through `connectPorts` (same path as a manual cable drag: poly fans, MIDI, structural pitch/gate).

Library drags cache a short-lived `AIStateMapper::createModule` probe for jack metadata before a real `ModuleComponent` exists.

---

## 8c. Double-click Port Disconnect

Double-clicking a **connected** jack removes every cable on that port — the same path as the right-click **Disconnect** menu (`GraphEditor::disconnectPort`, which fans across every raw channel a visible jack owns). An unconnected jack is a no-op. The first click of a double-click still begins (and immediately ends) a cable drag; the second click is intercepted in `ModuleComponent::mouseDown` (`getNumberOfClicks() >= 2`) so it does not start another drag.

### Preference (`Settings → Preferences → Double-click port to disconnect`)

Persisted as `doubleClickPortDisconnect` in `juce::ApplicationProperties`. Default: **on**. Restored in `MainComponent::initialiseCommon()` so the canvas honours it without opening Settings. When off, double-clicking a jack behaves like two single clicks (cable drag).

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

### ThresholdControlComponent (`Source/UI/ThresholdControlComponent.h`)

Live threshold readout used by Sample & Hold (meter-only, bipolar), ADSR (slider + unipolar meter)
and Comparator (slider + bipolar meter). The Decibels scale is ready for Compressor / Limiter to
adopt later without a new widget.

`TriggerMeterComponent` is a compatibility alias for the same type.

Two presentations:

- **Meter-only**: a thin bar (18 px) matching the original Sample & Hold trigger meter. The module
  keeps its own rotary for Threshold.
- **Slider+meter**: a caption, a level bar, and a linear slider whose thumb is the threshold
  "slice". The bar fills to the current input so the slice can be set by eye.

The bar switches to the `gateWire` colour while the detector is armed. A marker sits at the
**effective** threshold (knob + Threshold CV). Reads `ThresholdMeterSource`
(`getMeterLevel()`, `getEffectiveThreshold()`, `isOverThreshold()`, `getTriggerCount()`).

**Why it is a separate component, not something `ModuleComponent::paint` draws:** `ModuleComponent`
is `setBufferedToImage(true)`, so painting a live meter inside its `paint()` would invalidate that
cached image and re-run the module's text layout on every meter tick — exactly the repaint storm
§10 prohibits. Instead this owns a 20 Hz timer and repaints *itself* only when a displayed value
moves past a visible amount (see `needsRepaint`), leaving the parent's cached image untouched.

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `valueToNormalized` | `static float valueToNormalized(float, ThresholdScale, float minDb)` | Maps native units → [0, 1] |
| `valueToX` | `static float valueToX(float, float x, float width, ThresholdScale, float minDb)` | Maps native units → x offset; bipolar default matches the original meter |
| `needsRepaint` | `static bool needsRepaint(...) noexcept` | Mirrors the `timerCallback` repaint gate |
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
- **All UI animations are time-bounded** (see §11). They run for a finite transition duration and stop when settled. Never add continuous / per-frame animations outside the **two** exceptions defined in §11 (the AI thinking spinner and the timeline playhead), each of which is bounded by an activity, not by a duration.
- **The timeline panel adds no timer.** Its transport poll rides MainComponent's existing 10 Hz tick, only while the panel is visible, and repaints the ruler only when the ruler's own state (time signature, loop trio) changed. The playhead's 30 Hz strip repaint is the §11 exception and runs only while the transport is playing.

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
| **ModuleLibraryComponent rows** | Row hover-highlight; grab/dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()`; search-query substring highlight on matching labels | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` → `StatusBarComponent` |
| **AI request Cancel + spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner (time-bounded — stops on completion or cancel, confined to its region) | `AIChatComponent` |
| **Timeline playhead** | 30 Hz vertical position line, **playing only**, repainting only the strip between its old and new x | `TimelinePlayheadOverlay` (TL5-4) |

### Time-bounded animation rule

**All animations MUST be time-bounded.** An `AnimationDriver` runs for a finite duration and stops at `t = 1.0`.

There are exactly **two** permitted exceptions, and both are bounded by an *activity* rather than by a duration:

| Exception | Runs while | Confined to | Stops |
|---|---|---|---|
| AI thinking spinner (`AIChatComponent::SpinnerDot`) | a network request is in flight | its own 8×8 component | on completion or cancel |
| Timeline playhead (`TimelinePlayheadOverlay`) | the transport is PLAYING | a strip a few px wide, spanning the panel's ruler + lanes | on stop/pause, with one final strip |

Never add:
- Continuous `timerCallback` repaints on `ModuleComponent` or its children outside the existing gated 15 Hz gate.
- A free-running `AnimationDriver` (no duration, or duration far longer than the visible transition).
- Per-frame `repaint()` calls in any path that is always active (not gated to an active transition).

#### The playhead's confinement contract (TL5-4)

A third exception is not granted just because the second was. The playhead earned it by satisfying three clauses, all enforced in code and asserted in `Tests/TimelinePlayheadTests.cpp`:

1. **Playing only.** The 30 Hz `juce::Timer` is started on the play transition and stopped on the stop/pause transition — it never runs while the transport is stopped, so an idle app repaints nothing (`ZeroRepaintsOver100IdleFrames`).
2. **Strip only.** A frame never repaints the component. It repaints the *union of the old and new line strips* (`kStripHalfWidth` px either side of the line), clipped to the bounds; a frame whose rounded x did not move requests nothing at all (`PlayingRequestsConfinedStrips`).
3. **Explicit stop.** Stopping emits exactly **one** final strip — so the line settles on the position playback ended at — and then goes silent (`StopEmitsOneFinalStripThenSilence`).

#### The paint-count pattern

`TimelinePlayheadOverlay` routes **every** repaint it asks for through one protected virtual:

```cpp
protected:
    virtual void requestRepaintStrip (juce::Rectangle<int> strip);   // default: repaint (strip)
```

A test subclasses the component and overrides that seam to count calls and record rects, which turns "how many repaints does an idle app cost?" into an ordinary headless assertion — no peer, no message loop, no screenshot diffing:

```cpp
struct CountingPlayhead : synth::ui::TimelinePlayheadOverlay {
    using TimelinePlayheadOverlay::TimelinePlayheadOverlay;
    int requests = 0;
    void requestRepaintStrip (juce::Rectangle<int> r) override {
        ++requests;
        TimelinePlayheadOverlay::requestRepaintStrip (r);
    }
    void tick() { timerCallback(); }   // the protected timer callback, driven by hand
};
```

**Reuse this pattern for any future timed repaint.** A repaint budget that cannot be asserted is a repaint budget that will regress.

`PianoRollComponent` is the first reuse, and it is a *delegation*, not a fourth exception: the roll maps beats through its own zoom/scroll, so while it is open the overlay confines itself to `getSharedRegion()` (empty while the roll is open — the ruler rows follow the roll's mapping override too, see §16) and hands the roll the drawn beat through `TimelinePlayheadOverlay::LocalPlayheadClient`. The roll draws the line at its own x under an identical `requestRepaintStrip` seam and the identical no-move-no-repaint gate — still one timer for the whole panel, still zero repaints while stopped. See §16 (TL5-8).

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
- **Rows carry a `RowKind`** (`Header` / `SubHeader` / `Module` / `Snippet` / `Plugin` / `Action` /
  `EmptyHint`) and their owning `section`, so collapsing is just "skip rows whose section is
  collapsed". Headers stay visible. The Plugins section sub-groups its rows by format (`VST3`,
  `AudioUnit`, …) behind a non-clickable `SubHeader` row per format — sorted alphabetically by
  format, name-sorted within a group, and shown even for a single format — so a scan with more than
  one plugin format doesn't read as one undifferentiated list; collapsing the section hides its
  sub-labels along with everything else.
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

## 16. Timeline panel (TL5-1 through TL5-10)

`Source/UI/TimelinePanelComponent.h/.cpp` — `synth::ui::TimelinePanelComponent`, a bottom-docked
panel owned by `MainComponent`. This section was written incrementally, one subsection per task
(TL5-1 laid down the shell; TL5-2 through TL5-9 filled it with real content; TL5-10 added the
keyboard/focus rule that arbitrates between it and the graph editor) — each subsection below is
still the reference for its own piece, and this intro is only a map of how they compose.

Region layout, low-rate transport poll aside (see TL5-4 below), everything here is pure
layout-plus-paint with no timer or animation of its own — one region diagram for the whole panel:

```
+====================================================================+  <- resize grab strip
| Transport bar strip  (play/stop/record/loop, BPM, time-sig, ruler   |  TL5-5 (+ TL5-2's
| readout, metronome/count-in .......................... snap combo) |   snap selector)
+---------------------+----------------------------------------------+
| "+ Track"            | Ruler  (bar/beat ticks, loop brace)          |  TL5-3 (+ TL5-2)
| Track header column  +----------------------------------------------+
| (name/colour/M/S/R/  | Clip lanes  <-or->  Piano roll               |  TL5-3 / TL5-7 <-> TL5-8
|  binding chip),      | (one of the two, same rect, playhead overlay |
|  scrolls              | drawn on top of either)                     |  TL5-4 (playhead)
|                       +----------------------------------------------+
|                       | Automation strip (opens by shrinking the     |  TL5-9 (docked, optional)
|                       |  region above by its own fixed height)       |
+---------------------+----------------------------------------------+
```

Keyboard focus is orthogonal to this diagram, not another region: whichever of the graph editor /
clip lanes / piano roll the user last clicked owns Cmd+C/V/D, per the **Keyboard & focus**
subsection at the end of this section (TL5-10).

Inside a `SYNTH_ENABLE_TIMELINE` build, Preferences has a runtime kill switch on top of all of
this: "Show timeline (experimental)" (`PreferencesSettingsTab`, key `timelineFeatureEnabled`,
default **on**). Turning it off hides the user-facing entry points only — the toolbar button
(`MainComponent::toggleTimelineButton`), the Cmd+T command, and the Space play/stop transport key
all become inactive/invisible via `MainComponent::applyTimelineFeatureEnabled()`, which reuses the
toolbar toggle's own hide path if the panel happens to be open. It deliberately leaves everything
else alive: the `TimelineDoc`, its audio-thread publishing, and project load/save all keep working
exactly as before, so turning the preference back on picks up right where the user left off. The
compile-time `SYNTH_ENABLE_TIMELINE` flag itself stays a build/CI concern — this preference cannot
turn the feature back on in a flag-OFF build.

### What it is

The always-present scaffold (TL5-1): a themed background (`theme.colors.bg0`-family, same fallback
pattern as the toolbar/status-bar/sidebar panels), a thin top border separating it from the graph
editor above, and three child regions laid out in `resized()` — the diagram above is what each one
now holds; TL5-1 itself left the lanes/ruler area an unfilled centred "Timeline" placeholder, all
since replaced by the ruler/grid/clips/piano-roll/playhead content the subsections below describe:

- **Transport bar** (top strip, `Metrics::timelineTransportBarHeight`)
- **Track-header column** (left, `Metrics::timelineTrackHeaderWidth`)
- **Lanes/ruler area** (remainder)

All three are exposed as public rect getters (`getTransportBarBounds()`, `getTrackHeaderBounds()`,
`getLanesBounds()`) so every later task and its tests build on the same arithmetic instead of
re-deriving it. The component owns no timer and no animation of its own (TL5-4's playhead overlay
and TL5-9's knob-entry-point aside, each scoped to its own subsection below).

### Docking, toggle, shortcut

`MainComponent` carves the panel full-width, directly above the status bar: `resized()` and its
pure-geometry twin `computePanelBounds()` (§ see `PanelBoundsResult::timelineBounds`) remove it
from the bottom AFTER the status bar and BEFORE the AI-panel/library removals, so it spans the
whole window width regardless of which side panels are open.

A toolbar toggle (`ToolbarComponent::Slot::ToggleTimeline`, right-hand group, immediately before
`ToggleTheme`) and the **Cmd+T** shortcut (action id `toggleTimelinePanel`; see
[`shortcuts.md`](shortcuts.md)) both flip `MainComponent::isTimelineVisible`. Visibility persists
under the `timelinePanelVisible` key in `juce::ApplicationProperties`, default `false`.

### Height: user-resizable, persisted

The panel's height is **not** fixed. `Metrics::timelinePanelHeight` (220) is the **default and the
minimum**, no longer the law:

- **`MainComponent` owns the value** (`timelinePanelHeight_`), and it is the only thing that lays
  the panel out. `resized()`, `computePanelBounds()` and therefore **both ends of the show/hide
  slide** all read the member, never the metric directly.
- **Clamp rule**, applied in `MainComponent::clampTimelinePanelHeight()` on every layout pass, not
  only when the user drags: `[Metrics::timelinePanelHeight, max(metric, 75% of the window height)]`.
  Re-clamping per pass is what stops a height saved on a large window from swallowing a smaller
  window's canvas; on a window so short that 75% falls under the metric, the floor wins. Before the
  first layout (window height still 0) only the floor applies — otherwise construction would clamp a
  persisted height away against a window that does not exist yet.
- **Persistence**: the `timelinePanelHeight` int key (same name as the metric) in
  `juce::ApplicationProperties`, absent by default — absence is what makes the metric the default.
  Written **once per gesture**, on drag end, never per pixel.
- **The grab strip** (`TimelinePanelComponent::kResizeHandleHeight = 5`, `MouseCursor::
  UpDownResizeCursor`) spans the panel's full width along its top edge, *overlapping* the transport
  bar strip: `getTransportBarBounds()` still starts at `y == 0` (the three regions tile exactly, as
  before), but the transport controls inside it are laid out below the strip, so a resize grab never
  lands on a transport button. Idle it paints exactly the hairline the panel already drew there;
  hovered or dragging it brightens to the accent colour with a faint wash, and it repaints **only on
  a hover-state change** (§10–11's repaint discipline).
- **The panel never resizes itself.** Dragging reports the *desired* height — measured absolutely,
  from the panel's pinned bottom edge in screen coordinates, so the owner moving the top edge under
  the cursor can't make the gesture chase itself — through `onResizeHeight` (every drag step,
  unclamped) and `onResizeHeightCommitted` (once, on mouse-up: the cue to persist). The owner clamps,
  stores and re-runs its layout on each step, which is what makes the drag live. That is one
  user-driven layout pass per mouse event, not a free-running animation.

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

Contents (ruler/grid/snap, track headers, the playhead, the transport bar, clip lanes, the piano
roll, the automation strip, and finally the keyboard/focus rule tying it to the graph editor) are
TL5-2 through TL5-10, each documented in its own subsection below.

### TL5-2: ruler, grid, zoom/scroll, snap, loop brace

`Source/UI/TimelineViewState.h` (`synth::ui::TimelineViewState`) is the single pure,
headless-testable beat<->pixel mapping shared by every consumer: `pixelsPerBeat` (zoom, clamped to
`[kMinPixelsPerBeat=1.5, kMaxPixelsPerBeat=512]`), `firstVisibleBeat` (horizontal scroll, clamped
`>= 0`), `beatToX()`/`xToBeat()`, `zoomAroundX(factor, anchorX)` (keeps the beat under `anchorX`
fixed on screen, clamps preserved — see the comment on why the anchor invariant yields to the
`firstVisibleBeat >= 0` clamp at extreme zoom-out), `scrollBeats(delta)`, and `snapBeat(beat,
beatsPerBar)`. No JUCE dependency; `TimelinePanelComponent` owns the one instance and exposes it
via `getViewState()`.

**Snap** (`TimelineViewState::Snap`) — `Off, Bar, Whole, Half, Quarter, Eighth, Sixteenth`. Every
non-`Bar`/non-`Off` value is a **note value** (a fraction of a whole note), the DAW-conventional
reading of the grid selector: `Whole` (combo label `"1"`) = 4 beats — a full 4/4 bar, `Half` = 2,
`Quarter` (label `"1/4"`, the default) = 1 beat, `Eighth` = 0.5, `Sixteenth` = 0.25. `Bar` snaps to
multiples of `beatsPerBar` (`tsNum * 4 / tsDen` off the transport's time signature — same formula
`TransportService::getPosition()` already uses). Ties round up (toward +infinity, beats are never
negative in practice). On top of the division sits a master switch, `TimelineViewState::snapEnabled`
— the piano roll's `Q` button and the panel-wide `Q` key toggle it; `divisionBeats()` (the effective
grid every magnetic edit and grid paint reads) returns 0.0 while it is off, and
`divisionBeatsRaw()` keeps returning the chosen division (what the roll's one-shot quantise uses).
Picking a division from the combo flips the switch back on. The snap choice lives in a
`juce::ComboBox` docked in the transport bar's right-hand side (items `Off/Bar/1/1⁄2/1⁄4/1⁄8/1⁄16`;
`synth::ui::TimelineTransportBar` fills the rest of that strip, left-aligned — see TL5-5 below) and
persists under the `"timelineSnap"` int key (plus `"timelineSnapEnabled"` for the switch) in
`juce::ApplicationProperties`, set via
`TimelinePanelComponent::setApplicationProperties()` (non-owning pointer setter, same shape as
`AIChatComponent::setAccountService()`).

`Source/UI/TimelineRulerComponent.h/.cpp` (`synth::ui::TimelineRulerComponent`) is a thin strip
(`Metrics::timelineRulerHeight = 24`) docked at the top of the lanes region. It owns nothing: a
`TimelineViewState&` (shared with the panel) and an optional `synth::TransportService*` (setter,
non-owning, may be null). `paint()` reads `getPositionSnapshot()` once per frame (message thread,
cheap) for the time signature and loop bounds, draws bar ticks/labels with adaptive density (the
labelled-bar stride doubles until labels are `>= 40px` apart, so they never overlap; per-beat ticks
only appear once `pixelsPerBeat >= 8`) and a bracket over `[loopStartPpq, loopEndPpq]`.
No timer of its own: `repaint()` is called
after one of its own interactions, or by the panel's 10 Hz poll when the time signature or loop range
changed from elsewhere (TL5-4, below). The moving position line is the separate
`TimelinePlayheadOverlay` drawn over it.

**Two interaction zones.** The strip is split horizontally at `height / 2`
(`TimelineRulerComponent::Zone`): the **top** half owns the loop range, the **bottom** half owns the
playhead. The boundary row belongs to the playhead (`y < height * 0.5` is the loop zone). Both
gestures are therefore reachable with no modifier, which is the point — a plain drag anywhere in the
bottom half scrubs the cursor, which is what people expect a ruler to do.

The zone is decided **once**, from the `mouseDown` y, and latched in `gestureZone_` for the whole
gesture. A drag routinely leaves the band it started in (and often the strip entirely), and a
gesture must never change meaning mid-flight.

| Gesture | Effect |
|---|---|
| Press in the bottom (playhead) zone | `transport->locateBeat()` to the snapped beat under the cursor — on **press**, not release |
| Drag in the bottom zone | Keeps seeking as the pointer moves: the cursor follows the mouse (scrub) |
| Press-drag-release in the top (loop) zone | `transport->setLoop(min, max, true)` to the snapped `[start,end]` range (dragging leftwards normalises) |
| Click (no drag) in the top zone, **on a dimmed brace** | Re-arms that range (`setLoop(start, end, true)`), bounds untouched — the inverse of the Cmd+click below |
| Click (no drag) in the top zone, anywhere else | **Nothing.** Deliberate: the loop range is all this half owns, so a stray click can't clear or collapse it |
| Cmd+click (no drag), either zone | Toggles looping off, keeping the existing bounds |

**Locator visibility.** The brace is drawn whenever a RANGE exists (`loopEnd > loopStart`) —
**not** only while looping is armed. Disarming greys it out instead of hiding it, so a loop you set
earlier stays findable (and re-armable) rather than disappearing the moment you switch looping off.
`TimelineRulerComponent::braceStateFor(looping, start, end)` is the pure rule
(`BraceState::None / Inactive / Active`) that `paint()` and the `getBraceStateForTest()` seam both
call, so a drawn brace and an asserted one can't diverge; `braceColourFor()` picks the colour —
`colors.accent` armed, `colors.textMuted` at 70% alpha disarmed. Muted *text*, not a faded accent:
the hover band is already accent at 10% alpha, and a dimmed accent brace over it would still read as
lit. The click target for the re-arm is the brace's whole x-span across the loop half, not the 4 px
bar it paints (4 px is not a click target).

Both drag paths share the same throttle discipline: a command is posted only when the snapped value
changed since the last post, never once per pixel of movement (`TransportService`'s command FIFO
dedupes nothing). `postSeekIfChanged` dedupes on the beat clamped to `>= 0` — the same clamp
`locateBeat` applies — so dragging left of beat 0 can't re-post identical seeks; `postLoopIfChanged`
dedupes on the `[start,end]` pair. `mouseUp` calls its zone's poster again, so it is a no-op when the
last drag update already posted the final value.

**Hover affordance.** `mouseEnter`/`mouseMove` set the hovered zone, `mouseExit` clears it. The
cursor changes per zone (`PointingHandCursor` over the playhead half, `LeftRightResizeCursor` over
the loop half) and `paint()` tints the hovered half with `accent` at 10% alpha, under the loop brace
so the brace stays legible. `repaint()` fires only when the hovered zone actually **changes** — never
per mouse-move pixel — and there is no timer or animation involved (§11).

**Grid + wheel:** `TimelinePanelComponent::paint()` draws the same bar/beat lines directly into the
lanes region below the ruler (bar lines at full `colors.border`, beat lines at 35% alpha — no
dedicated grid tokens exist, and one caller didn't justify adding any). `mouseWheelMove()` is
implemented once, on the panel, with Cubase-style bindings: **plain vertical wheel scrolls the
track rows vertically** (`TimelineViewState::trackScrollY`, shared with the header column — a
scrollbar drag on the header viewport writes the same value back via
`HeaderViewport::onScrolledY`, and `syncTrackScroll()` is the one re-sync point); **Shift+wheel or
a trackpad's own `deltaX` scrolls horizontally** (`deltaY` converted to beats at the current zoom —
a constant *pixel* distance per wheel unit, so the same physical gesture covers less musical time
zoomed in); **Cmd+wheel** (`mods.isCommandDown()`, already Ctrl-on-other-platforms via JUCE) zooms
horizontally around the cursor; **Cmd+Shift+wheel** zooms vertically — it scales
`TimelineViewState::rowHeightScale` within `[0.5, 3.0]`, which multiplies the themed row height in
BOTH `TimelineClipLaneArea::getRowHeight()` and the panel's `layoutTrackHeaders()`, anchored so the
row under the pointer stays put. `mouseMagnify()` (trackpad pinch — deliberate enough to need no
modifier) maps plain pinch to horizontal zoom and Shift+pinch to vertical zoom, on the panel and
inside the piano roll alike. JUCE bubbles an unhandled wheel event from the ruler child up to the
panel, so both regions share identical behaviour from one implementation.

### TL5-3: track headers, binding chips, add-track

`Source/UI/TimelineTrackHeaderComponent.h/.cpp` (`synth::ui::TimelineTrackHeaderComponent`) — one
row per `synth::Track` (`Metrics::timelineTrackRowHeight`, 56 px — shared with the clip-lane area,
TL5-7 below, so header rows and clip rows always line up), living in the panel's track-header
column. The column is a fixed
`"+ Track"` strip (22 px) at the top plus a `juce::Viewport` below it, so a project with more
tracks than fit **scrolls**; rows are never compressed. Both live inside `getTrackHeaderBounds()`,
so the panel's three regions still tile exactly.

**The document is the truth.** A header stores no state of its own: it re-reads name, colour,
mute/solo/arm and binding from the doc in `refreshFromDoc()`, and every edit is written back
through the doc. Headers are rebuilt/refreshed **only** from `TimelineDoc::Listener::timelineChanged`
(`TimelinePanelComponent` is the listener) — no timer, no polling. A notification whose track *set*
is unchanged refreshes the existing rows in place; only an added/removed/reordered track rebuilds
them, so a mute click doesn't destroy the row the user is typing a name into.

**Row contents:** colour swatch (click cycles the palette), a track-kind badge (`"MIDI"` / `"AUD"` /
`"AUTO"`, fixed per `TrackKind` — identity chrome, not a control), name label (double-click to
edit), an `A` button (visible only when `Track::lanes` is non-empty), `M` / `S` / `R` toggles, and
the binding chip. `R` flips `Track::armed` in the document and nothing else — arming is not
recording; the record button and `MidiRecorder::startRecording` live on the transport bar (TL5-5,
below), which looks for the first `armed` track when it starts a take.

**The `A` button** toggles this track's automation lane in the (single, doc-wide) automation strip.
The header only reports the click via `onAutomationToggleRequested` — it never tracks open/closed
state itself, since it can't see whether another track's lane is the one currently shown.
`TimelinePanelComponent::toggleAutomationForTrack` decides: already open on one of this track's own
lanes closes the strip; anything else (closed, or open on a different track) opens the track's first
lane.

**Colour** resolves *only* through `synth::ui::resolveTrackColour` (`Source/UI/TrackColour.h`): the
track's stored `colourArgb` when non-zero, otherwise a deterministic 8-entry palette indexed by the
track's position; a muted track comes back desaturated and dimmed (same hue). The palette is fixed
rather than theme-derived because the add-track flow *writes* the resolved colour into the document —
a theme-dependent value would mean a project opened under another theme came back recoloured. Unlike
`CableColour.h` there is no persisted override layer: the doc already stores the choice.

**Binding chip semantics** — three states, two of them amber (`theme.colors.warning`):

| Track state | Chip | Meaning |
|---|---|---|
| `bindingUuid` resolves | the node's plain display name (e.g. `"Track In"`) | plays through that node |
| `bindingUuid` empty | `"Unbound"` (amber) | never pointed anywhere; the track plays nowhere |
| `orphaned` | `"Missing"` (amber) | it WAS bound and the node is gone — retained, never auto-deleted |

An `Automation`-kind track shows **no chip at all** (`setVisible(false)`, decided in
`refreshFromDoc()`) — that track hosts lanes, and a node binding is meaningless for it; the bottom
half-row is simply empty. `Midi`/`Audio` tracks are unaffected.

The chip always carries a tooltip explaining what it shows and, when amber, how to fix it; the
`"#id"` suffix a bound name used to carry unconditionally is now added only in the re-bind menu, and
only to an option whose display name collides with another live candidate
(`MainComponent::getAvailableTrackInNodes`) — a lone node's name, on the chip or in the menu, always
stays plain.

Clicking the chip does two things: it **selects** the bound node in the GraphEditor's
`SelectionModel` (a highlight only — no canvas scroll, no focus change) and opens a menu listing
every live node of **the type this track's kind can be fed by** — `Track In` for a MIDI track,
`Track Audio` for an Audio track (TL6-4) — **not claimed by another track**, plus
`"New Track In node"` (which likewise creates whichever type the track's kind needs). Offering the
wrong type would let a user bind a track to a node that structurally cannot play it: both modules
match on **kind as well as uuid**, so the result would be a track that silently plays nothing.
Picking one calls `TimelineDoc::setTrackBinding` as one undoable step, then reconciles.

> **A binding is NEVER re-established automatically — least of all by name.** An orphaned track
> stays orphaned until the user picks a node from that menu. Two nodes can carry the same display
> name, and a silent re-bind would quietly play a track through someone else's instrument.

**"+ Track"** (TL6-4; it read `"+ MIDI Track"` and added one outright until audio tracks existed,
and carries the tooltip *"Add a MIDI or Audio track"* so the two-item menu isn't a surprise)
opens a two-item menu — **MIDI Track** / **Audio Track** — whose ids are
`TimelinePanelComponent::kAddMidiTrackMenuId` / `kAddAudioTrackMenuId`. Both entries land on
`TrackHeaderHost` (`addMidiTrack()` / `addAudioTrack()`), and
`TimelinePanelComponent::applyAddTrackMenuChoice(id)` is the headless seam — the same split the
binding and context menus use, since a `juce::PopupMenu` never runs in the test binary.
`MainComponent::simulateAddMidiTrackClick()` / `simulateAddAudioTrackClick()` call straight into it.

**The MIDI entry** is ONE compound undo step (`AppUndoManager::recordCombinedChange`, graph +
timeline in a single transaction, so one Cmd+Z removes all of it and redo restores it with the same
node uuid):

1. add a `Midi` track named `Track N` — **the doc side goes first**. `TimelineDoc::addTrack` refuses
   past `kMaxTracks`, and a node created before that refusal is known stays in the graph with no
   track to play through: `recordCombinedChange` *records* a mutation, it does not roll one back. A
   track with no binding yet is never flagged orphaned, so the intermediate state is inert;
2. create a `Track In` node through `AIStateMapper::createModule` (so it round-trips through
   `graphToJSON`/`applyJSONToGraph` — that is how undo, redo and `.agsproj` reproduce it), assign a
   fresh uuid and mirror it into the processor with `ModuleBase::setNodeUuid`, and place it at the
   canvas' left edge below every existing module (`GraphEditor::findLeftEdgeSlotBelowModules`);
3. **auto-wire only when unambiguous** — if the patch contains **exactly one** MIDI-driven
   instrument (module type `Poly MIDI`, `Oscillator`, `Wavetable`, `Sampler`, `Sequencer` or
   `Poly Sequencer`; MIDI *sources* — `Track In`, `External MIDI`, `MIDI Keyboard` — are excluded),
   connect `Track In -> that node` on the MIDI channel. With none or several, no wire is drawn: a
   chip that reads "bound" over an unwired node is fine, the cable is the user's to draw, whereas
   guessing wrong plays the track through the wrong instrument. Note `acceptsMidi()` cannot be the
   rule — `ModuleBase` returns `true` for every module in the app;
4. bind the track to the new node's uuid and give it the palette colour for its index.

**The Audio entry** mirrors it exactly (one compound step, track added first, factory-created node,
uuid minted and mirrored, placed at the left edge, an `Audio` track named `Audio N` bound to it with
its palette colour), with one difference in step 3: the auto-wire target is never ambiguous,
because the master
bus is a singleton — but there are **two** possible sinks and the order matters. If TL6-3's
`Rec Tap` has already been spliced in front of Audio Output, wiring straight to the output would
route the track **around** the tap and quietly leave it out of every subsequent take, so
`createTrackAudioNode()` prefers the tap when one exists and falls back to Audio Output otherwise.
That makes both orderings compose: a track added *before* the first take is re-spliced by
`ensureMasterRecordTap()`, and one added *after* it lands on the tap directly. Both channels are
wired (`0 → 0`, `1 → 1`).

**Delete track** (right-click a header) is the same compound step in reverse: the track and its
bound `Track In` / `Track Audio` node go together, and come back together.

Headless test seams (a `juce::PopupMenu` never runs in the test binary): `collectBindingOptions()` /
`applyBindingMenuChoice(id)` and `applyContextMenuChoice(id)` are the menus' semantics without the
menu, and `handleChipClick(showMenu=false)` exercises the selection affordance on its own. The row
talks to the app exclusively through `synth::ui::TrackHeaderHost` (implemented by `MainComponent`),
so it is fully testable against a stub with no graph — see `Tests/TimelineTrackHeaderTests.cpp`.

### TL5-4: the playhead

`Source/UI/TimelinePlayheadOverlay.h/.cpp` (`synth::ui::TimelinePlayheadOverlay`) — a transparent,
non-intercepting (`setInterceptsMouseClicks(false, false)`) overlay the panel adds **last** (so it is
topmost) and sizes to `getLanesBounds()`, i.e. the whole ruler + lanes region. Its local `x == 0` is
`lanesBounds_.getX()`, which is also the ruler's origin and therefore exactly `TimelineViewState`'s —
no offset arithmetic anywhere in the overlay. It draws a `kLineWidth = 2 px` vertical line in
`theme.colors.accent` (literal cyan fallback with no themed LnF), full height.

**This is the second of the two exceptions to the no-unconditional-per-tick-repaint rule.** Its
confinement contract, the paint-count test pattern it introduces, and why a third exception is not
free are all in §11 — read that before touching this component.

**Two timers, one of them borrowed:**

| Rate | Owner | What it does |
|---|---|---|
| 10 Hz | `MainComponent::timerCallback` (**existing** timer, `#if SYNTH_ENABLE_TIMELINE`, only while `timelinePanel.isVisible()`) | `TimelinePanelComponent::updateFromTransport(snapshot, outputLatencySeconds)` |
| 30 Hz | `TimelinePlayheadOverlay`'s own `juce::Timer` | re-reads the transport and requests the movement strip — **only while playing** |

The low-rate poll is the **sole** owner of the 30 Hz timer's lifecycle: it sees the play/stop
transition and calls `startTimerHz`/`stopTimer`. The 30 Hz tick deliberately does *not* stop itself
when it notices a stopped transport — one owner is easier to reason about, and a tick after playback
stopped simply finds an unchanged x and requests nothing. Worst case the timer runs for one extra
poll interval, repainting nothing.

`TimelinePanelComponent::updateFromTransport` has a second job: the ruler paints the time signature
and the loop brace, and **nothing else repaints it** when those change from outside its own mouse
gestures (a bundle load, a host tempo map, TL5-5's transport controls). So the poll diffs a small
`RulerTransportState` (time signature + loop trio — the *position* is deliberately excluded, since
the playhead is the only thing that moves with it) and repaints the ruler only on a change; a time
signature change also repaints the panel, whose lanes grid derives its bar spacing from it. The first
poll seeds the struct instead of counting as a change.

**Latency offset.** The drawn beat is `ppq - outputLatencySeconds * (bpm / 60)`, clamped `>= 0`, so
the line matches what is being **heard** rather than the block currently being rendered.
`outputLatencySeconds` comes from the new `AudioEngine::getOutputLatencySamples()` — the open output
device's `getOutputLatencyInSamples()`, `0` in Hosted mode and `0` when no device is open (every
headless test). It is report-only, exactly like `getGraphLatencySamples()`. Graph latency is
deliberately *not* added in: it is patch-dependent, mostly zero, and never compensated anywhere,
whereas the device buffer is the term that actually separates "rendered" from "heard".

**Zoom/scroll while playing.** The old line position is remembered in **pixel** space
(`getLastRequestedLineX()`), never re-derived from the old beat. A zoom changes the mapping, so the
stale pixels that must be repainted are where the line *actually was* — remapping the old beat would
repaint the wrong place and leave a smear. One zoom therefore costs one wider-than-usual strip, which
is the correct trade (repainting extra is safe; missing pixels is not). A loop wrap costs the same:
one strip spanning the jump.

**Stopped** the overlay asks for nothing, but it still *draws*: `paint()` renders the line at the
current position whenever the panel paints for any other reason. Painting is not what the contract
restricts; asking for a repaint is.

### TL5-5: the transport bar

`Source/UI/TimelineTransportBar.h/.cpp` (`synth::ui::TimelineTransportBar`) — play/stop, record,
loop, BPM and time-signature editors, and the bar:beat readout, left-aligned in the transport-bar
strip (the snap combo stays docked right, TL5-2 above). Buttons are **square** — `min(22 px, the
strip height after padding)`, centred in their slot — with `kGap = 7px` between them and `kGap * 2`
between groups; the two editable labels and the readout follow, in that order.

**No SVG assets.** All three buttons are one `GlyphButton` (a `juce::Button` subclass) drawing a
plain `juce::Path` per glyph in `paintButton` — a triangle/square for play-stop, a circle for
record, an open arc with an arrowhead for loop. This mirrors the CLAUDE.md rule that themes never
swap typefaces: a one-off shape for a single caller doesn't earn a new icon asset either.

**Glyph geometry: one centred square, always.** Every glyph is drawn inside the button's shorter
side, inset by `kGlyphInsetRatio` (24%) on each edge — never a fraction of the *width* applied to
both axes, which is what flattened all four glyphs once the panel's 5 px resize grab strip left the
bar ~19 px tall, and is exactly the "really dense" the founder reported. Everything scales off
that square (the loop arc's stroke and arrowhead, the note's head/stem), so the row stays legible at
any strip height. `Tests/TimelineTransportBarTests.cpp::GlyphButtonsAreSquareAndSpaced` pins
squareness and the gaps at both the full and the trimmed strip height.

> **Record-red is deliberately theme-independent.** An engaged record button is
> `TimelineTransportBar::kRecordRedArgb` (`0xFFE53935`) — a filled red circle, a red border and a
> faint red wash behind it — **whatever the theme's accent is**, and themes may not override it. A
> hardware record LED is red on every desk; drawn in a cyan (or green) accent, "armed" stops reading
> as armed at all. It is the one colour on this bar that is not a theme token — every other lit
> glyph (play/stop, loop, metronome) still uses `colors.accent`. Idle record is a neutral outline
> (`colors.textPrimary` at 75%), not a dim red one. `GlyphButton::glyphColour()` is the single
> source for both the paint and the `getRecordGlyphColourForTest()` seam.

**The transport is the truth, read fresh, not cached.** Every button click and every editor commit
reads `TransportService::getPositionSnapshot()` **at the moment of the action**, rather than from a
value this bar remembers between polls:

- **Play/Stop** — one `GlyphButton` whose glyph flips between the two icons on `getToggleState()`.
  The click reads `getPositionSnapshot().playing` to decide `play()` vs `stop()`, so it works
  correctly even if nothing has polled `updateFromTransport()` since the last click (no dependency
  on visual resync happening in between).
- **Loop** — the click reads the CURRENT `loopStartPpq`/`loopEndPpq` off the snapshot and re-posts
  `setLoop(start, end, !looping)`. `TransportService`'s own construction default is `[0, 4)`, so "no
  bounds ever set" and "preserve existing bounds" fall out of the same one-line handler — there is
  no separate "default bounds" case to maintain.
- **BPM label** — a `juce::Label` (`setEditable(false, true, false)`, the same double-click-to-edit
  idiom as the track-name label), whose `onTextChange` calls `transport->setBpm()` — always accepted
  (clamped to `[TransportService::kMinBpm, kMaxBpm]` inside the service), so there is no revert case.
  It is also **draggable**: a nested `BpmDragLabel` overrides `mouseDown`/`mouseDrag` to turn
  vertical movement into a live `setBpm()` call, ±1.0 BPM per 4 px (±0.1 with Cmd held, for fine
  adjustment), anchored to the snapshot's BPM at `mouseDown` so the gesture is reproducible from the
  anchor + total delta regardless of how many `mouseDrag` calls land in between. Double-click and
  drag are independent gestures: JUCE dispatches `mouseDoubleClick` separately from
  `mouseDown`/`mouseDrag`/`mouseUp`, so overriding the latter three does not disturb `Label`'s own
  `editDoubleClick` handling.
- **Time-sig label** — same double-click idiom, parses `"N/D"` and calls `setTimeSignature(n, d)`,
  which validates numerator `1..64` and a fixed denominator set (`1/2/4/8/16/32`) and returns `false`
  **without posting anything** on rejection. The label then reverts to whatever the snapshot is
  CURRENTLY reporting (not a remembered value) — a rejected edit never touched the transport, so the
  snapshot is already the last known-good time signature.
- **`updateFromTransport()`** (the drive seam, called from the panel's existing 10 Hz poll) resyncs
  the play/loop button visuals and the two labels' text from the snapshot — so a Space-bar play
  triggered elsewhere reflects here within one tick — but skips a label mid-edit
  (`Label::isBeingEdited()`): `Label::setText()` unconditionally discards an open editor's contents,
  so a poll landing mid-keystroke would otherwise fight the user's own typing.

**Bar:beat readout** — `TimelineTransportBar::formatBarBeat(ppq, tsNumerator, tsDenominator)` is a
**static, pure** helper (no `Component`, headless-testable on its own): `"BAR.BEAT.TICKS"`, 1-based
bar (zero-padded to 3 digits), 1-based beat (unpadded), ticks = 1/960 of a beat (zero-padded to 3
digits) — `beatsPerBar = tsNum * 4 / tsDen`, the same formula used throughout the timeline panel.
Pinned examples (see `Tests/TimelineTransportBarTests.cpp::FormatBarBeatTable`): `(0.0, 4/4)` ->
`"001.1.000"`, `(5.5, 4/4)` -> `"002.2.480"`, `(3.0, 3/4)` -> `"002.1.000"`. Painted in JetBrains
Mono via `juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1, plain)` —
`AppLookAndFeel::getTypefaceForFont` resolves the default monospaced font name to
`theme.type.monoFamily` (JetBrains Mono in every built-in theme), the same indirection
`AIChatComponent`'s debug console and `SignInDialog`'s code label already use. Repainted **only
when the formatted string changes** — a plain string-diff cache, not a strip-confinement contract
like the playhead's (§11): the readout has no timer of its own and moves only when its owner polls
it, so there is nothing to bound beyond "don't repaint an unchanged tick".
`getReadoutRepaintCountForTest()` is the test seam, the same counting idiom
`TimelinePanelComponent::getTransportUpdateCountForTest()` uses.

**Recording is the one control the bar is not authoritative over.** Starting a take needs an armed
MIDI track, which only `MainComponent` can see (it owns the `TimelineDoc`). The record button's
click computes `!getToggleState()` and reports that as *intent* through
`std::function<void(bool)> onRecordToggled` — it never flips its own toggle state. `setRecordingState(bool)`
is the ONE thing that ever does, called back by the owner with the real outcome. `MainComponent`'s
implementation (installed in `initialiseCommon()`, `#if SYNTH_ENABLE_TIMELINE`):

- **ON** — iterates `timelineDoc.getTracks()` for the first `armed && kind == TrackKind::Midi`
  track. None found -> `setRecordingState(false)` + `statusBar.showMessage("Arm a track to
  record")`, and the transport is left untouched — a rejected request must not have the side effect
  of starting playback. Found -> **record implies roll** (a DAW convention: the record button starts
  the transport if it isn't already playing), then `midiRecorder.startRecording(track, currentPpq)`.
- **OFF** (button click, or the 10 Hz poll noticing `playing -> stopped` while
  `midiRecorder.isRecording()` — the user hit Space/Stop instead of the record button) — both routes
  go through one `MainComponent::commitMidiRecording()`: `midiRecorder.stopAndCommit(doc, undo)`,
  `midiRecorder.hadOverrun()` -> `statusBar.showMessage("Dropped MIDI events during recording")`,
  then `setRecordingState(false)`. One choke point means the explicit and the auto-commit paths can
  never diverge — see `docs/architecture.md`'s MidiRecorder wiring entry (hook 5) for the full
  before/after ordering (armed-check before roll, so a rejected click has zero side effects).

`AudioEngine::setMidiCaptureSink(&midiRecorder)` is the other half of the app-level wiring (feeds
`MidiRecorder::captureBlock` from `AudioEngine::renderNextBlock`'s one collector-merged buffer) —
see `Tests/MidiRecorderTests.cpp` for the model-level coverage and
`Tests/TimelineTransportBarTests.cpp` for the button-to-commit path.

### TL5-6: metronome + count-in

Two more controls join the transport-bar strip, right after the loop button and before the BPM
label: a metronome toggle and a 3-item count-in selector. Both are `TimelineTransportBar` members —
not routed through `TimelinePanelComponent`, which has no other reason to know either setting
exists — and both persist themselves via the bar's own `setApplicationProperties(props)`, restoring
`"timelineMetronomeEnabled"` (bool, default off) and `"timelineCountInBars"` (int 0–2, default 0)
and re-persisting on every change, the same restore-then-persist-on-change idiom
`TimelinePanelComponent` uses for its snap combo. `TimelinePanelComponent::setApplicationProperties`
is a pure forward to the bar's version for these two keys; `TimelinePanelComponent::setMetronome`
forwards a `synth::Metronome*` the same way `setTransport` forwards a `TransportService*`.

**Metronome toggle** — one more `GlyphButton` (`Glyph::Metronome`), drawing a plain "quarter note"
(a filled ellipse notehead + a `juce::Rectangle` stem) rather than a `juce::Path` like its three
siblings — asset-free for the same CLAUDE.md reason the others are. Unlike Record, there is no
owner-side veto: the click directly flips both the button's own visual state and
`synth::Metronome::setEnabled` (via the bar's non-owning `synth::Metronome*`, set through
`setMetronome()`, mirroring `setTransport`'s null-safe contract) — no intent/outcome split is
needed. `setMetronome()` and `setApplicationProperties()` may run in either order: whichever runs
SECOND is what makes the persisted enabled value real, since each applies the last-known value to
the metronome pointer if the other has already been supplied.

**Count-in selector** — a `juce::ComboBox` ("Off" / "1 bar" / "2 bars", `getCountInBars()` returning
0/1/2), read by `MainComponent`'s record flow at the moment Record is clicked — never cached
elsewhere. See `docs/architecture.md`'s Metronome subsection for the full count-in choreography
(locate-back, forced-on click, the punch-in filter) this selector feeds.

Layout: `metronomeButton_` (a square button, like its three siblings) + `kGap` + `countInCombo_`
(64 px) + `kGap * 2`, inserted between the loop button and the BPM label — the bar's fixed-width
strip grows by ~98 px, well inside the timeline panel's normal width.

### Edit-tool strip (Select / Split / Glue / Erase / Mute / Draw)

`Source/UI/EditTool.h` declares the Cubase-style tool row shared by the clip lanes (TL5-7) and the
piano roll (TL5-8): `enum class EditTool { Select, Split, Glue, Erase, Mute, Draw }`, plus
`kAllEditTools` (an `std::array<EditTool, 6>` in that order), `editToolKeyDigit(tool)` and
`editToolName(tool)`. It is deliberately JUCE-free — gesture routing in both editors and the
strip's button wiring all switch on it, and `editToolForKeyChar(int keyChar)` is what
`TimelinePanelComponent::keyPressed()` consults for the number-key mapping, so tool switching is
testable with no UI at all.

**One active tool, owned by `TimelinePanelComponent`.** The clip lanes and the piano roll share the
same lanes rect and only one is ever visible, so a tool row that changed meaning depending on which
editor happened to be showing would be a trap. `setActiveTool(EditTool)` pushes the tool into
**both** `TimelineClipLaneArea::setActiveTool` and `PianoRollComponent::setActiveTool` unconditionally
(dontSendNotification on every strip button, set explicitly rather than via the radio group, since
this method is also reached from a number key or from a test — no button was necessarily clicked)
and lights the matching strip button; number keys and clicking a button are the only two ways a
user reaches it. Switching tools cancels whatever gesture/preview is already in flight in either
editor rather than trying to reinterpret it under the new tool — a half-finished drag has no
meaning under a different tool.

**Numbering follows Cubase**, so the muscle memory transfers: **1** Select, **3** Split, **4** Glue,
**5** Erase, **7** Mute, **8** Draw. **2** (Range Selection), **6** (Zoom) and **9** (Play/Scrub) are
Cubase tools this app doesn't ship yet, and the gaps are reserved **on purpose**:
`editToolForKeyChar` returns `std::nullopt` for them rather than clamping to a shipped tool, so
`TimelinePanelComponent::keyPressed()` leaves those three digits unconsumed and whatever they mean
elsewhere (nothing, today) is untouched. Shipping one of the missing three later costs no rebind —
the digit is already reserved for exactly that tool. See [`shortcuts.md`](shortcuts.md) for the
full non-rebindability rationale (same class of binding as Delete/Escape/Q/L/P).

**The strip itself** is six `juce::DrawableButton`s (`ImageOnButtonBackground`), built unconditionally
in `TimelinePanelComponent`'s constructor (a headless build simply has no icon to draw in them —
`getToolButton(tool)` is never null once the panel exists), one shared radio group id so clicking one
un-toggles the rest, each with a tooltip that carries the digit (`"Split (3)"`, etc. —
`editToolName(tool) + " (" + editToolKeyDigit(tool) + ")"`). Laid out left-to-right in `kAllEditTools`
order (1, 3, 4, 5, 7, 8) immediately left of the snap combo/toggle in the transport bar — both are
"how the next edit behaves" chrome, so they read as one group without pushing the transport controls
off their left-aligned home. `applyToolStripTheme()` (constructor + `lookAndFeelChanged()`) re-applies
each icon from `AppLookAndFeel::getIcon` and sets the active-tool highlight as a **background colour**
(`colors.toolActive`, not a different icon tint — the glyph reads the same lit or not; see
[`theming.md`](theming.md)), null-guarded on both a headless LnF and a headless icon library.

**Custom per-tool cursors** (`Source/UI/ToolCursors.h`, `makeToolCursor(EditTool, const
juce::Drawable*)`) render the SAME already-tinted `Icon::Tool*` drawable the strip button paints
into a 24×24 `juce::Image` and wrap it in a `juce::MouseCursor`, so the cursor can never drift out of
sync with whatever theme is active — there is no separate cursor-only asset or tint step to go stale.
Hotspots are not uniform: **Select** hotspots at the arrow's tip `(4, 2)` and **Draw** at the pencil's
tip `(3, 21)` (both icons have an obvious off-centre working point, the way every DAW places a click
point there); **Split/Glue/Erase/Mute** hotspot at the icon's geometric centre `(12, 12)` (a
scissors' cut happens where the blades cross — the centre — and glue/erase/mute act on whatever is
directly under the pointer, so there is no other candidate point). Headless-safe by construction: a
null icon (asset library not linked in) falls back to a stock cursor per tool — `NormalCursor` for
Select, `CrosshairCursor` for Draw and for the remaining four (no single stock cursor reads as
"split" or "mute", so the crosshair at least telegraphs "a non-Select tool is active"). Both
`TimelineClipLaneArea` and `PianoRollComponent` cache their own six cursors
(`rebuildToolCursors()`), rebuilt only on a theme change — never per mouse-move, since building one
renders an icon into an `Image`.

### TL5-7: clip lanes

`Source/UI/TimelineClipLaneArea.h/.cpp` (`synth::ui::TimelineClipLaneArea`) fills the lanes region
below the ruler (`getLanesBounds()` minus the ruler strip — the same rect the bar/beat grid is
painted into) with per-track rows of `synth::Clip` rects: drag to move, drag an edge to trim, a
context menu to split/duplicate/delete, and marquee (rubber-band) multi-select. Backed by
`synth::ui::ClipSelectionModel` (`Source/UI/ClipSelectionModel.h`), the clip analogue of
`SelectionModel` (§12.2) — a `std::set<synth::ClipId>` with the same add/remove/toggle/
setSelection/retainOnly contract, ordered ascending by id so a batched move or delete always walks
clips in a stable order regardless of click order.

**Ownership.** `TimelinePanelComponent` owns the `ClipSelectionModel` and the lane area
(`getClipSelection()` / `getClipLaneArea()`); the lane area holds the selection model and the
shared `TimelineViewState` by reference, exactly the relationship the ruler already has with the
view state. `MainComponent` forwards its one `AppUndoManager` in (`TimelinePanelComponent::
setUndoManager`), the same gated `#if SYNTH_ENABLE_TIMELINE` wiring block that installs the doc,
transport and track-header host (TL5-3).

**Z-order, and the one relocation this task makes.** `TimelinePanelComponent::paint()` still paints
the bar/beat grid directly, unchanged — that stays the ONE place the grid is painted. The lane area
is added as a child positioned over exactly that same rect, *before* the playhead overlay (added
last). Since JUCE always paints a parent before its children, the result is grid → clips →
playhead with no extra bookkeeping.

**Row geometry.** `Metrics::timelineTrackRowHeight` (56 px, code-only) is the single row height
both the track-header column and the clip-lane area lay out at — see TL5-3 above.
`TimelineTrackHeaderComponent::kRowHeight` is kept as the matching headless literal fallback rather
than deleted, read by both components' `dynamic_cast<AppLookAndFeel*>`-with-fallback pattern.
`TimelineClipLaneArea::computeClipRect(viewState, trackIndex, startBeat, lengthBeats, rowHeight)` is
a pure static function (no doc, no component) — `[beatToX(start), beatToX(start+length)]` × `[row *
rowHeight, rowHeight]` — so geometry is unit-testable with no component or LookAndFeel at all.

**Rendering.** Every track in doc order gets one row; every clip on it, a rounded rect filled with
`synth::ui::resolveTrackColour(track.colourArgb, trackIndex, track.muted || clip.muted)` (§TL5-3's
resolver, reused rather than re-invented — dimmed when **either** flag is set, since the two are
independent in the model: a clip the user muted individually dims exactly like one sitting on a
muted track), a name (width > ~40 px) and a thin pitch-mapped note preview (width > ~24 px,
clip-relative note beats offset by the clip's current — possibly mid-drag — start). A muted clip
keeps its shape, its selection border and its waveform/notes, and loses only the fill/border
brightness and its name label's alpha (`kMutedClipLabelAlpha`) — painted straight from
`synth::Clip::muted` on the doc, **never** from a `TimelineSnapshot`, which no longer contains a
muted clip at all (see `docs/architecture.md`'s TimelineSnapshot section). Selected clips get a
brighter border and a slight fill lift. Repaints happen only on: doc changes (`refreshFromDoc()`,
routed in from `TimelinePanelComponent::timelineChanged()`, which also calls
`ClipSelectionModel::retainOnly` so a clip removed by any path can never stay selected), view-state
changes (zoom/scroll/snap), and interactions — never a timer.

**Interactions (Select tool).** Everything below is `EditTool::Select`'s gesture table — the tool
this component grew up with, and the only one that drags, resizes, marquees or double-click-authors
(see **Edit tools** further below for what the other five tools do instead; switching away from
Select disables all of this):

| Gesture | Effect |
|---|---|
| Click a clip | Selects it (replacing the selection unless Shift/Cmd/Ctrl, which toggles just that clip) |
| Click empty lane space | Deferred: only a press that never becomes a drag clears the selection on mouse-up — the same `pendingEmptyCanvasClick` trick `GraphEditor::mouseDown/mouseUp` uses for the canvas, so a drag is never mistaken for a deselect |
| Drag from empty lane space | Marquee — intersection hit-test (`clipHitTestMarquee`), additive with Shift; there is no drag-to-pan here (scrolling is wheel-only, TL5-2), so a plain drag also starts a (non-additive) marquee |
| Drag a clip's body | Moves it (and every other selected clip) by one Snap-quantised beat delta shared across the whole selection, computed from the clip that was grabbed and clamped so no clip's start goes below 0, **plus** one shared track-row delta (see **Cross-track drag** below) — no longer same-track-only |
| **Alt** + drag a clip's body | Copy-drag: the originals stay exactly where they are (doc and screen both); release commits a `duplicateClip` + `moveClipToTrack` per dragged clip at the destination, and the **copies** end up selected. There is no Alt-**click** action — copying a clip onto itself isn't something anyone asks for by clicking |
| Drag within 6 px of the right edge | Resizes (trims) the clip's length, Snap-quantised, floored at 1/16 beat |
| Drag within 6 px of the left edge | Moves the start and shrinks/grows the length so the **end** stays fixed; the clip's notes (clip-relative) travel with it — a deliberate divergence from per-note-anchored trimming, deferred to a later task |
| Right-click a clip | `PopupMenu`: **Split at pointer** (enabled only when the Snap-quantised pointer lands strictly inside the clip), **Glue with next** (enabled only when a legal join target exists — greyed out, not hidden, so the menu's items never move around), **Duplicate**, **Mute**/**Unmute** (one toggling item, labelled for what the click will do), **Rename…** (opens the inline editor — see below), **Delete**, and — for an audio clip only (non-empty `assetRef`), below a separator — **Relink audio…** (TL6-6) — preserves the existing selection, the same rule `GraphEditor`'s cable/canvas menus follow |
| Double-click a clip | Opens the piano roll on it (`onClipDoubleClicked` → `TimelinePanelComponent::openPianoRoll`) |
| Double-click empty lane space | Authors content on the row under the pointer — see **Adding content** below (MIDI: a one-bar clip; audio: a file chooser; automation: nothing) |
| Drop audio files from the OS | Imports the first readable one onto the audio row under the cursor — see **Adding content** |
| Delete / Backspace | Deletes every selected clip as ONE undo step; returns `false` (key falls through) when the selection is empty |
| Escape | Clears the selection; returns `false` when it is already empty |
| P | **Loop the selection** — sets the transport loop to the selected clips' `[min startBeat, max endBeat]` span and arms looping; returns `false` when nothing is selected |

**Cross-track drag.** A plain (non-copy) move drag previews **one shared track-row delta** for the
WHOLE dragged set, derived from the vertical drag distance (`round(dy / rowHeight)`), legal only if
**every** dragged clip's destination row exists and accepts its payload — `TimelineDoc::
moveClipToTrack`'s kind rule: an audio clip (non-empty `assetRef`) only onto a `TrackKind::Audio`
row, a MIDI clip only onto `TrackKind::Midi`, neither onto `Automation`. An illegal drop for **any**
clip in the set clamps the whole group's row delta back to 0 — a same-lane move, i.e. exactly what
this drag did before it could cross tracks — rather than dropping only the clips that would have fit
and silently tearing the selection apart. `getPreviewRowDeltaForTest()` is the row delta the
in-flight drag would apply; a copy-drag never moves the originals on EITHER axis
(`effectiveGeometryFor` and `effectiveRowFor` share the same `copyDrag_` guard — they must agree or
the original slides while its row holds), so the destinations paint as translucent ghosts
(`paintDragGhosts`: source-track colour at 0.4 alpha plus a 1 px outline, no name label — a real
blur would be per-frame image filtering, which §10–11 rules out) while the source clips keep
painting where they are. Ghost geometry comes from one helper (`dragGhostRectFor`) shared with
`getDragGhostRectsForTest()`, the same single-enumeration reasoning as `buildVisibleCables()`.

**Inline rename.** `beginRenameClip(ClipId)` (reached from the context menu's **Rename…**) opens a
`juce::TextEditor` over the clip's name area, pre-filled and `selectAll()`ed: Return commits through
`renameClip()` (which calls `TimelineDoc::setClipName` — trims, rejects a blank result, one undo
step), Escape cancels, and losing focus **commits** (the same three outcomes every other in-place
rename in this app has — clicking away is a commit, not a cancel). Any rename already in flight
commits first if a second one opens, and the editor detaches itself **before** either outcome runs,
so the `onFocusLost` callback its own teardown fires re-enters to a null editor and stops rather than
double-committing. `getRenameEditorForTest()` is the test seam (a live `juce::TextEditor` is no more
testable than a `juce::PopupMenu`).

**One undo step per gesture.** Every drag/trim previews locally (a member offset or length, read
back by `paint()` through `effectiveGeometryFor()`) and commits to the doc exactly once on
mouse-up, through `AppUndoManager::recordTimelineChange` — a multi-clip move or a multi-clip Delete
is one `recordTimelineChange` call however many clips it touches, mirroring
`GraphEditor::dragSelectionBy()`/`finalizeSelectionDrag()` and `deleteSelection()`'s one-transaction
contract for modules. `showMenuAsync` never runs headlessly, so the context menu's Split/Glue/
Duplicate/Mute/Delete actions are exercised in tests through `applyClipContextChoice(ClipId, choice,
pointerBeat)` — the same menu-without-the-menu idiom `TimelineTrackHeaderComponent::
applyBindingMenuChoice`/`applyContextMenuChoice` already establish (TL5-3). `ClipContextChoice` also
carries `Rename`, which is deliberately **inert** in this function — renaming opens a
`juce::TextEditor` rather than mutating the doc, so its commit path is `renameClip()` and this enum
case exists only so the menu's whole vocabulary is enumerable (a test can assert the choice mutates
nothing).

**Edit tools (Split / Glue / Erase / Mute / Draw).** `handleToolMouseDown()` routes every non-Select
tool: Split/Glue/Erase/Mute act **immediately on press** (a DAW's tool click is expected to land
under the finger, not on release) and hit-test a clip first — a click on empty lane space with any
of these four held does nothing at all, deliberately, rather than falling through to a selection
change (which would make an Erase click look like it selected something). All four route straight
into `applyClipContextChoice` — **the tool and the menu item are the same code path**, which is what
keeps "the tools are an accelerator for the menu" literally true: `Split` → `SplitAtPointer` at the
Snap-quantised pointer beat, `Glue` → `GlueWithNext` against `findGlueTarget(id)` (the clip on the
SAME track with the smallest `startBeat` at or after `id`'s end — a gap is a legal join target,
since `TimelineDoc::joinClips` treats a gap as silence and only rejects an overlap; picking the
abutting clip only would make the tool silently inert on the very arrangement, clips with gaps,
where gluing is most useful), `Erase` → `Delete`, `Mute` → `ToggleMute`. None of the four ever starts
a drag, a marquee, a trim or the double-click authoring gestures — every mouse-move and mouse-up is
a no-op while one of them is active, so a mis-aimed drag can never silently move or trim a clip
instead of doing the click action.

**Draw** is the one exception with a drag: press anchors on the **floor-snapped** beat of the row
under the pointer (`floorSnappedBeatAt` — the grid line at or *before* the click, so the clip lands
in the cell it was aimed at) on a **Midi** row only (an Audio row's content is an imported asset and
an Automation row's is breakpoints — neither is something a pencil can draw); drag grows a ghost
rect to the **ceil-snapped** beat under the pointer (`ceilSnappedBeatAt`, floored at one snap
division — or `kMinClipLengthBeats` with Snap off — so a drag that has entered a cell always
includes the whole cell); release commits. A press that never dragged falls back to
`createMidiClipAt` — the SAME one-bar-clip authoring gesture the empty-lane double-click uses (see
**Adding content** below), so a plain pencil click and a plain double-click land in the same place.

**Split/Draw preview seams — repaint only on a state change.** Both previews are gated on the
previewed STATE changing, never on raw pointer movement, mirroring the paint-count discipline
`TimelinePlayheadOverlay::requestRepaintStrip` established: `updateSplitPreview(pos)` recomputes the
hovered clip + snapped beat, compares against the cached pair, and only then calls the virtual
`requestToolPreviewRepaint(region)` with the union of the old and new preview rects — pointer
movement inside one snap cell over the same clip costs zero repaints. `updateDrawGesture`/
`commitDrawGesture` follow the identical rule for the Draw ghost rect. `requestToolPreviewRepaint`
is the ONE seam both previews cost through (`getSplitPreviewForTest()` / `getDrawGhostRectForTest()`
expose the state itself, so a test can assert on it without decoding pixels) — a test subclass
overrides it and counts, the same pattern `PianoRollComponent::requestRepaintPreviewStrip` and the
playhead overlay's own seam use. `mouseEnter` re-applies the active tool's cursor and `mouseExit`
drops the Split preview, so a hover line never survives the pointer leaving the lanes.

**Relink audio… (TL6-6).** Offered whenever the clicked clip's `assetRef` is non-empty, whether the
asset currently resolves (a plain re-point) or is missing (see `docs/architecture.md`'s
asset-management section for the missing-asset placeholder this same field drives). Unlike the
three actions above, this is a plain callback (`onRelinkAudioRequested`) rather than a
`ClipContextChoice` — it needs a host `juce::FileChooser` and a `synth::AssetManager` import,
neither of which `TimelineClipLaneArea` has. `MainComponent::promptRelinkClipAsset` opens the
dialog and calls `relinkClipAsset(id, chosenFile)`; the headless path,
`MainComponent::relinkClipAssetForTest(id, chosenFile)`, calls the same function directly and
never goes through the menu (or `showMenuAsync`) at all. The import lands in the current bundle's
`Audio/`, or — with no bundle yet — the app-data `Recordings/` convention TL6-3 takes use; every
other clip that shared the OLD ref is rewritten alongside the clicked one, as ONE undo step.

**Adding content.** Recording and the AI tools are not the only ways in: both gestures below author
content directly, and each is ONE undo step.

*Double-click empty lane space.* The row under the pointer decides what happens. A **Midi** row gets
a new clip at `floorSnappedBeatAt(x)` — the snap grid line at or *before* the click, never after it,
so the clip lands in the cell it was aimed at — one bar long (the transport's time signature, 4
beats with no transport), auto-named `"Clip N"` from the row's clip count, selected, and then fired
through the SAME `onClipDoubleClicked` hook a clip double-click uses, so the user lands straight in
the piano roll ready to draw notes. An **Audio** row asks for a file through `audioFileChooser_` — a
`std::function` seam defaulting to a real async `juce::FileChooser` filtered by
`juce::AudioFormatManager::getWildcardForAllFormats()`, which a test replaces with a lambda that
answers synchronously (`juce::FileChooser`, like `showMenuAsync`, never runs in a test process) — and
reports the choice through `onAudioFileDropped`, exactly as a file drop does. An **Automation** row,
and a double-click below the last row, do nothing.

*OS file drag-and-drop.* `TimelineClipLaneArea` is a `juce::FileDragAndDropTarget`.
`isInterestedInFileDrag` is EXTENSION-based (`AudioFormatManager::findFormatForFileExtension`) — no
dragged file is ever opened — and true when at least one file qualifies. `fileDragMove` highlights
the **audio** row under the cursor with an accent wash, repainting only the rows involved and only
when the row actually changes (`fileDragExit`/`filesDropped` clear it); a MIDI row, an automation row
and the space below the last row neither highlight nor accept a drop. `filesDropped` reports the
FIRST readable audio file (a multi-file drop makes one clip, not N).

*Who imports.* Neither gesture imports anything here: `onAudioFileDropped(TrackId, snappedBeat,
File)` hands the decision outwards and `MainComponent::importAudioFileToClip` does the work, for the
same reason **Relink audio…** does — the lane area owns no `AssetManager` and no bundle root. That
method reuses `relinkClipAsset`'s policy verbatim: a saved project imports into the bundle's `Audio/`
(`AssetManager::importAudioFile`), and an unsaved one into the app-data `Recordings/` convention
`chooseTakeFiles()` writes takes into, so `saveToFile`'s existing `adoptRecordingsAssets` sweep moves
it into the bundle on the first save. The clip's length is the file's own duration in beats
(`audioFileLengthInBeats`, at the transport's current bpm), `sourceStartSeconds` is 0, and the clip +
its asset binding are batched into ONE `recordTimelineChange`. A failed import (unreadable or
non-audio) reports through the status bar and mutates the document not at all. Headless seam:
`MainComponent::importAudioFileToClipForTest`.

*Empty-row hint.* A row with no clips paints one dim line, centred, straight from doc state — no
timer, no animation, `Theme::Colors::textMuted`: **"Double-click to add a clip — or arm (R) and
record"** on a Midi row, **"Drop an audio file — or arm (R) and record"** on an Audio row, and
nothing on an Automation row (its content is breakpoints, authored in the automation strip). The line
is dropped rather than truncated when the row is shorter than 24 px or narrower than the text plus
its padding.

**Panel-scoped Delete key.** The lane area grabs keyboard focus on `mouseDown` (same as
`GraphEditor::mouseDown`), so pressing Delete right after a click lands on `TimelineClipLaneArea::
keyPressed` rather than whichever panel had focus before. This is the *local* half of Delete-key
arbitration — this section's final **Keyboard & focus** subsection (TL5-10) formalises the
cross-panel rule that decides which of the graph editor / clip lanes / piano roll a given keypress
belongs to in the first place.

**P = loop the selection** (Cubase's locators-to-selection) rides on that same local half: an
unmodified `P` handled in `TimelineClipLaneArea::keyPressed`, **not** a `ShortcutManager` command,
for exactly the reason Delete/Escape aren't (see [`shortcuts.md`](shortcuts.md) and TL5-10 below) —
a bare letter in the app-wide table would fire from any panel that doesn't consume it first. The
lane area owns no transport: `getSelectedClipSpan()` computes `[min startBeat, max endBeat]` across
the selection (any row, ignoring anything unselected) and hands it outwards through
`std::function<void(double startBeat, double endBeat)> onLoopRangeRequested`, which
`MainComponent` wires to `transport.setLoop(start, end, /*enabled=*/true)` — the same
"the lane decides what, the owner does it" division as `onAudioFileDropped`/`onRelinkAudioRequested`
above. Nothing selected (or no owner listening) returns `false` so the key keeps its meaning
elsewhere. The piano-roll surface has no equivalent yet.

Tests: `Tests/TimelineClipLaneTests.cpp` — `ClipSelectionModel`/`clipHitTestMarquee` unit coverage,
pure-geometry tests for `computeClipRect`, and interaction tests driven by hand-built
`juce::MouseEvent`s (same pattern as TL5-2's ruler tests in `Tests/TimelinePanelTests.cpp`) against
a bare `TimelineDoc` + `AppUndoManager` + `TimelineClipLaneArea`, no `MainComponent` needed. The
authoring gestures are split across three files, each covering the half it owns: the lane area's
(group 7 there — snapping, one-bar length, one undo step, the injected chooser, drag interest and the
row highlight, the hint text and its paint), the panel's (`Tests/TimelinePanelTests.cpp` group 6 — a
new clip really opens the piano roll), and the import's (`Tests/AssetManagerTests.cpp` group 2 —
saved-bundle vs `Recordings/` destination, length from the file, failure mutating nothing).
`Tests/TimelineClipEditingTests.cpp` covers the edit-tool layer added on top: each tool's
click-acts-immediately behaviour and its empty-space no-op, `findGlueTarget`'s gap-bridging,
Alt-copy vs plain move, the cross-track kind check (legal drop, illegal drop clamping the whole
group back to 0), the split/draw preview seams' repaint-only-on-change discipline (via a counting
subclass overriding `requestToolPreviewRepaint`), the inline rename's commit/cancel/focus-loss
paths, and the clip clipboard's audio-field round trip (see the clip clipboard subsection below).

### TL5-8: piano roll

`Source/UI/PianoRollComponent.h/.cpp` (`synth::ui::PianoRollComponent`) is a minimal per-clip note
editor shown INSIDE the timeline panel's lanes region — no separate window. Backed by
`synth::ui::NoteSelectionModel` (`Source/UI/NoteSelectionModel.h`), `ClipSelectionModel`'s sibling
keyed on `synth::NoteId` with the identical add/remove/toggle/setSelection/retainOnly contract,
plus a `noteHitTestMarquee` free function mirroring `clipHitTestMarquee`.

**Entry/exit.** Double-clicking a clip in `TimelineClipLaneArea` fires its `onClipDoubleClicked(ClipId)`
callback, which `TimelinePanelComponent`'s constructor wires to `openPianoRoll(ClipId)`. That call:
hides `clipLaneArea_`, shows `pianoRoll_` (same rect — see Z-order below), and calls
`PianoRollComponent::openClip`, which frames the clip: the pitch scroll centres on its median note
pitch (60 for an empty clip), and the roll's own horizontal mapping is set so the clip's START sits
at the keys column's right edge, zoomed so the whole clip fits the grid width (clamped to
`TimelineViewState`'s pixels-per-beat bounds). A drawn "◀ Clips" back button (top-left of the header strip) and Escape (when
nothing is selected — a first Escape only clears the selection) both call the roll's own
`requestClose()`, which closes itself immediately (so `isOpen()` is accurate even with no owner
wired) and fires `onCloseRequested` — the panel wires this to `closePianoRoll()`, which re-shows
`clipLaneArea_`. If the edited clip disappears from the doc (any mutation, from any path —
`TimelinePanelComponent::timelineChanged()` calls `pianoRoll_.refreshFromDoc()` on every doc
notification, mirroring the clip-lane area's own refresh seam), `refreshFromDoc()` notices the clip
is gone and closes the roll the same way. Panel API: `openPianoRoll(ClipId)` / `closePianoRoll()` /
`isPianoRollOpen()`.

**Z-order.** `pianoRoll_` occupies the exact same rect `clipLaneArea_` does (`gridLanesBounds_`),
added via `addChildComponent` (not `addAndMakeVisible`, so it starts invisible) right after
`clipLaneArea_` and before `playhead_` — so only one of clip-lane-area/piano-roll is ever visible,
and the playhead overlay stays topmost and untouched either way (same bounds, same
`viewState_.beatToX` mapping it always had).

**Coordinate system — the roll owns its own horizontal mapping.** The keys column is a real 44 px
GUTTER: `PianoRollComponent::beatToX(absBeat)` is `kKeysColumnWidth + (beat - firstVisibleBeat) *
pixelsPerBeat` against the roll's OWN `TimelineViewState` member (`rollView_`), so `x ==
kKeysColumnWidth` is the first visible beat and the clip's opening bar is reachable. (It previously
was not: the column was an opaque strip painted OVER the leftmost 44 px of the shared mapping, with
clicks there ignored — the first bar of a clip parked at x == 0 was unclickable and half-hidden.)
The roll's zoom and scroll are its own; the panel-wide `TimelineViewState` is still shared, but for
exactly ONE thing — the **snap division** (`snapBeat`/`divisionBeats`), so the roll's gridlines, its
snapped edits and the panel's snap selector can never disagree.

**Playhead delegation** is the consequence, and the rule. `TimelinePlayheadOverlay` maps beats
through the SHARED view state, so inside the roll's rect its x is simply wrong. Instead of adding a
second timer, the overlay grew a delegation seam: `TimelinePlayheadOverlay::LocalPlayheadClient`
(`isLocalPlayheadActive()` + `setPlayheadBeat(absBeat)`), plus `setLocalPlayheadRegion(rect)` — the
client's rect in the overlay's own coordinates, set by `TimelinePanelComponent::resized()` (the
sibling offset is only known there). While a client is active the overlay:

- confines `paint()` and every repaint strip to `getSharedRegion()` — everything **above** the
  client's rows, i.e. the ruler strip only; and
- hands the client the DRAWN beat (position minus output latency, clamped ≥ 0) on **every**
  `refreshLine`, *before* its own "did my x move?" gate — a differently-zoomed mapping can move on a
  frame where the shared one didn't.

`PianoRollComponent` then runs the identical confinement contract on its own side: its
`requestRepaintStrip(Rectangle<int>)` is the paint-count seam (same pattern, same test style as
`TimelinePlayheadTests.cpp`), a beat whose rounded x is unchanged requests **nothing** (so a stopped
transport costs zero repaints), a moved beat requests the union of the old and new strips clipped to
the grid rect, and the FIRST beat after an open costs exactly one strip (there was no line on screen
yet). The line is drawn in `paint()` before the keys column and the header, so both clip it exactly
the way they clip a note that has scrolled off to the left. There is still exactly ONE playhead
timer in the whole panel — the overlay's, playing-only.

Vertically, pitch maps at `pixelsPerSemitone_` px/semitone (default `kPixelsPerSemitone` = 10,
Cmd+Shift+wheel scales it within `[kMinPixelsPerSemitone, kMaxPixelsPerSemitone]` = `[4, 40]`);
`firstVisiblePitch_` names the HIGHEST pitch drawn at the grid's top row, clamped to `[0, 127]`. A
20 px header strip sits above both the keys column and the grid, housing the back button and a "Q"
(quantise) button — both plain `juce::Path`/text shapes, never a Unicode glyph through a themed font
(the same "draw it, don't asset it" rule `TimelineTransportBar`'s `GlyphButton` follows, and the
reason `TimelineViewState::divisionBeats` below and every label here go through `AppLookAndFeel` —
see the CLAUDE.md font-swap invariant).

**Gridlines** are drawn from state alone, faintest level first so a bar line always wins a shared
pixel: the current snap division at alpha 0.14 (only when it is finer than a beat — `Snap::Off` has
no division at all), beats at 0.30, bars at 0.75. Each level is dropped entirely when its spacing
falls under `kMinGridLinePixels` (3 px), the same adaptive-density idea the panel's own grid uses.
`visibleLineRange(spacingBeats)` is the ONE range computation `paintGridLines` and the
`getGridLineCountForTest` seam both walk, so the assertion can never drift from the paint. Changing
the snap selector repaints the roll (`TimelinePanelComponent`'s `snapCombo_.onChange`); no timer.

**Rendering.** Row backgrounds alternate white/black-key tint (`colors.bg1` / `colors.surfaceHi`,
the same tokens `ModuleComponent`'s themed `MidiKeyboardComponent` uses), a C-octave label per
octave in the keys column (mono font via `juce::Font::getDefaultMonospacedFontName()`, resolved to
the theme's mono family by `AppLookAndFeel::getTypefaceForFont` — never a raw family string), and
the clip's span outside `[clipStart, clipEnd)` dimmed. Notes are rounded rects tinted by velocity
(`colors.midiWire`, brightened by `velocity/127`), selected notes get an accent border. Repaints
happen only on doc/listener refresh, interaction, and view-state changes — no timer.

**Gestures (Select tool)** — everything below is `EditTool::Select`'s table; the other five tools replace it entirely (see **Edit tools** further below) (each previews locally — a member delta/length/velocity read back by `paint()` through
`effectiveGeometryFor()` — and commits ONCE via `AppUndoManager::recordTimelineChange` on mouse-up,
so a multi-note move/scrub/delete is one undo step):

| Gesture | Effect |
|---|---|
| **Single click on empty grid** | DESELECTS (click-through). Creates nothing, writes no undo step. |
| **Double-click on empty grid** | Creates ONE note: pitch from the row, start snapped, length exactly **one snap division** (1 bar quantise → a 1-bar note; 1/4 → a quarter; 1/16 beat when Snap is Off), velocity 100, channel 1. Selected, one undo step. Snapping up past the clip's end steps back one division rather than creating nothing. |
| **Drag from empty grid** | Marquee (intersection hit-test) — a plain drag REPLACES the selection; Shift or Cmd/Ctrl makes it additive. A press that never crosses the drag threshold is still just the deselect click above (same deferred-click promotion the clip lanes use), and there is nothing to retrain because the roll has no drag-to-pan (scrolling is wheel-only) |
| Click a note | Selects it; **Shift** toggles it in/out of the selection, **Cmd** adds it (never removes — the drag that may follow scrubs the whole selection) |
| Drag a note's body | Moves it + every other selected note, by one shared snapped beat delta and one shared semitone delta |
| Drag within 5 px of a note's right edge | Resizes (trims) just that note's length, Snap-quantised, floored at one division (1/16 beat when Snap is Off) — even inside a wider selection |
| **Double-click a note** | Deletes it, one step (standard DAW idiom — the mirror of double-click-to-create) |
| Delete / Backspace | Deletes the selection, one step; returns `false` when the selection is empty |
| Escape | Clears the selection; closes the roll when nothing is selected |
| Cmd+vertical-drag on a note | Scrubs velocity, ~1/px, clamped to `[1, 127]` independently per note (multi-selection scrubs all by the same delta) |
| "Q" button (plain click, or the `Q` key) | **Toggles snap** — flips the shared `TimelineViewState::snapEnabled`, so grid magnetism switches off/on everywhere (roll, clip lanes, ruler) while the chosen division survives underneath. Paints lit (accent fill/border) while snap is effective, muted while off. A view-state toggle, never a document edit — no undo step. Fires `onSnapToggled` so the panel persists the choice and repaints the other grid painters. |
| Shift+click on "Q" (or Shift+`Q`) | **One-shot quantise**: snaps the SELECTED notes' starts to the chosen division (per-note `moveNote`, one mutation lambda — `TimelineDoc::quantiseNotes` has no note-subset overload); with **nothing selected it quantises every note in the clip** via `quantiseNotes` directly. Reads `divisionBeatsRaw()`, so it works even while the snap toggle is off (cleaning up free-hand notes is its whole point). Flashes on every press, and writes NO undo step when the clip is already quantised (`recordTimelineChange` drops no-op mutations). |

**Edit tools (Split / Glue / Erase / Mute / Draw).** `handleToolMouseDown(pos)` routes every
non-Select tool exactly the way `TimelineClipLaneArea::handleToolMouseDown` does for clips: Split/
Glue/Erase/Mute act on a single click and hit-test a note first (a click on empty grid with one of
these four held does nothing — no deselect, no marquee). `performSplit(id, pos)` cuts at the
snapped, CLIP-relative beat under the pointer via `splitBeatFor`, which returns `nullopt` (no-op)
unless the cut leaves at least `kMinNoteLengthBeats` on BOTH sides — a split that would leave a
sliver on either side would silently mean "resize to nothing" instead of "split". `performGlue(id)`
absorbs the next note of the **same pitch** via `glueCandidateFor` — the smallest `startBeat` at or
after the clicked note's own end; gaps ARE bridged (the glued note runs from the clicked note's
start to the absorbed note's end), which is what makes gluing a staccato pair into one sustained
note possible at all. `performErase(id)` deletes it. `performMuteToggle(id)` flips `MidiNote::
muted` (`TimelineDoc::setNoteMuted`) — a muted note paints **outline-only** (no fill, dimmed border)
straight from the doc's flag, the note-level twin of the clip lane's dimmed-fill treatment; a note
inside a muted clip already contributes nothing to the run (`TimelineSnapshot` excludes the whole
clip), so a note's own mute only matters inside an unmuted clip. **Draw** anchors on the
floor-snapped beat of a mousedown, grows a length preview on drag (`getDrawPreviewLengthForTest()`),
and on release either creates the dragged-length note or — for a press that never dragged — falls
back to the SAME one-note-per-division gesture the Select tool's double-click-on-empty-grid
performs. The Split tool's hover preview (the clip-relative cut beat under the pointer) repaints
only through its own seam, `requestRepaintPreviewStrip` (see below), never the playhead's
`requestRepaintStrip` — a hover that repainted the playhead's strip would be a bug, not a rounding
difference, which is why the two are separate virtuals a test can count independently.

**Note clipboard.** `PianoRollComponent` owns its OWN clipboard (`ClipboardNote`, distinct from
`TimelineClipLaneArea`'s clip clipboard) as a MEMBER — it deliberately outlives `openClip()`, so
"copy here, open another clip, paste there" works. Each entry stores its offset **relative to the
earliest selected note** (not an absolute or clip-relative beat), which is what lets a copied block
survive being pasted into a different clip at a different position: the block keeps its internal
shape and only its anchor moves. Every field a note carries is captured, `muted` included — a muted
note pastes back muted, the same way a split or a duplicate carries the flag.

- `copySelectedNotes()` captures the selection; `canPasteNotes()` is true only with a non-empty
  clipboard AND an open clip (a roll with nothing open has nowhere to put the block).
- `pasteNotesAtPlayhead()` anchors the block at the snapped, CLIP-relative playhead position when
  it lands inside `[0, clip length)`, else at `0.0` — a playhead parked outside the edited clip
  still pastes something visible rather than nothing. `MainComponent::perform` **primes** the
  playhead immediately before pasting (`setPlayheadBeat(transport.getPositionSnapshot().ppq)`): the
  roll only has a playhead position because the overlay PUSHES one via `setPlayheadBeat` while
  playing, so a stopped transport would otherwise paste at whatever beat playback last stopped
  pushing rather than under the position the user can actually see. `buildPastedNotes` applies the
  same clip-window policy every edit here does: a note landing at/after the clip's end is skipped,
  one that would overrun it is clamped, and one with less than `kMinNoteLengthBeats` of room left is
  skipped rather than shrunk below the editor's floor.
- `duplicateSelectedNotes()` copies the selection to immediately after its own span (`max end - min
  start`, same pitches), one undo step, selects the copies — and does **not** touch the clipboard
  (duplicating isn't copying; stomping a clipboard the user filled deliberately would be a
  surprise).
- `cutSelectedNotes()` is copy then delete, one undo step (the clipboard is filled first, so a cut
  is always paste-able).
- `selectAllNotes()` selects every note in the open clip.
- `repeatSelectedNotes(count)` places `count` copies of the selection block, each one span further
  along, **clipped at the clip's end**: placement stops at the first block that would fall entirely
  outside the clip rather than piling every remaining copy onto the last beat. One undo step; every
  created note ends up selected.

All six mirror `TimelineClipLaneArea`'s clip clipboard verbs one-for-one (see the TL5-7 subsection
above) — the two clipboards are entirely separate stores, so copying clips never makes Paste live
on the roll or vice versa; see [`shortcuts.md`](shortcuts.md) for the surface-routing table that
picks which one Cmd+C/V/D/X act on.

**Arrow-key editing.** `PianoRollComponent::keyPressed()` handles Left/Right/Up/Down directly
(matched on key CODE, since `juce::KeyPress::operator==(int)` also requires no modifiers, which
would miss Shift+Up) and returns `false` — falls through — when the selection is empty, so the keys
keep whatever meaning they have elsewhere with nothing selected. `nudgeSelectedNotes(direction)`
moves the WHOLE selection by one shared grid-division delta (the current snap division, or
`kMinNoteLengthBeats` with Snap off) — never per-note, which would silently reshape a chord —
clamped so the group's earliest start never goes below 0 and its latest end never crosses the
clip's length, the same "clamp the group together" rule the drag gestures use.
`transposeSelectedNotes(semitones)` moves every selected note by one shared semitone delta, clamped
into `[0, 127]` as a group so a chord transposes as a chord and never collapses at the pitch
extremes: Up/Down is one semitone, **Shift**+Up/Down is a full octave (12 semitones), the same
octave-jump convention every DAW uses. Both return `true` (consumed) even when the clamp left
nothing to move — the key WAS applicable, it just had nowhere left to go. **Alt+Left/Right
navigates BETWEEN notes** instead of moving them: `selectAdjacentNote(forward)` walks
`Clip::notes`' canonical (startBeat, pitch, id) order by index — no second ordering is defined that
could drift from the doc's comparator — anchoring forward on the selection's LAST selected note and
backward on its FIRST, and collapses to a single-note selection on the neighbour just outside the
block, so repeated presses sweep the clip. At either end the selection is kept and the key still
consumed (the same rule as a fully-clamped nudge); selection-only, so no undo step ever; an
off-screen target scrolls into view minimally via `setHorizontalView` (horizontal only — Alt+Up/Down
stays unhandled/reserved, and yanking the vertical view on a horizontal walk would lose the user's
place). Alt was chosen over Cmd/Shift: plain arrows already nudge, Shift+Up/Down is the octave, and
Cmd+arrows carry OS-level jump-to-boundary semantics. Tool-switching digit keys
are deliberately **not** handled here at all (see the edit-tool strip subsection above) — that
binding belongs to the panel, so the roll and the panel can never disagree about which tool is
active.

**No pencil-by-default any more.** Creating a note is the double-click; a single click on empty grid
is a plain deselect, and a plain drag from empty grid does nothing at all. Marquee is reached only
through Shift, decided entirely at `mouseDown`. JUCE dispatches `mouseDoubleClick` from
`internalMouseUp` — i.e. AFTER the second `mouseDown`/`mouseUp` pair — so the deselect (or the
select-a-note) has already happened by the time the create/delete runs; the double-click is the last
word either way.

The Q button's flash is a **one-shot** `juce::Timer` (`kQuantiseFlashMs` = 120; `timerCallback()`
stops the timer on its first call) repainting only the button's own rect — bounded, never a running
animation, per the CLAUDE.md repaint invariant. The tooltip is served by `juce::TooltipClient`
resolved by position (`getTooltipFor(Point<int>)`), since the header's buttons are drawn shapes, not
child `juce::Button`s.

**Edits stay inside the clip window.** `TimelineDoc` itself only clamps a note's `startBeat >= 0`
finite — "notes can only exist inside the clip" is this editor's own policy, enforced before every
write: draw/resize clamp `[start, start+length)` into `[0, clipLength)`; a multi-note move clamps
its shared delta so the group's earliest start never goes below 0 and its latest end never crosses
`clipLength` (the same clamp-the-group-together reasoning as `TimelineClipLaneArea::mouseDrag`'s
Move branch, extended with an upper bound because notes, unlike clips, live inside one). A note
whose available room shrinks to zero-length is rejected by `TimelineDoc::addNote`'s own validation.

**Wheel.** All four bindings are handled here and NOTHING bubbles to the panel — the roll's
zoom/scroll are its own, so the shared `TimelineViewState` must not move when the wheel lands inside
the roll:

| Binding | Effect |
|---|---|
| Cmd+wheel | Horizontal zoom around the beat under the cursor (`rollView_.zoomAroundX`, anchor = `x - kKeysColumnWidth`), same `exp(deltaY * 2.0)` factor as the ruler's zoom so the two feel identical |
| Cmd+Shift+wheel | Vertical zoom — scales `pixelsPerSemitone_` within `[4, 40]`, keeping the pitch under the cursor put |
| Shift+wheel, or a trackpad's `deltaX` | Horizontal scroll (`kScrollPixelsPerWheelUnit` px per unit, converted to beats at the roll's own zoom) |
| Plain wheel | Vertical (pitch) scroll, `kPitchScrollSemitonesPerWheelUnit`, clamped to `[0, 127]` |

Zoom is not persisted across opens — `openClip` reframes to the clip every time.

`TimelineViewState::divisionBeats(beatsPerBar)` (factored out of `snapBeat` for this task) exposes
the EFFECTIVE snap grid as a plain beat value (0.0 while the snap toggle is off);
`divisionBeatsRaw()` is the chosen division regardless of the toggle — what `performQuantise()`
feeds `quantiseNotes`/`moveNote`, since neither takes a `TimelineViewState::Snap` directly.

**The ruler above the roll shows the clip's REAL timeline position.** While the roll is open,
`TimelinePanelComponent::openPianoRoll` installs the roll's own view state into the ruler
(`TimelineRulerComponent::setMappingOverride(&roll.getRollViewState(), kKeysColumnWidth)` — the
offset is the keys gutter, which sits right of the ruler's x == 0). Labels, ticks, the loop brace
AND the ruler's drag-to-loop / drag-to-scrub gestures all map through the override (the snap
division still comes from the shared state), so opening a clip parked at bar 6 shows "6" at the
gutter's edge rather than wherever the lanes were scrolled. The roll fires
`onHorizontalViewChanged` on every zoom/scroll so the panel can repaint the ruler; close restores
the shared mapping. While the override is live the playhead overlay's shared-mapping line would be
a lie in the ruler strip too, so the overlay's local-client region covers the ruler rows as well as
the roll's (`TimelinePanelComponent::resized`) — the roll draws the only playhead line.

Tests: `Tests/PianoRollTests.cpp` — `NoteSelectionModel`/`noteHitTestMarquee` unit coverage (mirrors
`TimelineClipLaneTests.cpp`'s groups 1–2) and `PianoRollComponent` interaction tests driven by
hand-built `juce::MouseEvent`s against a bare `TimelineDoc` + `AppUndoManager` +
`PianoRollComponent`, no `TimelinePanelComponent` needed. The gesture table is pinned test by test
(single click deselects and creates nothing; double-click creates one note per snap division, at two
divisions; double-click deletes; click-select-then-drag moves, one step), as are the first-bar
reachability fix, zoom-around-cursor's fixed point (plus "the shared view state never moves"), the
gridline-density seam, and the local playhead — the last through `CountingRoll`, a subclass
overriding `requestRepaintStrip`, exactly as `TimelinePlayheadTests.cpp` does for the overlay.
Note: `juce::MouseWheelDetails` has no default member initialisers, so wheel tests construct it
`{}`-initialised or a garbage `deltaX` decides the branch.
The edit-tool/clipboard/arrow-key layer is covered in the same file: each tool's click-acts-immediately behaviour, `splitBeatFor`'s both-sides-must-fit rule, `glueCandidateFor`'s gap-bridging, the outline-only muted-note paint, the Draw tool's drag-vs-click fallback, the note clipboard's cross-clip survival and its clip-window clamps on paste/repeat, and the arrow keys' shared-delta clamp with an empty selection falling through.

### TL5-9: automation strip

A horizontal strip docked at the BOTTOM of the lanes region (`gridLanesBounds_`), toggled open by
selecting a lane — from the lane picker inside the strip itself, or from ANY generic auto-UI knob's
right-click menu (`ModuleComponent` → `GraphEditor::onAutomateParameterRequested` →
`MainComponent::automateParameter`). While open it takes exactly `Metrics::
timelineAutomationStripHeight` (72, code-only) off the bottom of `gridLanesBounds_`, so
`TimelineClipLaneArea`/`PianoRollComponent` (and the playhead overlay, trimmed the same amount)
shrink by that much — never the other way around, and the ruler/track-header column are untouched.

**Strip chrome** (`TimelinePanelComponent`'s own members, laid out in `resized()`): a header row —
four tool `juce::TextButton`s (glyphs `P` / `✎` / `╱` / `⌫`, radio-grouped so exactly one is down),
a lane-picker `juce::ComboBox` (every doc lane, labelled `"NodeName · paramId"` via
`TrackHeaderHost::getNodeDisplayName(lane.nodeUuid)` — falling back to the uuid's first 8
characters when it doesn't resolve — the SAME interface the track-header binding chip already
uses, so no second graph-aware seam was added), a record-mode `juce::ComboBox` (Off/Read/Touch/
Latch/Write, 1-based combo id = `LaneRecordMode` + 1) bound to `TimelineDoc::setLaneRecordMode`
through `AppUndoManager::recordTimelineChange` (a manual selector change IS a user gesture, unlike
`AutomationRecorder`'s own programmatic Write-drops-to-Touch-on-stop call — see that setter's
header comment), and a close `✕` button — above `synth::ui::AutomationLaneEditor`, the curve
canvas. Panel API: `showAutomationLane(LaneId)` / `closeAutomationStrip()` /
`isAutomationStripVisible()`; headless hooks `applyAutomationLaneMenuChoice(int)` /
`applyAutomationRecordModeChoice(int)` (juce::PopupMenu/ComboBox don't run in a test — the same
"headless hook" idiom every other timeline sub-component's context menu already follows).

**`Source/UI/AutomationLaneEditor.h/.cpp`** (`synth::ui::AutomationLaneEditor`) is the curve canvas,
editing ONE `synth::AutomationLane` at a time. X is the SAME shared `TimelineViewState` the clip
lanes use (so it lines up with the playhead pixel-for-pixel — the piano roll is the one surface that
maps beats through its own zoom/scroll instead; see TL5-8); Y maps the lane's own
`RangeSnapshot [min..max]` linearly onto the component's height, top = max
(`valueToY`/`yToValue`). The curve is sampled every ~2 px by building a local
`TimelineSnapshot::Point[]` from the lane's breakpoints and calling `AutomationKernel::evaluate`
with a fresh `AutomationCursor` — paint is not hot, so re-deriving this on every repaint (rather
than caching it) is deliberate: it is the SAME evaluator the audio thread uses, so the canvas can
never show a shape real playback wouldn't produce.

**Tools** (`AutomationLaneEditor::Tool`, set by the strip's header buttons):

| Tool | Gesture |
|---|---|
| Pointer | Drag a HANDLE moves it — beat snapped via the shared view-state snap, value clamped to the lane's range; tension/curve carry over untouched. Drag a SEGMENT (not a handle — hit-tested first) scrubs the segment's LEFT point's tension, ±0.01 per vertical pixel, clamped to `[-1, 1]` (`AutomationKernel`'s own "shape comes from the LEFT point" contract). Double-click empty space adds a point at that (beat, value), Linear/tension 0. |
| Pencil | Freehand drag collects raw (beat, value) samples (no snapping — that's the point of freehand); on mouse-up they are thinned by `synth::AutomationRecorder::thinPoints` (reused, not re-implemented — its RDP helper is `public static` precisely so a second caller can reach it) at the SAME `kThinningEpsilonFraction` scaled to the lane's own range, and replace whatever existed inside the dragged beat span. |
| Line | Drag previews a straight line from press to release; mouse-up replaces the dragged span with exactly the two (snapped) endpoints, Linear. |
| Eraser | Drag removes every handle it touches — collected into a set as the pointer passes over them (dimmed in the preview), deleted on mouse-up. |

Right-click a SEGMENT shows Hold/Linear (ticking the current one), routed through the headless
`applySegmentCurveChoice(beat, curve)` hook. Right-click a HANDLE shows `{Delete point}`. Escape
clears in-flight tool-drag state and returns `true`; when idle it returns `false` so the key falls
through to `TimelinePanelComponent`'s own `keyPressed` (added for this task), which closes the
strip — the same ancestor-chain fallthrough `TimelineClipLaneArea`/`PianoRollComponent`'s own
panel-scoped Delete/Escape already relies on, one level further up.

**One gesture, one mutation.** Every preview above is strictly component-local (a handful of
`preview*_` members, read back by `paint()`) and NEVER touches the doc during `mouseDrag` — commit
happens exactly once, on mouse-up. The subtlety: `TimelineDoc`'s own single-point mutators
(`addBreakpoint`/`removeBreakpoint`) each bump the revision counter independently, so a gesture that
touches several points (Pencil's thin-and-replace, Line's remove-span-then-add-two-endpoints, a
Pointer move that lands on a different beat, Eraser's multi-point sweep) calling them in a loop
would cost one revision bump — one audio-thread republish — PER POINT instead of per gesture. TL5-9
therefore adds one new batched primitive, `TimelineDoc::editBreakpoints(laneId, removeBeats,
addPoints)`: removes every existing point at a beat in `removeBeats`, then inserts every point in
`addPoints` (validated/clamped exactly like `addBreakpoint`), as ONE `applyMutation` call however
many points move either way. Every multi-point gesture above routes through it; only the genuinely
single-point ones (tension scrub, curve toggle, double-click-add, record-mode select) still call a
plain single mutator, because those already cost exactly one bump on their own.

**Knob entry point.** `ModuleComponent`'s generic auto-UI slider branches (`createControls()`'s
float/int cases) attach `this` as a `MouseListener` on the slider (`addMouseListener(this, false)`
— safe because `this` outlives every child slider, both being torn down together in
`~ModuleComponent()`). `ModuleComponent::mouseDown` checks `e.eventComponent != this` FIRST (a hit
on a child fires the SAME override, in the CHILD's local coordinate space, which the body-click
geometry further down must never see) and, on a right-click, shows `"Automate '<Param>'"` via a
`juce::Component::SafePointer<ModuleComponent>` (the popup's callback is async — the module can be
gone by the time it fires) that calls `owner.onAutomateParameterRequested(nodeId, paramId)` — a new
`GraphEditor` host seam mirroring `onSaveSnippetRequested` exactly (`GraphEditor` owns no
`TimelineDoc`, so it hands the pair back to the one component that owns both the doc and the
graph). `MainComponent::automateParameter(nodeId, paramId)` (public — also the test's headless
hook) resolves the node's uuid (ensure-uuid, mirrored into the processor, the same idiom
`createTrackInNode()`/`AIStateMapper` use at every uuid writer site), finds the first
`TrackKind::Automation` track or creates one, binds a lane with the parameter's real
`NormalisableRange` (`addLane` dedupes doc-wide — a repeat call for an already-automated parameter
is a no-op that returns the existing lane), opens the timeline panel via the SAME toggle-button
click path `simulateToggleTimelineClick()` uses if it's hidden, and opens the strip on that lane.

Tests: `Tests/AutomationEditorTests.cpp` — `AutomationLaneEditor` gesture/publish-discipline
coverage (mirrors the `TimelineClipLaneArea`/`PianoRollComponent` hand-built-`juce::MouseEvent`
idiom against a bare `TimelineDoc` + `AppUndoManager`), the panel's strip open/close/record-mode
selector, and a `#if SYNTH_ENABLE_TIMELINE`-gated `MainComponent` integration test for the knob
entry point.

### TL5-10: keyboard & focus

By TL5-9 the panel has THREE independently-editable surfaces competing for the same physical
keys: the graph editor, the clip lanes, and the piano roll (each already grabbing keyboard focus on
its own `mouseDown` — TL5-7/TL5-8's own subsections above). Cmd+C/V/D are global
`ApplicationCommandManager` commands owned by `MainComponent`, so — unlike Delete/Escape, which
each surface already intercepts locally via its own `keyPressed` — nothing decided *which*
surface's selection and clipboard they should act on until this task.

**The one resolver.** `MainComponent::resolveEditSurface() const` is the single focus-ownership
rule:

```cpp
enum class EditSurface { Graph, TimelineClips, PianoRoll };
```

It returns `TimelineClips`/`PianoRoll` when the timeline panel is visible AND real keyboard focus
(`juce::Component::getCurrentlyFocusedComponent()`) sits inside the clip-lane area / piano roll
respectively, and `Graph` otherwise — including when the timeline panel is hidden outright,
regardless of what a stale focus pointer inside it might point at. Nothing new grabs focus for
this: every surface already does it on `mouseDown` (`GraphEditor::mouseDown` is the idiom's
original; `TimelineClipLaneArea`, `PianoRollComponent` and `AutomationLaneEditor` all copy it), so
"the surface you last clicked owns the verbs" falls out of ordinary JUCE focus tracking with no
extra bookkeeping. Headless tests can't always create a real focus grab (`grabKeyboardFocus()`
needs a native peer — see `Tests/FocusArbitrationTests.cpp`'s `SurfaceResolverRealFocus`, which
documents why this repo doesn't attempt one), so `MainComponent::setEditSurfaceOverrideForTest()`
is consulted FIRST and short-circuits the real-focus check when set.

**Cmd+C/V/D/X and Cmd+R route by surface** — `MainComponent::getCommandInfo`/`perform` branch on
`resolveEditSurface()` for `AppCommands::copySelection/pasteSelection/duplicateSelection/
cutSelection/repeatSelection`:

- **Graph** — Copy/Paste/Duplicate unchanged: `GraphEditor::copySelection()/pasteClipboard()/
  duplicateSelection()` against its own `ModuleClipboard`. Cut is **composed** from the two that
  already exist (`copySelection()` fills the clipboard without touching the graph or the undo
  stack, then `deleteSelection()` removes the selection inside its own `recordStructuralChange`),
  so it costs exactly one graph-undo step and the copy half survives the undo. Repeat is
  **inactive** here — always — because "N copies, each one selection-span further along" is a
  time-axis idea and a spatial canvas has no such axis; Duplicate is the graph's answer to "another
  one of these".
- **TimelineClips** — the clip clipboard, owned by `TimelinePanelComponent` (it already owns the
  clip selection — see TL5-7): `copySelectedClips()` serialises the selected clips — notes (each
  with its own `muted` flag), name, length, `muted`, and every audio field (`assetRef`, `gainDb`,
  both fades, `sourceStartSeconds`) — starts relative to the earliest selected clip, into it;
  `pasteClipsAtPlayhead()` re-inserts them onto their ORIGINAL tracks (by `TrackId`), re-based so
  the earliest clip lands at the transport's CURRENT position (snapped via the shared
  `TimelineViewState`), in ONE `AppUndoManager::recordTimelineChange`; the track fallback is
  **kind-aware** (`TimelineDoc::moveClipToTrack`'s rule) — a clip lands back only on a track that
  still plays its payload, else the doc's first track of the required kind, else it is skipped.
  Audio fields go back through `setClipAsset`/`setClipGainDb`/`setClipFades` rather than a raw
  struct write, so a clipboard `assetRef` is re-validated exactly like a freshly-loaded file's — a
  clipboard is only as trustworthy as whatever filled it (this closes a bug: the clipboard used to
  silently drop a copied audio clip's asset). `duplicateSelectedClips()` calls
  `TimelineDoc::duplicateClip()` per selected clip, batched the same way. `cutSelectedClips()` is
  copy then delete the selection, as ONE `recordTimelineChange` (never wrapped a second time — that
  would make Cmd+Z a two-step undo for one gesture). `repeatSelectedClips(count)` makes `count`
  back-to-back copies of the selection's own span (`max end - min start`, not each clip's own
  length, so a multi-clip rhythm tiles intact), the first starting one span-length after the
  selection's start, batched into one undo step. All four leave their result selected, mirroring
  `GraphEditor`'s own "the copies are what you probably want next" convention.
- **PianoRoll** — the roll's OWN note clipboard (see the TL5-8 subsection's **Note clipboard**
  above) closes what was previously a deliberate v1 gap: Copy/Paste/Duplicate/Cut/Repeat all act on
  notes now. Paste **primes** the roll's playhead from the live transport
  (`timelinePanel.getPianoRoll().setPlayheadBeat(audioEngine.getTransport().
  getPositionSnapshot().ppq)`) immediately before pasting — priming, not a side effect, since the
  roll only has a playhead position because the overlay pushes one while playing, and a stopped
  transport never does.

`getCommandInfo`'s Paste case is active only when the SURFACE-MATCHING clipboard has something in
it — `GraphEditor::canPaste()` for Graph, `TimelinePanelComponent::canPasteClips()` for
TimelineClips, `PianoRollComponent::canPasteNotes()` for PianoRoll (both halves: a non-empty
clipboard AND an open clip) — so copying modules never makes Paste live on the clip lanes or the
roll, or vice versa. Cut shares Copy's enablement predicate on every surface (a cut is a copy that
also deletes). Repeat's predicate is `hasClipSelection()`/`hasNoteSelection()` on the timeline
surfaces and unconditionally `setActive(false)` on Graph. `Cmd+Shift+A` (`AppCommands`/actionId
`selectAllModules`, kept for a persisted binding's sake even though the verb widened) is routed by
the SAME resolver: `TimelinePanelComponent::selectAllClips()` on TimelineClips,
`PianoRollComponent::selectAllNotes()` on PianoRoll, `GraphEditor::selectAllModules()` on Graph —
unlike the clipboard verbs it is **always active**, since each surface's own `selectAll*` just
returns `false` harmlessly when there's nothing to select. See
[`shortcuts.md`](shortcuts.md#surface-routing-tl5-10-who-cmdcvdxr-and-cmdshifta-act-on) for the
user-facing table.

**Space is global.** `AppCommands::togglePlayback` (`ShortcutManager` action id `togglePlayback`,
default binding: bare spacebar, no modifiers) is deliberately NOT routed by `resolveEditSurface()`
— it always toggles the transport, from any surface, via
`TimelinePanelComponent::getTransportBar().getPlayStopButton().triggerClick()` (the SAME choke
point the transport bar's own click handler uses — see TL5-5 above — so the button's visual state
and a Space-triggered toggle can never disagree). Safe to claim app-wide for the same reason
Cmd+C/V is (see `shortcuts.md`): a focused `juce::TextEditor` consumes the spacebar itself (types a
space character) before `MainComponent::keyPressed`, the sole dispatch point, ever sees it. Inactive
outright (`getCommandInfo` -> `setActive(false)`) in a `SYNTH_ENABLE_TIMELINE=OFF` build, where
there is no transport to toggle.

**Delete stays panel-local.** Unlike C/V/D, Delete/Escape were never routed through
`ShortcutManager`/`ApplicationCommandManager` at all (see `shortcuts.md`'s reasoning) — each
surface's own `keyPressed` already handles its own selection and falls through (`return false`) on
an empty one, which is what let an unmodified `Delete` binding be surface-scoped for free since
before this task. TL5-10 adds no new production code here, only
`Tests/FocusArbitrationTests.cpp`'s `DeletePerSurface`, which pins that a clips-focused Delete never
touches the graph, a graph-focused Delete never touches the clips, and an empty selection on either
falls through rather than eating the key.

Tests: `Tests/FocusArbitrationTests.cpp` — one test per verb x surface (`resolveEditSurface()`
override coverage, clip copy/paste-at-playhead/duplicate incl. the missing-track fallback, the
piano-roll inactive gap, Space from every surface, per-surface Delete, and the resolver's real-focus
fallback behaviour).
