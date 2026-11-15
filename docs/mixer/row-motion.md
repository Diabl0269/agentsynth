# Insert and send row motion

An insert or send row that is added or removed opens or closes where it was. The lists are rebuilt as new objects on
every edit, so `MixerPanelComponent::takeOrBuildStripColumn` takes a `MixerRowsMotion::Snapshot` of the old column
(`captureRowSnapshot`: the rows' keys and, for a removal, a picture of the section) and `rebuildFrom` hands it to the
new column (`playRowMotion`) once it is laid out. Master keeps its column across edits and does the same inside
`setColumn`. Rows are keyed by module uuid (a split row by its fork's uuid) and by send slot, so a drag-reorder, a
retarget or a toggle changes no key and is not a row that comes or goes (its own motion is the drop's settle, or, for an
undo or redo, [the look glide below](#an-undo-glides-what-the-forward-edit-changed)). A change that adds and removes
together, a section scrolled off its top, or an off-screen panel lands at once. The motion is the shared `ExitEnterListMotion`
([animation](../layout/animation.md#delete-and-undo-animation)). Tests: `Tests/UI/Mixer/MixerRowsMotionTests.cpp`.

A not-connected row ([mixer](mixer.md#not-connected-modules)) has the key `dangling:<module uuid>`, so a cable that newly lists a module opens a gap and grows it in, and cutting it shrinks it away; the motion reads `dangling:<uuid>` and `<uuid>` as one row. A module that only changes between the two (nothing else comes or goes) is not a picture motion: its row slides to its new place while its look dims or brightens (`MixerInsertList::startKindFlip`). A module that joins a split as a path (its row goes under the fold, the split row comes in) grows the split row in where it stood; the undo shrinks the split's rows away and the module's row shows where they were. The tooltip over a not-connected row says it is not in the sound.

A drag-reorder or Alt+Up/Down move of an insert is the same shape as a send's: `MixerInsertList` runs the shared
`Layout/ReorderDrag/` session over its feasible run, the commit holds the panel's rebuild, and the dropped row settles
from where it was let go (`pendingInsertSettle_`, played on the rebuilt column's list). A dry row has the key
`dry:<fork uuid>:<path>`, so adding a module to that path animates as an ordinary row add. See
[Reordering inserts](mixer.md#reordering-inserts).
A sound source at the head of a chain (an instrument, with nothing feeding it) cannot move, and no row moves above it (`MixerInsertList::moveFeasible`).

## The canvas follows a reorder

When an insert moves (Alt+Up/Down on a focused row, or a drag), the moved modules' cards trade places on the canvas
too, so the patch reads in the new chain order. `synth::followReorderOnCanvas` (`UI/Graph/InsertGap/InsertGapKeyboard.cpp`, called from `MixerInsertList::moveRowTo`)
takes the run of rows the move rotated and deals its cards into the slots they held: the first card's left edge, then
each gap between neighbours, so cards of different widths never overlap. It runs only when every card in the run is on
screen, in one container (the root or one macro), on one row (tops within 8 px) and left to right in the old chain
order; otherwise the user laid the cards out by hand and they stay put while the chain still reorders. The positions
land through the canvas's own card move in one `CardGlideAnimator::Scope`, so cards and cables glide, and inside the
reorder's own undo record, so one Cmd+Z restores the chain order and both positions. Tests:
`MixerInsertCardSwapTest` in `Tests/UI/Mixer/MixerRowMoveAndRevealTests.cpp`.

## An undo glides what the forward edit changed

An undo or redo rebuilds the column, so everything the forward edit left in place would otherwise snap. While
`AppUndoManager::isRestoring()` is true, the panel's snapshot also carries each row's drawn look (`MixerRowLook`: top,
dim, name, read through the old list's glide, so a second restore mid-glide leaves from where the row is drawn) and the
strip's name; the new column's lists start `MixerRowLookGlide` from them (`playRowMotion(snapshot, restored)`, Master in
`setInsertEntries`):

- a row that moved (a move or reorder of an insert or send, in either direction) slides from its old top;
- a bypassed row brightens, an un-bypassed one dims (insert name, send name and knobs; a send's controls fade by their own
  alpha while it runs, then take the plain dimmed look);
- a send retargeted, or a module renamed, cross-fades the old name into the new;
- the strip's name in the header cross-fades (`MixerColumnHeader::glideNameFrom`).

The controls (bypass and show buttons, knobs) follow the drawn place. Rows that come or go keep the motion above, and a
module that joins or leaves the chain keeps its kind flip; the glide skips a list whose rows played one of those. 160 ms,
`easeOutCubic`. Under Reduce Motion rows are at their places on the first frame and fade in there (the name cross-fade
and the dim are already fades, so they just run shorter); Animations Off and a panel that is not showing land at once.
The forward bypass of an insert or send row (a click, Space or Enter) glides its dim the same way (`dimOnly`: only a row
that kept its name eases, and only its dim; Reduce Motion fades, Animations Off is instant). The other forward edits
are not animated by this (a drop settles through `startSettleFrom`). Guards: `Mixer insert row`, `Mixer send row` and `Mixer insert bypass dim` in `UndoGlideSurfaces`,
and the look-glide cases of `Tests/UI/Mixer/MixerRowsMotionTests.cpp`.

## Column motion

A strip or bus column that comes or goes grows in or shrinks away the same way, sideways, whichever mixer shows it: the
docked panel, one in the main area or a window, and the "both places" mirror. No add path arms it. At the top of its own
rebuild (`noteColumnSetComing`, while the old columns still stand) the panel compares the shown channels with the new
snapshot; when exactly one kind of change is coming (columns only added, or only removed) it prepares the motion (a
removal takes its picture of the old columns now) and `rebuildFrom` ends it after layout. That covers Add Track, Make
channel, Create Channels, Add Bus, a plugin track, Duplicate Track and the undo or redo of each, including the undo of a
step that drops no track (which never reached the track hooks). `TrackGrowInScope` and the undo hooks still group
several rebuilds into one motion through `noteColumnsLeaving` / `finishColumnChange`. A second rebuild after the start
(the add's own follow-up) leaves the motion playing: `resized()` ends it only when the viewport moved or resized.

Landing at once: a load (`TimelineDoc::getReplaceCount` moved without an undo or redo), the catch-up rebuild when the
panel becomes visible, the first fill with several columns, a clear, a change that adds and removes together, a pinned
column, and a panel that is not showing. Tests: `Tests/UI/Mixer/MixerPanelColumnMotionTests.cpp`.
