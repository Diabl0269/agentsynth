# Linked clips

A linked clip is a copy of a MIDI clip that keeps sharing its music with the clip it came from: edit
the notes of any clip in the group and every other clip in it changes too. It is the "linked" or
"shared" clip of other DAWs, for a drum pattern or a riff that repeats through a song.

## What the user does

- **Duplicate linked**, in a clip's right-click menu, or **Shift+Cmd+D** (rebindable
  `timelineClipDuplicateLinked`, [shortcuts](../control/shortcuts.md)) on the clip lane, adds a copy
  linked to the clip. From the menu the copy lands right after the clicked clip; the key copies every
  selected MIDI clip and tiles the copies after the selection as one block, the way Duplicate does, each
  copy linked to its own source. Either is one undo step and leaves the copies selected. Audio clips
  never link: the menu item is greyed for them and the key skips them.
- **Duplicate** (the menu item, Cmd+D, Alt+drag) still makes an independent copy.
- **Make unique**, in the menu of a linked clip only, takes that clip out of its group. It keeps its
  notes; later edits no longer reach the others. One undo step.
- A linked clip draws a small chain-link mark at the right of its name strip (`linkMarkBounds`, painted
  only when the clip is wide enough to show its name). Its tooltip says how many clips it is linked
  with and points at Make unique. The keyboard clip's screen-reader text ends ", linked with N other
  clips" (`describeClipForAccessibility`).

## What is shared

Following the common DAW model, a link shares the clip's **content**: its notes and its length,
plus its drum-machine settings (steps per bar, swing, per-drum mute and solo; see [drum-machine](drum-machine.md)).
Everything about where and how each copy plays stays its own: name, start, track, mute, and the audio
fields. So a right-edge resize of one member resizes all of them, while moving, renaming or muting one
does not touch the others.

Every note edit goes through a `TimelineDoc` note mutator (`addNote`, `removeNote`, `moveNote`,
`resizeNote`, `setNoteVelocity`, `setNoteMuted`, `clearNotes`, `quantiseNotes`,
`quantiseNoteLengths`), and so does `resizeClip`. Each one rewrites the other members inside the same
`applyMutation` (`TimelineDoc::syncLinkedClips`, `TimelineDocLinkedClips.cpp`). The edit is therefore one
revision and one notification, and the piano roll, quantise, the velocity lane or a MIDI overdub
is one undo step for the whole group: `AppUndoManager::recordTimelineChange` diffs the
document around the mutation. Note ids stay unique across the document: the other members get fresh
copies of the edited clip's notes in its sorted order, so their note ids change when another member is
edited.

## Edits that end a link

| Edit | Result |
|------|--------|
| Split (menu, Split tool, range edges) | Both halves leave the group; the other members keep their notes and length |
| Glue with next | The glued clip leaves the group (the absorbed clip is gone) |
| Left-edge resize, range trims (`resizeClipLeft`, `trimClipStart`, `trimClipEnd`) | The trimmed clip leaves the group |
| Delete a member | The rest stay linked |
| Make unique | That clip leaves the group |
| Duplicate Track | The copy's linked clips are linked to each other in new groups, never to the original track's |
| Copy / paste, range copy | The pasted clips are new and unlinked |

A group never has one member: whenever a mutation leaves a single clip in a group, or a linked clip
becomes an audio clip, `dissolveLoneLinkGroups` (run by every mutation, a load included) unlinks it.

## In the file

`Clip::linkGroup` is an `int64` id, `0` for an unlinked clip. It is written as the optional clip
property `"linkGroup"` only when it is not 0, so an unlinked clip's JSON is unchanged and
`kFormatVersion` stays 1. A negative or non-integer value fails the load. New group ids come from the
clip-id counter (`nextClipId`), which is saved and never hands out a value twice, so a new group can
never collide with an existing one; on load that counter is floored past every group id in the file.

## AI

The AI's timeline operations only add new clips (`placeClips`, `placeMidiClip`); none edits an
existing clip, so no operation shape changed. The arrangement summary the model reads marks each
linked MIDI clip's window with `(linked)` ([arrangement context](../ai/arrangement-context.md)).

## Tests

`Tests/Timeline/TimelineDoc/TimelineDocLinkedClipsTests.cpp` (linking, every note mutator on either
member, one notification, one undo step, Make unique, split/glue/trim/delete/duplicate-track rules,
serialisation and the lone-member load);
`Tests/UI/Timeline/TimelineClipLane/TimelineClipLaneLinkedClipsTests.cpp` (the real context menu,
the key and its rebind, the link mark's pixels and tooltip, the screen-reader text);
`Tests/Timeline/ArrangementContextTests.cpp` (`LinkedClipsAreMarked`).
