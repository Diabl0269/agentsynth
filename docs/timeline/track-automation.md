# Track Automation Lanes

Automation of a parameter on a module that belongs to exactly ONE track lives **under that track**
in the arrangement, as lane rows that expand and collapse per track. Automation for a module that no
single track owns (a master bus, a bus several tracks send into, a free LFO) stays in the bottom
automation strip ([automation](automation.md)) — those are the **global** lanes.

## Which track owns a lane

`Source/Timeline/AutomationPlacement.h` (Core, pure, recomputed on demand — the answer moves as
cables are patched, so nothing caches it):

- **`computeTrackOwnership(graph, doc)`** walks forward from every Midi/Audio track's bound source
  node (`Track::bindingUuid` → the Track In node of a MIDI track, the Track Audio node of an audio
  track; both are `isTrackSourceNode`). A node reached by exactly one track belongs to it; a node
  reached by two or more is shared and dropped.
- **The edge rule is the track↔channel link walk's** (`isLinkSignalEdge`, shared through
  `ChannelFlowsInternal.h`): MIDI and audio signal edges only — never an attenuverter's hidden
  modulation leg, never an audio edge landing on a ModCV or sidechain input. A CV cable from track
  A's LFO into track B's filter does not make A reach B's chain.
- **Terminals end a branch and are never owned** (`isReachTerminal`: Master, Record Tap, the graph's
  own IO). Every track reaches Master, and a one-track project must not make that track own the
  master bus.
- Automation-kind, unbound and orphaned tracks contribute no source.
- `resolveOwningTrack(graph, doc, nodeUuid)` is the single-node form.

## The placement seam

