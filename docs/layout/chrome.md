# Application Chrome

The app's frame — toolbar strip, library sidebar, AI panel, status bar, the timeline and mixer
docks, and the mod-matrix overlay — is carved out of `MainComponent::resized()`, the single
canonical layout method. It slices top to bottom:

```
+---------------------------------+  <- toolbar strip  (Metrics::toolbarHeight = 44 px)
|  [Lib] [Save][Load][Cfg][<][>]      ...      [AI][Panel][ ]  |
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

- Left group: Library, New, Save, Load, Settings, Feedback, Undo, Redo
- Flex spacer (`withFlex(1.0f)`) filling the gap
- Right group: ToggleAiPanel, ToggleBottomPanel, ToggleTheme

Auto Arrange, Minimap and Matrix are not here: they sit on the [canvas toolbar](#canvas-toolbar).

`paint()` fills the toolbar background with `theme.colors.bg0` via a
`dynamic_cast<AppLookAndFeel*>`; a null cast (headless tests, non-themed context) falls back to the
hardcoded colour `0xff0B0D10`.

**Narrow mode** fires when `bounds.getWidth() <= Metrics::minWindowWidth` (480 px): every button's
`prefWidth` becomes 32 (icon-only). In wide mode each button has a labelled preferred width —
Library 96, New 88, Save 112, Load 116, Settings 96, Undo 72, Redo 72, ToggleAiPanel 92, ToggleBottomPanel 100,
ToggleTheme 110. Feedback sits in the same sub-group as Settings (`groupOf()` returns the same id
for both, so no separator is drawn between them; its tooltip and Cmd-hold key-cap name Cmd+Shift+F, `openFeedback`) and is 84 px wide, enough for its "Feedback" caption
at full size.

**Sub-group visual grouping.** `ToolbarComponent`'s local `groupOf(slot)` helper maps each `Slot`
to its sub-group, `layoutButtons()` inserts a 12 px spacer `FlexItem` at every sub-group boundary
within the left and right loops, and `paint()` draws a 1 px border-token hairline at the midpoint of
each intra-section sub-group gap, using the buttons' own post-layout bounds and guarded on non-null
plus `isVisible()`. The left/right section boundary is excluded — that is the existing flex spacer.
It no-ops when the `LookAndFeel` is not the themed `AppLookAndFeel`.

**Toggle state.** `applyToolbarIcons()` and `MainComponent::setLibraryVisible()` both call
`setToggleState(..., juce::dontSendNotification)` on their panel-toggle button (Library, Minimap,
Matrix, AiPanel, Panel) so the button's toggle state always matches panel visibility. Always
`dontSendNotification`, never `setClickingTogglesState`, so a button's own `onClick` never
double-fires.

### Toolbar buttons

Every button is a `synth::ui::ToolbarButton` (`Source/UI/Chrome/ToolbarButton/`), a
`juce::DrawableButton` subclass that never uses DrawableButton's image children: it carries the
state and `AppLookAndFeel::drawToolbarButton` (`AppLookAndFeelToolbarButton.cpp`) paints it. The
other `DrawableButton`s in the app (the mixer's icon buttons, detach buttons) keep
`drawDrawableButton`, unchanged.

- **Groups and colours.** `applyToolbarIcons()` gives each button its icon and colour group
  (`ToolbarButton::setIcon(icon, group)`; `toolbarGroupHue()` resolves the token): **file** (New,
  Save, Load) `hueGreen`; **edit** (Undo, Redo, Auto Arrange) `hueAmber`; **view** (Library,
  Minimap, Matrix, Panel) `accent`; **AI** `hueRose`; **housekeeping** (Settings, Light mode)
  `hueViolet`; **Feedback** sits beside Settings but has its own group, `hueGreen`.
- **Chip.** The icon sits on a 30 x 24 chip, radius 7, 4 px from the top of the button: the group
  colour at 16 percent at rest and 26 percent on hover (10 and 16 percent on a light theme, any
  theme with `isDark` false). A lit toggle fills the chip solid with the colour. In narrow mode
  (no caption) the chip is centred vertically in the button.
- **Glyph.** 19 px, drawn from the icon's 24-unit SVG grid in four colour roles (see
  [icons](icons.md#multi-role-icons)). On a lit chip the roles invert (colour and paper turn
  `iconInk`, soft turns ink at 40 percent, ink takes the colour), cross-fading with the chip.
  Hide/Show panel has its own glyph (`Icon::TogglePanel`). Save is the exception: a floppy in its
  own fixed colours (black body, silver shutter, white label with a tiny "Agent / Synth" in outlines
  that only reads at large sizes), identical on every theme. Feedback is a speech bubble (paper) with
  group-colour lines inside a group-colour ring over a soft tint of it.
- **Caption.** Inter semi-bold 10.5 px (`AppLookAndFeel::uiSemiBoldFont`), 2 px under the chip:
  `textMuted` at rest, `textPrimary` on hover, press and when lit, `textDisabled` when disabled.
  Captions are never coloured.
- **Ground.** Nothing at rest, `surfaceHi` at 60 percent on hover and 85 percent pressed (radius
  `pillRadius`).
- **Disabled.** No chip; the glyph at 40 percent.
- **Motion.** Hover, press and lit each ease (see
  [animation](animation.md#what-moves-and-how)); on hover the glyph lifts 1 px and does
  one small per-icon motion (Undo and Redo swing their arrow about its elbow and bounce past the
  swing before settling). Under Reduce Motion nothing moves; only the colours change.
- **The AI button's loop.** Its glyph is a patch cable with a spark at the plug, and instead of a
  hover motion a pulse runs along the cable and the spark blooms, 2.8 s a cycle, while the assistant
  is working (`AIChatComponent::onWaitingChanged` drives `toggleAiPanelButton.setBusy`) and while the
  button is hovered ([icons](icons.md#the-ai-button)). While busy the button's AX description and help
  text read "Assistant is working"; nothing repaints when it is idle, and Reduce Motion keeps it
  still.

The art is rebuilt from the icon library's untinted originals on every `setIcon`/`refreshArt` (and
on a look-and-feel change), so a theme switch re-colours the whole bar with no accumulated tint.
`applyToolbarIcons()` runs once at the end of `initialiseCommon()`, after every theme switch via
`changeListenerCallback`, and from `MainComponent::resized()` only when the narrow mode actually
flips (which changes the captions):

```cpp
bool prevNarrow = toolbarNarrowMode_;
toolbar.layoutButtons(toolbarBounds);       // updates toolbar.isNarrowMode()
toolbarNarrowMode_ = toolbar.isNarrowMode();
if (toolbarNarrowMode_ != prevNarrow)
    applyToolbarIcons();                    // rebuild only on mode flip
