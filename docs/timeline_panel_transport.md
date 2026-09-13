# Timeline Panel: Transport

The playhead, the transport bar, the metronome and count-in, and the edit-tool strip (§4-7
below) for the timeline panel. The panel shell, ruler/grid/zoom/scroll/snap and loop-brace
behaviour live in [`docs/timeline_panel_core.md`](timeline_panel_core.md) (§1-2); track headers,
binding chips and Add-Track live in
[`docs/timeline_panel_tracks.md`](timeline_panel_tracks.md) (§3); clip lanes, the piano roll, the
automation strip, and keyboard/focus arbitration live in
[`docs/timeline_panel_clips_automation.md`](timeline_panel_clips_automation.md) and
[`docs/timeline_panel_piano_roll.md`](timeline_panel_piano_roll.md).

---

## 4. The Playhead

`Source/UI/TimelinePlayheadOverlay.h/.cpp` (`synth::ui::TimelinePlayheadOverlay`) — a transparent,
non-intercepting (`setInterceptsMouseClicks(false, false)`) overlay the panel adds **last** (so it is
topmost) and sizes to `getLanesBounds()`, i.e. the whole ruler + lanes region. Its local `x == 0` is
`lanesBounds_.getX()`, which is also the ruler's origin and therefore exactly `TimelineViewState`'s —
no offset arithmetic anywhere in the overlay. It draws a `kLineWidth = 2 px` vertical line in
`theme.colors.accent` (literal cyan fallback with no themed LnF), full height.

**This is the second of the two exceptions to the no-unconditional-per-tick-repaint rule.** Its
confinement contract, the paint-count test pattern it introduces, and why a third exception is not
free are all in `docs/layout_visuals_animation.md` §3 — read that before touching this component.

**Two timers, one of them borrowed:**

| Rate | Owner | What it does |
|---|---|---|
| 10 Hz | `MainComponent::timerCallback` (**existing** timer, only while `timelinePanel.isVisible()`) | `TimelinePanelComponent::updateFromTransport(snapshot, outputLatencySeconds)` |
| 30 Hz | `TimelinePlayheadOverlay`'s own `juce::Timer` | re-reads the transport and requests the movement strip — **only while playing** |

The low-rate poll is the **sole** owner of the 30 Hz timer's lifecycle: it sees the play/stop
transition and calls `startTimerHz`/`stopTimer`. The 30 Hz tick deliberately does *not* stop itself
when it notices a stopped transport — one owner is easier to reason about, and a tick after playback
stopped simply finds an unchanged x and requests nothing. Worst case the timer runs for one extra
poll interval, repainting nothing.