`findOrCreateLaneHostTrack(graph, doc, nodeUuid)` is the ONE place a new lane's track is chosen:
the owning track when there is one, otherwise the doc's Automation-kind track, created when missing
(invalid at `TimelineDoc::kMaxTracks`). All three lane-creating paths call it inside their own
mutation: `MainComponent::automateParameter` (right-click a knob → Automate),
`MainComponent::addPluginAutomationLane` (the strip's "Add lane..." entries, hosted-plugin
parameters, send slots, and a track header's "Add automation lane" submenu) and `TimelineOps`'
`writeLane` (the AI path).

**Existing lanes are never migrated.** `TimelineDoc::addLane` dedupes doc-wide, so a repeat request
for an already-automated parameter returns the existing lane wherever it lives. Placement happens
once, at creation: re-patching a module into (or out of) a track later does not move its lane.

Every "show me this lane" path then ends in `TimelinePanelComponent::revealAutomationLane(laneId)`:
a lane on a Midi/Audio track expands that track, scrolls the row into view and focuses it; a lane
on an Automation-kind track opens the bottom strip on it, as before.

## Expanding a track

A Midi/Audio track header's `A` button expands and collapses that track's lane rows
([tracks](tracks.md#the-automation-button)). The row context menu offers the same toggle ("Show
automation" / "Hide automation") and an **"Add automation lane"** submenu: one submenu per module the
track owns (`TrackHeaderHost::getTrackAutomationParameterOptions`), each listing that module's
not-yet-automated parameters — automatable `RangedAudioParameter`s for a built-in module, instance
parameters for a hosted plugin, ACTIVE send slots for a Channel Strip. The options resolve against
a snapshot taken when the menu was built.

Expanded state is **in-session UI state**: `TimelineViewState::expandedLaneTracks` (a set of
`TrackId` values). It is not written to the project; there is no per-project UI-state mechanism to
put it in.

## Row geometry

`Source/UI/Timeline/TrackRowLayout.h` is the one mapping between vertical rows and (track, optional
lane), in content coordinates. `TimelineClipLaneArea::getRowLayout()` builds it (lazily, keyed on
the doc revision, the row height and the expanded set) and **both** columns read that same object:
the header column (`TimelinePanelComponent::layoutTrackHeaders`, drag-to-reorder boundaries,
scroll-into-view, the maximum scroll) and the clip lanes (row rects, hit testing, the drag's row
delta, the live-recording strip, the Draw ghost, the file-drop highlight). Nothing computes
`index * rowHeight` any more.

- A lane row is `laneRowHeightFor(trackRowHeight)` = ¾ of a track row (floor 18 px), so it scales
  with `rowHeightScale` like the track rows do.
- Automation-kind tracks never expand (their lanes are global). An id in the expanded set that
  names no track is ignored, so nothing has to prune the set when a track is deleted.
- **Clips never land on a lane row.** `trackIndexAt()` answers "no track" on a lane row, so a
  double-click, the Draw tool and a file drop author nothing there. A clip DRAG over lane rows maps
  them to their parent track (`trackIndexForDrag`), so dragging a clip across an expanded track's
  lanes still targets that track row, and a cross-track drag keeps its old rounding with no lanes
  expanded.
- Drop-insertion boundaries are track-block edges: a track and its lane rows move as one unit.

## Lane rows

`Source/UI/Timeline/TrackAutomationLanes/` — `TrackAutomationLanes`, `TimelinePanelComponent`'s
collaborator, owns the expanded set's writer, a pool of lane-row headers and a pool of lane-row
editors, the focused lane, and the toolbar's automation controls.

- **Header part** (`TrackLaneHeaderComponent`, in the track-header column, indented under its track
  with the track's colour stripe): "Module · Param" (the same `automationLaneLabel` the strip's
  picker uses, via `TrackHeaderHost::getNodeDisplayName` / `getParameterDisplayName`), a
  record-mode selector (Off/Read/Touch/Latch/Write, one `recordTimelineChange`, exactly like the
  strip's) and, on right-click, **Remove lane** (one undo step). An orphaned lane's name is amber.
  A click focuses the lane.
- **Content part**: an `AutomationLaneEditor` per VISIBLE lane row, in a click-through layer
  (`setInterceptsMouseClicks(false, true)`) laid over the clip lanes at the same rect, so a press on
  a track row still reaches the clips below. Editors map X through the shared `TimelineViewState`,
  so they line up with clips and the playhead, and paint in their track-lane style: a faint tint of
  the track colour over the panel's grid, the curve in the track colour, an accent outline while
  focused. The layer sits above the clips and below the playhead, and hides while the piano roll is
  open.
- **Pooling.** Editors exist only for rows inside the visible window and are reused across scrolls;
  an editor already showing a still-visible lane keeps it. Neither pool ever frees a component
  during a session: a doc mutation fired from inside an editor's own `mouseUp` (or a header's own
  combo callback) re-lays out the rows, and must never delete the component on the stack.
- **Focus** is explicit state (`getFocusedLane()`), not "whichever editor has keyboard focus" —
  real focus is a best-effort no-op without a native peer. The focused editor also grabs keyboard
  focus when it is showing.

## Tools

The timeline's one edit tool drives every lane-row editor (`laneEditorToolFor`,
`LaneToolMapping.h`): Select → Pointer, Erase → Eraser, Draw → the curve selector's choice
(Freehand → Pencil, Line → Line, Sine/Triangle/Square/Saw Up/Saw Down/Random → Shape of that kind),
and the clip-only tools (Split, Glue, Mute) → Pointer. The curve selector shows while Draw is active
([edit-tools](edit-tools.md#the-curve-selector)). The bottom strip keeps its own tool row.

## Global lanes stay in the strip

The strip's lane picker lists only lanes on Automation-kind tracks; a track-owned lane is edited in
its own row. The toolbar's **global automation** button toggles the strip and badges the global lane
count; with no global lane yet it opens the strip empty, on its "Add lane..." picker.

## Automation follows events

A panel-wide toggle in the toolbar, persisted under `timelineAutomationFollowsClips` (default OFF).
While ON, a clip edit also edits the automation under the clip on the **track's own lanes** (lanes
stored on that track) in the SAME `recordTimelineChange` as the clip edit:

| Clip edit | Automation |
|---|---|
| Drag-move (same track or another) | the span's points move with it |
| Alt copy-drag, Duplicate (menu or Cmd+D), Repeat | the span's points are copied to each copy |
| Delete, Cut | the span's points are removed |
| Copy, then Paste | the span's points are captured at copy time and written where the paste lands |

The span is the clip's half-open `[start, end)`. Carried points replace whatever sat in the
destination span on the target lane; a clip with no automation under it never clears anything where
it lands. **To another track** the target is that track's lane with the same `paramId` when there is
exactly one (lane identity is unique doc-wide, so another track can only ever match by parameter id);
otherwise a move leaves the points where they were and a copy writes nothing. Values are clamped into
the target lane's range. Resize, split and glue never touch automation. There is no clip nudge
command today.

`TimelineDoc::transferAutomationSpans(edits)` (`TimelineDocAutomation.cpp`) is the batched
primitive: every read comes from the lanes' original points, writes run in three passes (source
removals, destination clears, inserts), and the whole batch is ONE `applyMutation` — a multi-clip
drag whose spans overlap never reads a point the same batch already moved. The paste path writes
through `editBreakpoints` per carried lane inside the paste's own `recordTimelineChange`.

**A clip never moves without its automation.** When the automation half would be refused — a
target lane would go past `kMaxBreakpointsPerLane`, or a track no longer resolves — the WHOLE edit
is refused before anything changes: no clip moves or copies, no undo step is pushed, and the status
bar says why (`kAutomationSpanRefusedMessage`, via `TimelineClipLaneArea::onStatusMessage`, which
the panel's clipboard verbs report through too). The check is a dry run —
`TimelineDoc::canTransferAutomationSpans` for drag / duplicate / repeat, a simulation of the
`editBreakpoints` calls for paste — made against the doc as it is before the edit; clip edits never
touch lanes, so the answer cannot change inside the edit. Delete and Cut only erase points and so
are never refused. Pinned by `TimelinePanelClipClipboardControllerTests.cpp`.

## Tests

- `Tests/UI/Timeline/TrackRowLayoutTests.cpp` — the layout in isolation.
- `Tests/Timeline/AutomationPlacementTests.cpp` — ownership (chain, shared, free modulator,
  modulation leg, Master, audio track, unbound/Automation tracks) and the placement seam (owned,
  global, reused Automation track, `kMaxTracks`, never migrated).
- `Tests/Timeline/TimelineDoc/TimelineDocAutomationSpanTests.cpp` — `transferAutomationSpans`.
- `Tests/UI/Timeline/TrackAutomationLanesTests.cpp` — the A toggle and rows, lane add/remove on an
  expanded track, header context menu, reveal/scroll/focus, reveal of a global lane, editor
  pooling, clip hit testing and drag over lane rows, tool mapping and curve selector, lane header
  record mode/remove with undo, strip lists only global lanes, the global toggle, and follows-events
  (off by default; drag, delete, duplicate, repeat, copy/paste, cut — each one undo step).
- `Tests/UI/Timeline/AutomationEditorTests.cpp` — `MainComponent::automateParameter` on a track's
  instrument focuses its lane row; on a module two tracks share it goes to the strip.
