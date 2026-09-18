# Timeline Transport Bar

`Source/UI/Timeline/TimelineTransportBar.h/.cpp` (`synth::ui::TimelineTransportBar`) — play/stop,
record, loop, metronome, count-in, BPM and time-signature editors, and the bar:beat readout,
left-aligned in the transport-bar strip. The snap combo stays docked right
([view](view.md#snap-divisions)); the edit-tool strip sits immediately left of it
([edit-tools](edit-tools.md)).

## Layout and glyphs

Buttons are **square** — `min(26 px, the strip height after padding)`, centred in their slot — with
`kGap = 7 px` between them and `kGap * 2` between groups; the two editable labels and the readout
follow, in that order. `kButtonSize` (26) and `Metrics::timelineTransportBarHeight` (34) are sized
together so the glyphs render at their intended size instead of being clamped back down by
`min(kButtonSize, bounds.getHeight())`.

**No SVG assets.** Every button is one `GlyphButton` (a `juce::Button` subclass) drawing a plain
`juce::Path` per glyph in `paintButton` — a triangle/square for play-stop, a circle for record, an
open arc with an arrowhead for loop, a filled ellipse notehead plus a `juce::Rectangle` stem for
the metronome. This mirrors the root `CLAUDE.md` rule that themes never swap typefaces: a one-off
shape for a single caller does not earn a new icon asset either.

**Glyph geometry: one centred square, always.** Every glyph is drawn inside the button's shorter
side, inset by `kGlyphInsetRatio` (24%) on each edge — never a fraction of the *width* applied to
both axes, which flattens all four glyphs once the panel's 5 px resize grab strip leaves the bar
~19 px tall. Everything scales off that square (the loop arc's stroke and arrowhead, the note's
head and stem), so the row stays legible at any strip height.
`Tests/UI/Timeline/TimelineTransportBarTests.cpp::GlyphButtonsAreSquareAndSpaced` pins squareness
and the gaps at both the full and the trimmed strip height.

## Record red

An engaged record button is `TimelineTransportBar::kRecordRedArgb` (`0xFFE53935`) — a filled red
circle, a red border and a faint red wash behind it — **whatever the theme's accent is**, and
themes may not override it.

**Why.** A hardware record LED is red on every desk; drawn in a cyan or green accent, "armed" stops
reading as armed at all. It is the one colour on this bar that is not a theme token — every other
lit glyph (play/stop, loop, metronome) still uses `colors.accent`. Idle record is a neutral outline
(`colors.textPrimary` at 75%), not a dim red one. `GlyphButton::glyphColour()` is the single source
for both the paint and the `getRecordGlyphColourForTest()` seam.

## The transport is the truth

Every button click and every editor commit reads `TransportService::getPositionSnapshot()` **at the
moment of the action**, rather than from a value this bar remembers between polls:

- **Play/Stop** — one `GlyphButton` whose glyph flips between the two icons on `getToggleState()`.
  The click reads `getPositionSnapshot().playing` to decide `play()` vs `stop()`, so it works
  correctly even if nothing has polled `updateFromTransport()` since the last click.
- **Loop** — the click reads the CURRENT `loopStartPpq` / `loopEndPpq` off the snapshot and
  re-posts `setLoop(start, end, !looping)`. `TransportService`'s own construction default is
  `[0, 4)`, so "no bounds ever set" and "preserve existing bounds" fall out of the same one-line
  handler — there is no separate "default bounds" case to maintain.

  The loop range has a second consumer in the Export Audio dialog (see
  [`architecture_audio_engine.md`](../architecture_audio_engine.md#bounceexport)):
  `MainComponent::promptExportAudio` reads `loopStartPpq` / `loopEndPpq` off a fresh snapshot to
  decide whether "Current loop range" is offered as a bounce range whenever the region is
  non-degenerate, and seeds from it when it is selected. The offer does not depend on the loop
  being ARMED — a disengaged loop still names a real span, and `TransportService` always carries a
  valid `[start, end)`, so there is no separate "locators unset" state to detect. A bounce unloops
  for the duration and hands the region back. Read-only: the dialog never calls `setLoop` itself.
- **BPM label** — a `juce::Label` (`setEditable(false, true, false)`, the same double-click-to-edit
  idiom as the track-name label) whose `onTextChange` calls `transport->setBpm()` — always accepted
  (clamped to `[TransportService::kMinBpm, kMaxBpm]` inside the service), so there is no revert
  case. It is also **draggable**: a nested `BpmDragLabel` overrides `mouseDown` / `mouseDrag` to
  turn vertical movement into a live `setBpm()` call, ±1.0 BPM per 4 px (±0.1 with Cmd held, for
  fine adjustment), anchored to the snapshot's BPM at `mouseDown` so the gesture is reproducible
  from the anchor plus total delta regardless of how many `mouseDrag` calls land in between.
  Double-click and drag are independent gestures: JUCE dispatches `mouseDoubleClick` separately
  from `mouseDown` / `mouseDrag` / `mouseUp`, so overriding the latter three does not disturb
  `Label`'s own `editDoubleClick` handling.
- **Time-sig label** — the same double-click idiom; parses `"N/D"` and calls `setTimeSignature(n,
  d)`, which validates numerator `1..64` and a fixed denominator set (`1/2/4/8/16/32`) and returns
  `false` **without posting anything** on rejection. The label then reverts to whatever the
  snapshot is CURRENTLY reporting, not a remembered value — a rejected edit never touched the
  transport, so the snapshot is already the last known-good time signature.
- **`updateFromTransport()`** — the drive seam, called from the panel's 10 Hz poll. It resyncs the
  play and loop button visuals and the two labels' text from the snapshot, so a Space-bar play
  triggered elsewhere reflects here within one tick, but skips a label mid-edit
  (`Label::isBeingEdited()`): `Label::setText()` unconditionally discards an open editor's
  contents, so a poll landing mid-keystroke would otherwise fight the user's own typing.

## Editable labels take their colours from the theme

**An editable `juce::Label`'s editor does NOT inherit the app's `TextEditor` colours**, and the
mechanism is worth knowing before adding a third one — without the fix below, both fields above
type white on white on every light theme.

`Label::createEditorComponent` copies `Label::textWhenEditingColourId` /
`backgroundWhenEditingColourId` / `outlineWhenEditingColourId` onto the new editor's
`TextEditor::textColourId` / `backgroundColourId` / `focusedOutlineColourId` — but only for ids
that `isColourSpecified()`, and `LookAndFeel_V4` **does** specify `textWhenEditingColourId` from
its own default-scheme white. So a themed `TextEditor::textColourId` is set correctly and then
clobbered by V4's white on the way in.

`AppLookAndFeel::applyTheme` therefore sets all three Label editing ids, plus
`CaretComponent::caretColourId`, which V4 leaves black and therefore invisible on a dark theme.
That covers every editable label at once — these two and the track-name label. A raw
`juce::TextEditor` (the clip-rename and marker-rename editors) is unaffected either way: it reads
`TextEditor::textColourId` straight off the LookAndFeel. Pinned by
`TimelineTransportBarTest.InlineFieldEditorsTakeTheirColoursFromTheTheme`, which asserts the TOKENS
in a light and a dark theme so the fix cannot regress into a second hardcoded colour.

## Bar-beat readout

`TimelineTransportBar::formatBarBeat(ppq, tsNumerator, tsDenominator)` is a **static, pure** helper
(no `Component`, headless-testable on its own): `"BAR.BEAT.TICKS"`, 1-based bar (zero-padded to 3
digits), 1-based beat (unpadded), ticks = 1/960 of a beat (zero-padded to 3 digits), with
`beatsPerBar = tsNum * 4 / tsDen` — the same formula used throughout the timeline panel. Pinned
examples (`TimelineTransportBarTests.cpp::FormatBarBeatTable`): `(0.0, 4/4)` → `"001.1.000"`,
`(5.5, 4/4)` → `"002.2.480"`, `(3.0, 3/4)` → `"002.1.000"`.

Painted in JetBrains Mono via `juce::Font(juce::Font::getDefaultMonospacedFontName(),
theme.type.value + 1, plain)` — `AppLookAndFeel::getTypefaceForFont` resolves the default
monospaced font name to `theme.type.monoFamily` (JetBrains Mono in every built-in theme), the same
indirection `AIChatComponent`'s debug console and `SignInDialog`'s code label use.

Repainted **only when the formatted string changes** — a plain string-diff cache, not a
strip-confinement contract like the playhead's: the readout has no timer of its own and moves only
when its owner polls it, so there is nothing to bound beyond "do not repaint an unchanged tick".
`getReadoutRepaintCountForTest()` is the test seam, the same counting idiom
`TimelinePanelComponent::getTransportUpdateCountForTest()` uses.

## Recording

**Recording is the one control the bar is not authoritative over.** Whether a take actually
captures anything, and onto which track, is something only `MainComponent` can see — it owns the
`TimelineDoc`. The record button's click computes `!getToggleState()` and reports that as *intent*
through `std::function<void(bool)> onRecordToggled`; it never flips its own toggle state.
`setRecordingState(bool)` is the ONE thing that ever does, called back by the owner with the real
outcome, and it is set the same way **regardless of whether anything is armed** — the indicator
reflects record-ON, not "a take is capturing".

`MainComponent`'s implementation, installed in `initialiseCommon()`:

- **ON does NOT require an armed track.** It iterates `timelineDoc.getTracks()` for the first
  `armed && (kind == TrackKind::Midi || kind == TrackKind::Audio)` track — first-armed-wins, there
  is deliberately no "record both at once" — but either way the transport rolls: **record implies
  roll** (a DAW convention — the record button starts the transport if it is not already playing)
  and `setRecordingState(true)` fire unconditionally. An armed MIDI track additionally calls
  `midiRecorder.startRecording(track, currentPpq)`; an armed Audio track resolves a
  `RecordTapModule` tap and take files *before* the transport moves — a request that cannot be
  honoured (no Audio Output in the patch, or the take file cannot be created or opened) must not
  leave the transport rolling — and then starts its capture.

  **With nothing armed**, the transport still rolls and the indicator still lights, identical to
  Play plus a lit record indicator; no take of either kind starts, and
  `statusBar.showMessage("Recording started - no track is armed")` explains the silence. Arming a
  track *mid-roll* does not retroactively start a take either: `TimelineDoc::setTrackArmed` has no
  listener watching for this, so the user has to stop and press Record again once something is
  armed.
- **OFF** — a button click, or the 10 Hz poll noticing `playing -> stopped` while
  `midiRecorder.isRecording()` (the user hit Space or Stop instead of the record button). Both
  routes go through one `MainComponent::commitMidiRecording()`: `midiRecorder.stopAndCommit(doc,
  undo)`, then `midiRecorder.hadOverrun()` → `statusBar.showMessage("Dropped MIDI events during
  recording")`, then `setRecordingState(false)`. One choke point means the explicit and the
  auto-commit paths can never diverge — see
  [`architecture_app_wiring.md`](../architecture_app_wiring.md)'s `MidiRecorder` wiring entry
  (hook 5) for the full ordering. With nothing armed there was never a take to commit, so OFF just
  turns the indicator back off.

`AudioEngine::setMidiCaptureSink(&midiRecorder)` is the other half of the app-level wiring, feeding
`MidiRecorder::captureBlock` from `AudioEngine::renderNextBlock`'s one collector-merged buffer. See
`Tests/Timeline/MidiRecorderTests.cpp` for the model-level coverage and
`Tests/UI/Timeline/TimelineTransportBarTests.cpp` for the button-to-commit path.

## Transport actions

Record has two entry points into the same gate. `AppCommands::transportRecord`
(`MainComponentCommandTable.cpp`) is a command-dispatched action a MIDI Remote hardware button, or
a Settings-rebound key, can invoke via `ApplicationCommandManager::invokeDirectly`. It reaches
`handleRecordToggle` by calling `getRecordButton().triggerClick()` — the SAME button the mouse
clicks, so both entry points hit the identical armed-track gate above; there is no second, looser
path.

`transportPlay` / `transportStop` / `transportReturnToStart` call `TransportService` directly:
play and stop are guarded on the current snapshot so each is idempotent, and return-to-start is
`locateBeat(0)` with no implicit stop. `transportToggleLoop` and `transportToggleMetronome` reuse
the loop and metronome buttons' own `triggerClick()` the same way `transportRecord` reuses
Record's — see [`shortcuts.md`](../shortcuts.md#transport-family) for the full action-id table. All
six ship unbound by default; only `togglePlayback` keeps a default key (Space).

## Metronome and count-in

Two controls sit in the transport-bar strip, right after the loop button and before the BPM label:
a metronome toggle and a 3-item count-in selector. Both are `TimelineTransportBar` members — not
routed through `TimelinePanelComponent`, which has no other reason to know either setting exists —
and both persist themselves via the bar's own `setApplicationProperties(props)`, restoring
`"timelineMetronomeEnabled"` (bool, default off) and `"timelineCountInBars"` (int 0–2, default 0)
and re-persisting on every change, the same restore-then-persist-on-change idiom
`TimelinePanelComponent` uses for its snap combo. `TimelinePanelComponent::setApplicationProperties`
is a pure forward to the bar's version for these two keys, and
`TimelinePanelComponent::setMetronome` forwards a `synth::Metronome*` the same way `setTransport`
forwards a `TransportService*`.

**Metronome toggle** — unlike Record, there is no owner-side veto: the click directly flips both
the button's own visual state and `synth::Metronome::setEnabled`, via the bar's non-owning
`synth::Metronome*` set through `setMetronome()`, mirroring `setTransport`'s null-safe contract. No
intent/outcome split is needed. `setMetronome()` and `setApplicationProperties()` may run in either
order: whichever runs SECOND is what makes the persisted enabled value real, since each applies the
last-known value to the metronome pointer if the other has already been supplied.

**Count-in selector** — a `juce::ComboBox` ("Off" / "1 bar" / "2 bars", `getCountInBars()`
returning 0/1/2), read by `MainComponent`'s record flow at the moment Record is clicked, never
cached elsewhere. See
[`architecture_audio_engine.md`](../architecture_audio_engine.md#metronome--count-in) for the full
count-in choreography — locate-back, forced-on click, the punch-in filter — this selector feeds.

Layout: `metronomeButton_` (a square button, like its siblings) + `kGap` + `countInCombo_` (64 px)
+ `kGap * 2`, inserted between the loop button and the BPM label. The bar's fixed-width strip grows
by roughly 98 px, well inside the timeline panel's normal width.
