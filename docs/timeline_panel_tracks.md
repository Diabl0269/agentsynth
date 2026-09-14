# Timeline Panel: Tracks

Track headers, binding chips, and the Add-Track flow (§3 below) for the timeline panel. The
panel shell, ruler/grid/zoom/scroll/snap and loop-brace behaviour live in
[`docs/timeline_panel_core.md`](timeline_panel_core.md) (§1-2); the playhead, transport bar,
metronome/count-in and edit-tool strip live in
[`docs/timeline_panel_transport.md`](timeline_panel_transport.md) (§4-7); clip lanes, the piano
roll, the automation strip, and keyboard/focus arbitration live in
[`docs/timeline_panel_clips_automation.md`](timeline_panel_clips_automation.md) and
[`docs/timeline_panel_piano_roll.md`](timeline_panel_piano_roll.md).

---

## 3. Track Headers, Binding Chips, Add-Track

`Source/UI/Timeline/TimelineTrackHeaderComponent.h/.cpp` (`synth::ui::TimelineTrackHeaderComponent`) — one
row per `synth::Track` (`Metrics::timelineTrackRowHeight`, 56 px — shared with the clip-lane area,
see `docs/timeline_panel_clips_automation.md` §1, so header rows and clip rows always line up), living in the panel's track-header
column. The column is a fixed
`"+ Track"` strip (22 px) at the top plus a `juce::Viewport` below it, so a project with more
tracks than fit **scrolls**; rows are never compressed. Both live inside `getTrackHeaderBounds()`,
so the panel's three regions still tile exactly.

**The column divider.** Because the regions tile, the header column's right edge and the lanes'
left edge are the same pixel — and with nothing drawn on it the track list ran straight into
whatever sat to its right. With the piano roll open that neighbour is the roll's own right-hand
utility sidebar (the scale panel), so the track list and that sidebar read as one undifferentiated
block, which is what the report was about. `TimelinePanelComponent::paint()` now draws a 1 px
`Colors::border` line on the seam, from the header column's top to the panel's bottom.

Two decisions in that one line. It is drawn by the **panel**, not by either neighbour: the seam is a
property of the panel's layout, so every future right-side sidebar inherits the divider instead of
having to remember to draw its own edge (and two neighbours both drawing one would double it). And
it starts at `trackHeaderBounds_.getY()`, i.e. **below the transport strip**, because that strip is
one continuous row of chrome across the full width — cutting it in half would imply a column
boundary its own controls do not respect.

**Toggle sizing.** The `M`/`S`/`R`/`A` toggles are `kToggleWidth` (24 px, up from 20) with an
explicit `kToggleGap` (4 px) between adjacent buttons — laid out edge-to-edge with no gap read as
one fused block, the worst offender in the timeline-panel button-size sweep.
`Metrics::timelineTrackHeaderWidth` grew alongside them (190 px, up from 160) so the wider,
gapped toggle group doesn't crush the name label down to single-digit pixel widths when a track's
`A` button is visible (4 toggles showing rather than 3).

**The document is the truth.** A header stores no state of its own: it re-reads name, colour,
mute/solo/arm and binding from the doc in `refreshFromDoc()`, and every edit is written back
through the doc. Headers are rebuilt/refreshed **only** from `TimelineDoc::Listener::timelineChanged`
(`TimelinePanelComponent` is the listener) — no timer, no polling. A notification whose track *set*
is unchanged refreshes the existing rows in place; only an added/removed/reordered track rebuilds
them, so a mute click doesn't destroy the row the user is typing a name into.

**Row contents:** colour swatch (click opens a full colour picker — see **Colour swatch** below), a
track-kind badge (`"MIDI"` / `"AUD"` / `"AUTO"`, fixed per `TrackKind` — identity chrome, not a
control), name label (double-click to edit), an `A` button (visible only when `Track::lanes` is
non-empty), `M` / `S` / `R` toggles, and the binding chip. `R` flips `Track::armed` in the document
and nothing else — arming is not recording; the record button and `MidiRecorder::startRecording`
live on the transport bar (§5, below), which looks for the first `armed` track when it starts a
take.

