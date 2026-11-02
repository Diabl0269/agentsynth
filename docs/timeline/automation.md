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
and refuse clicks there (`TimelineClipLaneArea::hitTest`). A lane row and each mod lane row can also be resized on its own ([lane-controls](lane-controls.md#resizing-a-lane)).

`TimelineAutomationLanes` (`Source/UI/Timeline/AutomationLanes/TimelineAutomationLanes/`) is the
panel's collaborator for all of it: the fold sets, the geometry it hands the layout
(`extraHeights()`, `rowHeightOverrides()`), and one lane header plus one `AutomationLaneEditor` per
visible lane, **pooled by `LaneId`**. A doc change that keeps a lane keeps its header and editor (an
in-flight gesture and keyboard focus survive the notification); only a lane that leaves the screen
loses them. The editors live in one click-through container over the lanes region (above the clip
lanes and the piano roll, below the playhead), placed at their row's y minus the vertical scroll and
clipped by it; the container hides while the piano roll is open.
The header column's "Add Track" strip is exactly as tall as what sits above the clip rows (the ruler, plus the roll's
toolbar while it is open), so a track header, its lane headers and their rows all start at the same y and have the same
height as the rows they label.

An open track with no lanes shows a 24 px "+ Add automation..." row, zoom-scaled like a lane row; once it has a
lane, that button shrinks to a small "+" in the empty gutter left of the last lane header's colour stripe, so no
empty row is left under the lanes ([below](#adding-a-lane-from-the-timeline)).

### The lane header

`AutomationLaneHeaderComponent` (`Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/`), indented
16 px under the track with the track's colour as a 4 px stripe at 45% alpha:

- a 14 px **marker icon** (`LaneMarkerIcon`, `Source/UI/Timeline/AutomationLanes/LaneHeaderParts/`): points on a line in
  the accent colour (`Glyph::LaneAutomation`), tooltip "Automation lane: drawn changes over time". It is display only
  (not a Tab stop, hidden from screen readers; the header's name "Cutoff automation lane" carries the meaning) and its
  tooltip is an ordinary info tooltip, so Preferences > "Show info tooltips" off silences it. It hands its mouse events
  to the header, so a press on it still drags the lane;
- the parameter name (11 px semibold, a real button, below) over the module name (9.5 px, muted, indented to sit under
  the name), from `TrackHeaderHost::getParameterDisplayName` / `getNodeDisplayName` with the old picker's fallbacks
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
- a strip of **control chips** on the bottom line, ending in a "+" button ([below](#the-control-chips)). The record mode
  is its Read / Write chip: a menu of Off/Read/Touch/Latch/Write (ids = `LaneRecordMode` + 1, Write's chip in the error
  colour) writing `TimelineDoc::setLaneRecordMode` as one undo step — a pick IS a user gesture, unlike
  `AutomationRecorder`'s own Write-drops-to-Touch-on-stop call.;
- a lane menu: **Add modulator...** ([below](#modulators)), **Change parameter...** and **Duplicate**
  ([below](#change-parameter-and-duplicate)), **Move to track** (every other MIDI/Audio
  track, `TimelineDoc::moveLaneToTrack`; the [amount lanes](#amount-lane) of the lane's modulators move with it) and
  **Delete lane** (`TimelineDoc::removeLane`), each one undo step, its block growing in or shrinking away ([lane motion](lane-motion.md)). There is no "..." button: a
  right-click anywhere on the lane ([below](#right-click-anywhere-on-a-lane)), Shift+F10 and Return on the focused
  row open it, and the header's tooltip says so (the space the button took goes to the value readout);
- **A click on the header** anywhere that is not a control (the marker and the value readout count as the header) gives
  the row keyboard focus on release (accent ring; Up/Down, Return and Cmd+Alt+Up/Down then work as for any focused
  header) and opens no picker or menu; a press that became a reorder drag focuses nothing.
- the **name button** (`LaneNameButton`, a `juce::Button`): a Tab stop with the shared accent ring, a border in the accent
  colour while the pointer is over it, named "Change parameter of Cutoff", tooltip "Pick a different parameter". A click
  on it (on release, so a press may still become a drag) and Return or Space on it open the Change parameter picker, with
  its search field. The button takes no mouse events of its own: the header handles the press (so the drag still starts
  on the name) and tells the button when it is hovered; the header's `getTooltip` answers the button's tip over the
  name. The pointing-hand cursor stays.

The edits live in `AutomationLaneActions` as free functions: a Delete or Move destroys the header
that asked for it during the doc notification, so the header copies what it needs and makes the
edit its last statement. Every control is a Tab stop with a title and tooltip ("Read / Write: Read or write automation
(Read)") and the shared focus ring; the row itself is named "Cutoff automation lane".

### The control chips

The bottom line of a lane header (and the title line of a mod lane row) carries a strip of small icon buttons, each one
an action of the lane menu (Read / Write, Add mod, Change parameter, Mute, Duplicate, Move to track, Create custom LFO,
Remove lane; on a mod lane Change source, Show in canvas, Remove modulator), then a "+" button. The set shown is the
user's, one per lane kind, kept in the user settings; the rest are listed in the "+" row, and Edit controls opens a tray
under the header to show, hide and reorder them. Everything about them is in
[`lane-controls.md`](lane-controls.md); the right-click menu is unchanged.

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
click on the parameter name still opens the Change parameter picker (it fires on release), the lifted header
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
  the lane before the control chips and the point keys.
  The lane keeps its header, its accessible names and its focus.
- **Modulator order.** The modulator rows of one lane have an order of their own, stored with the lane
  (`AutomationLane::modulatorOrder`, below), moved with the same keys and by their own drag: see [Modulators](#modulators).
- Not done: the drag does not auto-scroll the track list (a track's lanes normally fit on screen), and the slot the
  lifted block leaves is not outlined with the track list's dashed marker.

## Keyboard order

Up/Down walk the track list as one column: a track's row, then each of its open lanes (a lane header, followed by its
modulator rows), then the next track's row, and back. Folded lanes are not stops, and the last stop's Down reaches
"Add Track". Stepping onto a lane or modulator row also makes its track the focused one, so the routing pane follows.
Where the arrows navigate: only when focus is on the row itself (a lane header or a modulator row). Return on a row opens its menu. Up/Down on a control chip bubble to the row like any other key, and Cmd+Alt+Up/Down still move the lane; any other modified
arrow is left alone. On a modulator row Left folds it to its title line and Right opens it (as a tree view does), and
Cmd+Alt+Up/Down (the same rebindable `timelineMoveLaneUp` / `timelineMoveLaneDown`) move that modulator one slot among its
lane's modulators, from the row, any of its controls or its band; see [Modulators](#modulators). Each lane header is named "<Parameter> automation lane" for screen readers and shows the accent ring while
it holds focus (a click on the header takes focus, see [the lane header](#the-lane-header)); a modulator row is named
"<Source> mod lane on <Parameter>" and shows the same ring. The name on each is a Tab stop of its own (Return or Space opens
its picker). Tab from the timeline then goes to the routing pane and the scale pane
([focus regions](../control/focus-regions.md)). The panel's stop list is `TimelinePanelLaneKeyboard.cpp`.

## Right-click anywhere on a lane

A right-click opens the lane's own menu with its top-left at the pointer
(`contextMenuOptionsAtPoint`, see [layout](../layout/layout.md)) on: the lane header (the value readout forwards its
clicks to it), the lane's curve editor wherever it is not a handle or the curve itself (those keep Delete point and
Hold/Linear), a [modulator row](#modulators) and a modulator's band (its curve editor included), where it opens the
row's menu (Show on canvas, Change source..., Remove modulator). The editor asks its owner through `onLaneMenuRequested`, the band through
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
  parameter's default as its constant. The lane's modulators go with it: every routing into the old parameter's CV
  jack is cabled into the new one's at the same depth, its amount lane keeps its id, track and points (re-keyed to
  the new routing's Attenuverter) and the old routing is cut. One undo step for the lane and its modulators together.
  A parameter with no CV jack (a choice, say) still takes the lane, and the modulators stay on the old parameter; so
  does a modulator whose source already drives the new parameter's jack.
- **Duplicate** (`TimelineDoc::duplicateLane`, `duplicateLaneUndoable`; Cmd+D on a focused lane editor or header, the
  `duplicateSelection` action): the picker opens FIRST and the copy is created only when a parameter is picked, directly
  below its source on the same track with the same points (rescaled like above) and record mode, in one undo step.
  Dismissing the picker changes nothing, so there is never a transient lane without a parameter. The copy gets its
  own modulators: the same sources at the same depths into the new parameter's jack, each with a copy of the amount
  lane's points; the original keeps its own. Same no-CV-jack exception, still one undo step.

Both go through the host (`TrackHeaderHost::retargetLaneWithAttachments` / `duplicateLaneWithAttachments`,
`MainComponentLaneSubtree.cpp`), which wraps the lane's doc edit and `MainComponent::carryLaneAttachments(from, to,
track, Move|Copy)` in one graph + timeline undo step. That function is the one place that knows what hangs off a
lane (today the modulator routings, via `moveModulatorRouting` / `copyModulatorRouting`); a new kind of lane
attachment adds its branch there and is carried by both items. A host that cannot (a bare one) falls back to the
doc-only `retargetLaneUndoable` / `duplicateLaneUndoable` when the lane has no modulators.

A modulator row's **Change source...** (see [Modulators](#modulators)) is the same idea for the source of a routing.

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

- a 4 px left stripe in the owning track's colour at 45% alpha (`laneColourFor`, pushed to a readable contrast on the
  row's surface with `readableOn`), a 14 px **marker icon** (a violet wave, `Glyph::LaneMod`; tooltip "Mod lane: an LFO
  moves this parameter", "...an envelope..." for an envelope source, "...a modulator..." for any other; an info tooltip
  like the lane header's, display only, hidden from screen readers), the source card's title as a **name button** and,
  at the right end of that line, a small picture
  of the LFO's shape (`ModulatorShapeIcon`: sine, triangle, sawtooth, square, a staircase for sample and hold, and a
  curve with dots for custom; the glyphs are `Glyph::ShapeSine` .. `ShapeCustom`). The picture is display only, named
  and tooltipped "Sine shape" and so on, and cross-fades over 160 ms when the shape changes (at once while the row
  is not showing). The row has no "..." button: its menu, **Show on canvas** (select the card and centre the canvas on
  it), **Change source...** and **Remove modulator**, opens on a right-click anywhere on the row (the picture included), Shift+F10 and
  Return on the focused row. A **Sync** toggle sits on the third line. The row is inset like a lane header
  (`ModulatorRow::kIndent` = 16, the stripe, then the same 4 px padding), so its marker and name line up with the lane's
  above it, on the normal surface; the fold arrow sits in the gutter left of the stripe. The name button is named
  "Change source of LFO 1 on Cutoff", tooltip "Pick a different source", and a click on it or Return/Space opens the Change
  source picker (below); it is disabled, with the tooltip "This source cannot be swapped here", when the host cannot
  re-point the routing. A click on the row anywhere else that is not a control focuses the row on release (a drag from
  anywhere, the name included, still reorders or moves it and focuses nothing). The row is named "LFO 1 mod lane on Cutoff";
- the **shape** (Sine/Triangle/Sawtooth/Square/S&H/Custom) and the **rate** (a 1/1..1/32 combo while synced,
  a Hz bar while free-running);
- an **Amount** label and a signed readout in the track's colour ("+72%", "-30%" with a real minus sign,
  "0%"): the [amount lane](#amount-lane)'s value at the playhead when the routing has one, else the
  attenuverter's own `amount`. The amount is edited on the band, not in the row.

A **fold arrow** (`TrackFoldArrow`, the track header's arrow) sits at the left of the title line, a Tab stop that
Return, Space or a click toggles; it is named and tooltipped "Collapse LFO 1 on Cutoff" / "Expand LFO 1 on Cutoff", the
tooltip adding the key ("Left arrow" / "Right arrow" on the row). A folded row is one title line tall (a third of the row,
times the row zoom): the title and the amount readout stay (the readout moves up to the right end of the title line),
the shape picture and the shape, rate and sync controls are hidden, and its band shrinks with it, the rows and lanes
below closing up. Left on a focused open row folds it, Right on a folded row opens it. Fold state is runtime-only, never
saved: `TimelineAutomationLanes` keeps a set of folded routing keys (`ModulatorInfo::key()`); a routing replaced in
place keeps its fold ([row identity](modulator-row-identity.md)), a gone one's key is dropped. The height change is a 160 ms
`easeOutCubic` tween driven by `TimelineAutomationLanes` (the row shrinks to, and grows from, its title line; the controls
leave when it lands and return as it starts); it lands at once when the timeline is not showing, under Reduce Motion or
with Animations Off ([Motion rules](../layout/animation.md#motion-rules)).

**Order.** A lane's rows are in graph order until moved. Cmd+Alt+Up / Cmd+Alt+Down (`timelineMoveLaneUp` /
`timelineMoveLaneDown`, rebindable) on a focused modulator row, one of its controls (a combo or bar would take the arrow
itself, so the row's key listener is offered it first) or its band (where a bare Up/Down nudges the amount) move that
modulator one slot up or down among the modulators of the SAME lane; the first row's Up and the last row's Down do
nothing but still consume the key. One undo step (`setModulatorOrderUndoable`). The order is
`AutomationLane::modulatorOrder`, a list of routing keys (`modulatorOrderKey`: the routing's Attenuverter uuid, else
`ModulatorInfo::key()`) saved with the lane as an additive `modulatorOrder` array (absent = empty = graph order), copied by
Duplicate and kept by Change parameter. The panel sorts the lane's routings by it (`sortedByModulatorOrder`): listed keys
first in their stored order, every other routing after them in graph order; a stale key is ignored, never pruned. The
move computes the new order from the lane's rows as shown now and writes it through
`TimelineAutomationLanes::moveModulatorInOrder(lane, key, newIndex)`, the one function a later drag will also use. A
reorder keeps the same row and band objects (they are moved to their new slots, not rebuilt), so keyboard focus stays on
the moved row. The moved row, the row it swaps with and their bands slide from their old y to the new slot (160 ms `easeOutCubic`,
`TimelineAutomationLanes::glideModulatorRows(lane, oldYs)`, which a drag of modulator rows can reuse); under Reduce
Motion, Animations Off or with the timeline not showing they land at once. Left and Right fold the row only when the
row itself has focus, never from one of its controls.

**Drag.** Grab a modulator row's title or an empty part of it (a press on a control never starts a drag; a click without
4 px of movement is a plain click) and drag it. It is the lane reorder's gesture on the shared `ReorderDragSession`
(`TimelineAutomationLanesModulatorDrag.cpp`; 4 px threshold, lifted row drawn raised under the grabbing-hand cursor,
160 ms make-room, 140 ms settle, Esc gives everything back, the animation lands at once when the panel is not showing);
the row's band follows it. A line at the top of the slot marks where the drop lands, fading in and gliding between
slots (`DropIndicator`).
- *Within its lane* the other rows slide aside and the drop writes the new order through `moveModulatorInOrder`, the one
  function the keyboard move uses: one undo step, the same row and band objects.
- *Onto another lane* (any lane block of any open track; the slot is the gap between that lane's rows the pointer is at,
  or right under the lane row when it has none) the modulation moves to that lane's parameter. The pointer's lane is the
  drop target and `TrackHeaderHost::canMoveModulatorToLane(info, lane)` decides if it is valid: the parameter has a CV
  jack and the jack does not already have this source. Over a lane that fails that, or over no lane, the drop is "not
  here": no line is drawn, the lifted row dims, and releasing changes nothing (the row glides back).
  `TrackHeaderHost::moveModulatorToLane(info, lane, index)` (default false) is ONE
  `recordGraphTimelineAndMacroChange`: `MainComponent::moveModulatorRouting` cables the source into the new jack at the
  old depth, re-keys the amount lane to the new Attenuverter (same lane id, track and points) and cuts the old routing;
  then the destination lane's `modulatorOrder` gets the new routing's key at `index` and the old key leaves the order of
  every lane on the old jack. One Cmd+Z puts the routing, the amount lane and both orders back. The moved routing has a
  new key, so its row fades and grows in under the destination lane like any new routing ([lane motion](lane-motion.md)).
- Change parameter and Duplicate carry the lane's rows as shown (`shownModulatorOrder`) into its `modulatorOrder`, each
  carried routing's old key replaced by its new routing's key at the same position (`remapModulatorOrder`).
- The keyboard path is unchanged (Cmd+Alt+Up/Down within the lane); the row's tooltip says "Drag to reorder or move to
  another lane". The row's old gap closes on the drop as it grows in where it landed. Not done: no auto-scroll while dragging.

Any other source (an envelope, an envelope follower, a macro control...) gets a read-only row: the marker, its card
title and the amount readout; its band edits the amount exactly like an LFO's. Its menu is the same
(Show on canvas, Change source..., Remove modulator), and Change source... is offered for an envelope routed through
an Attenuverter just as for an LFO. A direct cable with no
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

**Change source...** (`TrackHeaderHost::canChangeModulatorSource` / `changeModulatorSource`, implemented by
`MainComponent`) points the routing at a different LFO or envelope and keeps the drawn [amount lane](#amount-lane). The host says
yes only for an LFO or an envelope (decided by the mod-source catalogue's group) routed through an Attenuverter (the
amount lane is keyed by that Attenuverter; a hand-patched direct cable has neither). The item opens a picker
(`TrackHeaderHost::getChangeSourceChoices`: every LFO and envelope output, macro ports skipped) under the group headers
"LFOs" and "Envelopes", the current source (same card and same output) ticked and disabled rather than hidden, a source
that already moves the parameter listed greyed out, and last a "New" group with **New LFO** and **New envelope** (a
green plus before the text, a "New" tag at the right; `ModMatrixPicker::Item::ticked` / `isNew`). The search placeholder is
"Search or create a source"; the New rows stay in the list, last, whatever is typed (Return picks the best existing
match, Down reaches New). The item is always enabled. "Add modulator..." stays LFO-only. A New pick calls
`TrackHeaderHost::changeModulatorSourceToNew(info, typeName, channel)` (type and channel from `newModuleSources()`:
"LFO" and "ADSR"): `MainComponent::repointModulator` is the recipe below with the source made by
`GraphEditor::addModulationSourceModule(..., recordUndo=false)` beside the target card, so create + cable + amount-lane
re-key + cut of the old routing are still ONE undo step. The new routing takes the old one's slot in the order of every
lane on the jack and the row re-binds in place ([row identity](modulator-row-identity.md)). On a pick
(the chosen output's channel comes from the catalogue, `LfoChoice::sourceChannel`), ONE
`recordGraphTimelineAndMacroChange` step cables the new source in at the old routing's
depth (`GraphEditor::connectModulationSource` with `recordUndo=false`, macro ports crossed as for "Add modulator..."),
re-keys the amount lane to the new routing's Attenuverter with `TimelineDoc::rebindLane` (same lane id, track and
points), and cuts the old routing (`removeModulator(..., removeLonelySource=false, recordUndo=false)`). There is no
confirm dialog because nothing is deleted: the old source stays on the canvas, even when this was its last cable.
Cmd+Z restores the old source, the lane's old key and the old order together, and the same row swaps back.

**Remove modulator** (`GraphEditor::removeModulator`) removes the routing (the attenuverter chain as a
whole, or a direct cable's edges), sweeps the macro ports the cable leaves empty, and removes the LFO too when
no other cable leaves it, through `requestDeleteModule(..., recordUndo=false)` so every pre-removal unbind
runs -- all one undo step. The routing's [amount lane](#amount-lane) goes in the same step
(`MainComponent::removeModulator` wraps the graph edit and the lane removal in one
`recordGraphTimelineAndMacroChange`). The amount lane belongs to the routing, not to the LFO, so it goes even
when the LFO stays to drive another jack. A row
that triggers a removal is destroyed by the refresh before the host call returns, so it copies what it
needs and makes the call its last statement.

**Asking first.** A removal that would also delete the LFO (this routing is the last thing it moves) first asks
"Remove LFO 1?" with **Remove LFO**, **Keep module**, **Cancel** and a **Remember my choice** box, from every way of
removing a modulation (`MainComponent::removeModulator`; the question is `confirmRemoveLfo`). An LFO with another
destination never asks. Details, the stored preference and its reset: [remove-last-modulator](remove-last-modulator.md).
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
  the step opening on the first move). From that first move the cursor is the grabbing hand and a value bubble
  (the amount, "+50%") floats at the pointer, as when dragging an empty lane's flat line; both go on release, and **Esc** puts
  the value back and ends the drag; **Up/Down** nudge it by 1%, **Shift** by 10%, each its own step;
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
for what it controls ("Cutoff LFO shape", "Cutoff LFO rate", ), with the
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

### Selecting, moving and stretching points

Point selection, the keyboard and glide moves, Cmd+D and the stretch box have their own doc: [`automation-points.md`](automation-points.md).

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
| Pointer | Drag a HANDLE moves it (the whole selection when it is selected, [points](automation-points.md#selecting-points)) — beat snapped via the shared view-state snap, value clamped to the lane's range; tension and curve carry over untouched. Drag a SEGMENT (on the curve, not a handle — hit-tested first) scrubs the segment's LEFT point's tension, ±0.01 per vertical pixel, clamped to `[-1, 1]`, following `AutomationKernel`'s own "shape comes from the LEFT point" contract. Double-click empty space adds a point at that (beat, value), Linear, tension 0 |
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

**The pen and its shape flyout.** The Draw tool button (`DrawPenButton`) shows the icon of the current shape, so it
always shows what the tool will draw, and a small chevron (the app's combo chevron, in the glyph's ink) in its bottom-right corner says it opens more. When the
shape changes the icon crossfades from the old to the new (`AnimationDriver`, `motionMs(160, 80)`: instant under
Animations: Off and when the panel is not on screen). A click on the corner (the bottom-right 10 x 10 px), pressing
and holding the button for 400 ms (it opens on the hold, without waiting for the release, and that release does not
also pick the tool), a right-click on the button (it never picks the tool), or the shape-menu key opens the flyout; a
click anywhere else on the button just picks Draw. The transport row has the same layout whatever the tool.

The flyout (`DrawShapeFlyout`) is a `juce::CallOutBox` under the button with six rows: the shape's icon, its name and
its shortcut right-aligned (read through `shortcutHintFor`, so a rebind shows). The current shape's row is lit. It
enters with `CalloutReveal` (the plain `PopupMotion` fade under Reduce Motion; at once under Animations: Off) and
closes only through `PopupMotion::dismissCallOut`. Picking a row (click, Enter or Space) closes it and calls
`TimelinePanelComponent::pickDrawShape`, which sets the shape, switches to Draw and stamps it over a lane range if
one is selected, so Range-drag, then a pick from the flyout, stamps in one go. Keyboard: focus lands on the current
shape's row; Up/Down move (wrapping), Home/End jump, Escape closes. Each row is a Tab stop with the accent focus
ring, is titled "Sine shape" and has a tooltip naming its key ("Sine shape  (Shift + 3)"). The Draw button's tooltip
names the flyout key beside the tool's own ("Draw  (8) · shapes: Shift + 8"). Headless tests take the flyout
through `test_hooks::drawShapeFlyoutHookForTest()` instead of a CallOutBox.

**Icons.** Every icon is a stroke path in a 24 x 24 box with a 1.8 px round stroke
(`LaneShapes/DrawShapeIcons.h`, shared by the pen and the rows). Free is a pen drawing a wobbly line, Sine the clean
wave; the other shapes are the line and the three stepped waves.

**Keys.** Shift+1..Shift+6 (`timelineShapeFree`..`timelineShapeSquare`, rebindable) pick Draw and that shape,
and stamp it over the lane range when there is one. Shift+8 (`timelineShapeMenu`, "Open shape menu", rebindable)
opens the flyout; it only opens it. Pressing the Draw key again while Draw is the tool steps to the next shape,
wrapping after Square.

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

## Create custom LFO from a range

Right-click a lane that holds the lane range, or two or more points selected with the Select tool (its header, or the
curve editor away from a handle), and the lane menu's first item, **Create custom LFO**, (also offered by a right-click
inside that stretch on the curve editor, see [lane controls](lane-controls.md#create-custom-lfo-from-the-range)) turns the drawn movement inside the range into a NEW LFO whose Custom wave is that
shape, so playback sounds the same and the LFO can then be reused elsewhere by drawing its amount band over other bars.
It always adds an LFO (a lane that already has modulators gets another row). One Cmd+Z removes the LFO, its row, its
amount lane and puts the lane's points back; Redo re-applies all of it, the wave included (it rides the node's saved
state).

- **Which stretch.** The Range tool's selection on the lane if there is one, otherwise the points selected on the lane
  with the Select tool (click, Shift-click or a box) once there are two or more: from the first to the last selected
  beat (`AutomationLaneEditor::pointSelectionSpan`, read by `TimelineAutomationLanes`'s `laneRangeProvider`). One point
  or none is no stretch and the item says "select a range first". Everything below is the same for both, and the
  Range selection wins when a lane has both.
- **What it makes.** An LFO on Custom, Sync, Retrig off, unipolar. Its rate is the smallest of the LFO's straight rates (1/128
  up to 8/1, i.e. 8 bars; the triplets and dotted values are never chosen) that fits the range, so 1 bar is 1/1, 3 beats is also 1/1 (the drawn part fills the first
  three quarters of the cycle and the last value holds to the end), 2 bars is 2/1. Its Level is the drawn movement's
  height in the parameter's own normalised range (skew included), rounded up to the knob's 0.01 step with the wave drawn
  that much lower so the product is exact, and the lane is flattened inside the range at the
  lowest value the curve reaches, so base plus the LFO equals the drawn curve. Its Phase offset is `frac(-start /
  cycleBeats) * 360` degrees, so under the song-locked phase of a synced LFO the cycle begins on the range's first
  beat. The routing's depth is full and its amount lane is 0 outside the range and +1 inside.
- **The wave.** It starts from the range's breakpoints (a Hold segment becomes a step: two points at one `x`, which
  `LfoCustomWave` reads right-continuously) and, wherever a straight line between two neighbours strays from the curve
  by more than 1/10000 of the parameter's range (a segment with tension, a skewed parameter), adds readings of the curve
  there, up to the wave's 64 points (63 plus the tail point when the range is shorter than the cycle). When there are
  more breakpoints than fit, it reads the curve at evenly spaced beats instead, which rounds off any steps; Level and the
  base value always come from every breakpoint in the range, so a dense wiggle is never called flat.
- **The lane.** Inside the range every breakpoint goes and one Hold point at the range start sits at the base value; a
  Hold point 1e-6 beats before the start keeps a sloped segment arriving at the range as it was, and the point at the
  range end takes up the curve in the shape of the segment it was in (a point already there is put back as it was).
  A tension on the segment around the end is kept but its curve restarts at the end.
- **Disabled, and why.** The item stays in the menu and says why in its own text: "(select a range first)" (no range, or
  the range is on another lane), "(no CV input)" (the parameter has no CV jack), "(range longer than 8 bars)", "(range
  is flat)" (the curve moves less than 1/10000 of the parameter's range), "(lane has too many points)".
- **Motion.** The old curve melts into the flat line: the editor keeps the old shape and cross-fades it out over the
  range (200 ms, 80 ms under Reduce Motion, at once under Animations Off or off screen) while the old points shrink away,
  repainting only the range's rectangle. The new modulator row and band fade and grow in like any new routing, and a row whose routing goes (Undo) fades out in a closing gap where it stood (`TimelineAutomationLanes::retireModulatorRows`; dropped at once off screen or under Animations Off).

The math is `CustomLfoFromRange` (`LaneShapes/`, pure, one plan shared by the menu's enabled state and the command);
the command is `TrackHeaderHost::createCustomLfoFromRange`, implemented by `MainComponent` as one
`recordGraphTimelineAndMacroChange` around `GraphEditor::addLfoModulator`, the LFO's settings, the amount lane
and `TimelineDoc::editBreakpoints`; `TimelineAutomationLanes` arms the lane's melt first and spends the range after.

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
and `Tests/Timeline/TimelineDoc/TimelineDocLaneOrderTests.cpp` (`moveLane`, save and load; the saved, copied and kept
`modulatorOrder`) cover the reorder. `AutomationLanesModulatorFoldOrderTests.cpp` covers the modulator rows' fold (a real
click on the arrow, Return/Space, Left/Right, the one-line height and the band following, the hidden controls, the names,
the tween and Reduce Motion) and their order (rows sorted by the stored order, Cmd+Alt+Up/Down from the row, a control and
the band, one undo step, the same row kept, the ends, a rebind). `AutomationLanesModulatorDragTests.cpp` covers the row drag
(a real press and drag on the row: reorder within the lane as one undo step, Esc, a click under 4 px, the drop line, a lane
that cannot take the row, the host call on a drop onto another lane) and, against a real `MainComponent`, the move
(routing on the new lane, the amount lane kept, the row at the drop index, one undo, a lane with no CV jack changing
nothing); `AutomationLanesCarryTests.cpp` also covers the carried `modulatorOrder`.

`Tests/UI/Timeline/AutomationLanes/AutomationLanesMenuTests.cpp` (right-click at the pointer on the header and the editor,
handle and segment keeping their menus, the menu key), `AutomationLanesRetargetTests.cpp` and
`AutomationLanesDuplicateTests.cpp` (the pickers, one undo step, no duplicate parameter, dismissal),
`AutomationLanesLaneMainTests.cpp` (the same against a real `MainComponent`, Cmd+D through its key handler) and
`Tests/Timeline/TimelineDoc/TimelineDocLaneRetargetTests.cpp` (the doc operations). The control chips' tests (`AutomationLanesControlChipsTests.cpp`) are in [`lane-controls.md`](lane-controls.md#tests).

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
or its editor: the first stroke creating the lane in one undo step, the knob drag and keys as graph steps, the knob drag's hand cursor and bubble,
double-click, editing and erasing the last point, the bipolar picture, names and the accessible value) and
`AutomationLanesAmountIntegrationTests.cpp` (hidden as a lane, an orphaned lane row after a canvas delete,
Remove modulator with and without the LFO, Move to track, a saved project reopening, and a project saved with
sections inside a macro migrated on load); the row's readout is in `AutomationLanesModulatorRowTests.cpp`; **Change source...** is in `AutomationLanesChangeSourceTests.cpp` (item shown/hidden by the host, the picker's groups, ticked current source and New rows for LFO and envelope rows, the real re-key, the source channel and the one undo step); the lane headers' markers, name buttons, click-to-focus, Info tooltips setting, mod row alignment and New LFO / New envelope against a real `MainComponent` are in `AutomationLanesLaneHeaderTests.cpp`; the **Change parameter / Duplicate carrying modulators** is in `AutomationLanesCarryTests.cpp` (both routings and their amount lanes moving or copied, the original untouched, one undo step each, a parameter with no CV jack, and a pick reaching the host call); the
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
`CustomLfoFromRangeTests.cpp` (the rate chosen per range length, the wave and level, the phase offset, base plus the
LFO reproducing linear, held, bent and skewed-range lanes, the 64-point budget, the flattening leaving every beat outside
the range as it was) and `AutomationLanesCustomLfoTests.cpp` (the menu item first, its disabled texts, the command
against a real `MainComponent`: the new LFO's settings and wave, its row and amount lane, one undo and redo, a second
row beside an existing LFO, the melt and the row's fade),
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