`TimelinePanelComponent::updateFromTransport` has a second job: the ruler paints the time signature
and the loop brace, and **nothing else repaints it** when those change from outside its own mouse
gestures (a bundle load, a host tempo map, the transport bar's controls, §5). So the poll diffs a small
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

**Follow playhead.** A toggle button sits immediately next to the snap toggle in the panel's
snap/tool strip (`followPlayheadButton_`, tinted via `Icon::FollowPlayhead` — see [`theming.md`
§3](theming.md#3-icon-tinting)). `kFollowPlayheadButtonWidth` is 30 px (up from 26, from the
timeline-panel button-size sweep); the snap toggle's `kSnapToggleButtonWidth` is **46 px**, wide
enough for the word `"Snap"`. The button is labelled with the VERB, not with its key: the key moved
from Q to J, and a button that spells its own letter goes stale the moment a user rebinds it — the
live key is in the tooltip, resolved through `synth::shortcutHintFor`. Backed by the boolean
preference `"timelineFollowPlayhead"`
(loaded/persisted the same way the snap-enabled flag is). Turning it on page-flips the panel's
view horizontally so the playhead never scrolls off screen while playing. The check rides the
SAME 10 Hz poll every other transport-driven repaint in `updateFromTransport` already uses — no
new timer — and is gated on **all four** of: the transport actually playing, the preference on,
the piano roll **closed** (the roll's own follow wiring, below, takes over while it's open), and
no clip drag in progress (`!clipLaneArea_.isDragInProgress()` — a follow flip landing mid-drag
would fight the gesture the user is mid-way through). Any one of the four false costs zero work.
When the (latency-compensated) drawn playhead beat moves outside `[firstVisibleBeat,
firstVisibleBeat + visibleBeats)`, the view re-centres so the beat lands **10% into the new page**
rather than flush against its edge — landing exactly on the edge would immediately re-trigger the
same flip on the very next poll once the beat advances one more sample.

**Roll-local variant.** `PianoRollComponent::setFollowPlayhead(bool)` mirrors the same feature
inside the roll's own horizontal view (`rollView_`), independent of the panel's — the roll has its
own zoom/scroll (see `docs/timeline_panel_piano_roll.md` §2) and therefore its own follow decision. It runs from inside
`setPlayheadBeat()` itself (the same seam the roll's local-playhead delegation, above, already
pushes a drawn beat through), gated on `followPlayhead_` on, no drag in flight
(`dragMode_ == DragMode::None`), and the edge-auto-scroll timer idle (the piano roll's own auto-scroll, `docs/timeline_panel_piano_roll.md` §2, is
already steering `rollView_` on its own terms — a follow flip on top of it would fight that
gesture the same way it would fight a clip drag in the panel). It runs BEFORE the strip-diff repaint
calculation below it in the same function, so that diff is computed against the mapping the line
will actually draw at, and it costs nothing while the beat is already inside the view — the common,
playing-and-visible case never calls `setHorizontalView` at all, so the zero-repaint-while-unmoved
contract above is untouched. `TimelinePanelComponent::setApplicationProperties()` pushes the shared
preference into the roll via `pianoRoll_.setFollowPlayhead(followPlayhead_)` — one preference,
two independent view states.

## 5. The Transport Bar

`Source/UI/TimelineTransportBar.h/.cpp` (`synth::ui::TimelineTransportBar`) — play/stop, record,
loop, BPM and time-signature editors, and the bar:beat readout, left-aligned in the transport-bar
strip (the snap combo stays docked right, §2 above). Buttons are **square** — `min(26 px, the
strip height after padding)`, centred in their slot — with `kGap = 7px` between them and `kGap * 2`
between groups; the two editable labels and the readout follow, in that order. `kButtonSize` (26,
up from 22) and `Metrics::timelineTransportBarHeight` (34, up from 28) were grown together in the
timeline-panel button-size sweep, so the glyphs actually render larger instead of being clamped
back down by `min(kButtonSize, bounds.getHeight())`.

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
  The loop range gained a second consumer with the Export Audio dialog (see
  [`docs/architecture.md`](architecture.md)'s bounce/export section): `MainComponent::promptExportAudio`
  reads `loopStartPpq`/`loopEndPpq` off a fresh snapshot to decide whether
  "Current loop range" is offered as a bounce range whenever the region is non-degenerate, and seeded
   from it when it is selected. As of P8-17 the offer no longer depends on the loop being ARMED — a
   disengaged loop still names a real span, so the option is available whether or not looping is live
   (TransportService always carries a valid `[start, end)`, default `[0, 4)`, so there is no separate
   "locators unset" state to detect); a bounce unloops for the duration and hands the region back.
   Read-only —
  the dialog never calls `setLoop` itself, it only offers what is already there.
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

> **An editable `juce::Label`'s editor does NOT inherit the app's `TextEditor` colours.** Both fields
> above typed **white on white** on every light theme until this was fixed, and the mechanism is
> worth knowing before adding a third one. `Label::createEditorComponent` copies
> `Label::textWhenEditingColourId` / `backgroundWhenEditingColourId` / `outlineWhenEditingColourId`
> onto the new editor's `TextEditor::textColourId` / `backgroundColourId` /
> `focusedOutlineColourId` — but only for ids that `isColourSpecified()`, and `LookAndFeel_V4`
> **does** specify `textWhenEditingColourId` from its own default-scheme white. So
> `AppLookAndFeel`'s themed `TextEditor::textColourId` was set correctly and then clobbered by V4's
> white on the way in. `AppLookAndFeel::applyTheme` now sets all three Label editing ids (plus
> `CaretComponent::caretColourId`, which V4 leaves black and therefore invisible on a dark theme),
> which fixes every editable label at once — these two and the track-name label in §3. A raw
> `juce::TextEditor` (the clip-rename and marker-rename editors) was never affected: it reads
> `TextEditor::textColourId` straight off the LookAndFeel. Pinned by
> `TimelineTransportBarTest.InlineFieldEditorsTakeTheirColoursFromTheTheme`, which asserts the
> TOKENS in a light and a dark theme so the fix cannot regress into a second hardcoded colour.
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
like the playhead's (`docs/layout_visuals_animation.md` §3): the readout has no timer of its own and moves only when its owner polls
it, so there is nothing to bound beyond "don't repaint an unchanged tick".
`getReadoutRepaintCountForTest()` is the test seam, the same counting idiom
`TimelinePanelComponent::getTransportUpdateCountForTest()` uses.

**Recording is the one control the bar is not authoritative over.** Whether a take actually
captures anything, and onto which track, is something only `MainComponent` can see (it owns the
`TimelineDoc`). The record button's click computes `!getToggleState()` and reports that as *intent*
through `std::function<void(bool)> onRecordToggled` — it never flips its own toggle state.
`setRecordingState(bool)` is the ONE thing that ever does, called back by the owner with the real
outcome, and it is set the same way **regardless of whether anything is armed** — the indicator
reflects record-ON, not "a take is capturing". `MainComponent`'s implementation (installed in
`initialiseCommon()`):

- **ON does NOT require an armed track.** It iterates `timelineDoc.getTracks()` for the first
  `armed && (kind == TrackKind::Midi || kind == TrackKind::Audio)` track (first-armed-wins — there
  is deliberately no "record both at once"), but either way the transport rolls: **record implies
  roll** (a DAW convention — the record button starts the transport if it isn't already playing) and
  `setRecordingState(true)` fire unconditionally. An armed MIDI track additionally calls
  `midiRecorder.startRecording(track, currentPpq)`; an armed Audio track resolves a
  `RecordTapModule` tap and take files *before* the transport moves (a request that cannot be
  honoured — no Audio Output in the patch, or the take file can't be created/opened — must not
  leave the transport rolling) and then starts its capture. **With nothing armed**, the transport
  still rolls and the indicator still lights — identical to Play plus a lit record indicator — no
  take of either kind starts, and `statusBar.showMessage("Recording started - no track is armed")`
  explains the silence. Arming a track *mid-roll* does not retroactively start a take either:
  `TimelineDoc::setTrackArmed` has no listener watching for this, so the user has to stop and press
  Record again once something is armed.
- **OFF** (button click, or the 10 Hz poll noticing `playing -> stopped` while
  `midiRecorder.isRecording()` — the user hit Space/Stop instead of the record button) — both routes
  go through one `MainComponent::commitMidiRecording()`: `midiRecorder.stopAndCommit(doc, undo)`,
  `midiRecorder.hadOverrun()` -> `statusBar.showMessage("Dropped MIDI events during recording")`,
  then `setRecordingState(false)`. One choke point means the explicit and the auto-commit paths can
  never diverge — see `docs/architecture.md`'s MidiRecorder wiring entry (hook 5) for the full
  ordering. With nothing armed there was never a take to commit, so OFF just turns the indicator
  back off.

`AudioEngine::setMidiCaptureSink(&midiRecorder)` is the other half of the app-level wiring (feeds
`MidiRecorder::captureBlock` from `AudioEngine::renderNextBlock`'s one collector-merged buffer) —
see `Tests/MidiRecorderTests.cpp` for the model-level coverage and
`Tests/TimelineTransportBarTests.cpp` for the button-to-commit path.

## 6. Metronome + Count-In

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

## 7. Edit-Tool Strip

`Source/UI/EditTool.h` declares the Cubase-style tool row shared by the clip lanes and the
piano roll (`docs/timeline_panel_clips_automation.md` §§1–2): `enum class EditTool { Select, Split, Glue, Erase, Mute, Draw }`, plus
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
the digit is already reserved for exactly that tool.

**All six tool digits are rebindable**, unlike the reserved 2/6/9 gaps above, which aren't bindings
at all. Each digit is a `ShortcutManager` action (`timelineToolSelect`, `timelineToolSplit`, …,
Timeline category) resolved directly by `TimelinePanelComponent::keyPressed()` — a *surface* action,
never dispatched through `ApplicationCommandManager` — so with a manager installed, an unbound tool
digit has no key at all, and a rebind takes effect immediately with no risk of colliding with the
Ctrl+Shift+digit grid-set commands (§2 above): the two live in the same category, but modifier
equality is exact, so a bare digit can never match a Ctrl+Shift one. Only a build with NO manager
installed (headless tests, an embedding with no settings store) falls back to the hardcoded digits
above via `editToolForKeyChar`. See [`shortcuts.md`](shortcuts.md#command-vs-surface-actions) for
the full command-vs-surface split and the tripwire test that guards it.

**The strip itself** is six `juce::DrawableButton`s (`ImageOnButtonBackground`), built unconditionally
in `TimelinePanelComponent`'s constructor (a headless build simply has no icon to draw in them —
`getToolButton(tool)` is never null once the panel exists), one shared radio group id so clicking one
un-toggles the rest, each with a tooltip that carries the digit (`"Split (3)"`, etc. —
`editToolName(tool) + " (" + editToolKeyDigit(tool) + ")"`). `kEditToolButtonWidth` is 28 px (up
from 24, from the timeline-panel button-size sweep). Laid out left-to-right in `kAllEditTools`
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

