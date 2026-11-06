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
  step (clips are split at both edges) and opens its name for typing. There is no grow animation.
- **Rename:** double-click a block or press the menu's Rename. Return or focus-lost commits, Esc cancels, an
  empty or unchanged name records nothing.
- **Block menu** (right-click a block): Rename, Change colour, Place again after this, Place again at the end,
  Make unique (only when the section plays more than once), Select its clips, Remove (keep the music), Delete
  and bars. "Place again" makes room for the music after it, unless the cover setting is on. Each choice is
  one undo step. The headless seams are the protected `openSectionContextMenu` and the public
  `applySectionMenuChoice`.
- **Strip menu** (right-click empty strip): Make sections from markers.
- **From a range:** with the Range tool, the range menu's **Make section** (after "Delete range and close
  gap") or Option+Cmd+S (`timelineMakeSection`, rebindable) turns the range into a section.
- **Clips follow:** a clip inside a section that plays more than once draws the link mark; its tooltip reads
  "Follows Chorus (plays 3 times). Edit any Chorus to change them all." and a screen reader adds ", follows
  Chorus".
- **Edge lines:** each block's start and end draw as a faint line down the lanes.
- **Settings (Preferences > Timeline):** "show the Sections strip" (`timelineShowSectionsStrip`, default on) and
  "placing a section covers the music where it lands" (`timelineSectionsCoverOnDrop`, default off; Shift flips
  it for one drop).
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

Tests: `Tests/Timeline/TimelineDoc/TimelineDocSectionsTests.cpp`, `TimelineDocSectionFileTests.cpp`.
