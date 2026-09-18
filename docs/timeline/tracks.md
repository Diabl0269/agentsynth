# Timeline Track Headers

`Source/UI/Timeline/TimelineTrackHeaderComponent.h/.cpp`
(`synth::ui::TimelineTrackHeaderComponent`) — one row per `synth::Track`, living in the timeline
panel's track-header column. The `"+ Track"` menu and the flows it starts are
[add-track](add-track.md); the clip rows the headers line up with are [clips](clips.md).

The row talks to the app exclusively through `synth::ui::TrackHeaderHost` (implemented by
`MainComponent`), so it is fully testable against a stub with no graph.

## The track-header column

Rows are `Metrics::timelineTrackRowHeight` (56 px) tall — shared with the clip-lane area, so header
rows and clip rows always line up. The column is a fixed `"+ Track"` strip (22 px) at the top plus
a `juce::Viewport` below it, so a project with more tracks than fit **scrolls**; rows are never
compressed. Both live inside `getTrackHeaderBounds()`, so the panel's three regions still tile
exactly.

**The column divider.** Because the regions tile, the header column's right edge and the lanes'
left edge are the same pixel, and with nothing drawn on it the track list runs straight into
whatever sits to its right. With the piano roll open that neighbour is the roll's own right-hand
utility sidebar, so the track list and that sidebar read as one undifferentiated block.
`TimelinePanelComponent::paint()` draws a 1 px `Colors::border` line on the seam, from the header
column's top to the panel's bottom.

Two decisions in that one line. It is drawn by the **panel**, not by either neighbour: the seam is
a property of the panel's layout, so every future right-side sidebar inherits the divider instead
of having to remember to draw its own edge, and two neighbours both drawing one would double it.
And it starts at `trackHeaderBounds_.getY()`, i.e. **below the transport strip**, because that
strip is one continuous row of chrome across the full width — cutting it in half would imply a
column boundary its own controls do not respect.

**Toggle sizing.** The `M`/`S`/`R`/`A` toggles are `kToggleWidth` (24 px) with an explicit
`kToggleGap` (4 px) between adjacent buttons; laid out edge-to-edge with no gap they read as one
fused block. `Metrics::timelineTrackHeaderWidth` is 190 px so the wider, gapped toggle group does
not crush the name label down to single-digit pixel widths when a track's `A` button is visible
(4 toggles showing rather than 3).

## The document is the truth

A header stores no state of its own: it re-reads name, colour, mute/solo/arm and binding from the
doc in `refreshFromDoc()`, and every edit is written back through the doc.

Headers are rebuilt or refreshed **only** from `TimelineDoc::Listener::timelineChanged`
(`TimelinePanelComponent` is the listener) — no timer, no polling. A notification whose track *set*
is unchanged refreshes the existing rows in place; only an added, removed or reordered track
rebuilds them, so a mute click does not destroy the row the user is typing a name into.

## Row contents

Colour swatch (click opens a full colour picker), a track-kind badge, a name label (double-click to
edit), an `A` button (visible only when `Track::lanes` is non-empty), `M` / `S` / `R` toggles, and
the binding chip.

