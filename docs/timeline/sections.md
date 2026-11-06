# Sections

A section (Intro, Verse, Chorus) is a named stretch of bars across every track. It plays at one or more
**placements**, and every placement of one section holds the same music: edit a clip, a note or an
automation point inside any placement and, in the same mutation, every other placement is rewritten to
match. There are no variations. This page covers the document model and the Sections strip that shows it.

## What the user does

The **Sections strip** is a 24 px row (`Metrics::timelineSectionStripHeight`) above the ruler, over the same
bars, with a "Sections" label above the track headers. Code: `Source/UI/Timeline/TimelineSectionStrip/`. The
strip never listens to the doc; the panel's `timelineChanged` calls `refresh()`. It is hidden while a clip is
open in the piano roll, and by the Settings toggle below.

- **Make a section:** drag across bars on the empty strip. The span snaps outward to whole bars, is at least
  one bar and stops at neighbouring blocks. Release makes "Section N" in the next palette colour as one undo
  step (clips are split at both edges) and opens its name for typing. The new block grows in from the side the
  drag started on.
- **Rename:** double-click a block or press the menu's Rename. Return or focus-lost commits, Esc cancels, an
  empty or unchanged name records nothing.
- **Move or place again by dragging:** press a block and drag past 4 px. It lifts (a ghost follows the pointer at
  the grab offset, with a 2 px raise and a shadow) and an accent line shows where it will land. Holding Option when
  the drag starts makes it a copy (copy cursor; the original stays and the drop calls `placeSection`), otherwise it
  moves (`movePlacement`). The drop beat snaps to whole bars, or to a neighbour's start or end (the dragged block's
  own edge or its far edge) when within 12 px; a drop strictly inside another block goes to that block's nearer
  edge (`sectionstrip::dropBeat`). The mode is make-room unless the cover setting is on; holding Shift at release
  flips it for that drop. Esc, or releasing above the strip, cancels with nothing written. One drop is one undo
  step, and the ghost settles into place (or back) over 140 ms.
- **Keyboard:** the strip takes focus on a click or Tab and the selected block carries the accent focus ring.
  Left/Right (the `timelineClipPrevious` / `timelineClipNext` actions, shared with the clip lane) and Home/End move
  between blocks; Return or the Rename key renames; Cmd+D (the `timelineDuplicateFocusedTrack` chord, shared with
  the track row) places the section again after it; Option+Left/Right (`timelineClipMoveEarlier` /
  `timelineClipMoveLater`, the move-this-item chord) move it one section earlier or later with make-room; Delete or Backspace deletes it
  and its bars; Shift+F10 opens its menu (the strip is a `KeyboardContextMenuProvider`). With no block selected
  Return starts nothing. Esc cancels a drag in flight.
- **Block menu** (right-click a block): Rename, Change colour, Place again after this, Place again at the end,
  Make unique (only when the section plays more than once), Select its clips, Remove (keep the music), Delete
  and bars. "Place again" makes room for the music after it, unless the cover setting is on. Each choice is
  one undo step. The headless seams are the protected `openSectionContextMenu` and the public
  `applySectionMenuChoice`.
- **Strip menu** (right-click empty strip): Make sections from markers.
- **From a range:** with the Range tool, the range menu's **Make section** (after "Delete range and close
  gap") or Shift+Cmd+J (`timelineMakeSection`, rebindable) turns the range into a section.
- **Clips follow:** a clip inside a section that plays more than once draws the link mark; its tooltip reads
  "Follows Chorus (plays 3 times). Edit any Chorus to change them all." and a screen reader adds ", follows
  Chorus".
- **Edge lines:** each block's start and end draw as a faint line down the lanes.
- **Settings (Preferences > Timeline):** "show the Sections strip" (`timelineShowSectionsStrip`, default on) and
  "placing a section covers the music where it lands" (`timelineSectionsCoverOnDrop`, default off; Shift flips
  it for one drop).
- **Motion** (`SectionStripMotion`, one `AnimationDriver` per effect, nothing running at rest): a new block grows
  in over 160 ms; a deleted block's picture shrinks toward its centre over 180 ms; a dragged block lifts over 100 ms
  and settles over 140 ms; when `getSectionStampRevision()` changes, every placement in
  `getLastStampedPlacements()` shows a 1 px accent outline that fades over 400 ms. Undo of a delete grows the block
  back; a project load grows nothing. Reduce Motion turns the grow and shrink into an 80 ms fade and the outline
  into an 80 ms fade, and the lift lands at once (a move has no fade form); Animations: Off, or a strip that is not
  on screen, starts nothing.
- **Accessibility:** the strip is a group titled "Sections" described as "Sections, N sections"; each block is
  a button titled like "Chorus, bars 13 to 20, 1 of 3". Block tooltips read "Chorus - 8 bars - plays 3 times".