```

Tests: `Tests/UI/Chrome/ToolbarButton/ToolbarButtonPaintTests.cpp` (group colours on a dark and
the Daylight theme, lit, disabled, captions, theme switch, body/part split, the Feedback slot) and
`ToolbarButtonMotionTests.cpp` (hover, press and Reduce Motion through real mouse events) and
`ToolbarAiSparkTests.cpp` (the AI icon at rest, the keyframes, busy, hover and Reduce Motion).

### Toolbar keyboard access

The toolbar is one Tab stop (the "toolbar" focus region) with a roving ring inside it. The buttons stay
direct children of `MainComponent`, but `ToolbarComponent::setButtons` turns off their own keyboard focus,
so the toolbar is the only thing that holds focus and the ring is the only focus indicator.

| Key | Action |
|---|---|
| Left / Right | Move the ring to the previous / next button. Hidden and disabled buttons are skipped (Undo and Redo while there is nothing to undo); it stops at the ends and never wraps |
| Home / End | First / last button |
| Space / Return | Press the ringed button (its `onClick`) |

Only plain keys are handled; any modified chord bubbles up to the app shortcuts. The ring starts on the
first button and afterwards returns to the last one used when the region regains focus. If the ringed
button stops being navigable (Undo after its last step), the ring passes to the nearest navigable
neighbour rather than jumping back to the start.

The focused button wears `paintFocusRing` (the solid accent ring), drawn by the toolbar in the margin
around the button, since the buttons are siblings that paint above the strip. The region outline on the
strip stays as before.

**Names.** `MainComponent::applyToolbarLabels` gives every button (the canvas toolbar's too) a screen-reader title and,
unless the toolbar is in narrow mode or Show button labels is off, the same visible text. A toggle's title is the action a press does ("Hide
Library" / "Show Library", likewise Minimap, Matrix, AI and Panel; "Light Mode" / "Dark Mode"), in both
widths, because these buttons expose no checked state to a screen reader. The toolbar's accessibility value is the name of the ringed button, so Left/Right announce it.
Tooltips name each button's shortcut where it has one.

### Show button labels

The Preferences switch **Show button labels** (Panels & Windows; `showToolbarLabels` in the user settings, on by
default; `Source/UI/Chrome/CanvasToolbar/ToolbarLabelsPreference.h`) takes the caption off every top-bar button and
every [canvas toolbar](#canvas-toolbar) button, leaving the icon chip. It also covers the Timeline panel's Add Track
and Snap buttons and the Mixer panel's Inserts, Sends, EQ, Reset Meters and Add Bus buttons
([inline buttons](#inline-buttons)). `applyToolbarLabels(iconOnly)` ORs it with the
narrow-width answer, so a narrow window is icon-only whatever the switch says, and a settings write
(`MainComponent::changeListenerCallback`) re-applies the labels at once through `applyToolbarLabelPreference()`,
which does work only when the value flipped. Titles and tooltips (with the key) are unchanged in both modes, so a
screen reader and a hover still name each button. Tests: `Tests/UI/Chrome/CanvasToolbarTests.cpp`,
`PreferencesSettingsTabToolbarLabelsTests.cpp`.

### Inline buttons

The Timeline and Mixer toolbars have no room for a caption under the chip, so their buttons are `ToolbarButton`s in an
inline layout (`ToolbarButtonLayout`, `toolbarChipSpec()`): the chip, then the caption to its right, in one row, so
no bar grows. The chip is the top bar's 30 x 24 with a 19 px glyph (`Inline`: the Timeline's 34 px bar and its Add Track
strip) or a 24 x 20 chip with a 15 px glyph (`InlineCompact`: the Mixer's 24 px bar). The look-and-feel paints them
with the same chip, glyph roles, hover, press and lit tweens as the top bar; a lit toggle with a caption also sits on a
faint pill of its group colour. `ToolbarButton::setLabel(name, captionShown)` sets the screen-reader title (always the
full name) and the caption (the name, or empty when labels are off); `getPreferredWidth()` is the width the caption
needs, and `setContentCentred(true)` (Add Track) centres chip and caption in a wider button.

| Button | Glyph (`Icon`) | Group colour | Lit |
|---|---|---|---|
| Timeline "Add Track" | `ToolbarAddTrack` | amber | no |
| Timeline "Snap" | `ToolbarSnap` | amber | while snapping is on |
| Mixer "Inserts" / "Sends" / "EQ" | `MixerInserts` / `MixerSends` / `MixerEq` | accent | while the section is shown |
| Mixer "Reset Meters" | `MixerResetMeters` | violet | no |
| Mixer "Add Bus" | `MixerAddBus` | amber | no |

The glyphs are multi-role SVGs ([icons](icons.md#multi-role-icons)). `MainComponent::applyToolbarLabels` pushes the
preference to `TimelinePanelComponent::setToolbarLabelsShown` and `MixerPanelComponent::setToolbarLabelsShown`; the
mixer is one panel in the dock, the Own panel and a detached window, so all three follow. Narrow mode does not touch
them. With labels off the tooltip still names the button ("Add Track: ...", "Snap on/off (J)"). Tests:
`Tests/UI/Chrome/ToolbarButton/ToolbarButtonPaintTests.cpp`, `TimelinePanelResizeTests.cpp`,
`MixerSectionPanelTests.cpp`.

## Canvas toolbar

Auto Arrange, Minimap and Matrix live in `synth::ui::CanvasToolbar` (`Source/UI/Chrome/CanvasToolbar/`), a strip on
the canvas's own top row, wherever the canvas is. It owns the three `ToolbarButton`s (same icons, groups, captions,
tooltips and keys as before; `MainComponent` wires their `onClick`, titles and tooltips in `applyToolbarIcons()`),
is a `MainComponent` child like the canvas, and is laid out by `MainComponent::layoutCanvas()`: the top
`CanvasToolbar::kHeight` (44 px, the top bar's height, so a captioned button gets the same room as on the top bar) of the main area when the
canvas is the main view, or of the Canvas tab body when it is a panel tab (the canvas there is 44 px shorter). In a rectangle so short that 44 px would be over half of it, the strip takes half, so the canvas never vanishes. It is
visible exactly when the canvas is, so it moves with it in a swap and has no animation of its own. The strip is the
same height with or without captions, so toggling Show button labels never moves the canvas. Buttons are left
aligned, 84 px wide with captions and 32 px without.

The canvas keys reveal a hidden canvas first: Cmd+L, Cmd+K and Cmd+M call `ensureCanvasShowing()` before acting
(`MainComponentCommandTable.cpp`), and adding a module, plugin or snippet from the Library with a click or Return
does the same (drags land on the canvas, so they already show it). The greyed-out Minimap keeps its
"Needs a taller canvas" tooltip ([minimap](minimap.md#placement-sizing-and-auto-hide)).

**Keyboard.** The strip is one Tab stop, the "canvasToolbar" focus region (right before Canvas, wherever the canvas is: the main
area or the bottom panel's Canvas tab; open while the strip is visible), with the same roving ring as the top bar: Left/Right/Home/End move across the visible, enabled
buttons (the greyed Minimap is skipped), Space/Return press the ringed one, modified chords bubble up. The buttons give
up their own focus, the ring is drawn with `paintFocusRing`, and the strip's accessibility value is the ringed button's
name.

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
  The button is the shared `synth::ui::IconButton` (Bare style, Play glyph, Stop while on): accent
  while playing, muted while stopped, the same glyphs as the timeline bar's play/stop
  ([`theming.md`](theming.md#themed-widgets)); `IconButton` and `IconGlyphs` are compiled into `Core`
  precisely so this bar can use them. **The
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
  The count (`AudioEngine::getDisplayVoiceCount`) sums Poly MIDI voices plus every module implementing `synth::VoiceCountSource` (Sampler Instrument mode, Pluck, Mallet), each reading a count its audio thread published per block.
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

A transient message (`showMessage()`) cross-fades over the normal text (160 ms in, 110 ms out; see
[animation](animation.md#fading-things-in-and-out)) and suppresses every tooltip on the row —
`getTooltipForPosition` returns `""` immediately — since it visually covers the segments it would
otherwise explain.

**Screen-reader text.** The painted segments have no components of their own, so the bar exposes one
accessibility value, `getAccessibilityText()`: the items currently drawn, in drawn order, for example
`Project clips, CPU 13%, RT 12.5 ms, position 001.1.000, 120 BPM, 0 voices`. It follows `paint()`'s own
visibility rules: the round-trip and position/tempo items are left out when the bar is too narrow to draw
them, an unavailable round trip reads "RT unavailable", and while a transient or sticky message covers the
bar the message alone is read. The mute and play/stop buttons stay separate named children. The value is
read when a screen reader reaches the bar; the 5 Hz updates do not announce themselves.

### The mod drop hint

`GraphEditor::beginConnectionDrag()` shows one status-bar message, ONCE per install: the first time
a cable drag starts from a modulation source's OUTPUT (`ModuleBase::getJackTargets(channel,
false)`'s role is `PortRole::ModCV`), it fires `onStatusMessage("Drop it on any knob to modulate
that parameter")` — reaching the status bar through the same `onStatusMessage` callback
[`docs/modules/modulation.md#drag-to-knob-modulation`](../modules/modulation.md#drag-to-knob-modulation)
routes every other macro/mod refusal through, MainComponent wires it to `statusBar.showMessage()`.
An INPUT drag (re-aiming an existing cable) and a plain audio/MIDI output drag never qualify.

Shown-once state is `synth::kModDropHintShownSettingKey` (`UserSettings.h`), persisted through
`GraphEditor::propertiesFile_` — the same `juce::PropertiesFile*` the macro recolour favourites
shelf and the piano roll's scale-assist panel persist through (see those members' own doc
comments) — set via `GraphEditor::setPropertiesFile()`, wired by `MainComponent` to
`appProperties.getUserSettings()`. A `nullptr` (e.g. a headless test that never calls the setter)
means the hint never fires and the flag never gets written, rather than crashing.

## Inline label editors and accessibility

An inline-editable label (the BPM and time-signature fields, a mixer channel name, a track name, the MIDI Remote
control name) is a `synth::ui::NonModalLabel` (`Source/UI/Layout/NonModalLabel.h/.cpp`), never a plain `juce::Label`.
`juce::Label::showEditor()` calls `enterModalState()`, and on macOS a modal component is reported as
`isAccessibilityModal`: the window's accessibility tree collapses to the title bar for as long as the editor is open.
`NonModalLabel::editorShown` schedules `exitModalState()` one message-loop turn later (the call cannot happen
inside the hook, because `showEditor()` enters modal state right after it returns). Nothing the modal state did is lost:
the editor is a child holding keyboard focus, and losing that focus commits (or discards, per `setEditable`) through
`Label::textEditorFocusLost`, as a click outside did via `inputAttemptWhenModal`. Escape is deferred too: JUCE 9.0.3's
`TextEditor::handleCommandMessage` dispatches it under the listener list's lock, and the base
`Label::textEditorEscapeKeyPressed` deletes the editor there (the unlock then writes freed memory), so
`NonModalLabel` restores the text at once and calls `hideEditor(true)` from a `callAsync`. The card-title rename is a raw
`juce::TextEditor` child and was never modal. `NonModalLabelTests.cpp` asserts that no component is modal and no
sibling is `isCurrentlyBlockedByAnotherModalComponent()` (the condition JUCE's accessibility code keys on) while an
editor is open; the native tree itself needs a window peer, which the headless suite never creates.

## Panel collapse and persistence

The library sidebar and the AI panel can each be fully hidden (width 0). State persists across
launches via `ApplicationProperties`:

| Key | Default | Component |
|---|---|---|
| `librarySidebarVisible` | `"1"` (true) | `moduleLibrary` left panel |
| `aiPanelVisible` | `"0"` (false) | `aiChatComponent` right panel |
| `mixerOwnPanelHeight` | absent (`220`, also the minimum) | the Mixer's "Own panel" strip — written once per resize drag, on mouse-up (FRO231) |
| `detachedPanelBothPlaces` | `"move"` | FRO336: "move"/"both" — Tab placement's Mixer detach button either moves the panel into its window (default) or keeps it docked and opens a second live view too, see [`docs/mixer/panel.md`](../mixer/panel.md#detach-mode-move-or-both-places) |
| `sidePaneOpen.<tab>` / `sidePaneWidth.<tab>` | absent (`mixer` open, `timeline` closed; `200`) | a bottom-panel tab's side pane (`sidePaneOpen.mixer`, `sidePaneOpen.timeline`): open state, and its width clamped to 160-320 px; the width is written when a resize drag ends, see [side pane](side-pane.md) |
| `mixerMirrorWindowBounds` | absent (centred default) | the "both places" mirror window's own position/size — same `DetachedPanelWindow` bounds-persist mechanism as `mixerWindowBounds` |
| `bottomDockActiveTab` | `"timeline"` | the bottom dock's active tab (`timeline`/`mixer`/`midiRemote`/`canvas`); a saved tab that is not offered falls back to the first offered one |
| `bottomDockTabOrder` | `"timeline,mixer,midiRemote,canvas"` | the dock's strip order, all four views; a saved three-entry order (older builds) gains `canvas` at the end, anything malformed falls back to the default, and an older build reading four entries falls back to its own default. See [the tab strip](../mixer/panel.md#the-tab-strip) |
| `mainView` | `"canvas"` | which view sits in the main area (`canvas`/`timeline`/`mixer`/`midiRemote`, see [the main view](#the-main-view)); its tab is not offered in the strip. Written on every swap and applied at launch. A saved view whose panel is in its own window, or a Mixer outside the Tab placement, is suspended for the session (the Canvas fills the main area) and the saved value is kept |

Both keys are read at the top of `initialiseCommon()`, before any `setVisible()` or
`addAndMakeVisible()` call. Cmd+B toggles the library sidebar, wired through `ShortcutManager`.

The Timeline and Mixer panels are a separate mechanism: each can additionally DETACH into its own
top-level window (`Source/UI/Layout/DetachablePanelHost/`) rather than only hide and show in place,
and the Mixer's dock-vs-own-panel-vs-window placement is itself a Preferences setting. See
[`docs/mixer/mixer.md`](../mixer/mixer.md).

### Bottom dock height

The bottom dock (Timeline / Mixer / Controllers tabs) is resizable from ONE top-edge grab strip,
`synth::ui::PanelResizeHandle`, owned by `BottomDockComponent` and therefore live on every tab
(FRO231). Dragging reports the desired total dock height through `BottomDockComponent::onResizeHeight`
(live) and `onResizeHeightCommitted` (mouse-up, only after a real drag); `MainComponent` clamps it
(`[Metrics::timelinePanelHeight, max(metric, 75% of the window)]`), lays out live and persists the
`timelinePanelHeight` key once per gesture. The height is the dock's, not a tab's, so it holds when
switching tabs. Full rules: [`docs/timeline/timeline.md`](../timeline/timeline.md#panel-height).

The dock's tab strip is drag-reorderable through the shared reorder drag (the lifted tab follows the
grab point, the others glide aside, the order is applied and persisted on release, Esc cancels):
[`docs/mixer/panel.md#the-tab-strip`](../mixer/panel.md#the-tab-strip) and
[`docs/layout/animation.md#reorder-drag`](animation.md#reorder-drag).

### The main view

The rect the canvas gets in the carve above is the **main area**, and one of four views fills it (`mainView`;
the canvas unless `BottomDockComponent::setMainView()` moved another one up). With the Timeline, the Mixer or the MIDI Remote
panel there, the dock moves that view's whole `DetachablePanelHost` (not the bare panel, so its side pane and
detach state travel with it) into `MainComponent`'s `MainViewHolder` (`Source/UI/Layout/MainViewHolder.h`), which
`MainComponent::layoutMainView()` places at the main rect; the canvas goes to the dock as a tab (below). A JUCE
child is clipped to its parent, so the dock could not show the view over the main area itself; the move is the
Mixer's Own-panel move generalised, and the dock lays out only the hosts still parented to it. When the view
leaves main its host returns to the dock as a tab that is not active (it fades in if it becomes the active one).
The Library and the AI panel keep their places beside the main area.

A view in the main area is on screen whether or not the panel is open: hiding the panel (Cmd+J) never hides it,
a dock tab switch never fades it, and every "is it showing" gate counts it:
`BottomDockComponent::isTimelineShowing()` / `isMidiRemoteShowing()` / `isMixerShowing()` (the playhead poll,
the activity tick, the meters, and the Timeline's edit commands through `resolveEditSurface()`),
`MainComponent::isTimelineReachable()` (snap grid, track step, timeline zoom and the locator keys) and the
`timeline`, `timelineRouting`, `scaleAssist` and `midiRemote` focus regions. Revealing it (Cmd+1/3, a focus
region's `open`, "Edit MIDI assignment...", adding a plugin automation lane) never opens the panel
(`ensureDockOpenFor()`), and `setActiveTab()` ignores the main view. A project load's edit block covers the holder
as well as the dock. `setMainView()` refuses a view that is open in its own window; while in main the host shows
no header of its own, so it cannot be detached from there. The Mixer goes to main only in the Tab placement
(Own panel and Window leave its host to `MixerPlacementController`, so `setMainView(Mixer)` is refused). In main the Mixer fills the area (`canGrowHost` is false,
`growHost` and `fitMixerHostToSections()` do nothing, so the shared panel height never changes), its `mixer` focus
region is open, and the "both places" mirror is not reachable from it (a tab's detach icon is not reachable from it;
a mirror already open closes without detaching the host). The user puts a view there with the swap icon on its tab
([the tab strip](../mixer/panel.md#the-tab-strip)); the icon on the Canvas tab puts the canvas back. A swap
relays the main area out through `BottomDockComponent::onMainViewChanged`, which also rebuilds the focus regions
so Tab follows the new layout. The same swap has a key per strip place, Opt+Cmd+1..3 (`swapDockTab<N>ToMain`, [shortcuts](../control/shortcuts.md)); a successful swap, by icon or key, is announced to screen
readers once ("Timeline in the main area"), and a refused one is silent.

**The main area is never empty.** The stored view (`getStoredMainView()`, the saved `mainView`) and the view in the
main area now (`getMainView()`) are separate in `BottomDockTabModel`. While the stored view's panel is open in its
own window, or the Mixer leaves the Tab placement, the model suspends it: the Canvas fills the main area for the
session, `refreshMainViewAvailability()` returns the host to the dock, and the setting on disk is not touched. When
the panel docks again (or the Mixer returns to the Tab placement) the stored view goes back to the main area;
choosing any view meanwhile, the Canvas included, replaces the stored choice. A tab detached while another view is
in main changes nothing in the main area (the Canvas tab keeps the panel usable), and a tab in its own window is not
in the strip, so its disabled swap icon is never on screen. The "both places" mirror is a second view of the docked
Mixer and never suspends anything. Each change fires `onMainViewChanged`. A swap with the panel open on screen is
animated, not a jump: the two views trade places in 240 ms ([swap motion](animation.md#what-moves-and-how)).

### The canvas as a tab

When another view is the main view the canvas is the dock's Canvas tab (the tab button is hidden while the
canvas is main, and the tab has no detach icon, and its swap icon puts the canvas back). `GraphEditor` stays a `MainComponent` child: the dock only
reports the tab body (`BottomDockComponent::getCanvasBodyBounds()`, everything under the tab strip) and lets
mouse events through it (`hitTest`); `MainComponent::layoutCanvas()` sets the editor's bounds to that rect on every
layout pass, so it follows the panel as it slides (no cross-fade; the relayout is the work a window drag already
does per frame). The editor is hidden, not zero-sized, while another tab shows or the panel is closed. When it
first lands in the panel its view is fitted once, after the slide ends. The zoom and pan it had in the main area are remembered meanwhile (this session only) and put back in the
layout pass where it returns there, before a swap takes its arriving picture, so it looks exactly as it was left; if
no module would be visible in that view any more, it is fitted to its modules instead.

`MainComponent::ensureCanvasShowing()` is the one reveal seam: a no-op while the canvas is main, else it opens
the panel and selects the Canvas tab. It runs before a channel chip click's canvas fallback, Locate (Go to
Output), the mixer's "Edit on canvas" (`MixerPanelComponent::onRevealCanvas`), a modulator's "show on canvas",
every project or patch load, and an accepted AI patch. Cmd+J closing the panel around the canvas hands keyboard
focus to the view in the main area, not the canvas ([focus regions](../control/focus-regions.md)).

### Tab strip keyboard and screen-reader access

The tab strip is one Tab stop of its own, the "dockTabs" focus region, registered between Canvas and the
panel regions; it is open whenever the dock is showing with at least one docked tab. Its focus holder is
`BottomDockComponent::getTabStripFocus()`, a transparent leaf behind the tab buttons that never takes the
mouse; the tab buttons give up keyboard focus themselves. A panel's own region is separate, so Tab goes
Canvas, tab strip, then the panel.

| Key | Action |
|---|---|
| Left / Right | Select the previous / next tab in the strip's current order, which switches the panel as a click does. Tabs that are not offered (detached, or the Mixer in another placement) are skipped; it stops at the ends |
| Home / End | First / last tab |
| Return | Move keyboard focus into the selected tab's panel (its region root) |
| Shift+Return | Press the selected tab's swap icon (below); consumed even while the icon is disabled |
| Option+Return | Press the selected tab's detach icon (below); consumed on the Canvas tab, which has none |

Otherwise plain keys only: Cmd+1/2/3 and Tab are not touched, and Up/Down are left unhandled. The shared key rule
is `tabStripKeyTarget` (`Source/UI/Layout/TabStripKeys.h`), the same one the Settings window's tab strip
uses ([accessibility](../development/accessibility.md#tab-strips)). Cmd+J (when it opens the pane) and
Cmd+1/2/3 (for a docked tab) leave keyboard focus on the strip; Cmd+J closing the pane around the focus
hands focus to the view in the main area ([shortcuts](../control/shortcuts.md#switching-tabs)). The selected tab wears
`paintFocusRing` while the strip has focus (not during a reorder drag), drawn by the dock over the tab.
Clicking a tab does not give the strip focus.

**Screen reader.** JUCE has no tab role, so each tab is exposed as a radio button, its closest selectable
and mutually exclusive item, named "Timeline", "Mixer", "Controllers" or "Canvas", with the selected state
following the active tab and a press that selects it (`BottomDockComponent::describeTab` is the data both the
handler and the tests use). Inside it is the swap icon, a button named "Show Mixer in the main
area" (its tooltip adds the key, Opt+Cmd+1-3); it is not a Tab stop, and it shows the focus ring while Shift is held on the focused strip (the moment
Return presses it). Left of it every tab but Canvas has a detach icon named "Detach Mixer to its own window" (its tooltip names Option+Return); a click detaches that tab's panel, and its ring shows while Option is held. In its own window Esc or Cmd+W returns the panel to the dock, and Cmd+1/2/3 raise and focus that window. The strip itself is a group named "Panel tabs" whose value is the selected tab.

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

**Logo.** The cat mark (`assets/brand/agentsynth-logo-256.png`, embedded through `cmake/Assets.cmake`, so
`BinaryData::agentsynthlogo256_png`) is drawn above the title, 96 pt square as a transparent cut-out (neon outline kept,
no tile or border) so it reads on both themes; 256 px is sharp at 2x. `getCardBounds()` and `resized()` both reserve its 96 pt plus a 12 pt
gap. Each time the overlay is shown it fades in and rises 10 px (320 ms, `easeOutCubic`, paint-only); Reduced Motion
lands it at once. Tests: `Tests/UI/Chrome/WelcomeScreenLogoTests.cpp`.

**Usage statistics card.** While `usageStatsAsked` and `shareUsageStats` are both false, an inset panel
(`UsageStatsPromptComponent`, accent border and fill from the theme) sits between the subtitle and the three start
buttons: heading, a plain-words body, Share and No thanks as equal buttons, and a "What we collect" link. All
three are Tab-reachable inside the overlay's focus container and show the shared focus ring. The card height
(`getCardBounds()`) and `resized()` both add `promptSlotHeight()`, the panel plus its 14 px gap scaled by the
open fraction, so after an answer the panel collapses (180 ms, `easeInCubic`, height and fade) and the card
re-forms with it instead of jumping; Share shows a one-line thanks first, and Reduced Motion lands at once.
Details: [usage statistics](../development/usage-statistics.md#the-welcome-screen-card).

App-only, gated on `ownedAudioEngine != nullptr` — see [architecture/audio-engine.md](../architecture/audio-engine.md)'s
Welcome screen subsection for the gating rationale and the guard-before-hide ordering that keeps a
Cancel answer from dismissing it. It fades out when a choice dismisses it and back in when Help > Show Welcome Screen
reopens it (hidden only after the fade, deaf to clicks while it leaves; [animation](animation.md#fading-things-in-and-out)).
It always shows at launch in the standalone app (never in the
plugin) and there is no opt-out; a legacy `showWelcomeScreenAtLaunch` value in an old settings file
is ignored. Help > Show Welcome Screen reopens it.

**Reopened over a project.** Help > Show Welcome Screen while the launch screen is gone puts the overlay back in its
*dismissible* mode (`WelcomeScreenComponent::setDismissible(true)`, set by `MainComponent::showWelcomeScreen()`): it is no
longer opaque, so the open project shows through a dimmed scrim (black at 60%) around the card, and a close button (the
shared `IconButton`, `Glyph::Close`, named "Close welcome screen", tooltip "Close (Esc)") sits in the card's top-right
corner. Escape, that button (a Tab stop; Space/Return press it, accent focus ring from the look-and-feel) or a click on the
scrim outside the card calls `onDismissRequested`, which `MainComponent` answers with `hideWelcomeScreen()`: the usual fade
out, and the project, its dirty state and its undo history are untouched. The launch screen (nothing to return to) stays
opaque with no close button and no Escape or click-outside action. Tests: `Tests/UI/Chrome/WelcomeScreenDismissTests.cpp`.

Keyboard: the overlay is a keyboard focus container, so Tab and Shift+Tab cycle through its buttons
(the close button when dismissible, New, Open default, Open existing, the recents, Contribute, What's New) instead of escaping to the
toolbar and canvas hidden underneath. Reopening it from the Help menu focuses the first button. It has
Escape action only in the dismissible mode above; the launch screen has no cancel, only a choice of how to start.

Between the Recent Projects list and the footer the card carries a low-key contribute line and a
"Contribute..." button. It fires `onContributeRequested`, which `MainComponent` wires to
`openContributePage()` (the same `kContributeUrl` page as `AppCommands::contribute`) without hiding
the overlay, and it shows whenever the overlay is shown.

## Toast

`ToastComponent` (`Source/UI/Chrome/Toast/`) is a small message pill with one optional action, for a change that is
bigger than what the user clicked ("Made 4 modules poly" with **Undo**). `MainComponent` owns one, a plain child laid
out by `resized()` (centred, 20 px above the canvas' bottom edge) and raised when shown; a second `show()` replaces the
first. It slides up 10 px while fading in (170 ms, `easeOutCubic`) and fades out in 130 ms; under Reduce Motion it only
fades (80 ms), under Animations Off or while its parent is not on screen it lands at once. It hides itself 4 s after
the last `show()` unless the pointer is over it or its action holds the keyboard focus; Esc dismisses it. The action
is a real `TextLinkButton`: focusable, titled "Undo", with a tooltip ("Undo this change"); it dismisses the toast, then
runs. The Poly pill's Undo only undoes while its step is still the last edit (`AppUndoManager::getEditSerial()`).
Colours are theme tokens only.

## Shortcut hint overlay

`Source/UI/Chrome/ShortcutHint/ShortcutHintOverlay` is a second full-window overlay: a child of
`MainComponent` that stays invisible except while Cmd is held, painting key-cap bubbles over the
toolbar, dock tabs and transport buttons. It is created at the end of `assembleToolbar()`, tracks the
window size through a `ComponentListener` (no line in `resized()`), raises itself to the front only
when it appears, and takes no clicks, so its z-order never matters at rest. Rules and placement:
[control/shortcuts.md](../control/shortcuts.md#shortcut-hints).

## Shared search

Every search or filter box matches and highlights the same way, through `Source/UI/Layout/SearchMatch.h`
(namespace `synth::ui`): the module library, the Mod Matrix / add-automation / add-modulator pickers
(`ModMatrixPicker`), the Preferences and Keyboard Shortcuts filters, the MIDI Remote action picker, the
Mixer zones channel filter, the MIDI destination picker, the card layout editor's control search and the port
connections panel's Add connection page (`PortTargetSearchPage`: jacks, knobs and "New <module>" rows).

- `searchMatches(text, query)`: the query is split on whitespace; every word must appear in the text,
  ignoring case, in any order (`"osc 8"` finds `"Oscillator 8"`). A blank query matches everything, so a box
  that must treat a blank query differently (the library hides no section for it) guards at its call site.
- `searchScore(text, query)`: `-1` no match, else lower is better. Per word the best occurrence counts: `0`
  the text starts with it, `1` it starts a word, `2` it sits inside a word; the worst word sets the score.
  Pickers use it to pre-highlight the best row.
- `searchHighlightSpans` + `drawSearchHighlightedText`: every hit of every word, sorted and overlap-merged,
  drawn as a rounded accent fill behind accent-coloured letters. Static, so Reduce Motion needs nothing. A
  box that paints its own rows (library, pickers, action picker, mixer rows) uses the painter; one that only
  hides components (Preferences, Shortcuts) shares just the matcher.

- `moduleSearchAliases(moduleName)`: the other names a module type goes by, appended to its name when a
  module/source search matches (`env`, `envelope`, `eg`, `contour` find the ADSR, `vco` the Oscillator, `vcf`
  the Filter, `amp` the VCA). It is keyed on the module's own name (a number suffix is ignored), so a renamed
  card still matches. The module library, the mod dot's Add source page (`ModSourceItem::searchText`) and the
  Mod Matrix source picker (`Item::searchText`) match on name plus aliases; the painter still highlights only
  the visible name, so an alias-only hit shows the row without a highlight. The port panel's Add connection page
  does the same: a grouped row paints only its port, the module's name is searched but not painted.

A new search box must use these; do not call `containsIgnoreCase` on the query.

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
  multi-output or multi-target module's nested source/dest submenu leaves read `"<Module> - <jack>"`
  / `"<Module> - <target>"`, not a bare jack name. `updateRowsFromGraph()` re-populates every row's
  combos (`populateCombos()`) BEFORE re-applying its selection (`refresh()`), because
  `populateCombos()` clears the combo box as a side effect of rebuilding it.
- **Sources are the jacks the card draws.** The source list offers one entry per visible output jack
  (`synth::ui::modSourceOutputs`, `Source/UI/Graph/ModMatrixEndpoints.h/.cpp`), not one per raw output
  channel: an LFO, whose card shows one CV jack over three raw channels (two are silent pass-throughs),
  is one entry named just "LFO 1". A jack that fronts two poly heads (Poly MIDI's Pitch and Gate) is
  listed once per head, named by role. A routing saved from a channel the list no longer offers keeps
  an entry of its own (`"<Module> - Out N"`), so its box never goes blank.
- **Searchable source and destination picker.** Clicking either combo (or Return/Space with it focused)
  opens `synth::ui::ModMatrixPicker` (`Source/UI/Graph/ModMatrixPicker.h/.cpp`) in a `juce::CallOutBox`
  instead of the stock menu; `PickerComboBox` overrides `showPopup()` and stays the closed display.
  `ComboBox::showPopupIfNotActive()` raises the combo's private "menu active" flag before calling
  `showPopup()`, and only the stock menu lowers it, so `PickerComboBox::showPopup()` lowers it itself
  (`hidePopup()`) before opening the picker: left raised, the combo ignores every later click and the
  panel stops refreshing. A search field at the top takes focus and filters word by word
  (the shared `synth::ui::searchMatches`, [below](#shared-search)): every space-separated word of the query must appear in the row
  text (module title plus output or target label, e.g. "Filter - Cutoff"), ignoring case and order, so
  "osc 8" finds "Oscillator 8", and the matched letters are highlighted in the row text and detail line; rows sit under the same category headers the menu
  had (Envelopes, LFOs, Oscillators, Sequencers, Filters, Effects, Other), and a header hides when none
  of its rows match. "Flat Sources" drops the headers. The popup's height comes from the full list, so it
  does not resize while typing. Up/Down move the highlight, Return picks the highlighted row (the best match
  by `searchScore`, the first among equals, until moved), Escape closes, a click picks. A pick selects the combo's id with a synchronous
  notification, so it goes through `ModRow::comboBoxChanged` and the macro-port routing like any other
  choice. The picker paints its own opaque themed panel (a parentless call-out is a new window that does
  not inherit the LookAndFeel), outlines itself with `paintFocusRegionOutline` while it holds focus, and
  every control carries a title and tooltip: the combos, amount slider and bypass button are named with
  their row ("Routing 2 source", "Routing 2 amount"), the delete button "Remove routing 2", plus the
  search field, each picker row, and the Add Modulation and Flat Sources buttons.
- **Keyboard.** The matrix is one focus region (Tab lands on it and leaves it). Inside it the arrow
  keys walk its controls (`Source/UI/Graph/ModMatrixKeyboard.h/.cpp`, a stateless key listener on the
  panel and its row container): Left/Right step through every control in reading order (Add
  Modulation, Flat Sources, then each row's source, destination, amount, bypass, delete), Up/Down move
  to the same column on the row above or below, and the amount slider keeps Up/Down to nudge the
  amount, as a mixer fader does. A combo never re-points its routing on an arrow key (the stock combo
  would, one undo step per press); Return or Space opens its picker. Every control draws the accent
  focus ring through `AppLookAndFeel`. `AccessibilityCoverageTests` audits the open matrix and the
  picker (`ModMatrix`, `ModMatrixPicker` at 0/0).
  The picker is also the timeline's "Add automation..." list ([automation](../timeline/automation.md#adding-a-lane-from-the-timeline)):
  an item's `searchText` adds words the row does not show.
  Test seams: `ModMatrixComponent::setPickerLauncherForTest` (capture the picker instead of launching a
  window) and `ModMatrixPicker::setSearchTextForTest`.
- **Labels follow the card title.** Every module in the source and destination combos is listed under
  `synth::moduleTitle` (`Source/AudioEngine/ModuleTitle.h`): the custom title the user gave the card,
  else the auto-numbered name ("LFO 3"). The 10 Hz tick compares a hash of every module title
  (`moduleTitlesSignature`) as well as the node count, so a rename (which changes no node count)
  re-populates the combos; a combo popup or picker that is open is never repopulated underneath the
  user, the change lands on the first tick after it closes.
- **Re-pointing a row goes through macro ports**, exactly like a dragged mod cable, as one undo step
  (the row's "Add Modulation" and delete buttons are undo steps too). See
  [`macros/auto-ports.md`](../macros/auto-ports.md#the-mod-matrix-re-points-like-a-drag).
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
