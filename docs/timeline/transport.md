# Timeline Transport Bar

`Source/UI/Timeline/TimelineTransportBar.h/.cpp` (`synth::ui::TimelineTransportBar`) — return to start, play/stop,
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

**No SVG assets.** Every button is one `GlyphButton`, a thin wrapper over the shared
`synth::ui::IconButton` (Framed style, [`theming.md`](../layout/theming.md#themed-widgets)). The
glyphs are plain `juce::Path` shapes in `Source/UI/Theme/IconGlyphs.cpp` — a triangle/square for
play-stop, a circle for record, an open arc with an arrowhead for loop, a filled ellipse notehead
plus a `juce::Rectangle` stem for the metronome. This mirrors the `Source/UI/CLAUDE.md` rule that themes
never swap typefaces: a one-off shape for a single caller does not earn a new icon asset either.

**Glyph geometry: one centred square, always.** Every glyph is drawn inside the button's shorter
side, inset by `kGlyphInsetRatio` (24%, in `IconGlyphs.cpp`) on each edge — never a fraction of the *width* applied to
both axes, which flattens all four glyphs once the strip gets short. Everything scales off that square (the
loop arc's stroke and arrowhead, the note's head and stem), so the row stays legible at any strip
height.
`Tests/UI/Timeline/TimelineTransportBarTests.cpp::GlyphButtonsAreSquareAndSpaced` pins squareness
and the gaps at both the full strip height and a shorter one.

## Record red

An engaged record button is `TimelineTransportBar::kRecordRedArgb` (`0xFFE53935`) — a filled red
circle, a red border and a faint red wash behind it — **whatever the theme's accent is**, and
themes may not override it.

**Why.** A hardware record LED is red on every desk; drawn in a cyan or green accent, "armed" stops
reading as armed at all. It is the one colour on this bar that is not a theme token — every other
lit glyph (play/stop, loop, metronome) still uses `colors.accent`. Idle record is a neutral outline
(`colors.textMuted`, the shared icon-button rest colour), not a dim red one. Record is
`IconButton::setOnColour(kRecordRedArgb)`; `IconButton::glyphColour()` is the single source for both
the paint and the `getRecordGlyphColourForTest()` seam.

## The transport is the truth

Every button click and every editor commit reads `TransportService::getPositionSnapshot()` **at the
moment of the action**, rather than from a value this bar remembers between polls:

- **Play/Stop** — one `GlyphButton` whose glyph flips between the two icons on `getToggleState()`
  (`IconButton::setGlyphWhenOn`).
  The click reads `getPositionSnapshot().playing` to decide `play()` vs `stop()`, so it works
  correctly even if nothing has polled `updateFromTransport()` since the last click.
- **Loop** — the click reads the CURRENT `loopStartPpq` / `loopEndPpq` off the snapshot and
  re-posts `setLoop(start, end, !looping)`. `TransportService`'s own construction default is
  `[0, 4)`, so "no bounds ever set" and "preserve existing bounds" fall out of the same one-line
  handler — there is no separate "default bounds" case to maintain.

  The loop range has a second consumer in the Export Audio dialog (see
  [`architecture/audio-engine.md`](../architecture/audio-engine.md#bounceexport)):
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

**Record also starts playback** (Preferences, Timeline category, key `recordStartsPlayback`, default on — the convention of
Cubase, Logic, Pro Tools, Reaper and Ableton). Off, pressing Record from a stop only engages recording; the transport is
left alone and the take starts when the user presses Play. A MIDI take is armed at once with punch-in 0:
`MidiRecorder::captureBlock` is a no-op while the transport is stopped, so nothing records until Play, a relocation before
Play cannot drop notes, and the playing -> stopped edge commits it as usual. Audio takes are deferred, because their captures would
record the stopped input: the indicator lights, a status message says to press Play, and the 10 Hz poll re-enters
`handleRecordToggle(true)` once the transport rolls, so the take begins up to one poll (about 100 ms) after Play. The count-in
only applies when Record itself starts the transport. Pressing Record again cancels either kind with nothing recorded. The record
button's tooltip follows the preference ("... implies Play" / "... the take starts when you press Play"). Already playing,
the preference changes nothing.

`MainComponent`'s implementation, installed in `initialiseCommon()`:

- **ON does NOT require an armed track.** It iterates `timelineDoc.getTracks()` for the first armed
  MIDI track that records every input (`Track::recordsEveryMidiInput`) — first-armed-wins among those —
  and adds every armed MIDI track with [an input of its own](#recording-from-one-input)
  (`collectMidiTakeTargets`). **Every armed audio track** records too, each from
  [its own input](#recording-audio-from-each-tracks-input). Either way the transport rolls: **record implies
  roll** (a DAW convention — the record button starts the transport if it is not already playing) and
  `setRecordingState(true)` fire unconditionally. MIDI targets call
  `midiRecorder.startRecording(targets, currentPpq)`; the audio takes are resolved, and their files chosen,
  *before* the transport moves (`resolveAudioTakes`) — a request that cannot be honoured (no Audio Output to
  tap for Whole mix, or a take file that cannot be created or opened) must not leave the transport rolling —
  and then start their captures (`startAudioTakes`). Record-on also tells the engine at once
  (`AudioEngine::setRecordingEngaged`) so a track [monitoring](tracks.md#monitoring) on Auto keeps hearing
  its input when the transport starts.

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
  [`architecture/app-wiring.md`](../architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step)'s `MidiRecorder` wiring entry
  (hook 5) for the full ordering. With nothing armed there was never a take to commit, so OFF just
  turns the indicator back off.

`AudioEngine::setMidiCaptureSink(&midiRecorder)` is the other half of the app-level wiring, feeding
`MidiRecorder::captureBlock` from `AudioEngine::renderNextBlock`'s one collector-merged buffer, plus
the per-device copy below. See `Tests/Timeline/MidiRecorderTests.cpp` for the model-level coverage and
`Tests/UI/Timeline/TimelineTransportBarTests.cpp` for the button-to-commit path.

### Recording from one input

Each MIDI track has an input ([the Input row](tracks.md#the-input-row)): a device (or all MIDI inputs)
and a channel (or any). The default, all inputs and any channel, is exactly the old behaviour, so
an existing project records as it always did.

- **Who records.** A take has up to `MidiRecorder::kMaxTargets` (16) targets. The first-armed track
  that records every input (when the first-armed-wins search above picked a MIDI one) is one; every
  armed MIDI track with its own device or channel is another, in document order. So two players on
  two controllers, each armed into their own track, record at once. An audio take can run beside
  them, and the 10 Hz poll and a device change commit both kinds.
- **What each target hears.** A target on all inputs reads the collector-merged buffer, filtered by
  channel when it has one. A target on one device reads that device's notes only, filtered by channel
  when it has one. Every target that matches gets its own copy of a note.
- **Where the device comes from.** The merged buffer has lost which input a message came from, so
  the engine keeps a second, per-device copy of note on/off: `MidiInputSources`
  (`Source/AudioEngine/MidiInputSources.h`). One lock-free `AbstractFifo` lane per device identifier,
  handed out on the message thread the first time the identifier is seen (when the input opens, or
  at record start) and never recycled, so each lane has one producer: that device's driver thread.
  `handleIncomingMidiMessageFromSource` pushes into it right beside the collector, after the bounce
  and MIDI Remote early-outs, so a note is in a lane exactly when it is in the merged stream. The
  device callback drains every lane, recording or not, into one block of callback-relative sample
  positions, placed by the message's timestamp the way `juce::MidiMessageCollector` places it (a gap
  longer than a block is squeezed into the block; a note older than a second is a backlog from a
  paused device and is dropped). Every render pass of that callback hands the recorder the slice of
  the block it renders, so a sliced callback records each note once, at its own beat;
  `renderNextBlock` then empties the block, so a hosted or bounce render never sees it. Nothing on
  the MIDI or audio thread locks, allocates or logs.
- **A device that is not connected.** Its lane is reserved at record start, so it records nothing
  until it comes back, and then records at once (same identifier). It never falls back to every
  input. In the plugin there is no hardware, so a device target records nothing; a channel-only
  input still works there.
- **Commit.** `stopAndCommit` pairs notes per target and adds one "Take" clip per target that
  captured anything, all inside one `recordTimelineChange`: one Record press is one undo step for
  the MIDI takes (an audio take beside them is its own step, as before).

Tests: `MidiRecorderInputFilterTests.cpp` (filters, the lanes, and a device-thread / audio-thread /
message-thread run meant for `-fsanitize=thread`), `TrackMidiInputRecordTests.cpp` (end to end
through `MainComponent` and the device callback, sliced and not), `TimelineDocTrackMidiInputTests.cpp`
and `TimelineRoutingPaneInputTests.cpp`.

### Recording audio from each track's input

Each audio track has an input ([the Input row](tracks.md#audio-tracks)): No input (the default), one device input
or a stereo pair, or Whole mix. One Record press records every armed audio track at once
(`MainComponentAudioTakes.cpp`), one `AudioTake` each in `MainComponent::audioTakes_`:

- **A device input** records exactly that input, straight from the device (or the host), beside the graph: the
  engine's `synth::InputTakeRecorder` (up to 16 slots at once) is fed the device-input snapshot every render pass,
  anchored at the transport sample like the Rec Tap. What you hear while playing went through the graph, so an
  input take is placed with the same round-trip latency (`getRecordingLatencySamples`: input + graph + output).
- **Whole mix** records the master through the `RecordTapModule` spliced before Audio Output, as every audio take
  used to. The tap is one take, so only the first armed Whole-mix track records it; a second says so.
- **Nothing to record from.** No input, or an input that is not connected (picked on another device, or turned
  off), records nothing for that track, and the status bar says why ("Vox has no input - pick one in the side
  pane", "Vox: its input is not connected"). The other tracks still record.
- **Take files** never collide: `chooseTakeFiles` skips numbers already given to another take of the same press.
  If any take cannot open its file, the whole press is refused and every file of it deleted.
- **Commit.** `commitAudioRecording` stops every take, places each one through the rate, tempo and latency frozen
  at record-on, and adds every clip in ONE `recordTimelineChange`: one undo step for the press's audio. The live
  recording strip is drawn per take, from its own peaks (the tap's or its recorder slot's).

Wait-for-Play, the count-in and a device change mid-take behave as for a single take. Tests:
`InputTakeRecorderTests.cpp`, `TakeCaptureWriterTests.cpp`, `MainComponentAudioTakesTests.cpp`,
`TimelineDocTrackAudioInputTests.cpp`.

## Return to Start button

The leftmost glyph button (a bar with a left-pointing triangle). A click runs the app's own
`transportReturnToStart` command through `TimelineTransportBar::onReturnToStart` (wired in
`MainComponent::wireTimelineRecordToggle`), so the button and the shortcut or MIDI action share one
locate-and-track-nudge path; with no hook wired it just calls `locateBeat(0)`. Like the command it
relocates only and never stops playback. It is a `GlyphButton` like the other four, so it is
keyboard-focusable, draws the accent focus ring (`paintFocusRegionOutline`) and carries a title and description
for screen readers (every glyph button has a title now) plus the tooltip "Return to Start". It is not a MIDI Learn
target — the command has no assignment surface on the bar, and `collectPickCandidates` still lists the four
learnable buttons. No shortcut is bound by default, so the tooltip names none.
Tests: `TimelineTransportBarReturnToStartTests.cpp` (bar) and
`ShortcutManagerTransportActionsTests.cpp::ReturnToStartButtonLocatesToBeatZero` (through `MainComponent`).

## Transport actions

Record has two entry points into the same gate. `AppCommands::transportRecord`
(`MainComponentCommandTable.cpp`) is a command-dispatched action a controller hardware button, or
a Settings-rebound key, can invoke via `ApplicationCommandManager::invokeDirectly`. It reaches
`handleRecordToggle` by calling `getRecordButton().triggerClick()` — the SAME button the mouse
clicks, so both entry points hit the identical armed-track gate above; there is no second, looser
path.

`transportPlay` / `transportStop` / `transportReturnToStart` call `TransportService` directly:
play and stop are guarded on the current snapshot so each is idempotent, and return-to-start is
`locateBeat(0)` with no implicit stop. The cursor moves (`transportNudge*Beat` / `*Bar`), loop
jumps (`transportJumpToLoopStart` / `transportJumpToLoopEnd`) and marker jumps
(`transportJumpToNextMarker` / `transportJumpToPreviousMarker`) also relocate through
`locateBeat` without changing play state; see `Source/Transport/TransportNudge.h` for how nudges
fired inside one audio block accumulate, and `Source/Transport/MarkerJump.h` (same accumulate
state, searching `TimelineDoc::getMarkers()` for the nearest marker ahead of / behind the current
position — never wrapping past the first or last). `transportToggleLoop` and `transportToggleMetronome` reuse
the loop and metronome buttons' own `triggerClick()` the same way `transportRecord` reuses
Record's — see [`shortcuts.md`](../control/shortcuts.md#transport-family) for the full action-id table. All
of them ship unbound by default; only `togglePlayback` keeps a default key (Space).

## Gliding the cursor

With the timeline focused, **holding Cmd+Left / Cmd+Right** glides the cursor (the transport position, the
thing the cursor nudge actions move) back or forward. Two rebindable Timeline actions, `timelineGlideBack`
and `timelineGlideForward`, resolved by `TimelinePanelComponent::keyPressed` like the panel's other keys.

- **Motion.** With snap on, a hold starts at four grid steps per second, so a coarse grid (1/1) sets off
  quickly and a fine one (1/32) gently (1/16 starts at one beat per second; snap off also
  starts at one beat per second). It then eases in (cubic) to a cap of eight bars per second, reached two
  seconds after the glide starts; a grid so coarse that its start speed passes the cap simply starts at the cap.
  The grid is read when the key goes down. The position is the exact integral of that speed
  (`synth::CursorGlide`, `Source/Transport/CursorGlide.h`, clock-free), so it does not depend on the frame
  rate.
- **Tap versus hold.** Nothing moves for the first 150 ms. A key released inside that window is a tap: one grid
  step (the next grid line in the direction pressed) with snap on, one beat with snap off. A tap settles to
  its target.
- **Release.** Acceleration resets on key-up. With snap on the cursor settles to the nearest grid line over
  140 ms (`easeOutCubic`); snap applies to this final landing only, never to the motion before it. With snap
  off it stops where it is.
- **Key repeat** from the OS is swallowed: only the first key-down starts the ramp.
- **Key up** is seen through `keyStateChanged` and `modifierKeysChanged` on the panel, and every frame also
  checks whether the key and its modifiers are still down, so a release that never reaches the panel (focus
  moved mid-hold) still ends the glide. Releasing Cmd first ends it too. On macOS an arrow key's state comes
  from the window server (`physicalArrowKeyDown`, `Source/UI/Timeline/CursorGlide/PhysicalKeyState.cpp`), not
  `KeyPress::isKeyCurrentlyDown`: Cocoa drops the key-up of a Command chord, so JUCE fakes one straight after
  the key-down and would end every Cmd+arrow hold as a tap. Other keys, and every key off the Mac, use JUCE's
  state.
- **Frames.** `TimelineCursorGlide` (`Source/UI/Timeline/CursorGlide/`) runs a `ReorderFramePump` (the shared
  VBlank `AnimationDriver`) while a key is held, and another for the settle. No free-running timer; nothing
  runs when idle. The settle lands at once when the panel is not on screen.
- **Moving the position** goes through `locateTransportTracked` on the same `TransportNudgeState` the nudge
  actions use (`MainComponent` hands the panel its own via `setTransportNudgeState`), so a nudge fired during
  or just after a glide builds on the glide's target. While playing, the glide scrubs: each frame relocates
  the transport, and playback continues from wherever the key is released.
- **In view.** Every move calls the follow-playhead page flip (`pageViewToShowBeat`), regardless of the Follow
  Playhead toggle: that toggle governs playback, and a cursor you are steering must stay visible. It is skipped
  while the piano roll is open (the roll has its own view).

There is no button, so there is no hint bubble; the actions appear in Settings > Shortcuts under Timeline.

## Right-click MIDI Learn (FRO133)

Each of the four glyph buttons (`TimelineTransportBar::GlyphButton`) carries its own MIDI Learn
menu on right-click, the same "MIDI Learn '<name>'..." / mapped block shown on every other learnable
surface ([`midi-remote-ui.md`](../control/midi-remote-ui.md#right-click-midi-learn--coverage)), but
the target is an **action** id, not a graph parameter — the bar stays graph-free, exactly as the
rest of this doc describes: `TimelineTransportBarMidiLearn.cpp`'s `actionIdForGlyph()` maps
Play/Stop → `"transportTogglePlayStop"`, Record → `"transportRecord"`, Loop → `"transportToggleLoop"`,
Metronome → `"transportToggleMetronome"` — the exact same four ids this section documents above,
so a mapping learned here and a keyboard shortcut rebind in Settings can never disagree about which
command a button drives.

`GlyphButton` is right-click-safe (`synth::ui::midilearn::RightClickSafeButton`,
[`midi-remote-ui.md`](../control/midi-remote-ui.md)): a plain `juce::Button` fires `onClick` on any
mouse button, so without the guard a right-click meant to open the menu would also toggle
playback/record/loop/metronome on its way there.

`TimelineTransportBar::onMidiLearnRequested`/`onMidiForgetRequested`/`onQueryMidiMappingsForActions`
(actionId-keyed, not the `(nodeId, paramId)` shape `GraphEditor`'s module-card callbacks use) are
wired in `MainComponent::wireMidiRemoteEngine()` to `MidiLearnController::armAction()`/
`forgetAction()`/`queryActionMappings()`. An action assignment is a **global** setting stored on the
learned controller's `ControllerProfile`, not the project's `"midiRemote"` doc — see
[`midi-remote.md`](../control/midi-remote.md#undo): "the profile stays" applies here the same way it
does to an auto-created profile from a parameter Learn. The badge/armed-outline overlay rides the
bar's own existing 10 Hz poll (`updateFromTransport()`) and paints in a dedicated
`paintOverChildren()` (the four buttons are children, so it must paint AFTER them, unlike the
readout text in `paint()`).

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
[`architecture/audio-engine.md`](../architecture/audio-engine.md#metronome--count-in) for the full
count-in choreography — locate-back, forced-on click, the punch-in filter — this selector feeds.

Layout: `metronomeButton_` (a square button, like its siblings) + `kGap` + `countInCombo_` (64 px)
+ `kGap * 2`, inserted between the loop button and the BPM label. The bar's fixed-width strip grows
by roughly 98 px, well inside the timeline panel's normal width.
