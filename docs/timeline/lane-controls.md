# Lane header control chips

A timeline lane header (and a mod lane row, on its title line; see [automation.md](automation.md#the-lane-header)) carries a strip of 22 px icon buttons (20 px or less on a short
row: the chip is the line's height at most) then a "+" button. `LaneControlStrip` is the one component for both rows; the
owner gives it `Actions` (run a control, can it act now, what the tooltip adds) and the strip does the rest.
A lane row or a mod lane row can be made taller or shorter on its own (drag the strip on that row's bottom edge, or Option+= / - / 0 with it focused; see [Resizing a lane](#resizing-a-lane)); the chips keep their size and only the row around them changes.

- **The catalogue** (`LaneControls.h`) is the actions the lane menus already have, in this order. Automation lane: Read /
  Write, Add mod, Change parameter, Mute, Duplicate, Move to track, Create custom LFO, Remove lane. Mod lane: Change
  source, Show in canvas, Remove modulator. Each chip runs the menu item of the same name (`applyMenuChoice`), so the
  undo steps, pickers and test hooks are the menu's own; Read / Write opens a menu of the five modes and Move to track a
  menu of the other tracks beside the chip. **Mute** is the record mode Off and puts the mode it replaced back (Read for
  a lane that was already Off); its chip reads as pressed while the lane is Off. A mod lane has no Mute: a routing cannot
  be muted without removing it or zeroing its amount. A chip whose action cannot run now (no CV input, no free parameter,
  no other track, no range for Create custom LFO) stays listed but disabled, and its tooltip ends with the reason.
- **The shown set is one per lane kind**: every automation lane shows the same chips in the same order, every mod lane
  its own, held by `LaneControlSet` (owned by `TimelineAutomationLanes`) and kept in the user settings as a comma list of
  control ids under `timelineLaneControls.automation` / `timelineLaneControls.mod` (unknown ids and repeats are
  dropped), so it outlives the project, the session and a project load. A change on one lane redraws every lane of the
  kind at once. The defaults: Read / Write and Add mod on an automation lane, Change source on a mod lane.
- **Names**: the accessible name is the control's name ("Read / Write", "Remove modulator" on a mod lane), the tooltip
  "Name: what it does" (Read / Write adds the current mode). Chips are Tab stops with the accent ring; Return and Space
  press; every other key (Up/Down, Cmd+Alt+Up/Down) bubbles to the header. A right-click on a chip opens the lane menu.
- **Replace Module**: Cmd+Alt+R (Ctrl+Alt+R off the Mac) with a lane header focused replaces the lane's target module, and
  with a modulator row focused the row's source module. The header and row leave the chord to the app; the canvas then
  shows and selects the card and opens the "Replace with..." picker on it. A module inside a collapsed macro (every
  instrument track's modules, by default) is unfolded first, one undo step, so the picker has a card to sit on. A
  focused modulator row also makes the timeline the lane surface for Copy, Cut and Duplicate, as a focused header does.
- **Narrow header**: chips that do not fit the strip's width (the module note keeps 40 px, a mod row's name 40 px) are
  not drawn and are listed in the "+" row.
- **The "+" row** (`LaneControlMoreRow`, "More actions for this lane"): a labelled button for every catalogue control
  the header does not show, in catalogue order, ending with **Edit controls**. It is a child of the timeline panel
  (`LaneControlSet::setOverlayParent`), placed 2 px right of the header over the lanes region and clamped inside the
  panel, wraps into lines up to 470 px, and slides open left to right in 180 ms (easeOutCubic; shut in the same time,
  easeInCubic; at once under Reduce Motion, Animations Off or off screen). A press on a button closes it and runs the
  control; Esc, a press anywhere else, or "+" again closes it. From the keyboard the first button takes focus. It
  follows its header while the list scrolls (`TimelineAutomationLanes::placeBodies`).
- **Edit controls** (one lane in the timeline at a time; entering it ends any other): the header gets an accent outline,
  "+" becomes a pressed pencil ("Editing controls: press Esc or Done to finish") and a 34 px **tray** slides down under the
  header (280 ms, easeInOutCubic) with the controls the header does not show, dimmed, and a **Done** button. When the hidden chips and Done do not fit one row the tray gets another 34 px row (chips never overlap) and the rows below move by its real height. The tray plus a
  2 px gap are part of the lane's block (`TimelineAutomationLanes::modulatorOffset`), so every row below moves down with
  it. Click, Return or Space on a chip shows it (at the end) or hides it; dragging a chip from the header onto the tray
  hides it, from the tray onto the header shows it at the drop point (neighbours slide apart while it hovers), and
  dragging sideways in the header reorders; Left/Right walk the chips, Alt+Left/Right move the focused one, Done, Esc or
  the pencil leave. Chips in the header are 4 px wider with a grip while editing; a chip that changes sides grows in
  (220 ms, a small overshoot on the header). The drag moves a ghost of the chip; the real chip stays put until the drop.
  Alt+Left/Right and Left/Right are the arrows of a list of controls, so they are not rebindable actions.

## Tests

`AutomationLanesControlChipsTests.cpp` covers the control chips (the defaults per kind and the saved set's parsing, names and
tooltips, each chip against its menu action, Mute and its restore, the "+" row's exact contents, closing, narrow-header
overflow and Reduce Motion), Edit controls (the tray making room for the rows below, click/Space/Alt+arrows, real drags header
to tray and back, one lane at a time, the set shared by a kind and read back from the settings by a fresh panel) and a mod
row's own chips.

## Resizing a lane

Every row has its own height on top of the shared vertical zoom, the same way a track row does
([tracks](tracks.md#one-tracks-height)): the lane's own row and each of its modulator rows resize alone, like lanes in
Cubase, and only the rows below move.

- **The data.** The lane row is `AutomationLane::heightScale`; a modulator row is its entry in
  `AutomationLane::modulatorHeightScales`, keyed by the routing's `modulatorOrderKey` (the Attenuverter's uuid, like
  `modulatorOrder`), and a row with no entry uses `AutomationLane::modulatorHeightScale` (`modulatorRowScale(key)` reads
  either). All are 0.5 to 4, default 1, saved with the project, and written through `TimelineDoc::setLaneHeightScale` /
  `setModulatorHeightScale` (one undo step each through `setLaneHeightScaleUndoable` /
  `setModulatorHeightScaleUndoable`). `TimelineAutomationLanes::laneRowHeight(lane)` and `modulatorRowHeight(lane, index)`
  read them (or the drag preview).
- **Older projects.** Before rows had their own heights, a lane's `heightScale` scaled its modulator rows too. A file
  without `modulatorHeightScale` loads it as the lane's `heightScale`, so those rows look exactly as they did; it is
  written only when it differs from `heightScale`, and `modulatorHeightScales` only when not empty, so a project at the
  defaults saves byte for byte as before.
- **The strips.** One thin vertical-resize strip (`EdgeResizeHandle`, id `laneHeightHandle` for a lane row,
  `modulatorHeightHandle` for a modulator row) sits on the bottom edge of each row in the header column, past the row's
  edit tray where the next row starts. Drag it to resize just that row (the layout previews each step and the doc is
  written once on release; a drag past 0.5 or 4 clamps), or double-click it for the default. The lowest row of a track's
  last lane sits one strip above the track's own resize strip, which owns the exact seam above the next track: one row
  edge, then the track edge. Strips hide while a lane or modulator row is being dragged to a new place, and while a block
  grows in.
- **Keys.** With a lane header focused, or a modulator row (or one of its controls, or its band), the track-height keys
  act on that row only: Option+= taller, Option+- shorter, Option+0 default (`timelineIncreaseTrackHeight`,
  `timelineDecreaseTrackHeight`, `timelineResetTrackHeight`, x1.25 a step, one undo step each).
- **Names.** A strip is named "Resize <parameter> lane" or "Resize <source> mod lane on <parameter>" (the row's own name)
  for screen readers, and its tooltip says it resizes that row only and lists the keys.
- **Carries.** Duplicate copies every height; Change parameter, Change source and dragging a mod row onto another lane
  move a row's height to its routing's new key (`TimelineDoc::rekeyModulatorHeights`), in the same undo step.
- Open edit trays keep their own fixed height and do not scale; a folded modulator row is a third of its own full height.

Tests: `AutomationLanesHeightTests.cpp` (real drags on each strip, previews equal commits, the other rows untouched, the
strip positions, the keys on a header, a row and a band, an older project), `TimelineDocRowHeightTests.cpp` (setters,
save and load, the older-file rule, rekeying) and the carry case in `AutomationLanesCarryTests.cpp`.

## Create custom LFO from the range

A right-click inside the selected range on the lane's curve editor (the Range tool's range, or the stretch from the
first to the last of two or more points selected with the Select tool) shows **Create custom
LFO** as the whole menu when it lands on empty space (the lane menu stays on the header, its "..." button and a
right-click outside the range), and as the last item, after a separator, of the handle menu ("Delete point") or the
segment menu ("Hold" / "Linear") when it lands on a point or a segment. The item is the header's own, built by
`AutomationLaneHeaderComponent::addCreateCustomLfoItem` (same text, enabled state and reasons) and runs the same
host callback, so it is one undo step and animates identically (`AutomationLaneEditor::addRangeMenuItems`).