- **Test mode:** a `timelineSections` probe publishes `sections` (`name:start:length:ordinal/count`, joined
  by `;`) and `selectedSection`.

## Model

- `Section { id, name, colourArgb, lengthBeats }` and `SectionPlacement { id, section, startBeat }` live on
  `TimelineDoc` (`getSections()`, `getPlacements()`). Sections are in creation order, placements sorted by
  `(startBeat, id)`. Section and placement ids share one counter (`nextSectionId`).
- Names are trimmed, non-empty and at most `kMaxMarkerTextLength`. Caps: `kMaxSections` 256,
  `kMaxPlacements` 1024.
- After every mutation and every load: lengths are finite and above 0, starts finite and at or above 0,
  placements never overlap (touching is fine), every placement names a section, and a section with no
  placement is removed (`finishMutation`, like the lone link groups).

## What a placement holds

For the placement at `[s, e)`: every clip on every track whose `startBeat` is in `[s, e)` (a clip may run
past `e` and still belongs to it), and every automation breakpoint on every lane whose beat is in `[s, e)`.
Content is compared relative to `s`, ignoring every id and each clip's `linkGroup`; everything else on the
clip counts. `sameClipContent` (`TimelineDocSectionSync.cpp`) lists the fields, and a unit test fails when a
`Clip` field is added without it.

## Keeping placements in step

`syncSections()` runs in `finishMutation` before `dissolveLoneLinkGroups()`. A runtime-only cache holds each
placement's content after the previous mutation. A placement is "changed" when it has a cache entry that
differs from its current content (a new or just-loaded placement has none). The source is the changed
placement with the lowest start; its content is stamped onto every other placement of the section:

- per track, equal clip counts overwrite in place (ids kept, notes get fresh ids); otherwise the target's
  clips are replaced by copies with fresh ids (skipped on a track that would pass `kMaxClipsPerTrack`);
- per lane, the target's points are replaced by the source's, shifted.

One edit is one revision, one notification and one undo step. `getSectionStampRevision()` and
`getLastStampedPlacements()` (runtime only) tell a UI which placements were rewritten.

A load never stamps: a file whose repeats differ keeps them as they are until one is edited, then that one
wins. Compound ops hold a `TimelineDoc::SectionEditScope`, which suspends the sync so a half-finished edit
never stamps; the last scope to end runs one sync, and only when it would change something.

## Operations

`makeSection`, `placeSection`, `movePlacement` (`SectionDropMode::MakeRoom` shifts everything later, `Cover`
overwrites what is under the drop), `removePlacementKeepMusic`, `deletePlacementAndBars`,
`makePlacementUnique` (moves one placement to a new section; rejected for a section's only placement),
`renameSection`, `setSectionColour` and `makeSectionsFromMarkers` (markers stay; a repeated label joins one
section only when its span has the same length and identical content, otherwise it becomes "Chorus 2").
`insertTime` and `deleteTime` edit the whole song: clips, lane points, markers and placements all move; a
placement that strictly contains an insert point stops being a placement and keeps its music. Every op
validates first and changes nothing when it rejects. Ops that may split or trim audio take `secondsPerBeat`.

## File format

Additive, `kFormatVersion` stays 1. `"sections"` is written only when there are sections:
`{"id", "name", "colourArgb", "lengthBeats", "placements": [{"id", "startBeat"}]}`, with `"nextSectionId"`.
`fromVar` is all-or-nothing and rejects wrong types, duplicate ids (one id space), a section with no
placement, overlap, a bad name, length or start, the caps and unknown keys inside a section or placement.
`validateTimeline` allow-lists both keys with the same rules (`TimelineValidatorSections.cpp`).

## AI context

`ArrangementContext::summarize` adds a line after the markers:
`Sections (bars): "Intro" 1-4; "Verse" 5-12 (1 of 2); ...` one entry per placement, whole entries only, at
most a quarter of the budget, with a `+K more` tail. Bars follow the transport's time signature.

## AI songs

A song build request carries `"sections": true`. The answer then makes real sections instead of markers: after
the `placeClips` ops come `makeSection` (`name`, `startBeat`, `endBeat`: the bars that hold the section's clips,
in the next palette colour) and `placeSection` (`name`, `beat`: play it again, covering the empty bars), then any
`writeLane` ops. Both run inside the batch's one undo step (`TimelineOpsSections.cpp`); a name used twice, a
`placeSection` naming no section, an overlap or a cap rejects the whole batch. A `writeLane` that comes after is
written over every bar, then the sync copies the first placement's points onto each repeat, so every repeat ends
up with the automation of the first one. See [timeline ops](../ai/timeline-ops.md).

Tests: `Tests/Timeline/TimelineOpsSectionTests.cpp`, `Tests/Timeline/TimelineDoc/TimelineDocSectionsTests.cpp`, `TimelineDocSectionFileTests.cpp`.
