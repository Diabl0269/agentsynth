# Timeline Automation Lanes

Automation lanes fold out **under the track that owns them** (`Track::lanes`), Cubase style: one
row per lane, its header in the track-header column and its curve editor across the lanes region,
both at the same y. There is no separate automation strip.

## Lane rows

Every track shows a fold arrow in its header (the Unassigned section only while it holds lanes) ([tracks](tracks.md#the-automation-fold-arrow));
pressing it (or Return/Space on it, or the rebindable "Show/Hide Track Automation" key on the
focused header) folds the track's lanes open or closed. Fold state is runtime-only, per `TrackId`,
owned by the panel and never saved: a track starts folded, except that `showAutomationLane()`
opens the track of the lane it shows, and the Unassigned section starts open.

Each open lane is a row of `Metrics::timelineAutomationLaneRowHeight` (40 px) times the same
vertical-zoom scale the clip rows use. The rows enter the one row layout as the track's extra area
([tracks](tracks.md#row-layout)), so the track headers, the clip lanes, hit testing, the reorder
drag and the scroll limit all move them together. The clip lanes paint only a bg-1 backdrop there
and refuse clicks there (`TimelineClipLaneArea::hitTest`).

`TimelineAutomationLanes` (`Source/UI/Timeline/AutomationLanes/TimelineAutomationLanes/`) is the
panel's collaborator for all of it: the fold sets, the geometry it hands the layout
(`extraHeights()`, `rowHeightOverrides()`), and one lane header plus one `AutomationLaneEditor` per
visible lane, **pooled by `LaneId`**. A doc change that keeps a lane keeps its header and editor (an
in-flight gesture and keyboard focus survive the notification); only a lane that leaves the screen
loses them. The editors live in one click-through container over the lanes region (above the clip
lanes and the piano roll, below the playhead), placed at their row's y minus the vertical scroll and
clipped by it; the container hides while the piano roll is open.

Under the last lane of an open track (the Unassigned section too) sits a 24 px "+ Add automation..."
row (an open track with no lanes shows only that row), zoom-scaled like a lane row ([below](#adding-a-lane-from-the-timeline)).

### The lane header

`AutomationLaneHeaderComponent` (`Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/`), indented
16 px under the track with the track's colour as a 4 px stripe at 45% alpha:

- the parameter name (11 px semibold) over the module name (9.5 px, muted), from
  `TrackHeaderHost::getParameterDisplayName` / `getNodeDisplayName` with the old picker's fallbacks
  (the raw `paramId`; the uuid's first 8 characters);
- a value readout (mono 10 px, muted): the lane's curve at the playhead beat, evaluated on the
  message thread with `AutomationKernel::evaluate` over the doc's breakpoints (never through
  `AutomationUiFeed`, which has a single reader), shown as the parameter's own text when
  `TrackHeaderHost::getParameterValueText` can give it, else the number. It refreshes from the
  panel's existing transport poll (`updateFromTransport`), only when the beat changed, only for
  headers on screen, and repaints only when the text changed;
- a record-mode combo (Off/Read/Touch/Latch/Write, combo id = `LaneRecordMode` + 1, Write in the
  error colour) writing `TimelineDoc::setLaneRecordMode` as one undo step — a manual pick IS a user
  gesture, unlike `AutomationRecorder`'s own Write-drops-to-Touch-on-stop call;
- a "..." menu: **Add LFO modulator** ([below](#modulators)), **Move to track** (every other MIDI/Audio
  track, `TimelineDoc::moveLaneToTrack`; the [sections](#sections) of the lane's LFOs move with it) and
  **Delete lane** (`TimelineDoc::removeLane`), each one undo step.

The edits live in `AutomationLaneActions` as free functions: a Delete or Move destroys the header
that asked for it during the doc notification, so the header copies what it needs and makes the
edit its last statement. Every control is a Tab stop with a title and tooltip ("Cutoff record mode",
"Lane menu for Cutoff") and the shared focus ring.

### Unassigned automation

The `TrackKind::Automation` track holds lanes no single track owns
([below](#which-track-a-lane-lands-on)). `TimelineDoc` keeps it **after every other track** on
every path: `addTrack` appends other tracks above it, `moveTrack` never moves it nor places a track
below it, and `fromVar` re-orders an older file. Its header row is a 26 px **section header**
(a per-track row-height override in the layout) spanning the header column and the lanes region in
surface-hi: fold arrow, "Unassigned automation" and the lane count, with no swatch, M/S/R, chips or
clip row. Its lanes are ordinary lane rows with the curve in text-muted. The reorder drag treats
it as no slot. When a Move or Delete takes its last lane, `TimelineDoc::removeEmptyAutomationTracks()`
removes it inside the same undo step; opening a project already drops an emptied one.

## Panel API

`showAutomationLane(LaneId)` opens the lane's track, scrolls the row into view and gives its editor
keyboard focus; `getSelectedAutomationLane()` is the lane last shown or focused.
`collectAutomationLaneOptions()` / `applyAutomationLaneMenuChoice(int)` are the headless lane
choices the mixer send tests drive: choosing an existing lane shows it, choosing an "Add lane..."
entry asks `TrackHeaderHost::addPluginAutomationLane` (find-or-create) and then shows it. The
choices re-run the collector at click time rather than resolving against a build-time snapshot:
lanes are document data mutated only on the message thread. Contrast the add-track Plugin submenu
([add-track](add-track.md#menu-options-resolve-against-a-build-time-snapshot)), whose backing list
a background thread mutates. Test hooks: `isTrackAutomationExpandedForTest`,
`laneRowBoundsForTest`, `laneEditorForTest`, `laneHeaderForTest`.

## Adding a lane from the timeline

Right-clicking a knob is not the only way to put a lane on a track. Two entry points open the same
picker of **that track's** parameters:

- the **"+ Add automation..." row** that closes an open track's lane rows (`AddAutomationRow`,
  `Source/UI/Timeline/AutomationLanes/AddAutomation/`): `AddAutomationRow::kBaseHeight` (24 px) times the same vertical zoom, text-muted 10 px,
  indented like the lane headers, in the header column. It is one more row of the track's extra area
  (`TimelineAutomationLanes::extraHeights()`), so it moves with the lanes in the one row layout and
  with the track in a reorder drag. The lanes region behind it is the clip lanes' backdrop and takes no
  click. It exists exactly while the track is open: any open ordinary track, lanes or not (on an empty
  track it is the whole fold-out, so its fold arrow is how the timeline starts automation), and the
  Unassigned section while it holds lanes.
- the track header's right-click **"Add automation..."** ([tracks](tracks.md#row-context-menu)).  Once the
  lane lands the track opens and shows it above the row.

The row is a Tab stop with the accent focus ring; Return and Space press it in the same event
(`juce::Button` posts its own Return click to the message queue and has no Space handling), its name and
tooltip read "Add automation to <Track>", and a click does not take focus off the clips.

**The picker** is the Mod Matrix's `ModMatrixPicker`, reused ([chrome](../layout/chrome.md)): a search
field over rows grouped under their module's title, Up/Down/Return/Escape, a `juce::CallOutBox` anchored
on the row or header. Two small generalisations: an item may carry `searchText` the row does not show (the
module title, so typing "filter cut" finds Cutoff under "Filter 1"), and a query now matches word by word,
every word anywhere in the row or its search text (a superset of the old substring match); and
`setAccessibleNames` re-words its screen-reader names. `collectAddAutomationChoices` turns the host's
parameters into its items, regrouped so a module's rows are adjacent, and drops any parameter that already
has a lane.

**What a track offers** (`TrackHeaderHost::getAutomatableParameters(TrackId)`, implemented by
`MainComponentAutomationLanes.cpp`): the float and int parameters of every module
[the track plays](#which-track-a-lane-lands-on), grouped by module title, plus that module set's hosted-plugin
parameters and active Channel Strip send slots (the existing "Add lane..." sources, filtered to those
nodes). Hidden plumbing is never offered: Attenuverter, macro port nodes, and the Track In / Track Audio
sources. A parameter that already has a lane is left out. For the Unassigned section the list is the
modules **no** single track plays (a shared effect, the master bus, an unconnected module).

**What a pick does**: `TrackHeaderHost::addAutomationLane(TrackId, AutomatableParameter)` creates the lane
**on the track the picker was opened for**, not the track the ownership rule would choose, as one undo step,
then the panel calls `showAutomationLane` (opens the track, scrolls the row in, focuses its editor). It runs
through the same helper `automateParameter` and `addPluginAutomationLane` use (`MainComponent::addLaneUndoable`:
the parameter's real range and index hint, one `recordTimelineChange`), with the target track explicit
instead of resolved inside the mutation. A node that has never been automated gets its uuid at that moment,
the same ensure-uuid step the knob path takes.

Test hooks: `setAddAutomationPickerHookForTest` (receives the picker instead of a call-out),
`addAutomationRowForTest`, `addAutomationRowBoundsForTest`.

## Modulators

A lane can carry **modulators**: rows directly under the lane, one per modulation routing into that
parameter's CV jack. An LFO's output then adds on top of the lane's automated base value, exactly as a
CV cable on the canvas does ([modulation](../modules/modulation.md)).

**Rows are derived from the graph, never stored in the timeline doc.** For each open lane the panel asks
`TrackHeaderHost::getModulators(nodeUuid, paramId)`; `MainComponentModulators.cpp` finds the parameter's CV
channel (`GraphEditor::modulationChannelFor`: the module's `getModulationTargets()` entry whose `paramId`
matches, or whose name matches the parameter's display name when the target has no `paramId`) and returns
every `AudioEngine::getModulationRoutings()` ModCV routing into that node and channel -- whether it was added
from the lane or patched by hand on the canvas. Every node a row names is named by uuid (`ModulatorInfo`,
`Source/UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h`), since node ids do not survive an undo
restore. The rows refresh after every graph change: `MainComponent::reconcileTimelineAfterGraphChange` (undo,
redo, load), `GraphEditor::onGraphStructureChanged` (anything that rebuilds cards), and the undo manager's
change broadcast (a cable drag, which rebuilds no card). `TimelineAutomationLanes` pools a lane's rows by
the routings' keys, so a refresh that finds the same routings keeps the same row objects (focus and an
in-flight drag survive) and only a changed set rebuilds that lane's rows.

**Add LFO modulator** (the lane's "..." menu) asks `TrackHeaderHost::addLfoModulator`, which reaches
`GraphEditor::addLfoModulator` (`GraphEditorModulators.cpp`): a real LFO card (Sine, synced at 1/4, bipolar,
full level) placed beside the target card (left of it, else right; anti-overlapped by `resolvePlacement`
at its estimated and then its real size), cabled from its output into the parameter's CV jack through
`connectPorts` (so `addModRouting` inserts the hidden attenuverter), the attenuverter given a uuid and a
depth of 0.5. When the target is a macro member the LFO joins that macro (`addSelectionToMacro`, after the
cable exists, so the cable is interior and no port is minted); otherwise `makeRoomFor` runs on the new card.
All of it is ONE `recordGraphAndMacroChange` step. A parameter with no CV jack keeps the item, disabled, as
"Add LFO modulator (no CV input)": a menu item has no tooltip, and the reason has to reach a screen reader.

**The row** (`ModulatorRow`, `Source/UI/Timeline/AutomationLanes/Modulators/`) is 54 px times the row zoom,
indented 28 px (one step further in than a lane header), three lines in the header column (title; shape and
rate; sync and depth):

- an `LFO` tag in the mod-wire colour (resolved by `GraphEditor::modulationWireColour`, i.e. through
  `resolveCableColour`, so a user colour override applies), the LFO card's title, a **Sync** toggle and a
  "..." menu: **Show on canvas** (select the card and centre the canvas on it) and **Remove modulator**;
- the **shape** (Sine/Triangle/Sawtooth/Square/S&H/Custom), the **rate** (a 1/1..1/32 combo while synced,
  a Hz bar while free-running) and the **depth** (the attenuverter's `amount`, shown as a percentage).

Any other source (an envelope, an envelope follower, a macro control...) gets a read-only row: a `CV` tag,
its card title and the depth only. A direct cable with no attenuverter has no depth control.

Every edit goes through `TrackHeaderHost::setNodeParameter(uuid, paramId, value, ParameterEditPhase)`, which
`MainComponent` implements with the canvas knobs' own undo idiom: `captureBeforeState` at `Begin`,
`setValueNotifyingHost` per `Change` (the card follows live), `pushSnapshotFromCapture` at `End`; a combo
pick, a toggle click or a key press is `Once` (all three). It does not call begin/endChangeGesture: the
card's `ModuleComponent` captures into the same single undo slot on a gesture, and two owners of that slot
would interleave. The rows read their values back (`getNodeParameter`) on the panel's existing transport
poll, only for rows on screen, writing a control only when its value moved and never the one mid-drag.

**Macro ports in the chain.** A row names the real modulator and the real parameter, not the macro ports
between them: `MainComponent::getModulators` follows each end of the routing through ports
(`realEndpointBehindPorts`, `Source/UI/Graph/ModMatrixEndpoints.h`), whether the attenuverter sits before or
after the port and however many macros deep. **Remove modulator** follows the cable the same way and removes
every port the removal leaves with nothing on one side, even when the "delete ports on last cable" preference
is off (the person asked for the removal), all in the one undo step.

**Remove modulator** (`GraphEditor::removeModulator`) removes the routing (the attenuverter chain as a
whole, or a direct cable's edges), sweeps the macro ports the cable leaves empty, and removes the LFO too when
no other cable leaves it, through `requestDeleteModule(..., recordUndo=false)` so every pre-removal unbind
runs -- all one undo step. When the LFO goes, its [sections](#sections) lane goes in the same step
(`MainComponent::removeModulator` wraps the graph edit and the lane removal in one
`recordGraphTimelineAndMacroChange`; an LFO that still drives another jack stays, with its sections). A row
that triggers a removal is destroyed by the refresh before the host call returns, so it copies what it
needs and makes the call its last statement.

**Layout**: a lane's block is its row plus its modulator rows (`TimelineAutomationLanes::laneBlockHeight`,
the one helper every geometry function walks), so modulator rows count into the track's extra height and
move with the track. A track's modulators show whenever its lanes are open. Over the lanes region each row
is a `ModulatorBand` ([sections](#sections) for an LFO; for any other source a faint 28% band in the mod-wire
colour that takes no clicks, so the clip lanes underneath decide (and refuse) as they do for a lane row's
backdrop).

### Sections

An LFO modulator can be on only in chosen parts of the song. In the LFO row's band the user draws
**sections**: blocks of bars where it is on; outside them it does nothing. With no sections the modulator is
on everywhere, as it always was.

**Storage** (`ModulatorSections.h/.cpp`): an ordinary automation lane on the LFO's own `level` parameter
(0..1, `nodeUuid` = the LFO's uuid, Read mode) on the same track as the lane it modulates. Every point is
Hold: `(beat 0, 0)`, then `(start, 1)` and `(end, 0)` per block (a block starting at beat 0 has no leading
zero); overlapping and touching blocks merge. The existing `AutomationApplier` plays it, so there is no engine
concept: the LFO's level is 0 outside the blocks and 1 inside, and the Depth control (the attenuverter) stays
independent. The lane is found by `sectionsLaneFor(doc, lfoUuid)` wherever it sits (the doc-wide
one-lane-per-parameter rule).

**Hidden as a lane**: while the LFO is a modulator of another lane on the same track, that `level` lane is not
drawn as a lane row; it is the modulator row's band (`TimelineAutomationLanes::deriveRoutings` computes the set
once per sync and refresh, `isSectionsLane`). It also does not count in the track's fold-arrow or badge lane
count (`TimelineTrackHeaderComponent::setHiddenLaneCount`), the lane choices the mixer's "Automate" entries
use, or the track's row height. If the cable is removed by hand the LFO stops being a modulator there and the
level lane shows as an ordinary lane again; nothing is lost. A level lane that something modulates itself also
stays a lane row.

**Gestures** (real mouse handling on the band, which takes clicks only inside its own row), following the
timeline's edit tool the way the lane editors do (`sectionToolFor`):

- **Draw**: drag paints an on block over the dragged span (snapped with the shared view-state snap), creating
  the lane on first use. **Erase**: drag turns the span off; out of a band with no lane it leaves "everywhere
  but here".
- **Select** (and every other tool): drag a block's edge (6 px zone) to resize it, its middle to move it, click
  to select; **Delete**/**Backspace** removes the selected block, **Escape** clears the selection.
  **Double-click** on empty band adds a one-bar block at the snapped beat.
- Each gesture is ONE undo step and one doc write: the preview is component-local (`preview_`) and
  `ModulatorBand::commit` runs `applySections` inside `AppUndoManager::recordTimelineChange` on mouse-up, like
  `AutomationLaneEditor`. Creating the lane is `addLane` plus `editBreakpoints` in that one step. **Erasing the
  last block removes the lane in the same step**, so the modulator is back to "on everywhere" (an all-zero lane
  meaning "off everywhere" would be confusing); the band's tooltip says so.

**Painting**: blocks in the mod-wire colour at 28% fill with a 1 px outline and 3 px radius, the selected block
outlined in the accent; with no lane the whole band is the faint full band and its tooltip reads "Draw
sections to play this modulator only there". Blocks use the shared `TimelineViewState` x mapping exactly like
clip lanes.

**Keyboard and screen reader**: the band is a Tab stop with the focus ring, named "Cutoff LFO sections" with
the blocks as its description ("bars 9-16, 25-32" or "on everywhere") and a tooltip naming the tools. Left/Right
move the selection between blocks, Delete removes, **Return** adds a one-bar block at the playhead, Escape
clears. All standard navigation keys, so none is a rebindable action.

**With the rest of the timeline**: a moved lane takes its LFOs' sections with it
(`sectionsLanesTravellingWith`, except those of an LFO another lane on the track also uses), and Remove
modulator removes them with the LFO (above). Deleting the target lane leaves the modulators and their sections
in the graph; the level lane then shows as an ordinary lane.

Every control is a Tab stop that a click does not take focus to, shows the accent focus ring, and is named
for what it controls ("Cutoff LFO shape", "Cutoff LFO depth", "Modulator menu for Cutoff LFO"), with the
same text as its tooltip. Test hooks: `refreshModulators`, `modulatorRowForTest`, `modulatorBandForTest`,
`modulatorRowBoundsForTest`.

## The curve canvas

`Source/UI/Timeline/AutomationLaneEditor.h/.cpp` (`synth::ui::AutomationLaneEditor`) edits ONE
`synth::AutomationLane`: one editor per open lane row. Its curve is drawn in the owning track's colour
(`setCurveColour`; text-muted on the Unassigned section).

X is the SAME shared `TimelineViewState` the clip lanes use, so it lines up with the playhead
pixel-for-pixel; the piano roll is the one surface that maps beats through its own zoom and scroll
instead ([piano-roll](piano-roll.md#horizontal-mapping)). Y maps the lane's own `RangeSnapshot
[min..max]` linearly onto the component's height, top = max (`valueToY` / `yToValue`).

The curve is sampled every ~2 px by building a local `TimelineSnapshot::Point[]` from the lane's
breakpoints and calling `AutomationKernel::evaluate` with a fresh `AutomationCursor`.

**Why it is re-derived on every repaint rather than cached.** Paint is not hot, and this is the
SAME evaluator the audio thread uses, so the canvas can never show a shape real playback would not
produce.

## Tools

`AutomationLaneEditor::Tool` stays the editor's internal enum; the lanes have no tool row of their
own and follow the timeline's edit tool ([edit-tools](edit-tools.md)) through one function,
`automationToolFor(EditTool, shiftDown)` (`AutomationLanes/AutomationToolMapping.h`):
Select → Pointer, Draw → Pencil (Line while Shift is held at mouse-down), Erase → Eraser, every
other tool → Pointer. `TimelinePanelComponent::setActiveTool` fans the tool out to every editor
(`setEditTool`), and the editor re-reads Shift at each mouse-down.

| Tool | Gesture |
|---|---|
| Pointer | Drag a HANDLE moves it — beat snapped via the shared view-state snap, value clamped to the lane's range; tension and curve carry over untouched. Drag a SEGMENT (not a handle — hit-tested first) scrubs the segment's LEFT point's tension, ±0.01 per vertical pixel, clamped to `[-1, 1]`, following `AutomationKernel`'s own "shape comes from the LEFT point" contract. Double-click empty space adds a point at that (beat, value), Linear, tension 0 |
| Pencil | Freehand drag collects raw (beat, value) samples — no snapping, that is the point of freehand. On mouse-up they are thinned by `synth::AutomationRecorder::thinPoints` (reused, not re-implemented — its RDP helper is `public static` precisely so a second caller can reach it) at the SAME `kThinningEpsilonFraction` scaled to the lane's own range, and replace whatever existed inside the dragged beat span |
| Line | Drag previews a straight line from press to release; mouse-up replaces the dragged span with exactly the two snapped endpoints, Linear |
| Eraser | Drag removes every handle it touches — collected into a set as the pointer passes over them (dimmed in the preview), deleted on mouse-up |

Right-click a SEGMENT shows Hold/Linear, ticking the current one, routed through the headless
`applySegmentCurveChoice(beat, curve)` hook. Right-click a HANDLE shows `{Delete point}`.

Escape clears in-flight tool-drag state and returns `true`; when idle it returns `false` so the key
falls through to the panel.

## One gesture, one mutation

Every preview above is strictly component-local (a handful of `preview*_` members, read back by
`paint()`) and NEVER touches the doc during `mouseDrag` — commit happens exactly once, on mouse-up.

The subtlety: `TimelineDoc`'s own single-point mutators (`addBreakpoint` / `removeBreakpoint`) each
bump the revision counter independently, so a gesture that touches several points — Pencil's
thin-and-replace, Line's remove-span-then-add-two-endpoints, a Pointer move that lands on a
different beat, Eraser's multi-point sweep — calling them in a loop would cost one revision bump,
i.e. one audio-thread republish, PER POINT instead of per gesture.

`TimelineDoc::editBreakpoints(laneId, removeBeats, addPoints)` is the batched primitive that fixes
it: it removes every existing point at a beat in `removeBeats`, then inserts every point in
`addPoints` (validated and clamped exactly like `addBreakpoint`), as ONE `applyMutation` call
however many points move either way. Every multi-point gesture routes through it; only the
genuinely single-point ones — tension scrub, curve toggle, double-click-add, record-mode select —
still call a plain single mutator, because those already cost exactly one bump on their own.

## The knob entry point

A card's generic knobs (built by its card body, `CardBody::createKnob` in `Source/UI/Graph/CardBody/CardBody.cpp`)
attach the `ModuleComponent` as a `MouseListener` on the slider (`addMouseListener(&card, false)` — safe because
the card owns the card body and so outlives every child slider).

`ModuleComponent::mouseDown` checks `e.eventComponent != this` FIRST — a hit on a child fires the
SAME override, in the CHILD's local coordinate space, which the body-click geometry further down
must never see — and, on a right-click, shows `"Automate '<Param>'"` via a
`juce::Component::SafePointer<ModuleComponent>` (the popup's callback is async, so the module can
be gone by the time it fires) that calls `owner.onAutomateParameterRequested(nodeId, paramId)`. That
is a `GraphEditor` host seam mirroring `onSaveSnippetRequested` exactly: `GraphEditor` owns no
`TimelineDoc`, so it hands the pair back to the one component that owns both the doc and the graph.

`MainComponent::automateParameter(nodeId, paramId)` — public, and also the test's headless hook —
resolves the node's uuid (ensure-uuid, mirrored into the processor, the same idiom
`createTrackInNode()` and `AIStateMapper` use at every uuid writer site), picks the lane's track
([below](#which-track-a-lane-lands-on)), binds a lane with the parameter's real
`NormalisableRange` (`addLane` dedupes doc-wide — a repeat call for an already-automated parameter
is a no-op that returns the existing lane), opens the timeline panel via the SAME toggle-button
click path `simulateToggleTimelineClick()` uses if it is hidden, and shows the lane under its track
(`showAutomationLane`).
Track creation (fallback case) and the lane are one undo step.

## Which track a lane lands on

The lane goes on the track that plays its module, so it sits next to the clips it shapes; a module
no single track plays falls back to the doc's one `TrackKind::Automation` track (found or created).
`addPluginAutomationLane` (the "Add lane..." lane choices) follows the same rule.

"Plays" is `synth::resolveTrackOwners` (`Source/Timeline/TrackOwnership.{h,cpp}`, pure), the same
claim auto-arrange makes for its track rows ([layout](../layout/layout.md#auto-arrange)) except that
sharing means no owner instead of "the earlier track":

1. A track owns what its start node (Track In / Track Audio) reaches through cables, unless another
   track reaches it too (the master bus, a shared effect: unowned).
2. A node nothing reaches, whose owned cable neighbours all belong to one track, joins it (an
   instrument feeding the channel, an LFO cabled into a filter's CV jack).
3. A pure modulator (modulation consumers, no cable leaving, nothing modulating it) follows what it
   modulates: one owning track, or unowned when its consumers span two. A MIDI retrigger cable into
   it does not claim it.

`MainComponent::resolveAutomationOwners` builds the input from the live graph (every cable is a
flow edge, `getModulationRoutings()` the modulation edges) and the doc's tracks with a binding.

**Opening a project** (`loadBundleFromFile`, `loadAutosaveFromFile`) runs
`MainComponent::moveLanesToOwningTracks`: each lane on an Automation track whose module has an owner
moves there through `TimelineDoc::moveLaneToTrack` (same lane id, points, record mode, range; one
revision bump each), and an Automation track emptied by the move (no lanes, no clips) is removed.
It is part of the load, not an undo step. Lanes of unowned modules stay put. Lanes created by the AI
timeline ops (`TimelineOps`, no graph) still start on the Automation track and move on the next open.
See [`modulation.md`](../modules/modulation.md) for the user-facing description of the right-click route.

## A lane follows its send through a reorder (FRO296)

`TimelineDoc::swapLaneParams(nodeUuid, paramA, paramB)` retargets whichever of the two `(nodeUuid,
paramId)` lanes exists to the OTHER paramId, in place — a rebind, never a delete+recreate, so a
lane's points and record mode survive untouched. It exists specifically for a mixer send-slot swap
(`synth::swapSends`/`synth::moveSendRow`,
[`docs/mixer/sends-and-buses.md#slots-are-sparse`](../mixer/sends-and-buses.md#slots-are-sparse)):
`MixerPanelComponent::moveSendRow` — the one place that owns the graph, the `TimelineDoc` AND the
macros together — replays the exact `(slotA, slotB)` sequence `synth::moveSendRow` applied to the
graph, calling `swapLaneParams` once for `sendALevel`/`sendBLevel` and once for
`sendAPan`/`sendBPan` per swap, all inside the SAME
`AppUndoManager::recordGraphTimelineAndMacroChange` transaction as the slot swap itself. A lane on
neither side of a given pair is left alone (no lane is created); a lane on exactly one side just
gets the other's paramId, same as `rebindLane` (the nodeUuid half of this shape) leaving an unbound
side untouched.

This is a NEW, narrow case rather than a use of `rebindLane`: `rebindLane` only ever changes a
lane's `nodeUuid`, and the doc-wide one-lane-per-parameter invariant it enforces (reject if some
OTHER lane already owns the target identity) would make a true SWAP between two lanes on the SAME
node impossible to express as two sequential `rebindLane` calls — the second call would always
collide with the first's own new identity. `swapLaneParams` mutates both sides in one
`applyMutation`, so the invariant never sees an intermediate, colliding state.

## Tests

`Tests/UI/Timeline/AutomationEditorTests.cpp` — `AutomationLaneEditor` gesture and
publish-discipline coverage (mirroring the `TimelineClipLaneArea` / `PianoRollComponent`
hand-built-`juce::MouseEvent` idiom against a bare `TimelineDoc` + `AppUndoManager`), the panel's
lane rows in brief, and `MainComponent` integration tests for the knob entry point and which track
a lane lands on. `Tests/UI/Timeline/AutomationLanes/` covers the lane rows in depth: the fold arrow
by mouse and keyboard, what a click at each y hits, the curve tools through the timeline's edit
tool, the lane header's record mode, move and delete with undo, the value readout, the Unassigned
section (order, 26 px row, reorder, removal in the same undo step), the zoom anchor with lane rows,
and a saved project with lanes and a macro LFO reopening, and the "+ Add automation..." row and header menu
entry (`AutomationLanesAddRowTests.cpp`, with the MainComponent side, what each track offers and where a
pick lands, in `AutomationLanesAddMainTests.cpp`). Modulators: `AutomationLanesModulatorRowTests.cpp` (layout,
hit testing, row controls and names against a stub host), `AutomationLanesModulatorMainTests.cpp` (add, macro
join, no CV jack, a hand-patched cable, remove, undo), `AutomationLanesModulatorMacroRemovalTests.cpp` (rows
and Remove through macro ports, nested and either chain shape, a shared LFO, the preference off) and `AutomationLanesModulatorEditTests.cpp` (row edits and
their undo, the CV moving across a render and stopping at depth 0, a saved project reopening with its row).
Sections: `AutomationLanesSectionsModelTests.cpp` (the blocks/breakpoints algebra), `AutomationLanesSectionsBandTests.cpp`
(real mouse and key events on the band: each tool, one undo step and one doc write per gesture, names) and
`AutomationLanesSectionsIntegrationTests.cpp` (hidden as a lane and back, Remove modulator, Move to track, a
saved project reopening); the applier playing a section is in `AutomationApplierTests.cpp`.
