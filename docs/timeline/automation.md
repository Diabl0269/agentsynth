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
The header column's "+ Track" strip is exactly as tall as what sits above the clip rows (the ruler, plus the roll's
toolbar while it is open), so a track header, its lane headers and their rows all start at the same y and have the same
height as the rows they label.

An open track with no lanes shows a 24 px "+ Add automation..." row, zoom-scaled like a lane row; once it has a
lane, that button shrinks to a small "+" in the empty gutter left of the last lane header's colour stripe, so no
empty row is left under the lanes ([below](#adding-a-lane-from-the-timeline)).

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
  headers on screen, and repaints only when the text changed.
  The slot is `LaneValueReadout`, and it is the only readout: while the lane has a selected point it
  shows that point's value (read from the doc, so a drag shows it on release) in the theme accent colour,
  and goes back to the playhead value in muted when the selection clears (click on empty space, Escape,
  the point deleted). With several points selected it shows the keyboard-cursor point if that one is
  selected, else the first selected point. `TimelineAutomationLanes::updateSelectedReadout` feeds it from
  each editor's `onSelectionChanged` and after every doc refresh; a selection that empties and refills
  inside one doc notification (a move) changes nothing. The colour cross-fades in 130 ms with an
  `AnimationDriver` and lands at once under Reduce Motion or off screen; nothing repaints once settled.
  Its accessible name says which value it is ("Cutoff value at playhead" / "Cutoff selected point value"),
  with the text as its description, and its tooltip likewise;
- a record-mode combo (Off/Read/Touch/Latch/Write, combo id = `LaneRecordMode` + 1, Write in the
  error colour) writing `TimelineDoc::setLaneRecordMode` as one undo step — a manual pick IS a user
  gesture, unlike `AutomationRecorder`'s own Write-drops-to-Touch-on-stop call;
- a lane menu: **Add modulator...** ([below](#modulators)), **Change parameter...** and **Duplicate**
  ([below](#change-parameter-and-duplicate)), **Move to track** (every other MIDI/Audio
  track, `TimelineDoc::moveLaneToTrack`; the [amount lanes](#amount-lane) of the lane's modulators move with it) and
  **Delete lane** (`TimelineDoc::removeLane`), each one undo step. The "..." button opens it; so does a
  right-click anywhere on the lane ([below](#right-click-anywhere-on-a-lane)) and Shift+F10;
- a click on the parameter name opens the Change parameter picker (the name is a hit region with a pointing-hand
  cursor, not a separate control: the keyboard route is the menu item).

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

- the **"+ Add automation..." button** (`AddAutomationRow`,
  `Source/UI/Timeline/AutomationLanes/AddAutomation/`). On an open track with no lane it is a row:
  `AddAutomationRow::kBaseHeight` (24 px) times the same vertical zoom, text-muted 10 px, indented like the lane
  headers, in the header column; it is the track's whole fold-out (`TimelineAutomationLanes::extraHeights()`), so its
  fold arrow is how the timeline starts automation, and the lanes region behind it takes no click. Once the track has a
  lane the button is compact (`setCompact`): a `kCompactSize` (14 px) "+" in the gutter left of the last lane header's
  stripe, centred on that lane's row, adding no height; it moves with the lanes in the reorder drag. It exists exactly
  while the track is open: any open ordinary track, and the Unassigned section while it holds lanes.
- the track header's right-click **"Add automation..."** ([tracks](tracks.md#row-context-menu)).  Once the
  lane lands the track opens and shows it, and the "+" moves to the new last lane.

The row is a Tab stop with the accent focus ring; Return and Space press it in the same event
(`juce::Button` posts its own Return click to the message queue and has no Space handling), its name and
tooltip read "Add automation to <Track>", and a click does not take focus off the clips.

**The picker** is the Mod Matrix's `ModMatrixPicker`, reused ([chrome](../layout/chrome.md)): a search
field over rows grouped under their module's title, Up/Down/Return/Escape, a `juce::CallOutBox` anchored
on the row or header. Two small generalisations: an item may carry `searchText` the row does not show (the
module title, so typing "filter cut" finds Cutoff under "Filter 1"), and a query now matches word by word,
every word anywhere in the row or its search text (the shared `synth::ui::searchMatches`); and
`setAccessibleNames` re-words its screen-reader names. A third, for the [add-modulator picker](#modulators): an item may carry a muted second line (`detail`, also searched) and be `enabled = false` (greyed, never highlighted or picked). `collectAddAutomationChoices` turns the host's
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

## Reordering lanes

Grab a lane header and drag it up or down: the lanes of that track slide aside and the lane lands in the slot it was
dropped on (`TimelineAutomationLanes::beginLaneDrag` and friends, `TimelineAutomationLanesReorder.cpp`). It is the track
list's gesture (the shared `ReorderDragSession`, see [animation](../layout/animation.md#reorder-drag)): a 4 px threshold, so a
click on the parameter name still opens the Change parameter picker (it now fires on release), the lifted header
drawn raised, 160 ms make-room and 140 ms settle, Esc gives everything back, and the animation lands at once when the
panel is not on screen. A lane never leaves its own track: the block is clamped to that track's lanes.

- A block is a lane row plus its modulator rows, which travel together. Only the header column animates; the curve
  editors and modulator bands follow the new order when the drop commits, as the clip lanes follow a track reorder.
- The order is `Track::lanes` order. A modulator's [amount lane](#amount-lane) is an ordinary entry of that list that has
  no row, so the drop passes the doc index of the visible lane whose slot it took (`TimelineDoc::moveLane(lane, index)`:
  the index is where the lane ends up, clamped; a move that changes nothing is not a mutation) and the hidden lanes keep
  their relative places. One undo step (`moveLaneOrderUndoable`); the order is saved and loaded with the track.
- **Keyboard:** Cmd+Alt+Up / Cmd+Alt+Down (`timelineMoveLaneUp` / `timelineMoveLaneDown`, rebindable) move the lane whose
  header controls or editor hold focus one visible slot, one undo step, the block gliding into its slot. The keys reach
  the lane before the record-mode combo (which would change its selection on Up/Down) and before the point keys.
  The lane keeps its header, its accessible names and its focus.
- Not done: the drag does not auto-scroll the track list (a track's lanes normally fit on screen), and the slot the
  lifted block leaves is not outlined with the track list's dashed marker.

## Keyboard order

Up/Down walk the track list as one column: a track's row, then each of its open lanes (a lane header, followed by its
modulator rows), then the next track's row, and back. Folded lanes are not stops, and the last stop's Down reaches
"+ Track". Stepping onto a lane or modulator row also makes its track the focused one, so the routing pane follows.
Where the arrows navigate: only when focus is on the row itself (a lane header, a modulator row, or the lane's "..." button,
whose key bubbles to the header). The record-mode combo keeps its own Up/Down, and Cmd+Alt+Up/Down still move the lane; any other modified
arrow is left alone. Each lane header is named "<Parameter> automation lane" for screen readers and shows the accent ring while
it holds focus. Tab from the timeline then goes to the routing pane and the scale pane
([focus regions](../control/focus-regions.md)). The panel's stop list is `TimelinePanelLaneKeyboard.cpp`.

## Right-click anywhere on a lane

A right-click opens the lane's own menu (the one the "..." button opens) with its top-left at the pointer
(`contextMenuOptionsAtPoint`, see [layout](../layout/layout.md)) on: the lane header (the value readout forwards its
clicks to it), the lane's curve editor wherever it is not a handle or the curve itself (those keep Delete point and
Hold/Linear), a [modulator row](#modulators) and a modulator's band (its curve editor included), where it opens the
row's menu (Show on canvas, Remove modulator). The editor asks its owner through `onLaneMenuRequested`, the band through
`onMenuRequested`; neither builds a menu itself. Shift+F10 (`openContextMenu`) opens the same menu from a focused header,
editor, modulator row or band (`KeyboardContextMenuProvider`). A direct-cable modulator band is decoration that takes no
clicks, so a right-click there still falls through to the clip lanes; its row in the header column has the menu.

## Change parameter and Duplicate

Both open the "+ Add automation..." picker (`buildAddAutomationPicker`) for the lane's track. It already leaves out
every parameter that has a lane, so a lane never ends up automating a parameter twice (`TimelineDoc` refuses it too).
The host resolves the pick to a `LaneTarget` (node uuid, parameter, index hint and the parameter's real range;
`TrackHeaderHost::prepareLaneTarget`). Both items are disabled, with the reason in their text, without a host or when
no parameter is free.

- **Change parameter...** (`TimelineDoc::retargetLane`, `retargetLaneUndoable`): the lane keeps its id, track, position,
  record mode and curve. Points are stored in the parameter's own units, so each value moves onto the new range by its
  position in the old one (a quarter of the way up stays a quarter of the way up); a lane with no points takes the new
  parameter's default as its constant. One undo step. The lane's modulators stay bound to the OLD parameter, because the
  graph is the truth: they leave the lane, and a modulator's amount lane whose routing no longer reaches a lane shows as
  an ordinary lane row.
- **Duplicate** (`TimelineDoc::duplicateLane`, `duplicateLaneUndoable`; Cmd+D on a focused lane editor or header, the
  `duplicateSelection` action): the picker opens FIRST and the copy is created only when a parameter is picked, directly
  below its source on the same track with the same points (rescaled like above) and record mode, in one undo step.
  Dismissing the picker changes nothing, so there is never a transient lane without a parameter.

`TrackHeaderHost::canChangeModulatorSource` / `changeModulatorSource` are the seam for a "Change source..." item on a
modulator row; the default host says no and the app does not implement it yet (it needs the remove, the connect and the
amount lane's re-key in one graph + timeline undo step).

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

**Add modulator...** (the lane's "..." menu) opens a picker, the Mod Matrix's `ModMatrixPicker` again (as
"+ Add automation..." does): a search field, then **New LFO** first and every LFO in the project after it
(`AddModulatorPicker`, `Source/UI/Timeline/AutomationLanes/AddModulator/`; the rows come from
`TrackHeaderHost::getLfoChoices`, a graph walk for `LFOModule` nodes). An LFO row's first line is its card
title, plus "inside macro Pads" when it sits in a macro; its second line is what it already moves ("moves
Pad oscillator detune", several joined with commas, or "not connected yet"). The search matches word by word
over the name, the macro and the targets. An LFO that already moves **this** parameter stays in the list,
greyed, with "already moves Cutoff" as its reason: it is not highlighted by Up/Down and a click or Return on
it does nothing. Up/Down/Return/Escape work as in the other pickers. The item stays disabled, as "Add
modulator... (no CV input)", on a parameter with no CV jack: a menu item has no tooltip, and the reason has to
reach a screen reader.

- **New LFO** asks `TrackHeaderHost::addLfoModulator`, which reaches `GraphEditor::addLfoModulator`
  (`GraphEditorModulators.cpp`): a real LFO card (Sine, synced at 1/4, bipolar, full level) placed beside the
  target card (left of it, else right; anti-overlapped by `resolvePlacement` at its estimated and then its
  real size), cabled from its output into the parameter's CV jack through `connectPorts` (so `addModRouting`
  inserts the hidden attenuverter), the attenuverter given a uuid and a depth of 0.5. When the target is a
  macro member the LFO joins that macro (`addSelectionToMacro`, after the cable exists, so the cable is
  interior and no port is minted); otherwise `makeRoomFor` runs on the new card. All of it is ONE
  `recordGraphAndMacroChange` step.
- **An existing LFO** asks `TrackHeaderHost::connectModulator`, which reaches
  `GraphEditor::connectExistingLfoModulator`: no new card; the same cable and depth, made inside
  `MacroGroupController::applyProgrammaticConnectionChange` (as the Mod Matrix and the mixer sends do), so an
  LFO and a knob in different macros are joined through macro ports. The cable, its depth and any ports are
  ONE undo step. The new modulator row appears through `getModulators`, which already looks through ports.

Test hook: `synth::ui::test_hooks::addModulatorPickerHookForTest` (receives the picker instead of a call-out).

**The row** (`ModulatorRow`, `Source/UI/Timeline/AutomationLanes/Modulators/`) is 54 px times the row zoom,
indented 28 px (one step further in than a lane header), three lines in the header column (title; shape and
rate; sync and amount):

- an `LFO` tag and a left stripe in the owning track's colour (`laneColourFor`, pushed to a readable contrast
  on the row's surface with `readableOn`), the LFO card's title, a **Sync** toggle and a "..." menu: **Show on
  canvas** (select the card and centre the canvas on it) and **Remove modulator**;
- the **shape** (Sine/Triangle/Sawtooth/Square/S&H/Custom) and the **rate** (a 1/1..1/32 combo while synced,
  a Hz bar while free-running);
- an **Amount** label and a signed readout in the track's colour ("+72%", "-30%" with a real minus sign,
  "0%"): the [amount lane](#amount-lane)'s value at the playhead when the routing has one, else the
  attenuverter's own `amount`. The amount is edited on the band, not in the row.

Any other source (an envelope, an envelope follower, a macro control...) gets a read-only row: a `CV` tag,
its card title and the amount readout; its band edits the amount exactly like an LFO's. A direct cable with no
attenuverter has no amount: no readout, and its band is a decoration.

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
runs -- all one undo step. The routing's [amount lane](#amount-lane) goes in the same step
(`MainComponent::removeModulator` wraps the graph edit and the lane removal in one
`recordGraphTimelineAndMacroChange`). The amount lane belongs to the routing, not to the LFO, so it goes even
when the LFO stays to drive another jack. A row
that triggers a removal is destroyed by the refresh before the host call returns, so it copies what it
needs and makes the call its last statement.

**Asking first.** A removal that would also delete the LFO (this routing is the last thing it moves) first
shows "Remove LFO 1?": "Cutoff is the last thing LFO 1 moves, so removing it also deletes LFO 1 and its
settings. Cmd+Z brings it back." (Ctrl+Z off macOS), a **Don't ask again** box, and **Remove LFO** (Return) /
**Cancel** (Escape). Cancel changes nothing. Confirming removes as one undo step; with the box ticked it also
turns the preference off. The question is `confirmRemoveLfo` (`Modulators/RemoveLfoConfirm.{h,cpp}`), asked by
`MainComponent::removeModulator` before it calls `performRemoveModulator`; an LFO with another destination
never asks. The preference is the user setting `timelineAskBeforeRemovingLfo` (default ON), toggled in
Settings, Preferences, Timeline ("Ask before removing an LFO's last destination") and read at use time.
Test hook: `synth::ui::test_hooks::removeLfoConfirmHookForTest`.

**Layout**: a lane's block is its row plus its modulator rows (`TimelineAutomationLanes::laneBlockHeight`,
the one helper every geometry function walks), so modulator rows count into the track's extra height and
move with the track. A track's modulators show whenever its lanes are open. Over the lanes region each row
is a `ModulatorBand`: the routing's [amount lane](#amount-lane) for a routing through an attenuverter; for a
direct cable a faint 28% band in the mod-wire colour that takes no clicks, so the clip lanes underneath decide
(and refuse) as they do for a lane row's backdrop).

### Amount lane

How much a modulator moves its parameter can change over the song. The band beside a modulator row is the
routing's **amount lane**: -100%..+100% around a dashed centre line, with "+100%", "0" and "-100%" at the left
edge. Above the line the modulator pushes the parameter its own way; below it, it is inverted; on the line it
does nothing. Sections of the song where the modulator should be silent are simply stretches at 0.

**Storage** (`ModulatorAmountLane.h/.cpp`): an ordinary automation lane on the routing's hidden attenuverter
(`nodeUuid` = `ModulatorInfo::attenuverterUuid`, `paramId` `amount`, range -1..1, default 0, Read mode), on
the track of the lane it modulates. The existing `AutomationApplier` plays it like any lane, so there is no
engine concept. It is found by `amountLaneFor(doc, attenuverterUuid)` wherever it sits (the doc-wide
one-lane-per-parameter rule) and written only by `writeAmountLane`, which creates it, replaces every point, or
removes it when there are no points, as one mutation sequence the caller wraps in one undo step.

**The lane is created lazily, and an empty amount lane never exists.** While the transport plays, the applier
writes an empty lane's range default (0) every block, which would silence the modulator. So with no amount
lane the band shows a flat line at the attenuverter's current amount (the knob value, `getNodeParameter`),
and the knob plays. The first Draw stroke (or Shift-line) or a double-click creates the lane together with its
points in ONE `recordTimelineChange` step; only the stroke's points are written (the kernel holds the edge
values outside it). Erasing the last point removes the lane in the same step, and the knob plays again.

**Gestures**: the band holds an ordinary `AutomationLaneEditor` that edits a private one-lane proxy doc
(`ModulatorBandEdits.cpp`). The proxy mirrors the real lane, or with none is an empty lane whose default is the
knob value, so the editor paints the flat line. Every gesture the editor commits to the proxy is written into
the real doc as one undo step; the proxy re-syncs from the real doc on every doc change and on the panel's
transport poll (asynchronously after its own commit: a doc is never edited from inside its own notification).
With a lane, every curve tool works as on any lane row. With no lane:

- **Select** (and every tool but Draw and Erase): a vertical drag on the band sets the knob, relative like a
  knob (the band's height is the whole 200% span), as ONE graph undo step (`setNodeParameter` Begin/Change/End,
  the step opening on the first move); **Up/Down** nudge it by 1%, **Shift** by 10%, each its own step;
  **double-click** creates the lane with one point at the snapped beat.
- **Draw**: the editor takes the press, and the stroke creates the lane.

**Hidden as a lane**: an amount lane whose attenuverter is in a routing into a lane in the doc is not drawn as
a lane row; it is that modulator row's band (`TimelineAutomationLanes::deriveRoutings` computes the set once per
sync and refresh, `isAmountLane`). It is matched by the attenuverter's uuid from the routing, never by the
`amount` parameter name alone (other modules have an `amount` too). It does not count in the track's
fold-arrow or badge lane count (`TimelineTrackHeaderComponent::setHiddenLaneCount`), the lane choices the
mixer's "Automate" entries use, or the track's row height. An amount lane something modulates itself stays a
lane row.

**When the routing goes on the canvas** (the cable, or the LFO with it, deleted there): the attenuverter node
is gone, so the amount lane is orphaned like any lane whose node is gone -- kept, never deleted -- and with no
routing to draw it as a band it shows as an ordinary lane row. Undoing the canvas delete hides it again.
Only the timeline's **Remove modulator** removes it, in the same step (above).

**Painting**: the curve and points in the owning track's colour (`setCurveColour`, drawn with `readableOn`
against the lane background); the bipolar guide (`AutomationLaneBipolarGuide.h`) is drawn by the lane editor
for every lane whose range straddles 0 -- the dashed centre line always, the percentage labels only for a
-1..1 range.

**Keyboard and screen reader**: the band is the one Tab stop (the editor inside it takes no focus and is not
in the accessibility tree; its press hands focus to the band), with the focus ring, named "<Parameter>
<modulator> amount" ("Cutoff LFO 1 amount"), a slider whose value reads "+50%" (settable while there is no
lane, read-only with one) and a tooltip ("Drag to set how much LFO 1 moves Cutoff; draw to change it over
time. Up/Down nudges it (Shift: by 10%)"). Escape cancels a stroke in flight.

**With the rest of the timeline**: a moved lane takes the amount lanes of its routings with it
(`amountLanesTravellingWith`; a routing drives exactly one parameter, so an amount lane is never shared).
Deleting the target lane leaves the modulators and their amount lanes in the graph and the doc; with no lane to
draw it under, the amount lane shows as an ordinary lane row.

### Migration from sections

Projects saved before amount lanes stored LFO **sections** (blocks of the song where the LFO was on) as a lane
on the LFO's own `level` parameter, Hold 1 inside a block and 0 outside. Opening such a project converts it
once, in the load path (`MainComponent::migrateSectionsToAmountLanes`, right after `moveLanesToOwningTracks` in
`loadBundleFromFile` and `loadAutosaveFromFile`, with the graph already built; never on a reconcile, never an
undo step, and the document opens clean). For each `level` lane the sections UI hid (the LFO modulates another
lane on the same track and nothing modulates the level lane): every attenuverter routing out of that LFO gets
an amount lane on the same track with Hold points on the same block edges, the routing's current amount
inside a block and 0 outside (`amountPointsFromSections`, from `sectionsFromPoints` in
`ModulatorSections.h`); then the level lane is removed and the LFO's level set to 1. **An LFO with any direct
cable keeps its level lane**, now shown as an ordinary lane row: a direct cable has no amount to carry the
blocks. A routing whose parameter has no lane gets its amount lane too (so the sound is unchanged); it shows
as an ordinary lane row until that parameter gets a lane of its own.

Every control is a Tab stop that a click does not take focus to, shows the accent focus ring, and is named
for what it controls ("Cutoff LFO shape", "Cutoff LFO rate", "Modulator menu for Cutoff LFO"), with the
same text as its tooltip. Test hooks: `refreshModulators`, `modulatorRowForTest`, `modulatorBandForTest`,
`modulatorRowBoundsForTest`.

## The curve canvas

`Source/UI/Timeline/AutomationLaneEditor.h/.cpp` (`synth::ui::AutomationLaneEditor`) edits ONE
`synth::AutomationLane`: one editor per open lane row. Its curve **and its points** are drawn in the owning
track's colour (`laneColourFor` -> `setCurveColour`, refreshed on every doc change, so recolouring or muting
the track recolours its lanes at once; text-muted on the Unassigned section). The colour is drawn through
`readableOn(colour, laneBackground)` (`TrackColour.h`): a palette colour too pale for the light theme's lane
background (amber, green) is darkened toward black, and on a dark theme lightened, until it reaches a 3:1
contrast ratio, keeping its hue. A point being dragged is accent, one the eraser has touched is error, and
every point has a 1 px outline in the lane background so it stays distinct where it sits on the line. Where
points crowd (a neighbour on either side closer than two handle widths along the time axis, as in a shape stamped at a
fine grid) their handles are not drawn, so a dense run reads as its curve; the handle under the pointer and
the one being dragged, scrubbed or erased are always drawn, every point is still hit-tested, and zooming in
brings the handles back (`AutomationLanes/AutomationHandleDensity.h`). Under
the Draw tool the lane shows the pen cursor ([edit-tools](edit-tools.md#tool-cursors)).

X is the SAME shared `TimelineViewState` the clip lanes use, so it lines up with the playhead
pixel-for-pixel; the piano roll is the one surface that maps beats through its own zoom and scroll
instead ([piano-roll](piano-roll.md#horizontal-mapping)). Y maps the lane's own `RangeSnapshot
[min..max]` linearly onto the component's height, top = max (`valueToY` / `yToValue`). The range sits `kPlotPadPx` (8 px, less on a very short lane) inside the top and
bottom edges, so a point or the line at the maximum or minimum is drawn whole instead of half or fully outside the lane.

The curve is sampled every ~2 px by building a local `TimelineSnapshot::Point[]` from the lane's
breakpoints and calling `AutomationKernel::evaluate` with a fresh `AutomationCursor`.

**Why it is re-derived on every repaint rather than cached.** Paint is not hot, and this is the
SAME evaluator the audio thread uses, so the canvas can never show a shape real playback would not
produce.

### Point value bubble, grab cursor and the flat-line drag

The point under the pointer, or being dragged, shows a value bubble above it (below it when the point is at
the top of the lane): `AutomationLanes/PointReadout/PointValueBubble.{h,cpp}`, owned by the editor and painted
last in its `paint()`, so it is not a child and never takes focus. It fades in 160 ms and out 110 ms through
an `AnimationDriver` (resumed from the current opacity when a fade is interrupted), lands at once under
Reduce Motion (`prefersReducedMotion()`) or off screen, and repaints nothing once settled. While a point is
dragged the bubble follows its previewed value; an exit mid-drag does not drop it. The text is the
parameter's own text for the value (`AutomationLaneEditor::valueToText`, which `TimelineAutomationLanes`
sets to `laneValueText`, the lane header's path through `TrackHeaderHost::getParameterValueText`), falling
back to the plain number. Later per-point features (header readout) extend
`PointValueBubble` and the editor's `updateHover()`.

Under the Pointer tool the grab hand (`dragGrabCursor()`, see
[the drag cursor rule](../layout/animation.md#drag-and-drop-cursor)) shows only once a drag has started: moving a point
or the selection, scrubbing the curve, or dragging an empty lane's flat line. Hovering a point, the curve or the box shows
the plain arrow. The Draw tool keeps its pen.

A lane with NO points plays its constant value (the range default). Pressing within a handle's reach of that
flat line and dragging vertically moves the value by the pointer's travel, clamped to the range, with the
bubble showing it live; nothing is written until mouse-up, which commits through
`TimelineDoc::setLaneConstantValue` as one undo step. Once the lane has a point the line is a curve and this
drag does not apply.

### Typing a point's value

Double-click a point (Pointer tool), or press Return while the keyboard cursor is on one (a click or Alt+Left/Right puts
it there), and a small field opens right of the point (left when there is no room) with the value text selected:
`AutomationLanes/PointReadout/PointValueField.{h,cpp}`, a `juce::TextEditor` child of the editor, glued in
`AutomationLaneEditorValueField.cpp`. Type a number and press Return to set it; Escape cancels and the keyboard focus
goes back to the editor. A double-click on a point never adds one; a double-click on empty space adds a point exactly as
before.

- **Parsing.** `AutomationLaneEditor::textToValue` (set by `TimelineAutomationLanes` to `laneTextToValue`) turns the text
  into a lane value through `TrackHeaderHost::getParameterValueFromText`, the inverse of `getParameterValueText`:
  `MainComponent` resolves the lane's parameter and uses its own text-to-value, so "-12" on a dB parameter is -12 dB.
  With no host, or a parameter that does not resolve, the text is read as a plain number with an optional unit ("-12",
  "-12 dB", "3.5%"; `PointValueField::parseNumber`) in the lane's own units. Text with no digit is refused before any
  parser sees it, because a parameter's text-to-value reads "abc" as 0. The result is clamped to the lane's range.
- **Commit.** Return changes only that point's value (beat, tension and curve are kept) through `commitPointEdit`: one
  undo step, none when the value is unchanged. With several points selected only the double-clicked point takes the
  value and the selection stays as it was.
- **Invalid text** keeps the field open, draws its text and outline in the theme's `error` colour and commits nothing;
  typing again clears it. Losing focus commits text that parses and discards text that does not, like the inline label
  editors. The field closes by itself when its point is removed (undo).
- **Escape and Return are handled in `PointValueField::keyPressed`**, before `TextEditor` posts them as command
  messages, so the field closes inside the key press and no listener callback ever hides or refocuses it (see
  [the inline label editors](../layout/chrome.md#inline-label-editors-and-accessibility)). The field is a permanent
  child that is hidden, never deleted, while closed.
- **Motion.** The field fades in 140 ms and out 100 ms through an `AnimationDriver` (no new timer), landing at once under
  Reduce Motion or off screen. The value bubble steps aside while it is open.
- **Accessibility.** The field is Tab/keyboard-reachable while open, has the accent focus ring (`paintFocusRing`), the
  accessible title "Value of <parameter> point" (`AutomationLaneEditor::laneLabel`) and a tooltip naming Return and
  Escape.

### Selecting points

Points select the way clips and notes do (same modifiers, same Escape), through `LanePointSelection`
(`AutomationLanes/PointSelection/`, owned by the editor; `NoteSelectionModel`'s shape keyed on the point's
beat, since a lane has one point per beat). The selection is runtime view state, never saved. A SELECTED point is
a filled accent dot; unselected points keep the curve's colour.

| Gesture | Result |
|---|---|
| Click a point | Select just it; a click on an already selected point keeps the group so it can be dragged |
| Shift, Cmd or Ctrl + click a point | Toggle it in the selection (never starts a drag) |
| Drag a selected point | Move every selected point by the same beat delta (snapped like one point, the block stops with its first point at beat 0) and the same value delta (each value clamped on its own); one undo step |
| Plain drag on empty space | Box select, replacing the selection; Shift/Cmd/Ctrl + drag adds to it |
| Click empty space (no drag) | Clear the selection; double-click still adds a point |
| Escape | Clear the selection (the key is consumed only when something was selected) |
| Delete / Backspace | Remove the selected points, one undo step |
| Cmd+A, Cmd+C, Cmd+X, Cmd+V | The app's Select All / Copy / Cut / Paste commands, routed to the focused lane editor through `EditSurface::AutomationLane`; Duplicate and Repeat are inactive there |
| Left / Right | Nudge the selection one grid step (a sixteenth with snap off) as a block; with nothing selected the first press selects the first or last point |
| Up / Down | Nudge every selected value by 1% of the lane's range (Shift: 10%) |
| Alt + Left / Right | Move the keyboard cursor to the previous or next point and select only it |

"On empty space" means away from every point, from the flat line of an empty lane and from the curve itself:
a press within a handle's reach of the curve still scrubs that segment's tension, so tension scrub and box select
do not overlap. A move, nudge, delete or paste is one `TimelineDoc::editBreakpoints` call under one
`recordTimelineChange` (`LanePointEdits.cpp`); the notification it fires prunes the selection to the points that
still exist, so the editor selects the moved or pasted beats after the edit returns. Paste lands at the transport
position, snapped, with the earliest copied point there and each value clamped to the lane's range; the pasted
points become the selection. The clipboard (`LanePointClipboard`) lives on `TimelineAutomationLanes`, so a copy
survives its editor being folded away and pastes into any lane. The selection survives a doc notification that keeps
the lane and the point, and clears when a point disappears or the editor is pointed at another lane
(`setActiveLane`, or the editor leaving the pool when its track folds).

Removal and return animate (`LanePointGlide`): when a doc change only removes points (a delete, a cut, a redo) they
shrink and fade out over 110 ms `easeInCubic` while the old curve cross-fades into the re-formed one; when it only
brings points back (an undo, a paste, an added point) they fade in over 160 ms `easeOutCubic` the same way. A move
is both a removal and an addition, so it lands at once, as does everything under Reduce Motion or off screen. The
editor follows the doc through `laneDocChanged()` (from `TimelineAutomationLanes::refreshPooled`) and lazily from
paint and the input entry points by `TimelineDoc::getRevision()`.

Each point is reachable from the keyboard: Tab into the editor, Alt+Left/Right walk the points, Delete removes the
one the cursor is on. The focus ring (`paintFocusRing`) sits on the cursor point, or around the lane when there is
none. The editor's screen-reader description is rewritten on every selection change (point count, how many are
selected, and for a single point its bar and beat and its value in the parameter's own text; with two or more
selected it also names the stretch keys) and announced as a title change. The nudge and cursor keys are fixed, like Delete and Escape on the piano roll; Cmd+A/C/X/V are the
existing rebindable commands. Return stays free.

### Stretching a selection

With two or more points selected under the Pointer tool, a thin accent box (`LanePointStretch`, padded 8 px so a handle
never sits on a point) surrounds them with a small square handle centred on each of its four edges; handles stay inside
the lane even when a point sits at its top or bottom. The box fades in over 160 ms and out over 110 ms on one
`AnimationDriver` (Reduce Motion or an off-screen editor lands at once; nothing repaints once settled).

| Gesture | Result |
|---|---|
| Drag the right handle | Scale the selected beats about the left edge (the leftmost selected beat stays), so four points spread out evenly when dragged right and squeeze when dragged left |
| Drag the left handle | The same about the right edge |
| Drag the top / bottom handle | Scale the selected VALUES about the box's opposite edge (lowest value for the top handle, highest for the bottom one), each clamped to the lane's range; dragging past the opposite edge flattens, never flips; a selection whose values are all equal has nothing to scale |
| Escape during the drag | Cancel: nothing changes and the editor consumes the key |
| Alt+Shift+Left / Right | Move the right edge one grid step (a sixteenth with snap off) left or right: squeeze or stretch, one undo step per press |
| Alt+Shift+Up / Down | Grow or shrink the selection's value range by 5% about its lowest value, one undo step per press |

The dragged EDGE snaps to the shared view-state snap (the others follow proportionally and are not snapped, so a
stretch keeps the points' relative spacing); no beat goes below 0, and the closest two selected points never come nearer
than 1/64 beat, so a squeeze cannot collapse points onto one beat. Growing is bounded and pushing: when the edge comes
within one grid step of the nearest unselected point beyond it, that point and every unselected point after it move
along, kept a grid step beyond the edge with their own spacing (a tiny gap with snap off); a left stretch stops when
the pushed block reaches beat 0, and squeezing never pushes. Unselected points between the selected ones stay put.

The drag previews in the editor's preview state like a move (curve and dots redraw from it; the doc is untouched) and
commits on mouse-up as ONE `editBreakpoints` through `commitPointEdit`, pushed points included, so one Cmd+Z restores
everything; a drag that ends where it began commits nothing. The selection afterwards is the same points at their new
beats. The value bubble is hidden for the whole stretch. A handle takes the press before any point, segment or box
select under it (reach 6 px). A press on a point inside the box selects and drags it as usual; a press anywhere else inside the box
(over the curve too) drags the whole selection like grabbing one of its points: the delta is the pointer's travel since
the press with the beat snapped at both ends, one `editBreakpoints`, one undo step, the same points stay selected. The
box is kept inside the lane, so a point at the far left or top does not cut an edge off. Handles show the horizontal or vertical resize cursor on hover and for the whole drag, and the editor's
tooltip reads "Drag to stretch the selected points in time" / "Drag to scale their values" over them. The math
(`stretchBeats`, `scaleValues`) is pure; the editor glue is `AutomationLaneEditorStretch.cpp`.

For later per-point features: `getPointSelection()` is the selection (`getSelected()` beats, `selectedPoints(lane)`
the breakpoints, `boundingBox(lane, mapper)` the box around them in editor coordinates, `getCursor()`), and the
editor's `onSelectionChanged` fires after the selection or cursor changed.

## Tools

`AutomationLaneEditor::Tool` stays the editor's internal enum; the lanes have no tool row of their
own and follow the timeline's edit tool ([edit-tools](edit-tools.md)) through one function,
`automationToolFor(EditTool, shiftDown, DrawShape)` (`AutomationLanes/AutomationToolMapping.h`):
Select → Pointer, Draw → Pencil (Line while Shift is held at mouse-down, or when the Line shape is
picked), Erase → Eraser, every other tool → Pointer. The periodic Draw shapes and the Range tool's
lane range are taken before the tool mapping is consulted ([Draw shapes](#draw-shapes-and-the-lane-range)). `TimelinePanelComponent::setActiveTool` fans the tool out to every editor
(`setEditTool`), and the editor re-reads Shift at each mouse-down.

| Tool | Gesture |
|---|---|
| Pointer | Drag a HANDLE moves it (the whole selection when it is selected, [below](#selecting-points)) — beat snapped via the shared view-state snap, value clamped to the lane's range; tension and curve carry over untouched. Drag a SEGMENT (on the curve, not a handle — hit-tested first) scrubs the segment's LEFT point's tension, ±0.01 per vertical pixel, clamped to `[-1, 1]`, following `AutomationKernel`'s own "shape comes from the LEFT point" contract. Double-click empty space adds a point at that (beat, value), Linear, tension 0 |
| Pencil | Freehand drag collects raw (beat, value) samples — no snapping, that is the point of freehand. On mouse-up they are thinned by `synth::AutomationRecorder::thinPoints` (reused, not re-implemented — its RDP helper is `public static` precisely so a second caller can reach it) at the SAME `kThinningEpsilonFraction` scaled to the lane's own range, and replace whatever existed inside the dragged beat span |
| Line | Drag previews a straight line from press to release; mouse-up replaces the dragged span with exactly the two snapped endpoints, Linear |
| Eraser | Drag removes every handle it touches — collected into a set as the pointer passes over them (dimmed in the preview), deleted on mouse-up |

Right-click a SEGMENT (on the curve itself) shows Hold/Linear, ticking the current one, routed through the headless
`applySegmentCurveChoice(beat, curve)` hook. Right-click a HANDLE shows `{Delete point}`. Right-click anywhere else
on the lane opens the lane menu ([below](#right-click-anywhere-on-a-lane)).

Escape clears in-flight tool-drag state (a box included) and returns `true`; when idle it clears a point selection
and returns `true`, and returns `false` when there is nothing to clear so the key falls through to the panel.

## Draw shapes and the lane range

The Draw tool puts down one of six shapes (`synth::ui::DrawShape`, `AutomationLanes/LaneShapes/DrawShape.h`):
**Free** (the freehand pen; Shift+drag still draws a straight line), **Line** (a straight line), and four
periodic shapes, **Sine**, **Triangle**, **Saw** and **Square**. The panel owns the shape next to the edit
tool and pushes it to every lane editor the same way (`TimelinePanelComponent::setDrawShape`), the
[amount lanes](#amount-lane)' editors included (`ModulatorBand::setDrawShape`), so a box stamp works on an LFO's
amount too and creates its lane like a first pen stroke. The lane range is for ordinary lanes only.

**The shape strip.** Six small icon buttons (`DrawShapeStrip`) slide out of the right side of the Draw button
while Draw is the active tool or a lane range is selected (whatever the tool, so Range-drag then one click on a
shape stamps it), and slide back when neither holds (`updateShapeStripShowing`): one `PanelSlide` on one
`AnimationDriver`, 160 ms `easeOutCubic` in and 110 ms `easeInCubic` out, retargeted from where it is on an
interruption, landing at once when the panel is not on screen. The transport row gives the strip a width
proportional to the slide (`layoutTransportRow`, re-run on every frame instead of the whole panel layout), and
the buttons ride on the strip's right edge so they come out from behind the Draw button. Closed, the strip is
hidden, so it is neither a hint target nor in the accessibility tree. Each button is titled "Sine shape", its
tooltip names its key ("Sine shape  (Shift+3)"), the active shape is lit like the active tool, and like the tool
buttons it never takes keyboard focus: its keyboard path is the shortcut.

**Keys.** Shift+1..Shift+6 (`timelineShapeFree`..`timelineShapeSquare`, rebindable) pick Draw and that shape,
and stamp it over the lane range when there is one. Pressing the Draw key again while Draw is the tool steps to
the next shape, wrapping after Square.

**The box stamp.** With a periodic shape, a drag on a lane draws a box: its x edges are the press and the
pointer, both snapped; its y edges are the swing (low and high value). The shape previews inside a dashed box
with a chip such as "8 cycles · 2 bars". One cycle is one snap step (`divisionBeats`), or one beat with snap
off. Esc cancels. On release every point inside the span is replaced by the generated shape in one
`editBreakpoints` call, one undo step.

**Generation** is a pure function, `generateShapePoints(shape, start, end, cycleBeats, lo, hi)`
(`LaneShapes/LaneShapeGenerator.h`): Sine is 16 Linear points per cycle starting at the middle going up;
Triangle is 2 points per cycle; Saw ramps up and drops through a Hold top point placed a sliver
(`sawDropBeats`, at most 1/960 beat) before the next cycle, because a lane's beats are unique; Square is two
Hold points per cycle. A closing Linear point sits exactly at the span's end, so the segment into any point
after the span keeps its meaning. `estimateShapePointCount` bounds the size first: a stamp that would take the
lane past `kMaxBreakpointsPerLane` is refused before anything is generated, with no undo entry, and the lane
shows why for a moment.

**The lane range.** With the Range tool, a drag on a lane selects a snapped beat span on that lane only
(`LaneRangeSelection`: one lane range at a time across every lane, owned by `TimelineAutomationLanes`). It is
painted like the clip lanes' range, a wash with accent edges. Esc clears it, and so does a press anywhere
else: on a lane with another tool, or anywhere in the clip lanes (which also covers starting a clip range).
Starting a lane range clears the clip range. Picking a tool keeps it, so Range-drag, then a shape (its key, or
Draw and its button) stamps over the span at the lane's full height (min to max); Line ramps from the curve's
value at the start to its value at the end; Free does nothing. Delete or Backspace removes the points inside
it. Each is one undo step, and the range stays for the next verb. Stamped points are ordinary breakpoints:
Select moves them, the eraser removes them.

`AutomationLaneShapeGesture` holds all of this for one editor. The editor forwards its mouse, key and paint
calls to it first, so the editor's own tools are unchanged.

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

`AutomationLanesReorderTests.cpp` (the header drag, Esc, click versus drag, the track boundary, Cmd+Alt+Up/Down, a rebind)
and `Tests/Timeline/TimelineDoc/TimelineDocLaneOrderTests.cpp` (`moveLane`, save and load) cover the reorder.

`Tests/UI/Timeline/AutomationLanes/AutomationLanesMenuTests.cpp` (right-click at the pointer on the header and the editor,
handle and segment keeping their menus, the menu key), `AutomationLanesRetargetTests.cpp` and
`AutomationLanesDuplicateTests.cpp` (the pickers, one undo step, no duplicate parameter, dismissal),
`AutomationLanesLaneMainTests.cpp` (the same against a real `MainComponent`, Cmd+D through its key handler) and
`Tests/Timeline/TimelineDoc/TimelineDocLaneRetargetTests.cpp` (the doc operations).

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
Amount lanes: `AutomationLanesAmountModelTests.cpp` (`writeAmountLane`, the readout text, the sections format
read back and turned into amount points), `AutomationLanesAmountBandTests.cpp` (real presses routed to the band
or its editor: the first stroke creating the lane in one undo step, the knob drag and keys as graph steps,
double-click, editing and erasing the last point, the bipolar picture, names and the accessible value) and
`AutomationLanesAmountIntegrationTests.cpp` (hidden as a lane, an orphaned lane row after a canvas delete,
Remove modulator with and without the LFO, Move to track, a saved project reopening, and a project saved with
sections inside a macro migrated on load); the row's readout is in `AutomationLanesModulatorRowTests.cpp`; the
applier driving an attenuverter's `amount` from a lane is in `AutomationApplierTests.cpp`. Draw shapes:
`LaneShapeGeneratorTests.cpp` (points per cycle, where each shape starts, the saw's drop, the square's holds,
partial cycles, the estimate), `AutomationLanesShapePaintTests.cpp` (the preview stroke read back from
rendered pixels mid-drag; crowded handles hidden, the hovered one drawn, all back and grabbable zoomed in) and
`AutomationLanesPointBubbleTests.cpp` (the value bubble on hover and drag with its text and fallback, the grab
cursor, the flat-line drag as one undo step, its clamp, and the editor's accessible name),
`AutomationLanesPointValueFieldTests.cpp` (double-click and Return opening the field with its text selected, a typed
value as one undo step keeping tension and curve, a unit, Escape, invalid text, the clamp, an unchanged value, empty
space still adding a point, the parameter's own parser, focus loss, one of several selected points, the field closing
when its point goes) and `AutomationLanesPointSelectionTests.cpp` (click, Shift-click, box, Escape, Select All, multi-drag with its clamps as one
undo step, Delete/Backspace, nudge, the keyboard cursor, copy/cut/paste, the selection following the doc, the accent
dot read back from pixels, the description) and `LanePointMathTests.cpp` (the selection set, rigid-block move,
replace, box test, clipboard and the glide's state machine); the surface routing of Cmd+A/C/X/V is in
`FocusArbitrationAutomationLaneTests.cpp`,
`AutomationLanesShapeTests.cpp` (a sine box at 1/4 snap over a bar is four
cycles in one undo step, snap off is a cycle per beat, the chip, Esc, the strip shown only with Draw, Shift+3,
Draw again stepping shapes, the lane range and a click elsewhere clearing it, a shape button and a shape key
stamping over it, Line ramping on it, Delete, the point cap refusal, stamped points edited with Select,
double-click and the eraser).

## The marker beside an automated control

A knob or fader that has a lane shows a small marker at its top-left corner (a short line with a point at each end),
on the module card and on the mixer, so automated parameters stand out without opening the timeline. It follows the lane
through add, remove and undo, fades in and out, and its tooltip and accessible description say "Automated". Details in
[`layout/module-card.md#automated-marker`](../layout/module-card.md#automated-marker).