**Keyboard focus + M/S/R (T161).** A row is now a real focus target —
`setWantsKeyboardFocus(true)` in the constructor, the same pattern `TimelineClipLaneArea`/
`PianoRollComponent` already use for the surfaces they own — with a `keyPressed()` override that
resolves bare **M**/**S**/**R** (`timelineMuteFocusedTrack`/`timelineSoloFocusedTrack`/
`timelineArmFocusedTrack`, rebindable, Timeline category — see
[`shortcuts.md`](shortcuts.md#timeline)) into exactly the same `toggleMuted()`/`toggleSoloed()`/
`toggleArmed()` → `performEdit()` path the M/S/R **buttons**' own `onClick` calls, so a keystroke and
a click can never disagree about what "toggle" means or about the undo step it produces. Up/Down are
NOT `ShortcutManager` actions (arrow-key row navigation isn't rebindable anywhere else in this app
either); the row reports a direction via `onFocusMoveRequested` instead, since it owns neither the
sibling list nor the shared scroll state to act on it itself.

`TimelinePanelComponent::focusedTrackIndex_` is the model — an index into the doc's track order,
**deliberately not a field on `TimelineDoc`**: this is ephemeral UI state that must never touch
undo, reconcile or persistence. Two callbacks keep it in sync, both explicit rather than riding a
real `focusGained()` notification: `onSelectRequested` (a plain click — see `mouseDown()`) and
`onFocusMoveRequested` (Up/Down, resolved by `TimelinePanelComponent::moveFocusedTrack`, which clamps
at both ends rather than wrapping — the same rule `cycleSnapValue` uses for the grid — and starts at
row 0 either direction when nothing was focused yet). Explicit callbacks rather than a focus-event
round trip because `grabKeyboardFocus()` is a best-effort no-op without a native OS peer (every
headless test in this codebase, `FocusArbitrationTest::SurfaceResolverRealFocus` documents the same
constraint), so the model has to be told directly instead of waiting for an event that may never fire
in that environment. `moveFocusedTrack` still calls `grabKeyboardFocus()` on the destination row
regardless (harmless where it's a no-op, and what makes the NEXT real keystroke route there when a
peer does exist), and calls `TimelinePanelComponent::ensureTrackVisible()`, which scrolls the SAME
`viewState_.trackScrollY` (via `scrollTrackRows`, already clamped) every other vertical scroll/zoom
writer in this class treats as ground truth — never `trackHeaderViewport_.getViewArea()`, which is
only a cached snapshot of the last layout pass. `syncTrackHeaders()`'s rebuild branch preserves
`focusedTrackIndex_` **by `TrackId`**, not by numeric index, across a track add/remove/reorder — a
track deleted ABOVE the focused one must not silently hand focus to whatever now sits at the old
index; the focused track's id is resolved back to whatever new index it occupies, or cleared to `-1`
if it was the one removed. The `sameTracks` refresh-in-place branch (a rename, a mute click, a
re-bind) never touches `focusedTrackIndex_` at all, since the track SET didn't change.

**Click-to-select.** `mouseDown()` on anything that isn't a right-click now calls
`grabKeyboardFocus()` and fires `onSelectRequested` — the behaviour a comment on `nameLabel_`
reserved a plain click for before this landed (double-click still renames; a plain click on the LABEL
itself doesn't reach the row's own `mouseDown()`, but a click anywhere else on the row does). The
four toggle buttons (`M`/`S`/`R`/`A`) opt OUT of taking focus for themselves
(`setWantsKeyboardFocus(false)` + `setMouseClickGrabsKeyboardFocus(false)`) — `juce::Button` opts IN
by default, and without this a click on one of them would silently move real focus off the row and
onto the button, going stale the same way `TimelinePanelComponent`'s own tool-strip buttons would
without the identical fix. `nameLabel_` is deliberately excluded (it needs its own focus machinery
for double-click rename); the row's `focusOfChildComponentChanged()` override repaints for that case
(and for the binding chip), since `hasKeyboardFocus(true)` — what the outline below checks — includes
descendants.

**Whole-row drag-to-reorder (T166).** `TimelineDoc::moveTrack(id, newIndex)` moves a track within
`tracks[]` — display/serialization order only; the id, clips, lanes and binding travel with it.
It's safe to call at any time: nothing downstream keys a track by its position, only by id/uuid
(`TimelineClipLaneArea` re-derives order from `doc_->getTracks()` fresh at every layout/paint/hit-test
call rather than caching it, and the audio-thread snapshot — `TimelineSnapshot::TrackInfo` — matches
a MIDI source module to its track by `bindingUuid`, never by index). Clamped to `[0, tracks.size() -
1]`; a no-op (no revision bump, no notification) when the id doesn't resolve or is already there.

The drag surface is deliberately the WHOLE row, not a small dedicated handle (a handle was judged too
fiddly a target) — everything except the interactive children (name label, swatch, `M`/`S`/`R`/`A`
buttons, binding chip; each intercepts its own `mouseDown`) starts a drag. `TimelineTrackHeaderComponent`
stays sibling-blind about it, exactly like `onFocusMoveRequested` above: `mouseDrag()` only commits to a
drag once the pointer has moved `kRowDragThreshold` (4 px) past `mouseDown` — below that, it's a plain
click-to-select — and from then on hands raw **screen** Y positions up through `onRowDragStarted` /
`onRowDragged` / `onRowDragEnded`, the same "compare against something that isn't this component" idiom
`TimelinePanelComponent::ResizeHandle::desiredHeightFor` already uses for its own drag. `TimelinePanelComponent`
is the one place that can turn a Y position into "between which two tracks", since it owns the ordered
header list (`trackHeaderList_.headers`): `trackDropBoundaryForScreenY()` converts via
`trackHeaderList_.getLocalPoint(nullptr, ...)` (a null source component means "the point is already in
screen coordinates" — see the JUCE doc comment) and rounds to the nearest row BOUNDARY (0..headerCount)
so the live drop indicator (`TrackHeaderList::paintOverChildren()`, a 2px accent line — over children
because the header rows are children painted AFTER this component and each fills its own bounds, so a
line drawn in `paint()` would be painted over at every interior boundary; the same trap this file
documents twice already, see **Visual indicator** below and `TimelinePanelComponent::paintOverChildren`)
reads as "insert here between these two rows", not "replace this row". `endTrackDrag()` converts that
boundary into `moveTrack`'s target index (`dropBoundary > fromIndex ? dropBoundary - 1 : dropBoundary`
— the track's own old slot already vacated the array below it, so only a boundary ABOVE the old index
needs the `-1` correction) and drives the mutation through `trackHeaderHost_->performTrackEdit()`,
falling back to calling `TimelineDoc::moveTrack` directly when no host is installed — the same
convention `TimelineTrackHeaderComponent::performEdit()` already follows, so a panel driven straight
against a doc (every ungated panel-level test in `TimelinePanelTests.cpp`) still works.

**Ordering hazard — read before touching this code.** A reorder changes which `TrackId` sits at each
index, which makes `syncTrackHeaders()`'s "rebuild only when the SET of tracks changed" check trip
(same ids, different order at each slot) and rebuild the ENTIRE header column — destroying every
`TimelineTrackHeaderComponent`, *including the one whose `mouseUp()` is still on the call stack* that
triggered the mutation in the first place (`mouseUp` → `onRowDragEnded` → `endTrackDrag` →
`performTrackEdit` → `moveTrack` → `timelineChanged` → `syncTrackHeaders()`, all synchronous). Both
`TimelineTrackHeaderComponent::mouseUp()` and `TimelinePanelComponent::endTrackDrag()` are written so
every member write happens BEFORE the call that can trigger this, and nothing follows it — see the
`ORDERING HAZARD` comment on `onRowDragEnded` in `TimelineTrackHeaderComponent.h` and the matching
comment in `endTrackDrag()`. `TimelinePanelTests.cpp`'s
`WholeRowDragReordersTracksAndSurvivesTheHeaderRebuildMidGesture` drives a real drag through this exact
path (with no host, the worst case: the mutation runs with no extra indirection) specifically to pin it.
`DropIndicatorPaintsOverTheRowsNotUnderThem` separately pins the paint-order trap above by rendering the
header column mid-drag with a real theme installed and asserting the accent line is actually visible.

**Visual indicator.** `paintOverChildren()` calls `synth::ui::paintFocusRegionOutline` on itself —
the exact same helper (and treatment: translucent `accent`, theme border weight) every T159 focus
region ROOT already uses, reused verbatim one nesting level deeper: a track header row is a real
focusable leaf, just not a region root itself (the Timeline region's root stays the panel). Painted
over children, not in `paint()`, for the same reason `TimelinePanelComponent`'s own region outline is
— the colour swatch and the M/S/R/A toggles sit flush against the row's left/right edges, so an
outline drawn underneath them would be invisible along those edges.

**Reaching a row by keyboard alone.** Cmd+Shift+T / Tab land on the Timeline region ROOT (the panel),
never on a row — so `TimelinePanelComponent::keyPressed()` treats a bare **Down** specially when real
focus is on the panel root itself (`getCurrentlyFocusedComponent() == this`, never a looser "focus is
somewhere in the panel" check): it seeds `focusedTrackIndex_` at row 0. Scoped this tightly so it can
never steal an arrow key the clip lane area or piano roll haven't yet claimed for themselves — every
other keystroke that reaches this method still does so by bubbling up from wherever real focus
actually is.

**Kind-badge icon.** The `"MIDI"`/`"AUD"`/`"AUTO"` text is the fallback: when a themed
`AppLookAndFeel` is installed and the corresponding asset is linked in, `paint()` draws
`Icon::TrackMidi`/`TrackAudio`/`TrackAutomation` (`kindBadgeIcon(TrackKind)`) instead — the same
"draw the glyph when the library has it, fall back to text otherwise" contract every other icon
consumer in the app follows. `getKindBadgeIconForTest()` mirrors `getKindBadgeTextForTest()`'s
"value or empty" idiom (returning `-1` for "fell back to text") so a test can assert on which path
ran without decoding pixels. A track's kind never changes after creation, so this is a one-time
paint decision, not something `refreshFromDoc()` has to re-derive.

**M/S/R active-state colours and the binding-chip theme fix.** `applyThemeDerivedColours()` is the
one place every colour this component bakes via `setColour` — the binding chip's warning/normal
fill and the `M`/`S`/`R` buttons' active-state colours (`theme.colors.trackMuteOn`/`trackSoloOn`/
`trackArmOn`) — gets (re-)derived from the currently installed `LookAndFeel`. It runs from three
call sites: the end of the constructor (so the very first paint isn't relying on a later call),
`refreshFromDoc()` (so a doc-driven repaint always shows the right colours), and the component's
own `lookAndFeelChanged()` override (new in this change) — without that last call site, a theme
switch alone, with no accompanying doc change, left the chip and the M/S/R active colours frozen
on whatever theme was active the last time `refreshFromDoc()` ran.

**Colour swatch.** Clicking it builds a `synth::ui::ColourPickerPopup` (`Source/UI/
ColourPickerPopup.h` — see [`theming.md` §13](theming.md#13-colour-picker-popup)) via
`buildColourPicker()` and launches it in a `juce::CallOutBox` anchored on the swatch, replacing the
old palette-cycle click. Its favourites shelf persists through `TrackHeaderHost::
getAppProperties()` (a new, non-pure `TrackHeaderHost` method defaulting to `nullptr` so every
existing implementer keeps compiling) — `nullptr` degrades to an in-memory-only picker, same as a
headless test gets. Preview writes the doc directly with no undo step on every drag/favourite
click; closing the popup either restores the original colour with no undo step (no net change) or
performs the real edit as ONE undo step whose undo target is the original colour (see
`theming.md` §13 for the exact preview/commit contract). `createColourPickerForTest()` exposes
`buildColourPicker()`'s exact wiring without ever launching the `CallOutBox`.

**"MIDI destinations..." menu entry.** The chip's context menu (see below) gains one more item,
`kMidiDestinationsMenuId`, offered only when `offersMidiDestinationsMenuEntryForTest()` says the
track's binding resolves to something with MIDI to send. Choosing it (or a test calling
`applyBindingMenuChoice(kMidiDestinationsMenuId)`) opens a `synth::ui::MidiDestinationPicker`
(`Source/UI/Timeline/MidiDestinationPicker.h`) — a searchable, multi-select list of every live
MIDI-instrument node the track's bound Track In node could send MIDI to — in a `juce::CallOutBox`
anchored on the chip, via `openMidiDestinationsPicker()`/`buildMidiDestinationPicker()` (mirroring
the colour swatch's build/launch split, including `createMidiDestinationPickerForTest()` and a
`setOpenMidiDestinationsPickerHookForTest()` seam so the menu choice is exercisable without a live
callout). The candidate list is DYNAMIC ground truth, not an allowlist: every live graph node whose
`ModuleBase`-level `acceptsMidi()` is true (the per-module flags now reflect what each
`processBlock` actually consumes — see the module MIDI-flag audit in
`Tests/Modules/ModuleMidiFlagsTests.cpp`'s expected table), which automatically excludes MIDI *sources*
(Track In, External MIDI, MIDI Keyboard: they generate notes, they don't consume them). Rows render
in two sections: **Instruments** first (`isMidiInstrumentType()`, `Source/Modules/ModuleBase.h` —
the same set the add-track auto-wire target search uses) and **Other** for the remaining real MIDI
consumers (e.g. an ADSR's note-gate input); the section headers only appear when both groups are
present. **The graph is the truth**: toggling a row calls `TrackHeaderHost::
setMidiDestinationConnected(TrackId, nodeUid, connect)` — `MainComponent`'s implementation performs
one `recordStructuralChange` (add/remove the MIDI connection) then always calls
`reconcileTimelineAfterGraphChange()` — and immediately re-pulls `getMidiDestinationOptions(TrackId)`
to rebuild every row from what the graph now actually reports, rather than trusting the click. Both
host methods are non-pure with inert defaults (empty list / no-op) for the same
keep-every-implementer-compiling reason `getAppProperties()` is, and both no-op cleanly on a stale
popup (the track's binding or the target node no longer resolves) rather than crashing.

> **Known divergence, left alone deliberately.** `AIStateMapper` keeps its own, separately
> name-keyed `midiAcceptingTypes` list for the AI auto-wire path, and that list omits `Wavetable`.
> Unifying it with `isMidiInstrumentType()` would change AI auto-wire behaviour, which is out of
> scope for this change — the destination picker and the add-track auto-wire search both go
> through `isMidiInstrumentType()`; `AIStateMapper` does not.

**The `A` button** toggles this track's automation lane in the (single, doc-wide) automation strip.
The header only reports the click via `onAutomationToggleRequested` — it never tracks open/closed
state itself, since it can't see whether another track's lane is the one currently shown.
`TimelinePanelComponent::toggleAutomationForTrack` decides: already open on one of this track's own
lanes closes the strip; anything else (closed, or open on a different track) opens the track's first
lane.

**Colour** resolves *only* through `synth::ui::resolveTrackColour` (`Source/UI/Timeline/TrackColour.h`): the
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
`Track Audio` for an Audio track — **not claimed by another track**, plus
`"New Track In node"` (which likewise creates whichever type the track's kind needs). Offering the
wrong type would let a user bind a track to a node that structurally cannot play it: both modules
match on **kind as well as uuid**, so the result would be a track that silently plays nothing.
Picking one calls `TimelineDoc::setTrackBinding` as one undoable step, then reconciles.

> **A binding is NEVER re-established automatically — least of all by name.** An orphaned track
> stays orphaned until the user picks a node from that menu. Two nodes can carry the same display
> name, and a silent re-bind would quietly play a track through someone else's instrument.

**"+ Track"** (it used to read `"+ MIDI Track"` and added one outright until audio tracks existed,
and carries the tooltip *"Add a MIDI or Audio track"* so the two-item menu isn't a surprise)
opens a menu — **MIDI Track** / **Audio Track**, an **Instrument Track** submenu (**Oscillator** /
**Wavetable** / **Sampler**, poly variants, then a **Plugin** sub-submenu — see below), then a
separator and **Add Marker** — whose ids are `TimelinePanelComponent::kAddMidiTrackMenuId` /
`kAddAudioTrackMenuId` / `kAddInstrumentOscillatorMenuId` / `kAddInstrumentWavetableMenuId` /
`kAddInstrumentSamplerMenuId` / `kAddMarkerMenuId`. The MIDI/Audio entries land on
`TrackHeaderHost` (`addMidiTrack()` / `addAudioTrack()`); each Instrument submenu entry calls
`addInstrumentTrack(instrumentModuleType)` with its module type string.
`TimelinePanelComponent::applyAddTrackMenuChoice(id)` is the headless seam for all of them — the
same split the binding and context menus use, since a `juce::PopupMenu` never runs in the test
binary. `MainComponent::simulateAddMidiTrackClick()` / `simulateAddAudioTrackClick()` /
`simulateAddInstrumentTrackClick(menuId)` call straight into it.

**The Instrument submenu's Plugin sub-submenu (FRO42, P9-3h)** lists the scanned INSTRUMENT hosted
plugins (`TrackHeaderHost::getInstrumentPluginOptions()`, filtered on
`juce::PluginDescription::isInstrument` — effects are never offered — and on this app's OWN VST3/AU
build, matched against `synth::branding::kProductName`/`kCompanyName` rather than a re-typed
literal, so it can never offer to host itself; the library sidebar goes through a different
collector and is unaffected). Opening the menu (`TimelinePanelComponent::buildAddTrackMenu()`,
called by `openAddTrackMenu()` before it shows the result, and the headless test seam for
inspecting the built `juce::PopupMenu`'s contents) calls
`TrackHeaderHost::ensureInstrumentPluginsScanned()` first, which `MainComponent` wires straight to
its existing `maybeStartEagerPluginScan()` — the SAME hosted-mode-guarded entry point the eager
startup scan and the library sidebar's manual "Scan for plugins..." row use, never a second scan
trigger. An empty or still-in-progress list shows one disabled row — "Scanning for plugins..." or
"No instrument plugins found" (`TrackHeaderHost::isPluginScanInProgress()`) — instead of an empty
submenu. A same-named product built as both VST3 and AU (e.g. "Massive") is never shown as two
identical rows: every real entry's label ALWAYS carries its format in parentheses, using the
sidebar's own short form (`ModuleLibraryComponent`'s plugin sub-headers) — "Massive (VST3)" /
"Massive (AU)" — never a bare name.

Every real entry's id is `kAddInstrumentPluginMenuIdBase + index`, `index` into
`TimelinePanelComponent::collectInstrumentPluginMenuOptions()`'s result AT BUILD TIME — captured
into the member `instrumentPluginMenuSnapshot_` the moment the menu is built. Deliberately NOT the
same contract as the automation strip's lane picker (`collectAutomationLaneOptions`/
`applyAutomationLaneMenuChoice`, §5 below), which re-runs its collector at click time: an
automation lane is document data mutated only on the message thread, so that is safe. The
known-plugin list backing THIS menu is mutated by `PluginScanService::runScan` on a BACKGROUND
thread and re-sorted by name in `MainComponent::getInstrumentPluginOptions()` — a scan completing
between the menu opening and the click landing could otherwise change what index N means, silently
resolving the click against a plugin the menu never actually showed there (the BLOCKER this fixed:
live repro was a menu showing "Massive", clicking it, and getting a Sampler track instead).
`applyAddTrackMenuChoice` therefore resolves strictly against the snapshot, never by re-collecting.
Choosing one calls `TrackHeaderHost::addInstrumentPluginTrack(identity)` — see `docs/mixer.md` §8's
P9-3h entry for what it builds, why the load has to be asynchronous, and how a document replaced
mid-load (New Patch/Open/Load preset) is handled.

**Add Marker** sits below a separator because it is **not a track**: it adds no header row and no
graph node, it drops a flag on the ruler (see *Markers* under §2 above). It shares this menu
because `"+ Track"` is where a user reaches for "add something to the arrangement", and a second
button for one item would not earn its pixels. It is handled *before* `applyAddTrackMenuChoice`'s
`TrackHeaderHost` null-check — a marker is document data with nothing behind it to wire — and calls
`addMarkerAtPlayhead()`.

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

**The Audio entry** (T173a) now builds a **whole mixer channel**, not just a `Track Audio` node —
see [`docs/mixer.md` §8 item 2](mixer.md) for the full design. Steps 1-2 are the same as the MIDI
entry (doc side first, so `kMaxTracks` refuses before any node is created; factory-created node,
uuid minted and mirrored, placed at the left edge), but everything downstream of step 2 is
different, and it is all still ONE undo step — `AppUndoManager::recordGraphTimelineAndMacroChange`,
which extends `recordCombinedChange`'s graph+timeline transaction with a third domain, the macro
set:

1. `createTrackAudioNode(wireDirectlyToMasterBus=false)` creates the `Track Audio` node **unwired**
   — the direct-to-master-bus auto-wire the MIDI-entry-style flow used before T173a is now only
   `createAndBindTrackInNode()`'s behaviour (its ad hoc single-node rebind from the binding chip);
2. `synth::buildDefaultAudioChannel` (Core, `Source/Mixer/ChannelFlows.h`) wires the node into the
   factory default chain — `Track Audio -> Parametric EQ (bypassed) -> Compressor (bypassed) ->
   Channel Strip (Stereo)` — then splices Master (`synth::spliceMasterNode`, reusing the existing
   singleton after the first channel; the same "Rec Tap when spliced, else Audio Output" target
   `createTrackAudioNode()`'s old direct wire used) and wires the strip into Master's Mix input;
3. `GraphEditor::addMacroForMembers` boxes `{Track Audio, EQ, Compressor, Channel Strip}` into ONE
   collapsed macro named after the track. **Master stays outside the macro**, and the
   Strip -> Master cable is left a plain graph edge, deliberately never a macro port — see
   `docs/mixer.md`'s §8 item 2 for why (the Mix-vs-Direct classification `spliceMasterNode` does
   would break behind a `MacroOutlet`);
4. bind the track to the `Track Audio` node's uuid and give it the palette colour for its index —
   same as every other entry.

`GraphEditor::updateComponents()` runs **inside** the transaction's mutation (not after), so
`MacroSet::retainOnly()` sees every node above still alive when it reconciles macro membership.

**The Instrument entries** (T183) are the MIDI-track mirror of the Audio entry above — a `Track In`
feeding a chosen instrument, then the same factory default chain — see
[`docs/mixer.md` §8 item 2](mixer.md) for the full design. The picker offers exactly the
audio-producing MIDI instruments (**Oscillator**, **Wavetable**, **Sampler**) — deliberately not
every module the MIDI entry's own auto-wire search above recognises as "MIDI-driven": Poly MIDI
outputs CV/gate and Sequencer/Poly Sequencer generate MIDI, none of them audio. One undo step
(`AppUndoManager::recordGraphTimelineAndMacroChange`, `MainComponent::addInstrumentTrack`):

1. add a `Midi` track (doc side first, same `kMaxTracks` ordering reason as every other entry) —
   **T183 kept this a `TrackKind::Midi` track rather than adding a new kind**: it is exactly what
   the MIDI entry's own auto-wire (step 3 above) already produces once a cable is drawn by hand,
   this flow just draws that cable and builds the channel automatically (see `docs/mixer.md` §5.2's
   table note);
2. create the `Track In` node (same factory/uuid/placement idiom as the MIDI entry), then the
   chosen instrument to its right, and wire `Track In -> instrument` on the MIDI channel — always
   unambiguous, since the instrument was just created for this track alone;
3. a poly instrument's raw ch0-7 (up to 8 simultaneous voices) cannot feed
   `buildDefaultAudioChannel` directly, which wants one stereo pair — `synth::
   addVoiceMixerForPolyInstrument` (Core, `Source/Mixer/ChannelFlows.h`) sums them into a Voice
   Mixer first when the instrument's own `poly` parameter is on (docs/mixer.md §5.4/§5.8). A
   factory-created instrument defaults to poly OFF, so this is a no-op on the golden path today;
4. for an **Oscillator/Wavetable** instrument (P9-3i, FRO43, `docs/mixer.md`'s P9-3i entry): neither
   has an envelope of its own, so a held or released note drones forever. `synth::
   addEnvelopeAndVCAForRawInstrument` inserts an ADSR (gated by the same Track In MIDI as the
   instrument, forced non-poly) driving a VCA (also forced non-poly) ahead of the rest of the
   chain — AFTER the Voice Mixer from step 3, never before it. A no-op for **Sampler**, which
   already has its own one-shot playback envelope;
5. `synth::buildDefaultAudioChannel` wires the instrument (or the Voice Mixer/VCA, whichever step 3
   or 4 last produced) into the same `Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel
   Strip (Stereo)` chain the Audio entry uses, then splices Master. A split-block source
   (Oscillator/Wavetable, whose right leg is never ch1) passes its own
   `ModuleBase::rightAudioLegChannel()` (or `VCAModule::kRightBase`, once step 4 has run) as
   `buildDefaultAudioChannel`'s `sourceRightChannel` parameter instead of the ch1 default;
6. `GraphEditor::addMacroForMembers` boxes `{Track In, instrument, [Voice Mixer if any], [ADSR+VCA
   if Oscillator/Wavetable], EQ, Compressor, Strip}` into ONE collapsed macro named after the track,
   the same way the Audio entry's macro is built — Master stays outside it, for the same reason;
7. bind the track to the `Track In` node's uuid and give it the palette colour for its index.

**Delete track** (right-click a header) is the same compound step in reverse: the track and its
bound `Track In` / `Track Audio` node go together, and come back together.

**Make Channel** (right-click a header, above Delete Track; P9-3d/FRO25) turns the track's bound
chain into a mixer channel — see [`mixer.md`](mixer.md) §5.8 for what moves and what stays shared.
It is enabled only while `TrackHeaderHost::canMakeChannelForTrack()` is true (the chain has no
Channel Strip of its own yet), disabled — not hidden — afterwards, and `MainComponent` runs it as ONE
graph + timeline + macro undo step followed by the reconcile pass.

Headless test seams (a `juce::PopupMenu` never runs in the test binary): `collectBindingOptions()` /
`applyBindingMenuChoice(id)` and `applyContextMenuChoice(id)` are the menus' semantics without the
menu, `buildContextMenu()` plus `setShowContextMenuHookForTest()` capture the menu a real right-click
`mouseDown()` builds, and `handleChipClick(showMenu=false)` exercises the selection affordance on its
own. The row
talks to the app exclusively through `synth::ui::TrackHeaderHost` (implemented by `MainComponent`),
so it is fully testable against a stub with no graph — see `Tests/UI/Timeline/TimelineTrackHeaderTests.cpp`.