`R` flips `Track::armed` in the document and nothing else — **arming is not recording**. The record
button and `MidiRecorder::startRecording` live on the transport bar
([transport](transport.md#recording)), which looks for the first `armed` track when it starts a
take.

## Keyboard focus and M/S/R

A row is a real focus target — `setWantsKeyboardFocus(true)` in the constructor, the same pattern
`TimelineClipLaneArea` / `PianoRollComponent` use for the surfaces they own — with a `keyPressed()`
override that resolves bare **M**/**S**/**R** (`timelineMuteFocusedTrack` /
`timelineSoloFocusedTrack` / `timelineArmFocusedTrack`, rebindable, Timeline category — see
[`shortcuts.md`](../control/shortcuts.md#timeline)) into exactly the same `toggleMuted()` / `toggleSoloed()`
/ `toggleArmed()` → `performEdit()` path the M/S/R **buttons**' own `onClick` calls, so a keystroke
and a click can never disagree about what "toggle" means or about the undo step it produces.

Up/Down are NOT `ShortcutManager` actions (arrow-key row navigation is not rebindable anywhere else
in this app either); the row reports a direction via `onFocusMoveRequested` instead, since it owns
neither the sibling list nor the shared scroll state to act on it itself.

`TimelinePanelComponent::focusedTrackIndex_` is the model — an index into the doc's track order,
**deliberately not a field on `TimelineDoc`**: this is ephemeral UI state that must never touch
undo, reconcile or persistence. Two callbacks keep it in sync, both explicit rather than riding a
real `focusGained()` notification: `onSelectRequested` (a plain click, see `mouseDown()`) and
`onFocusMoveRequested` (Up/Down, resolved by `TimelinePanelComponent::moveFocusedTrack`, which
clamps at both ends rather than wrapping — the same rule `cycleSnapValue` uses for the grid — and
starts at row 0 either direction when nothing was focused yet).

**Why explicit callbacks rather than a focus-event round trip.** `grabKeyboardFocus()` is a
best-effort no-op without a native OS peer, which is every headless test in this codebase
(`FocusArbitrationTest::SurfaceResolverRealFocus` documents the same constraint), so the model has
to be told directly instead of waiting for an event that may never fire in that environment.

`moveFocusedTrack` still calls `grabKeyboardFocus()` on the destination row regardless — harmless
where it is a no-op, and what makes the NEXT real keystroke route there when a peer does exist —
and calls `TimelinePanelComponent::ensureTrackVisible()`, which scrolls the SAME
`viewState_.trackScrollY` (via `scrollTrackRows`, already clamped) every other vertical scroll or
zoom writer in this class treats as ground truth, never `trackHeaderViewport_.getViewArea()`, which
is only a cached snapshot of the last layout pass.

`syncTrackHeaders()`'s rebuild branch preserves `focusedTrackIndex_` **by `TrackId`**, not by
numeric index, across a track add, remove or reorder: a track deleted ABOVE the focused one must
not silently hand focus to whatever now sits at the old index, so the focused track's id is
resolved back to whatever new index it occupies, or cleared to `-1` if it was the one removed. The
`sameTracks` refresh-in-place branch (a rename, a mute click, a re-bind) never touches
`focusedTrackIndex_` at all, since the track SET did not change.

**Reaching a row by keyboard alone.** Cmd+Shift+T and Tab land on the Timeline region ROOT (the
panel), never on a row — so `TimelinePanelComponent::keyPressed()` treats a bare **Down** specially
when real focus is on the panel root itself (`getCurrentlyFocusedComponent() == this`, never a
looser "focus is somewhere in the panel" check): it seeds `focusedTrackIndex_` at row 0. Scoped
this tightly so it can never steal an arrow key the clip lane area or piano roll have not yet
claimed for themselves — every other keystroke that reaches this method still does so by bubbling
up from wherever real focus actually is.

## Click to select

`mouseDown()` on anything that is not a right-click calls `grabKeyboardFocus()` and fires
`onSelectRequested`. Double-click still renames; a plain click on the LABEL itself does not reach
the row's own `mouseDown()`, but a click anywhere else on the row does.

The four toggle buttons (`M`/`S`/`R`/`A`) opt OUT of taking focus for themselves
(`setWantsKeyboardFocus(false)` plus `setMouseClickGrabsKeyboardFocus(false)`) — `juce::Button`
opts IN by default, and without this a click on one of them would silently move real focus off the
row and onto the button, going stale the same way `TimelinePanelComponent`'s own tool-strip buttons
would without the identical treatment. `nameLabel_` is deliberately excluded: it needs its own
focus machinery for double-click rename. The row's `focusOfChildComponentChanged()` override
repaints for that case (and for the binding chip), since `hasKeyboardFocus(true)` — what the focus
outline checks — includes descendants.

## Drag to reorder

`TimelineDoc::moveTrack(id, newIndex)` moves a track within `tracks[]` — display and serialization
order only; the id, clips, lanes and binding travel with it. It is safe to call at any time:
nothing downstream keys a track by its position, only by id or uuid (`TimelineClipLaneArea`
re-derives order from `doc_->getTracks()` fresh at every layout, paint and hit-test call rather
than caching it, and the audio-thread snapshot — `TimelineSnapshot::TrackInfo` — matches a MIDI
source module to its track by `bindingUuid`, never by index). Clamped to `[0, tracks.size() - 1]`;
a no-op — no revision bump, no notification — when the id does not resolve or is already there.

The drag surface is deliberately the WHOLE row rather than a small dedicated handle, which is too
fiddly a target: everything except the interactive children (name label, swatch, `M`/`S`/`R`/`A`
buttons, binding chip; each intercepts its own `mouseDown`) starts a drag.

`TimelineTrackHeaderComponent` stays sibling-blind about it, exactly like `onFocusMoveRequested`
above. `mouseDrag()` only commits to a drag once the pointer has moved `kRowDragThreshold` (4 px)
past `mouseDown` — below that it is a plain click-to-select — and from then on hands raw **screen**
Y positions up through `onRowDragStarted` / `onRowDragged` / `onRowDragEnded`, the same "compare
against something that is not this component" idiom
`TimelinePanelComponent::ResizeHandle::desiredHeightFor` uses for its own drag.

`TimelinePanelComponent` is the one place that can turn a Y position into "between which two
tracks", since it owns the ordered header list (`trackHeaderList_.headers`):
`trackDropBoundaryForScreenY()` converts via `trackHeaderList_.getLocalPoint(nullptr, ...)` — a
null source component means "the point is already in screen coordinates" — and rounds to the
nearest row BOUNDARY (`0..headerCount`) so the live drop indicator reads as "insert here between
these two rows", not "replace this row". `endTrackDrag()` converts that boundary into `moveTrack`'s
target index (`dropBoundary > fromIndex ? dropBoundary - 1 : dropBoundary` — the track's own old
slot has already vacated the array below it, so only a boundary ABOVE the old index needs the `-1`
correction) and drives the mutation through `trackHeaderHost_->performTrackEdit()`, falling back to
calling `TimelineDoc::moveTrack` directly when no host is installed — the same convention
`TimelineTrackHeaderComponent::performEdit()` follows, so a panel driven straight against a doc
still works.

The drop indicator is a 2 px accent line drawn by `TrackHeaderList::paintOverChildren()`. **Over
children**, because the header rows are children painted AFTER this component and each fills its
own bounds, so a line drawn in `paint()` would be painted over at every interior boundary — the
same trap the focus outline below avoids the same way.
`DropIndicatorPaintsOverTheRowsNotUnderThem` pins it by rendering the header column mid-drag with a
real theme installed and asserting the accent line is actually visible.

**Ordering hazard — read before touching this code.** A reorder changes which `TrackId` sits at
each index, which makes `syncTrackHeaders()`'s "rebuild only when the SET of tracks changed" check
trip (same ids, different order at each slot) and rebuild the ENTIRE header column — destroying
every `TimelineTrackHeaderComponent`, *including the one whose `mouseUp()` is still on the call
stack* that triggered the mutation in the first place (`mouseUp` → `onRowDragEnded` →
`endTrackDrag` → `performTrackEdit` → `moveTrack` → `timelineChanged` → `syncTrackHeaders()`, all
synchronous). Both `TimelineTrackHeaderComponent::mouseUp()` and
`TimelinePanelComponent::endTrackDrag()` are written so every member write happens BEFORE the call
that can trigger this, and nothing follows it — see the `ORDERING HAZARD` comment on
`onRowDragEnded` in `TimelineTrackHeaderComponent.h` and the matching comment in `endTrackDrag()`.
`WholeRowDragReordersTracksAndSurvivesTheHeaderRebuildMidGesture` drives a real drag through this
exact path with no host — the worst case, where the mutation runs with no extra indirection —
specifically to pin it.

## Focus outline

`paintOverChildren()` calls `synth::ui::paintFocusRegionOutline` on itself — the exact same helper,
and the same treatment (translucent `accent`, theme border weight), every focus region ROOT uses,
reused verbatim one nesting level deeper: a track header row is a real focusable leaf, just not a
region root itself (the Timeline region's root stays the panel).

Painted over children rather than in `paint()` for the same reason `TimelinePanelComponent`'s own
region outline is: the colour swatch and the M/S/R/A toggles sit flush against the row's left and
right edges, so an outline drawn underneath them would be invisible along those edges.

## Kind badge

The `"MIDI"` / `"AUD"` / `"AUTO"` text is the fallback: when a themed `AppLookAndFeel` is installed
and the corresponding asset is linked in, `paint()` draws
`Icon::TrackMidi`/`TrackAudio`/`TrackAutomation` (`kindBadgeIcon(TrackKind)`) instead — the same
"draw the glyph when the library has it, fall back to text otherwise" contract every other icon
consumer in the app follows. It is identity chrome, not a control.

`getKindBadgeIconForTest()` mirrors `getKindBadgeTextForTest()`'s "value or empty" idiom (returning
`-1` for "fell back to text") so a test can assert on which path ran without decoding pixels. A
track's kind never changes after creation, so this is a one-time paint decision, not something
`refreshFromDoc()` has to re-derive.

## Colour

Colour resolves *only* through `synth::ui::resolveTrackColour`
(`Source/UI/Timeline/TrackColour.h`): the track's stored `colourArgb` when non-zero, otherwise a
deterministic 8-entry palette indexed by the track's position. A muted track comes back desaturated
and dimmed, same hue.

**Why the palette is fixed rather than theme-derived.** The add-track flow *writes* the resolved
colour into the document, so a theme-dependent value would mean a project opened under another
theme came back recoloured. Unlike `CableColour.h` there is no persisted override layer: the doc
already stores the choice.

**M/S/R active-state colours and the binding chip.** `applyThemeDerivedColours()` is the one place
every colour this component bakes via `setColour` — the binding chip's warning and normal fill, and
the `M`/`S`/`R` buttons' active-state colours (`theme.colors.trackMuteOn` / `trackSoloOn` /
`trackArmOn`) — gets re-derived from the currently installed `LookAndFeel`. It runs from three call
sites: the end of the constructor, so the very first paint is not relying on a later call;
`refreshFromDoc()`, so a doc-driven repaint always shows the right colours; and the component's own
`lookAndFeelChanged()` override. Without that last call site a theme switch alone, with no
accompanying doc change, leaves the chip and the M/S/R active colours frozen on whatever theme was
active the last time `refreshFromDoc()` ran.

## Colour swatch

Clicking it builds a `synth::ui::ColourPickerPopup` (`Source/UI/Chrome/ColourPickerPopup.h` — see
[`layout/colour-overrides.md`](../layout/colour-overrides.md#colour-picker-popup)) via
`buildColourPicker()` and launches it
in a `juce::CallOutBox` anchored on the swatch.

Its favourites shelf persists through `TrackHeaderHost::getAppProperties()` — a non-pure
`TrackHeaderHost` method defaulting to `nullptr` so every existing implementer keeps compiling;
`nullptr` degrades to an in-memory-only picker, same as a headless test gets. Preview writes the
doc directly with no undo step on every drag or favourite click; closing the popup either restores
the original colour with no undo step (no net change) or performs the real edit as ONE undo step
whose undo target is the original colour — see `docs/layout/colour-overrides.md` for the exact
preview/commit
contract. `createColourPickerForTest()` exposes `buildColourPicker()`'s exact wiring without ever
launching the `CallOutBox`.

## Binding chips

Three states, two of them amber (`theme.colors.warning`):

| Track state | Chip | Meaning |
|---|---|---|
| `bindingUuid` resolves | the node's plain display name (e.g. `"Track In"`) | plays through that node |
| `bindingUuid` empty | `"Unbound"` (amber) | never pointed anywhere; the track plays nowhere |
| `orphaned` | `"Missing"` (amber) | it WAS bound and the node is gone — retained, never auto-deleted |

An `Automation`-kind track shows **no chip at all** (`setVisible(false)`, decided in
`refreshFromDoc()`): that track hosts lanes, and a node binding is meaningless for it, so the
bottom half-row is simply empty. `Midi` and `Audio` tracks are unaffected.

The chip always carries a tooltip explaining what it shows and, when amber, how to fix it. A bound
name carries a `"#id"` suffix only in the re-bind menu, and only on an option whose display name
collides with another live candidate (`MainComponent::getAvailableTrackInNodes`) — a lone node's
name, on the chip or in the menu, always stays plain.

Clicking the chip does two things: it **selects** the bound node in the GraphEditor's
`SelectionModel` (a highlight only — no canvas scroll, no focus change) and opens a menu listing
every live node of **the type this track's kind can be fed by** — `Track In` for a MIDI track,
`Track Audio` for an Audio track — **not claimed by another track**, plus `"New Track In node"`,
which likewise creates whichever type the track's kind needs. Picking one calls
`TimelineDoc::setTrackBinding` as one undoable step, then reconciles.

**Why the menu is kind-aware.** Offering the wrong type would let a user bind a track to a node
that structurally cannot play it: both modules match on **kind as well as uuid**, so the result
would be a track that silently plays nothing.

## A binding is never re-established automatically

Least of all by name. An orphaned track stays orphaned until the user picks a node from the chip
menu.

**Why.** Two nodes can carry the same display name, and a silent re-bind would quietly play a track
through someone else's instrument. Degrade visibly, repair explicitly.

## MIDI destinations

The chip's context menu carries one more item, `kMidiDestinationsMenuId`, offered only when
`offersMidiDestinationsMenuEntryForTest()` says the track's binding resolves to something with MIDI
to send. Choosing it — or a test calling `applyBindingMenuChoice(kMidiDestinationsMenuId)` — opens
a `synth::ui::MidiDestinationPicker` (`Source/UI/Timeline/MidiDestinationPicker.h`): a searchable,
multi-select list of every live MIDI-instrument node the track's bound Track In node could send
MIDI to, in a `juce::CallOutBox` anchored on the chip, via `openMidiDestinationsPicker()` /
`buildMidiDestinationPicker()`. That mirrors the colour swatch's build/launch split, including
`createMidiDestinationPickerForTest()` and a `setOpenMidiDestinationsPickerHookForTest()` seam so
the menu choice is exercisable without a live callout.

The candidate list is DYNAMIC ground truth, not an allowlist: every live graph node whose
`ModuleBase`-level `acceptsMidi()` is true — the per-module flags reflect what each `processBlock`
actually consumes, see `Tests/Modules/ModuleMidiFlagsTests.cpp`'s expected table — which
automatically excludes MIDI *sources* (Track In, External MIDI, MIDI Keyboard: they generate notes,
they do not consume them). Rows render in two sections: **Instruments** first
(`isMidiInstrumentType()`, `Source/Modules/ModuleBase.h` — the same set the add-track auto-wire
target search uses) and **Other** for the remaining real MIDI consumers, e.g. an ADSR's note-gate
input. The section headers only appear when both groups are present.

`AIStateMapper` keeps its own, separately name-keyed `midiAcceptingTypes` list for the AI auto-wire
path, and that list omits `Wavetable` — so the two answers to "is this a MIDI-driven instrument"
differ. The destination picker and the add-track auto-wire search both go through
`isMidiInstrumentType()`; `AIStateMapper` does not.

**The graph is the truth.** Toggling a row calls `TrackHeaderHost::setMidiDestinationConnected(
TrackId, nodeUid, connect)` — `MainComponent`'s implementation performs one `recordStructuralChange`
(add or remove the MIDI connection) then always calls `reconcileTimelineAfterGraphChange()` — and
immediately re-pulls `getMidiDestinationOptions(TrackId)` to rebuild every row from what the graph
now actually reports, rather than trusting the click. Both host methods are non-pure with inert
defaults (empty list, no-op) for the same keep-every-implementer-compiling reason
`getAppProperties()` is, and both no-op cleanly on a stale popup — the track's binding or the
target node no longer resolves — rather than crashing.

## The channel chip

The CHANNEL chip (`[`docs/mixer/mixer.md`](../mixer/mixer.md)` §5.2) shares the bottom half-row, right of the binding chip,
whenever the track's notes or audio actually reach a `ChannelStripModule` — **linked or shared
alike**.

**Why there are two chips.** They answer different questions: the binding chip names the node
*upstream* of the track (what plays it), the channel chip names the mixer channel *downstream* of
it (where the sound ends up). This is how a MIDI track shows where its audio went without anyone
creating an extra audio track for it.

`ChannelChipComponent` (`Source/UI/Timeline/ChannelChipComponent.h`) draws the channel's name — its
macro's when the strip is boxed, else the one feeding track's name, else `"Channel"` — plus a
compact level meter. Clicking it **selects the channel and pans it into view** (the Locate Master
contract, not the binding chip's highlight-only one: a chip's whole point is finding something that
may be off-screen).

The header itself stays graph-free: everything it knows about the channel comes through
`synth::ui::TrackChannelLinkSurface`, the one seam `TrackHeaderHost::getChannelLinkSurface()` hands
back, and clicking routes to `revealChannelForTrack`. The real implementer is
`TrackChannelLinkController`, owned by `MainComponent`. Every "not linked" answer is false or null
and the header falls through to its existing behaviour unchanged, so the link can never half-apply.

**The meter has no timer of its own.** `TimelinePanelComponent` owns ONE 15 Hz `juce::Timer`
(started by `setTrackHeaderHost`) that ticks every header's `tickChannelMeter()`; each chip
repaints only when its drawn level crosses `ChannelChipComponent::kMeterRepaintThreshold`. With up
to `TimelineDoc::kMaxTracks` rows, a timer per chip would be 256 timers, and an ungated repaint
would breach the per-tick repaint rule (`docs/layout/rendering.md`) — this is the same
gated 15 Hz shape `ModuleComponent`'s own meter poll uses.

**A linked track's M/S show and drive its CHANNEL, not note gating** (`[`docs/mixer/mixer.md`](../mixer/mixer.md)` §5.2 (c)):
`refreshFromDoc()` reads the strip's mute and solo for a linked track and the doc's own flags for
every other one, and the toggles write through the link surface first, falling back to the
unchanged `doc.setTrackMuted` / `setTrackSoloed` path when the track is not linked. Because a strip
write is not a document change, nothing notifies the header afterwards — the toggle refreshes the
row itself, and `MainComponent::reconcileTimelineAfterGraphChange` refreshes every row after an
undo or redo restore.

## The automation button

The `A` button toggles this track's automation lane in the single, doc-wide automation strip
([automation](automation.md)). The header only reports the click via
`onAutomationToggleRequested` — it never tracks open/closed state itself, since it cannot see
whether another track's lane is the one currently shown.
`TimelinePanelComponent::toggleAutomationForTrack` decides: already open on one of this track's own
lanes closes the strip; anything else — closed, or open on a different track — opens the track's
first lane.

## Row context menu

**Right-click a header means anywhere on the row.** `TimelineTrackHeaderComponent::mouseDown()`
only ever sees a click that lands on the row's own background, because JUCE hands a click to
whichever component is directly under the cursor and never bubbles it to an ancestor on its own. A
right-click on `nameLabel_` or the M/S/R/A toggles — almost every pixel of the row — would
otherwise be swallowed silently instead of reaching this menu at all, while the binding chip and
the colour swatch would still show SOMETHING (their own menu or picker, opened for the wrong
reason, since `juce::Button` fires `onClick` from any mouse button).

Two small nested classes in `TimelineTrackHeaderComponent.h` close that: `ContextMenuForwardingLabel`
(wraps `nameLabel_`) and `ContextMenuForwardingButton` (wraps `muteButton_` / `soloButton_` /
`armButton_` / `automationButton_`). Each forwards a right-click straight to `showContextMenu()`
and, for the buttons, never lets the click reach `juce::TextButton`'s own `onClick`, so a
right-click on Mute does not also toggle mute. The binding chip and colour swatch are untouched —
they keep opening their own menu or picker.

The menu's items:

- **Delete track** — the same compound step the add-track flows produce, in reverse: the track and
  its bound `Track In` / `Track Audio` node go together, and come back together.
- **Make Channel** (above Delete Track) turns the track's bound chain into a mixer channel — see
  [`mixer.md`](../mixer.md) §5.8 for what moves and what stays shared. Enabled only while
  `TrackHeaderHost::canMakeChannelForTrack()` is true (the chain has no Channel Strip of its own
  yet), disabled — not hidden — afterwards, and `MainComponent` runs it as ONE graph + timeline +
  macro undo step followed by the reconcile pass.
- **"Save Track as Preset…"** and **"Set as Default Track Preset"** sit beside Make Channel, gated
  on the SAME `TrackHeaderHost::canSaveTrackPresetForTrack()` disabled-not-hidden rule: a track
  needs a channel before it has anything to save. The channel macro's own right-click menu offers
  the identical pair, gated instead on `synth::isChannelMacro` — there it is omitted entirely
  rather than disabled, matching that menu's existing "Mute Macro" omit-when-meaningless
  precedent. See [`mixer.md`](../mixer.md) §5.7/§5.8 for what a saved preset carries and how
  loading it is gated.

## Test seams

A `juce::PopupMenu` never runs in the test binary, so the menus expose their semantics without the
menu: `collectBindingOptions()` / `applyBindingMenuChoice(id)` and `applyContextMenuChoice(id)`.
`buildContextMenu()` plus `setShowContextMenuHookForTest()` capture the menu a real right-click
`mouseDown()` builds, and `handleChipClick(showMenu=false)` exercises the selection affordance on
its own.

Tests: `Tests/UI/Timeline/TimelineTrackHeaderTests.cpp`,
`Tests/UI/Timeline/TimelineTrackHeaderContextMenuTests.cpp` (the real-child-dispatch right-click
coverage), `Tests/UI/Timeline/TimelineTrackFocusTests.cpp`, and
`Tests/UI/Timeline/TimelinePanel/TimelinePanelTrackHeaderTests.cpp` for the panel-side column.
