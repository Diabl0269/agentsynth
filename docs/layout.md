# Layout System Reference

This document describes the soft-grid layout model: how modules snap to an 8 px grid, how the
anti-overlap spiral search resolves collisions on drop and drag-release, how `autoArrange`
computes a topological signal-flow layout, and the full `LayoutUtil` API reference.

- [1. Soft-Grid Model](#1-soft-grid-model)
- [2. kGridSize = 8 — Rationale](#2-kgridsize--8--rationale)
- [3. Anti-Overlap Spiral Search](#3-anti-overlap-spiral-search)
- [4. Auto-Arrange](#4-auto-arrange-cmdl--auto-arrange-button)
- [5. Toolbar & Status Bar Layout](#5-toolbar--status-bar-layout)
- [6. Module Width Buckets](#6-module-width-buckets)
- [7. LayoutUtil API Reference](#7-layoututil-api-reference)
- [8. Drag Affordance — Grid Dots + Landing Ghost](#8-drag-affordance--grid-dots--landing-ghost)
- [8b. Smart Connections](#8b-smart-connections)
- [8c. Double-click Port Disconnect](#8c-double-click-port-disconnect)

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
All graph nodes whose processor is a `ModuleBase` subclass (which includes *Audio
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
┌─────────────────────────────────┐  ← toolbar strip  (height: Metrics::toolbarHeight = 44 px)
│  [Lib] [Save][Load][Cfg][⟲][⟳][⬜]   ···   [Matrix][AI]  │
├──────────┬──────────────────────┤
│  Library │                      │  ← library sidebar  (width: Metrics::librarySidebarWidth = 200 px, or 0 when hidden)
│ (200 px) │    Graph canvas      │
│          │                      │  ← AI panel clips right (width: Metrics::aiPanelWidth = 300 px, or 0 when hidden)
├──────────┴──────────────────────┤
│  [Patch Name]  CPU  RT  Voices  [🔇] │  ← status bar  (height: Metrics::statusBarHeight = 24 px)
└─────────────────────────────────┘
```

The three dockable panels (library, AI, timeline) are carved at **their open fraction times their
full size**, not at a binary read of their visible/hidden flag: "or 0 when hidden" above is the
fraction resting at 0, and any value in between is a frame of the panel's slide. That is what makes
this method safe to call at any moment — a window resize, a theme change or a timeline height drag
*during* a slide re-derives the same proportions — and it is the whole animation mechanism: a
toggle moves the fraction and calls back in here. See `docs/layout_visuals_animation.md` §3's `PanelSlide` subsection.

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

**Sub-group visual grouping**: `ToolbarComponent`'s local `groupOf(slot)` helper maps each `Slot`
to the sub-group it documents, and `layoutButtons()` inserts a 12px spacer `FlexItem` at every
sub-group boundary within the left/right loops; `paint()` draws a 1px border-token hairline at the
midpoint of each intra-section sub-group gap (using the buttons' own post-layout bounds, guarded on
non-null && `isVisible()`), excluding the left/right section boundary (the existing `withFlex(1.0)`
spacer). No-ops when the `LookAndFeel` isn't the themed `AppLookAndFeel` (headless test runner).

**Toggle pills**: `applyToolbarIcons()` and `MainComponent::setLibraryVisible()` both call
`setToggleState(..., juce::dontSendNotification)` on their panel-toggle button (Library, Minimap,
ModMatrix, AiPanel, Timeline under its `#if` guard) so the button's toggle state always matches
panel visibility — always `dontSendNotification`, never `setClickingTogglesState`, so a button's
own `onClick` never double-fires. The pill's
hover/press/toggled-on paint states are fully owned by `AppLookAndFeel::drawDrawableButton`
rather than delegated to `LookAndFeel_V4::drawDrawableButton` — the stock `LookAndFeel_V2` base
does an unconditional flat `g.fillAll()` keyed only on toggle state, with no hover/press
distinction and no rounding. The toggled-on state is a ~13/15/20% (rest/hover/press) accent wash
with a 0.35-alpha 1px stroke — an "active" tint, not a filled button — and icon+label colour steps
through a `textMuted → textPrimary (hover/press) → accent (on)` / `textDisabled` ladder: the label
in `drawDrawableButton` (fixed 11px, bottom-docked with a 6px pad, replacing the stock formula
that starved it at `min(16, 25%·height)`), the icon via tinted `Drawable` clones built in
`MainComponent::applyToolbarIcons()` (`retintIcons()` tints the base `textMuted`; hover/on
variants are `replaceColour`'d and wired through `setImages`' state slots). A uniform
`DrawableButton::setEdgeIndent(8)` on all toolbar buttons is what keeps icon optical size
(~17-19px) consistent regardless of button width — height, not width, is the binding constraint
in `getImageBounds()`.

### Metrics layout tokens (code-only)

The following `Metrics` struct fields govern chrome layout. They are **not parsed from user JSON** — a user theme may not override them. `ThemeLoader` silently ignores them (unknown-key forward-compatibility). Their values come from the C++ struct defaults only.

| Token | Value | Meaning |
|---|---|---|
| `toolbarHeight` | 44 | Toolbar strip height (px) — sized so an ~18px icon and an 11px label fit with real breathing room (36px starved the label to ~7-9px via `DrawableButton`'s built-in geometry) |
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
- **Round trip**: `RT <n> ms`, immediately after the CPU figure (`x = 236`, 90 px wide), `textMuted`. Drawn only while it *fits* before the voice-count slot — a cramped window drops the segment rather than overlapping two readings — and only once a first reading has arrived
- **Transport cluster**: a play/stop glyph button (16×16) immediately after the round-trip segment (`x = 236 + 90 + 6 = 332`), followed by a `"bar.beat.ticks   BPM"` readout (118 px wide). Drawn/shown only while the whole cluster fits before the voice-count slot — the same fit-check-then-drop the round-trip segment uses, computed in `resized()` rather than `paint()` because the button is a live child component that must actually be hidden (`setVisible(false)`), not merely left undrawn. The glyph is accent-coloured while playing (the "obviously running" cue) and a neutral outline while stopped, same triangle/square shapes as `TimelineTransportBar::GlyphButton`'s own PlayStop case, reproduced rather than shared since `StatusBarComponent` lives in Core and cannot depend on AppUI. **Always visible regardless of the timeline panel's visibility** — before this, play/stop/position only existed inside `TimelineTransportBar`, a child of the (often-hidden) timeline panel
- Voice count: right-aligned before the mute button slot
- `masterMuteButton_` (`DrawableButton`): positioned in `resized()` at `(w-28, 2, 20, h-4)`

**Update contract:**
`update(float cpuPct, int voices, const juce::String& patch)` is gated — it only calls `repaint()` when any value changes by a visible amount (cpu delta > 0.5 %, voice count changed, or patch name changed). It contains **zero `writeToLog` calls**.

`updateRoundTripLatency(double milliseconds, bool available)` is a **sibling** setter with its own gate, so a moving CPU figure never repaints on account of an unchanged latency or vice versa. Its diff is on the **rendered string**, which means a latency drifting below the printed resolution costs no repaint at all. It shows `AudioEngine::getRecordingLatencySamples()` (input device + graph + output device — the amount a recorded take is shifted back by; see [`architecture.md`](architecture.md)'s "Latency alignment" section), fed from the same 5 Hz poll. `available == false` — Hosted mode, where the host owns both ends — draws `RT —` rather than a made-up number.

`updateTransport(bool playing, const juce::String& positionText, double bpm)` is another **sibling** setter, gated independently on `(playing, positionText, bpm)`: it builds `positionText + "   " + bpm (1 dp) + " BPM"` and only repaints (and calls `transportButton_.setToggleState(playing, dontSendNotification)`) when that diff changes. `positionText` arrives **pre-formatted** by the caller — normally `synth::ui::TimelineTransportBar::formatBarBeat(ppq, tsNumerator, tsDenominator)` — because `StatusBarComponent` is compiled into the `Core` CMake target, which cannot depend on `AppUI` (where `TimelineTransportBar` lives); `MainComponent` (in `AppUI`) is the one call site that does the formatting. Fed from `MainComponent::timerCallback`'s existing 5 Hz status-bar sub-tick, using the `PositionSnapshot` already read **unconditionally** every 10 Hz tick (before the `timelinePanel.isVisible()` guard) — see `docs/architecture.md`'s timerCallback inventory. The play/stop button's own click is wired by `MainComponent` to the same `TransportService::play()`/`stop()` calls `TimelineTransportBar`'s button uses; the button never flips its own toggle state (`setClickingTogglesState(false)` — "the transport is the truth", `updateTransport()` is the only setter).

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

The search field's `TextEditor` colours are re-applied in `parentHierarchyChanged()`, in addition
to `lookAndFeelChanged()`. `MainComponent`'s ctor applies the persisted theme in its ctor *body*,
after `moduleLibrary` (a plain member) was already default-constructed against whatever theme was
active before that — so without this, the field showed stale colours until the next theme switch.
`parentHierarchyChanged()` fires when `MainComponent::initialiseCommon()` calls
`addAndMakeVisible(moduleLibrary)`, which always runs after that ctor-body `applyTheme()` call.

### ModMatrixComponent chrome

`Source/UI/ModMatrixComponent.h/.cpp` paints rows with the following visual rules:

- **Row height**: `static constexpr int kRowHeight = 48` (was 40)
- **Zebra striping**: odd rows (`isZebraRow(rowIndex)` — `rowIndex % 2 == 1`) are tinted with `theme.colors.surfaceHi.withAlpha(0.45f)`; even rows are transparent (parent background shows through)
- **Hover highlight**: the currently hovered row is tinted with `theme.colors.accent.withAlpha(0.10f)`, overriding the zebra base
- **Hover tracking**: `ModRow::mouseEnter` calls `owner.setHoveredRow(rowIndex)`; `ModRow::mouseExit` calls `owner.setHoveredRow(-1)` only when that row still owns the hover, avoiding races when the cursor moves between rows. The `hoveredRow_` member defaults to `-1` (no hover)
- **Static helper**: `static bool isZebraRow(int rowIndex) noexcept` — exposed for unit tests
- **Column alignment**: header labels and `ModRow`'s combo columns share `kRowNumColW`(30) /
  `kSourceColFrac`(0.30f) / `kDestColFrac`(0.35f) / `kGutter`(8) constants on `ModMatrixComponent`,
  so they can't drift apart
- **Row separator**: a 1px `theme.colors.border` hairline is painted at the bottom of every row (in
  addition to zebra/hover), and above the footer band
- **Grouped-menu labels bake in the module name**: the closed `ComboBox`'s label resolves ONLY from
  the matching leaf item's own text (JUCE never concatenates ancestor submenu titles), so a
  multi-output/multi-target module's nested source/dest submenu leaves are text like
  `"<Module> · Out N"` / `"<Module> · <target>"`, not a bare `"Out 2"`. `updateRowsFromGraph()`
  re-populates every row's combos (`populateCombos()`) BEFORE re-applying its selection
  (`refresh()`), because `populateCombos()` clears the combo box as a side effect of rebuilding it.
- **Bypass/delete controls are `DrawableButton`** (`Icon::ModuleBypass`/`Icon::ModuleDelete`,
  `ImageFitted`), not `TextButton` — retinted via `ModRow::applyButtonIcons()`, called from the
  constructor and from `ModRow::lookAndFeelChanged()`, mirroring
  `ModuleComponent::applyHeaderButtonIcons()`. The bypass-active state uses
  `juce::DrawableButton::backgroundOnColourId` (a `TextButton`-only id would be ignored by
  `DrawableButton`'s look-and-feel path).
- **Amount readout**: a small `amountValueLabel` next to the amount slider shows the current value
  to 2 decimals, updated via the slider's `onValueChange` plus one explicit push right after
  `SliderParameterAttachment` construction (its constructor does not itself fire `onValueChange`).
- The empty-state message ("No modulations active…") now draws inside the post-header/footer
  `area` rect, not `getLocalBounds()`, so it no longer overlaps the title/header bands.

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

The header title text itself is drawn by `AppLookAndFeel::drawModulePanel()` with an asymmetric
inset — `header.withTrimmedLeft(22.0f)` — so the activity LED (`fillEllipse(6, 8, 8, 8)`, x = [6,14])
never overlaps the title, regardless of whether the LED is currently lit.

### Audio Output card identity treatment

Audio Output is a bare `juce::AudioGraphIOProcessor`, not a `ModuleBase`, so it otherwise renders
through the exact same generic path as every other card (its `getType()` falls back to
`ModuleType::Oscillator` — see the free function at the top of `ModuleComponent.cpp`). Two small,
purely additive blocks in `ModuleComponent::paint()` give it its own identity, both gated on a
local `isAudioOutputIONode(juce::AudioProcessor*)` helper (`dynamic_cast` to
`AudioGraphIOProcessor` + an `IODeviceType == audioOutputNode` check — the same type-not-name idiom
`isTerminalAudioSink` in `GraphEditor.cpp` uses for cable routing, so a `ModuleBase` named "Audio
Output" cannot impersonate the sink):

- **Identity glyph** — the `synth::theme::Icon::CatIO` speaker glyph, drawn in the activity LED's
  slot (`{4, 4, 16, 16}`). Audio Output is never a `ModuleBase`, so it never has a `VisualBuffer`
  and `cachedRMS` never leaves `0.0f` for this card — that slot is otherwise always dark.
- **Destination line** — one muted (`theme.type.micro`, `colors.textMuted`) text line under the
  header, e.g. `"MacBook Pro Speakers · 48 kHz · 2ch"`. Sourced **message-thread-only** from
  `AudioEngine`'s device state and pushed in through a small chain rather than read directly:
  `MainComponent::computeOutputDeviceInfoText()` (branches on `AudioEngine::isHosted()` — Hosted
  mode has no device manager, so it returns the fixed string `"Host audio"` instead of touching
  one; a Standalone build with no device open yet returns an empty string) is installed as a
  `std::function<juce::String()>` via `GraphEditor::setOutputDeviceInfoProvider`, and
  `GraphEditor::refreshOutputDeviceInfo()` calls it and pushes the result into the Audio Output
  `ModuleComponent` via `setOutputDeviceInfoText()`. An empty string means "draw no line", not an
  empty one. There is no timer: `refreshOutputDeviceInfo()` is called once right after the provider
  is installed (so the card is populated at startup) and again from
  `AudioEngine::onDeviceStateChanged`, right alongside the existing
  `refreshIoModulesAfterDeviceChange()` call that already re-points Audio Input's jacks on a device
  change. `setOutputDeviceInfoText()` repaints (the normal `ZoomFrozenCachedImage` invalidation
  seam — see `docs/layout_visuals_animation.md` §2/§3) only when the text actually changed, and is a no-op on every module that
  isn't the terminal audio sink, so a caller never needs to check `isAudioOutputIONode()` first.

The library sidebar's "I/O" category header (Audio Input / Audio Output) uses the same `CatIO`
icon via `ModuleLibraryComponent::categoryIconForHeader` — see docs/theming.md's icon list.

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

Generic bool-parameter toggles, and the `freqResponse`/`spectrum`/`scope` show/hide toggles, lay
out at full `contentX`/`contentW` (flush-left with the knob grid), not the centred
`narrowX`/`narrowW` band — a toggle's label reads better flush-left than centred in a narrow
column.

**Body content always starts below every jack.** `getContentTopY()` derives that y from
`getPortCenter()` — the same function that anchors wires — rather than recomputing the port
geometry, so content can never land on a port label again. Because the body is below the ports, it
does not need the narrow gutters that used to keep it clear of the port labels, which is what makes
three knobs per row fit inside the 280 px card.

The header-to-first-port gap also grew from 1 px to 9 px (base header offset constant `30 → 38` in
both `getContentTopY()` and `getPortCenter()`), giving the MIDI In/Out row breathing room under the
header hairline — every height below already includes that +8 px.

Three columns instead of two removes a knob row from most modules; the header-offset shift above
adds a further +8 px on top. Measured heights (`GraphEditor::estimateModuleSize()` mirrors these
for the library drag ghost; `ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents`
constructs every library-offered type and fails if this table drifts from what
`layoutDefaultContent()` actually produces):

| Module | Height (px) | Module | Height (px) |
|---|---|---|---|
| Oscillator | 553 | Sample & Hold | 571 |
| Filter | 463 | Comparator | 205 |
| LFO | 361 | Sampler | 665 |
| VCA | 273 | Chorus / Phaser / Flanger | 297 |
| ADSR | 359 | Bitcrusher | 343 |
| Poly MIDI | 205 | Pitch Shifter | 487 |
| Distortion | 343 | Compressor | 257 |
| Ring Modulator | 411 | Limiter | 181 |
| Delay | 257 | Voice Mixer | 321 |
| Reverb | 257 | External MIDI | 146 |
| Noise | 301 | Rec Tap / Track Audio / Hosted Plugin | 131 |
| Envelope Follower | 315 | Math | 259 |

Unchanged: Sequencer / PolySequencer 406, MidiKeyboard 150, Macros (tracks its `Knobs` count),
Attenuverter (square, `kNarrowWidth`), Wavetable 554, Parametric EQ 592, AudioInput / AudioOutput
(100 px floor).

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
like Oscillator (553 px) or Sampler (665 px) always land correctly even if the ghost preview used
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

Group multi-select drags never smart-connect. Snippet drops are excluded. "Dest input must be free" has exactly one exception — the terminal audio sink, which offers an insert instead (see below).

### Behaviour

1. During `updateDragPreview()`, if the mode allows it, cull neighbors whose **module** rects are more than **96 px** edge-to-edge, then score **jack-to-jack** distance (same 96 px cap). A pair is rejected when the source jack sits to the right of the dest jack — that stops wrap-around cables from a neighbor on the right into the dragged module’s left inputs.
2. Score compatible jack pairs with `scoreJackPair` role matching and free destination jacks. In **When main I/O is free** the source output must also be unwired. Stereo requires explicit Left/Right (or Audio L/R) labels — two unlabeled ports (e.g. Math A/B) are never treated as L/R. Cap at the best neighbor’s audio group (stereo L→L / R→R, or mono↔stereo fan of both legs, both-or-neither when a stereo dest has a taken leg) plus one MIDI suggestion. Mod-matrix / attenuverter destinations are skipped in v1. MIDI suggestions are limited to known MIDI sources/destinations (Sequencer, Poly MIDI, MIDI Keyboard, Oscillator, …) because `ModuleBase` defaults `producesMidi()`/`acceptsMidi()` to true for almost every card.
3. Frosted preview cables are drawn in `paintOverChildren` (~40% alpha via `drawConnectionWire`). Colours resolve **only** through `GraphEditor::colourForCable` → `synth::ui::resolveCableColour`, so the cable-colour mode and user overrides apply to previews too.
4. On drop / `finalizeModuleDrag`, pending suggestions are applied through `connectPorts` (same path as a manual cable drag: poly fans, MIDI, structural pitch/gate).

Library drags cache a short-lived `AIStateMapper::createModule` probe for jack metadata before a real `ModuleComponent` exists.

### Insert-in-series at the terminal audio sink

An occupied destination jack is a **hard stop** everywhere except one node: the graph's terminal audio sink. Audio Output is a bare `juce::AudioGraphIOProcessor` (never a `ModuleBase` — the graph's output channel count is tied to it) and is wired in essentially every real patch, so the occupied-destination rule used to mean an FX parked next to it could never be offered anything at all. There, and only there, an occupied jack becomes a **reroute** instead: the cable already on the sink is re-pointed *through* the dragged module.

`SmartSuggestion::isInsert` turns one suggestion record into that reroute. It carries two cable **sets** — `doomedLinks` (upstream → sink, all to be removed) and `upstreamCables` (upstream → ghost, replacing them) — plus the record's own `ghostJack` → `neighborJack`.

Both sets describe the **whole insert group**, not the record's own leg, and both are deduped, so applying them once per suggestion is idempotent. This is load-bearing: a Dual I/O upstream reaches the sink through *two* distinct cables, while a collapsed ghost output fans across both raw legs so only one jack pair survives the dedupe below. Hanging the doomed link off the surviving pair loses the other one, and that cable stays connected — summing into the sink's right leg alongside the ghost's output. Guarded by `SmartConnectionInsertRemovesEveryDoomedLegOfADualIOUpstream`.

`upstreamCables` is deduped by the raw **ghost-input** channels each cable covers — the mirror of the sink-side rule, since a collapsed jack on *either* end fans. Without it, a collapsed upstream's single jack wired into both of a Dual I/O ghost's inputs would duplicate its left leg onto the right. Guarded by `SmartConnectionInsertDoesNotDuplicateOneUpstreamLegOntoADualIOGhost`.

All of these must hold, or nothing is offered and the ordinary drop applies:

- The sink is detected **by type** (`AudioGraphIOProcessor::audioOutputNode`), not by the `"Audio Output"` name `isSingletonIOModule` matches on, so a `ModuleBase` that happened to carry that name cannot impersonate it.
- The ghost is the cable **source**, and has at least one audio **input** leg. A pure source (Oscillator, LFO) is refused: it cannot go in series, and offering it would be the silent parallel sum the drop rule exists to prevent.
- **Every** leg of the group is occupied, by one and the same upstream node — both-or-neither, mirroring the stereo group rule. A mix of free and occupied legs, or two different feeds, would change the summing.
- The feeding cable resolves through `findSingleUpstreamAudioLink`, which works at **cable** level (a visible output jack, never a raw graph edge) and returns nothing for a jack summed from several cables or fed through a mod routing / attenuverter chain.
- The upstream is not the ghost itself (a move that would self-loop).

One jack pair survives per **new** ghost→sink cable: a collapsed jack already fans to the whole raw pair, so a second pair for the sink's right leg would sum the ghost's *left* leg into it. Pairs whose raw destinations an earlier one already covers (via `resolvePolyLink`) are dropped — the doomed links are collected before this and are unaffected by it.

**Known limitation.** A Dual I/O upstream feeding a *collapsed* ghost routes only its left leg into the ghost: the ghost's single input jack owns both raw channels, so a second cable onto it would fold left and right together. The sink is left correctly wired in either case; the mismatch is inherent to collapsing a split pair.

**Preview vs. real jacks.** The library-drop ghost is an `AIStateMapper::createModule` probe, and the probe does *not* get `applyDefaultDualIOForNewModule` applied — so with the Dual I/O default on, a dropped module's real jack layout can differ from the one the preview measured. Pre-existing, not specific to inserts; it is why the Dual-I/O-ghost test above drives the move path instead of a library drop.

Scoring, the 96 px proximity cap and the left-to-right flow rule are unchanged — the ghost naturally hovers to the **left** of Audio Output, which is the insert position. An insert scores like the plain cable it replaces, so a neighbour offering a free jack can still win.

**Preview.** *Every* doomed cable is stroked first, dashed and dimmed (~18% alpha), underneath the frosted segments that replace them — otherwise the extra previews read as "and also", and the user expects the old wires to still be there after the drop. Paint-only; no new timers or repaints.

**Apply.** `applySmartSuggestions` drops **every** doomed cable first (`disconnectAudioLink`, the exact inverse of `connectPorts`, so a collapsed stereo wire takes both raw legs), then wires each `upstreamCables` entry into the ghost, then the ghost's own leg into the sink. Dropping only the current leg's cable would leave the other summing into the sink. All of it shares the caller's transaction, so **one** undo restores the original patch.

---

## 8c. Double-click Port Disconnect

Double-clicking a **connected** jack removes every cable on that port — the same path as the right-click **Disconnect** menu (`GraphEditor::disconnectPort`, which fans across every raw channel a visible jack owns). An unconnected jack is a no-op. The first click of a double-click still begins (and immediately ends) a cable drag; the second click is intercepted in `ModuleComponent::mouseDown` (`getNumberOfClicks() >= 2`) so it does not start another drag.

### Preference (`Settings → Preferences → Double-click port to disconnect`)

Persisted as `doubleClickPortDisconnect` in `juce::ApplicationProperties`. Default: **on**. Restored in `MainComponent::initialiseCommon()` so the canvas honours it without opening Settings. When off, double-clicking a jack behaves like two single clicks (cable drag).

---
