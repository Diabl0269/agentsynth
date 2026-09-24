# Application Chrome

The app's frame — toolbar strip, library sidebar, AI panel, status bar, the timeline and mixer
docks, and the mod-matrix overlay — is carved out of `MainComponent::resized()`, the single
canonical layout method. It slices top to bottom:

```
+---------------------------------+  <- toolbar strip  (Metrics::toolbarHeight = 44 px)
|  [Lib] [Save][Load][Cfg][<][>][ ]   ...   [Matrix][AI]  |
+----------+----------------------+
|  Library |                      |  <- library sidebar (Metrics::librarySidebarWidth = 200 px,
| (200 px) |    Graph canvas      |                      or 0 when hidden)
|          |                      |  <- AI panel clips right (Metrics::aiPanelWidth = 300 px,
+----------+----------------------+                          or 0 when hidden)
|  [Patch Name]  CPU  RT  Voices  |  <- status bar (Metrics::statusBarHeight = 24 px)
+---------------------------------+
```

Every dimension above is a `Metrics` field; the full table, including which fields a user theme may
override, is in [theming](theming.md#metrics).

**The three dockable panels are carved at their open fraction times their full size**, not at a
binary read of a visible/hidden flag. "Or 0 when hidden" is the fraction resting at 0, and any value
between is a frame of the panel's slide. That is what makes this method safe to call at any moment —
a window resize, a theme change or a timeline height drag *during* a slide re-derives the same
proportions — and it is the whole animation mechanism: a toggle moves the fraction and calls back in
here. See [animation](animation.md#panelslide).

## Toolbar

`ToolbarComponent::layoutButtons(bounds)` runs a single `juce::FlexBox` (row, align-center):

- Left group: Library, Save, Load, Settings, Feedback, Undo, Redo, AutoArrange
- Flex spacer (`withFlex(1.0f)`) filling the gap
- Right group: ToggleModMatrix, ToggleAiPanel

`paint()` fills the toolbar background with `theme.colors.bg0` via a
`dynamic_cast<AppLookAndFeel*>`; a null cast (headless tests, non-themed context) falls back to the
hardcoded colour `0xff0B0D10`.

**Narrow mode** fires when `bounds.getWidth() <= Metrics::minWindowWidth` (480 px): every button's
`prefWidth` becomes 32 (icon-only). In wide mode each button has a labelled preferred width —
Library 96, Save 112, Load 116, Settings 96, Undo 72, Redo 72, AutoArrange 120, ToggleModMatrix 104,
ToggleAiPanel 92. Feedback sits in the same sub-group as Settings (`groupOf()` returns the same id
for both, so no separator is drawn between them) and is always icon-only at a fixed 40 px, since it
never grows a text label.

**Sub-group visual grouping.** `ToolbarComponent`'s local `groupOf(slot)` helper maps each `Slot`
to its sub-group, `layoutButtons()` inserts a 12 px spacer `FlexItem` at every sub-group boundary
within the left and right loops, and `paint()` draws a 1 px border-token hairline at the midpoint of
each intra-section sub-group gap, using the buttons' own post-layout bounds and guarded on non-null
plus `isVisible()`. The left/right section boundary is excluded — that is the existing flex spacer.
It no-ops when the `LookAndFeel` is not the themed `AppLookAndFeel`.

**Toggle pills.** `applyToolbarIcons()` and `MainComponent::setLibraryVisible()` both call
`setToggleState(..., juce::dontSendNotification)` on their panel-toggle button (Library, Minimap,
ModMatrix, AiPanel, Timeline under its `#if` guard) so the button's toggle state always matches
panel visibility. Always `dontSendNotification`, never `setClickingTogglesState`, so a button's own
`onClick` never double-fires.

The pill's hover/press/toggled-on paint states are owned entirely by
`AppLookAndFeel::drawDrawableButton`, not delegated to `LookAndFeel_V4::drawDrawableButton`: the
stock `LookAndFeel_V2` base does an unconditional flat `g.fillAll()` keyed only on toggle state,
with no hover/press distinction and no rounding. Toggled-on is a 13/15/20% (rest/hover/press) accent
wash with a 0.35-alpha 1 px stroke — an "active" tint, not a filled button — and icon and label
colour step through a `textMuted -> textPrimary (hover/press) -> accent (on)` / `textDisabled`
ladder. The label is drawn in `drawDrawableButton` at a fixed 11 px, bottom-docked with a 6 px pad,
replacing the stock formula that starved it at `min(16, 25%*height)`. The icon is a tinted
`Drawable` clone built in `MainComponent::applyToolbarIcons()`: `retintIcons()` tints the base
`textMuted`, and the hover and on variants are `replaceColour`'d and wired through `setImages`'
state slots (see [icons](icons.md#token-to-tint-map)). A uniform
`DrawableButton::setEdgeIndent(8)` on all toolbar buttons keeps icon optical size (~17-19 px)
consistent regardless of button width — height, not width, is the binding constraint in
`getImageBounds()`.

**The icon re-clone is gated to narrow-mode transitions.** `applyToolbarIcons()` clones `Drawable`
objects to set button images, which is expensive, so `MainComponent::resized()` calls it only when
the mode actually flips:

```cpp
bool prevNarrow = toolbarNarrowMode_;
toolbar.layoutButtons(toolbarBounds);       // updates toolbar.isNarrowMode()
toolbarNarrowMode_ = toolbar.isNarrowMode();
if (toolbarNarrowMode_ != prevNarrow)
    applyToolbarIcons();                    // re-clone only on mode flip
```

A resize that does not cross the 480 px threshold skips the clone work entirely.
`applyToolbarIcons()` is also called unconditionally once at the end of `initialiseCommon()` and
after every theme switch via `changeListenerCallback`.

## Minimum window size

`Main.cpp`'s `MainWindow` constructor calls `setResizeLimits(480, 400, 8192, 8192)` — a hard
platform floor on the `DocumentWindow` — before `centreWithSize(1600, 900)`. This stops the window
shrinking below the point where toolbar buttons could clip to zero width.
`Metrics::minWindowWidth` / `minWindowHeight` carry the same values as layout constants, for
`ToolbarComponent`'s narrow threshold and for tests.

## Status bar

`Source/UI/Chrome/StatusBarComponent.h/.cpp` is a 24 px strip at the bottom of `MainComponent`.

- Patch name: left-aligned, padded 6 px from the left edge.
- CPU %: centre section, drawn in `theme.colors.warning` above 80%, otherwise `textMuted`.
- **Round trip**: `RT <n> ms`, immediately after the CPU figure (`x = 236`, 90 px wide),
  `textMuted`. Drawn only while it *fits* before the voice-count slot — a cramped window drops the
  segment rather than overlapping two readings — and only once a first reading has arrived.
- **Transport cluster**: a 16x16 play/stop glyph button immediately after the round-trip segment
  (`x = 236 + 90 + 6 = 332`), followed by a 118 px wide `"bar.beat.ticks   BPM"` readout. Drawn and
  shown only while the whole cluster fits before the voice-count slot — the same fit-check-then-drop
  the round-trip segment uses, computed in `resized()` rather than `paint()` because the button is a
  live child component that must actually be hidden (`setVisible(false)`), not merely left undrawn.
  The glyph is accent-coloured while playing and a neutral outline while stopped, the same
  triangle/square shapes as `TimelineTransportBar::GlyphButton`'s PlayStop case, reproduced rather
  than shared because `StatusBarComponent` lives in `Core` and cannot depend on `AppUI`. **The
  cluster is visible regardless of the timeline panel's visibility** — play/stop/position previously
  existed only inside `TimelineTransportBar`, a child of the often-hidden timeline panel.
- Voice count: right-aligned before the mute button slot.
- `masterMuteButton_` (a `DrawableButton`): positioned in `resized()` at `(w-28, 2, 20, h-4)`.

**Each reading has its own gated setter**, so a moving CPU figure never repaints on account of an
unchanged latency, or vice versa:

- `update(float cpuPct, int voices, const juce::String& patch)` repaints only when a value changes
  by a visible amount (CPU delta above 0.5%, voice count changed, or patch name changed). It
  contains **zero `writeToLog` calls**.
- `updateRoundTripLatency(double milliseconds, bool available)` diffs the **rendered string**, so a
  latency drifting below the printed resolution costs no repaint at all. It shows
  `AudioEngine::getRecordingLatencySamples()` — input device plus graph plus output device, the
  amount a recorded take is shifted back by (see
  [architecture/app-wiring.md#latency-alignment](../architecture/app-wiring.md#latency-alignment)) —
  fed from the same 5 Hz poll. `available == false`, which is Hosted mode where the host owns both
  ends, draws `RT --` rather than a made-up number.
- `updateTransport(bool playing, const juce::String& positionText, double bpm)` is gated
  independently on `(playing, positionText, bpm)`: it builds `positionText + "   " + bpm (1 dp) + "
  BPM"` and repaints, and calls `transportButton_.setToggleState(playing, dontSendNotification)`,
  only when that diff changes. `positionText` arrives **pre-formatted** by the caller — normally
  `synth::ui::TimelineTransportBar::formatBarBeat(ppq, tsNumerator, tsDenominator)` — because
  `StatusBarComponent` is compiled into `Core`, which cannot depend on `AppUI`; `MainComponent`, in
  `AppUI`, is the one call site that formats. It is fed from `MainComponent::timerCallback`'s 5 Hz
  status-bar sub-tick, using the `PositionSnapshot` already read **unconditionally** every 10 Hz
  tick, before the `timelinePanel.isVisible()` guard — see
  [architecture/app-wiring.md](../architecture/app-wiring.md)'s `timerCallback` inventory. The play/stop button's click is
  wired by `MainComponent` to the same `TransportService::play()` / `stop()` calls
  `TimelineTransportBar`'s button uses; the button never flips its own toggle state
  (`setClickingTogglesState(false)` — the transport is the truth, and `updateTransport()` is the
  only setter).

The status bar polls at 5 Hz, driven by `MainComponent`'s 10 Hz timer through an every-other-tick
guard (`statusBarTickCount_`), and repaints only itself.

**Static format helpers** (headless-testable, no JUCE GUI dependency):

- `formatCpu(float fraction)` — fraction is 0..1; `0.756f` becomes `"75.6%"`
- `formatVoices(int n)` — `0` becomes `"0 voices"`, `1` becomes `"1 voice"`, `8` becomes `"8 voices"`
- `formatPatch(const juce::String& s)` — empty or whitespace-only becomes `"Untitled"`
- `formatRoundTrip(double ms, bool available)` — `(12.34, true)` becomes `"RT 12.3 ms"`;
  `(anything, false)` becomes `"RT --"`; negative input clamps to `0.0`

**Tooltips come from the component itself, not from child widgets.** The patch name, CPU,
round-trip and transport-readout segments are painted text, not child components, so there is
nothing for `juce::TooltipWindow` (the single instance `MainComponent` owns — see
[theming](theming.md#themed-widgets)) to hit-test individually. `StatusBarComponent` is instead
itself a `juce::TooltipClient`: `getTooltip()` delegates to
`getTooltipForPosition(juce::Point<int>)`, a pure const helper mapping a LOCAL point to tooltip text
using the exact x-ranges `paint()` draws into (`isRoundTripSegmentVisible()` is shared by both, so
the two can never disagree about whether a segment is on screen).
`juce::TooltipWindow::getTipFor()` checks only the exact component the mouse is over and does not
walk up parents, so hovering `masterMuteButton_` or `transportButton_` — both real child components,
both already `SettableTooltipClient` via `juce::Button` — reaches THEIR OWN tooltip text, and
`getTooltip()` is never invoked for them.

The text, verified against what each value actually reads:

- **CPU %**: "Audio-engine DSP load: percentage of the audio callback's time budget spent rendering
  this block." The value is `audioEngine.isHosted() ? 0.0f : deviceManager.getCpuUsage() * 100.0` —
  JUCE's own callback-time-budget figure; hosted mode has no device of its own, so it shows 0 rather
  than a misleading reading.
- **Round trip**: "Round-trip latency: input device + audio graph + output device delay - the amount
  a recorded take is shifted back to line it up." Answered only while
  `isRoundTripSegmentVisible()` is true — a hidden segment has no tooltip.
- **Transport readout**: "Playback position (bar.beat.ticks) and tempo (BPM)."
- **Transport play/stop button**: "Play / Stop", set in the constructor — the same text
  `TimelineTransportBar`'s own play/stop button uses.

A transient message (`showMessage()`) suppresses every tooltip on the row —
`getTooltipForPosition` returns `""` immediately — since it visually covers the segments it would
otherwise explain.

## Panel collapse and persistence

The library sidebar and the AI panel can each be fully hidden (width 0). State persists across
launches via `ApplicationProperties`:

| Key | Default | Component |
|---|---|---|
| `librarySidebarVisible` | `"1"` (true) | `moduleLibrary` left panel |
| `aiPanelVisible` | `"0"` (false) | `aiChatComponent` right panel |

Both keys are read at the top of `initialiseCommon()`, before any `setVisible()` or
`addAndMakeVisible()` call. Cmd+B toggles the library sidebar, wired through `ShortcutManager`.

The Timeline and Mixer panels are a separate mechanism: each can additionally DETACH into its own
top-level window (`Source/UI/Layout/DetachablePanelHost/`) rather than only hide and show in place,
and the Mixer's dock-vs-own-panel-vs-window placement is itself a Preferences setting. See
[`docs/mixer/mixer.md`](../mixer/mixer.md).

### Bottom dock height

The bottom dock (Timeline / Mixer / MIDI Remote tabs) is resizable from ONE top-edge grab strip,
`synth::ui::PanelResizeHandle`, owned by `MixerDockComponent` and therefore live on every tab
(FRO231). Dragging reports the desired total dock height through `MixerDockComponent::onResizeHeight`
(live) and `onResizeHeightCommitted` (mouse-up, only after a real drag); `MainComponent` clamps it
(`[Metrics::timelinePanelHeight, max(metric, 75% of the window)]`), lays out live and persists the
`timelinePanelHeight` key once per gesture. The height is the dock's, not a tab's, so it holds when
switching tabs. Full rules: [`docs/timeline/timeline.md`](../timeline/timeline.md#panel-height).

## Welcome screen overlay

`Source/UI/WelcomeScreenComponent` is a full-window overlay, not a docked panel — it covers the
toolbar and canvas alike while shown, rather than carving a strip out of either. Two rules keep it
correctly positioned and stacked:

- **Added last.** `MainComponent::initialiseCommon()` calls `addAndMakeVisible(*welcomeScreen_)`
  after every other `addAndMakeVisible()` call. JUCE paints and z-orders children in add order, so
  this is what makes it sit on top of the toolbar buttons and canvas rather than underneath them.
- **Resized on every layout pass, visible or not.** `MainComponent::resized()`'s last line is
  `if (welcomeScreen_) welcomeScreen_->setBounds(getLocalBounds());` — the FULL window bounds, not
  whatever is left after the toolbar, status bar and panels have carved their strips out of the
  local `bounds` variable. Running it unconditionally rather than gated on `isVisible()` means a
  stale rect from before the last window resize can never show through the instant
  `showWelcomeScreen()` makes it visible again.

App-only, gated on `ownedAudioEngine != nullptr` — see [architecture/audio-engine.md](../architecture/audio-engine.md)'s
Welcome screen subsection for the gating rationale, the persisted `"showWelcomeScreenAtLaunch"` key
and the guard-before-hide ordering that keeps a Cancel answer from dismissing it.

## Mod matrix panel

`Source/UI/Graph/ModMatrixComponent.h/.cpp` is an untransformed sibling overlay on `GraphEditor`,
occupying the right-hand 600 px of the editor
(`modMatrix.setBounds(getWidth() - 600, 0, 600, getHeight())` in `GraphEditorCanvas.cpp`). Showing
and hiding is a 220 ms `easeOutCubic` bounds tween on an `AnimationDriver`.

Its rows paint under these rules:

- **Row height**: `static constexpr int kRowHeight = 48`.
- **Zebra striping**: odd rows (`isZebraRow(rowIndex)`, `rowIndex % 2 == 1`) are tinted with
  `theme.colors.surfaceHi.withAlpha(0.45f)`; even rows are transparent, so the parent background
  shows through. `isZebraRow` is a public static helper, exposed for unit tests.
- **Hover highlight**: the hovered row is tinted `theme.colors.accent.withAlpha(0.10f)`, overriding
  the zebra base. `ModRow::mouseEnter` calls `owner.setHoveredRow(rowIndex)`; `ModRow::mouseExit`
  calls `owner.setHoveredRow(-1)` only while that row still owns the hover, avoiding races when the
  cursor moves between rows. `hoveredRow_` defaults to `-1`.
- **Column alignment**: header labels and `ModRow`'s combo columns share the `kRowNumColW` (30),
  `kSourceColFrac` (0.30f), `kDestColFrac` (0.35f) and `kGutter` (8) constants on
  `ModMatrixComponent`, so they cannot drift apart.
- **Row separator**: a 1 px `theme.colors.border` hairline at the bottom of every row, in addition
  to zebra and hover, and above the footer band.
- **Grouped-menu labels bake in the module name.** A closed `ComboBox`'s label resolves ONLY from
  the matching leaf item's own text — JUCE never concatenates ancestor submenu titles — so a
  multi-output or multi-target module's nested source/dest submenu leaves read `"<Module> · Out N"`
  / `"<Module> · <target>"`, not a bare `"Out 2"`. `updateRowsFromGraph()` re-populates every row's
  combos (`populateCombos()`) BEFORE re-applying its selection (`refresh()`), because
  `populateCombos()` clears the combo box as a side effect of rebuilding it.
- **Bypass and delete are `DrawableButton`s** (`Icon::ModuleBypass` / `Icon::ModuleDelete`,
  `ImageFitted`), not `TextButton`s — retinted via `ModRow::applyButtonIcons()`, called from the
  constructor and from `ModRow::lookAndFeelChanged()`, mirroring
  `ModuleComponent::applyHeaderButtonIcons()`. The bypass-active state uses
  `juce::DrawableButton::backgroundOnColourId`; a `TextButton`-only id would be ignored by
  `DrawableButton`'s look-and-feel path.
- **Amount readout**: a small `amountValueLabel` beside the amount slider shows the value to 2
  decimals, updated from the slider's `onValueChange` plus one explicit push right after
  `SliderParameterAttachment` construction, whose constructor does not itself fire `onValueChange`.
- The empty-state message ("No modulations active...") draws inside the post-header/footer `area`
  rect, not `getLocalBounds()`, so it does not overlap the title and header bands.
