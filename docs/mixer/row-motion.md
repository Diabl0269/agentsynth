# Insert and send row motion

An insert or send row that is added or removed opens or closes where it was. The lists are rebuilt as new objects on
every edit, so `MixerPanelComponent::takeOrBuildStripColumn` takes a `MixerRowsMotion::Snapshot` of the old column
(`captureRowSnapshot`: the rows' keys and, for a removal, a picture of the section) and `rebuildFrom` hands it to the
new column (`playRowMotion`) once it is laid out. Master keeps its column across edits and does the same inside
`setColumn`. Rows are keyed by module uuid (a split row by its fork's uuid) and by send slot, so a drag-reorder, a
retarget or a toggle changes no key and does not animate. A change that adds and removes together, a section scrolled
off its top, or an off-screen panel lands at once. The motion is the shared `ExitEnterListMotion`
([animation](../layout/animation.md#delete-and-undo-animation)). Tests: `Tests/UI/Mixer/MixerRowsMotionTests.cpp`.

A drag-reorder or Alt+Up/Down move of an insert is the same shape as a send's: `MixerInsertList` runs the shared
`Layout/ReorderDrag/` session over its feasible run, the commit holds the panel's rebuild, and the dropped row settles
from where it was let go (`pendingInsertSettle_`, played on the rebuilt column's list). A dry row has the key
`dry:<fork uuid>:<path>`, so adding a module to that path animates as an ordinary row add. See
[Reordering inserts](mixer.md#reordering-inserts).

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
