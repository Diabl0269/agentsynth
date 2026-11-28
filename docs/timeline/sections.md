# Sections

A section (Intro, Verse, Chorus) is a named stretch of bars across every track. It plays at one or more
**placements**, and every placement of one section holds the same music: edit a clip, a note or an
automation point inside any placement and, in the same mutation, every other placement is rewritten to
match, unless the placement plays another variation (below). This page covers the document model and the Sections strip that shows it.

## What the user does

The **Sections strip** is a 24 px row (`Metrics::timelineSectionStripHeight`) above the ruler, over the same
bars, with a "Sections" label above the track headers. Code: `Source/UI/Timeline/TimelineSectionStrip/`. The
strip never listens to the doc; the panel's `timelineChanged` calls `refresh()`. It is hidden while a clip is
open in the piano roll, and by the Settings toggle below.

- **Make a section:** drag across the empty strip. The span follows the timeline's Snap unit
  (`sectionstrip::stripGrid`): Snap Bar makes whole bars, Snap 2 bars two-bar multiples, Snap 1/4 beats (a 10-beat,
  2.5-bar tag). Its start floors and its end ceils to that grid; it is at least one unit and stops at neighbouring
  blocks. Hold Shift (or turn Snap Off) to place it freely (raw beats, at least 1/16 of a beat so a click never
  makes a zero section). Release makes "Section N" in the next palette colour as one undo
  step (clips are split at both edges) and opens its name for typing. The new block grows in from the side the
  drag started on. Held in the strip's left or right 24 px edge zone the drag scrolls the timeline (below).
  Comparison: Logic Pro applies its Snap menu to marker and cycle-boundary edits and Control overrides it (Control-Shift
  goes finer), and Ableton Live's manual lists Alt/Cmd as "bypass snapping while dragging"; Cubase's marker
  bypass modifier is unconfirmed. Snap by default with a modifier to bypass (this design) is the common convention.
- **Rename:** double-click a block or press the menu's Rename. Return or focus-lost commits, Esc cancels, an
  empty or unchanged name records nothing.
- **Select several:** a click selects one block; Cmd-click adds or takes out one; Shift-click selects the run
  from the anchor (the last plain- or Cmd-clicked block) to the clicked one, and a second Shift-click moves the
  run's far end (Finder rules, `ListSelectionModel` in `Source/UI/Timeline/ListSelectionModel.h`, the same model
  the track list uses). Cmd+A with the strip focused selects every block; a click on empty strip clears. Every
  selected block draws the accent ring; the anchor carries the focus ring and is the block the one-block verbs
  (Rename, V, Make unique, Change colour, Select its clips) act on. A plain press on one of several selected keeps
  the set, so the press can drag it, and narrows to that block on a release that did not drag. A Cmd- or
  Shift-press only changes the selection; a drag after it moves the set when the pressed block is in it. A
  selected block's tooltip ends ", 2 selected: drag, Delete or Cmd + D acts on all", and each block's proxy
  reports its selected state to a screen reader.
- **Verbs on the set** (each one undo step, however many blocks): Delete or Backspace, and the menu's Delete,
  delete every selected block and its bars (latest first, so no delete shifts one still to go); a delete closes up
  only the run of blocks touching the deleted one (each starting where the one before ends), so what starts after a
  gap keeps its place, and a block with nothing touching it leaves its bars empty; the menu's
  Remove (keep the music) removes every selected placement; Cmd+D and "Place again after this" place every
  selected section again from the end of the last selected block, keeping the gaps between them
  (`TimelineDoc::placeSectionsTogether`), and the copies become the selection. A menu opened on a selected block
  applies these to the whole set; opened on another block it first selects just that one.
- **Move or place again by dragging:** press a block and drag past 4 px. It lifts (a ghost follows the pointer at
  the grab offset, with a 2 px raise and a shadow) and the strip shows the drop before it happens, the way a
  dragged track does: one pure function, `sectionstrip::planSectionDrop` (`SectionDropPlan.h`), gives the landing
  and where every other block ends up, and the accent drop line, the other blocks and the settle all read it.
  Every beat in the plan is in the layout after the drop, so the line, the ghost's left edge and the block's final
  start are one number, which `movePlacement` / `placeSection` take as the start the block gets.
  - **Make room** (the default): a moved block's own place is hidden and the gap closes; the other blocks glide
    (a `KeyedPointTween` keyed by placement id, 140 ms) to where they will be. With the ghost's left edge over
    another block, it goes before that block until the ghost's centre passes the block's centre (where it was
    drawn before the drag), then after it; otherwise the landing snaps to a neighbour's start or end within 12 px,
    else to the nearest line of the Snap unit (free with Snap Off). It never lands inside a block. Shift in a block drag already
    switches make-room and cover, so a block's landing uses the same Snap grid with no modifier override.
  - **Cover:** the blocks stay put; the original (on a move) and every block the drop would remove are dimmed.
    The landing is `sectionstrip::dropBeat` against the blocks as they are (the Snap unit, a neighbour's start or end
    or the dragged block's far edge against one within 12 px, a result inside a block goes to its nearer edge).
  Holding Option when the drag starts makes it a copy (copy cursor; the original stays and the drop calls
  `placeSection`), otherwise it moves (`movePlacement`). The mode is make-room unless the cover setting is on;
  Shift flips it, and the preview changes the moment Shift goes down or up (`modifierKeysChanged`); the release's
  own Shift decides the drop. Esc, or releasing above the strip, cancels with nothing written and eases the blocks
  back. One drop is one undo step; the ghost settles over 140 ms onto the drop line while the real block stays
  hidden, so it never shows in two places. Clip lanes redraw at the drop (they do not preview the move).
  - **A group:** pressing one of several selected blocks drags them all. `sectionstrip::planGroupDrop` (of which
    `planSectionDrop` is the one-block case) plans it with the group's span L (first start to last end): every
    member keeps its distance from the group's left edge, so the gaps between members stay, with the left edge at the
    landing; with make room a move takes the whole span out (every block after it moves earlier by L) and the
    unselected blocks between the members go along at their places in the group's shape (`sectionstrip::blocksBetween`,
    `DropPlan::carried`), then everything from the landing moves later by L. So the layout with the group taken out is
    the same wherever the group is held, a drop at the group's own start changes nothing, dragging away and back
    restores the song, and the song keeps its length; the drop hint says so ("Release to insert here, the section between comes too"). With
    cover a block is covered only where it overlaps a member's landed place, what sits between landed members is
    left alone, and a copy leaves the blocks between where they are. Every member draws as a
    ghost at its own distance, the pressed one under the pointer at its grab offset; the originals hide (make room) or dim
    (cover) as one block's does. The drop calls `TimelineDoc::movePlacementsTogether`, or for an Option-drag
    `placeSectionsTogether`, once, as one undo step, and the dropped blocks (or the copies) stay selected.
- **A free span pushes nothing:** make room opens room only when the landing span [landing, landing + L) overlaps
  another block (the originals of a copy stay and count; a move always opens its span again, so a move into a free spot moves nothing else) or any clip (music loose between
  blocks is never covered). A span nothing overlaps moves nothing, so an Option-drag of Section 1 (bar 5) over the free bars 1-4 puts the copy at bar 1 and leaves
  Section 1 where it is. The one plan decides it (`DropPlan::opensRoom`) and `TimelineDoc` makes the same call
  (`openRoomAt`, in `placeSection` and `placeSectionsTogether`), so the preview is the commit. The hint then reads "Release to place here" ("place a copy here").
  Cmd+D, "Place again after this" and Option+Left/Right take the same rule. A copy also snaps its right edge to a block's
  left edge, so it can sit just before the original. Undo is one step.
- **Edge scroll:** a block drag or a make-drag held in the strip's left or right 24 px zone scrolls the shared
  timeline view at the clip lanes' pace (`EdgeAutoScroll.h`, faster nearer the edge, one eased step a display frame), and each frame
  re-derives the drop line, the ghost and the plan (or the make-drag's outline) from the still pointer, so they
  keep following. Scrolling turns follow-playhead off and redraws the ruler and lanes (`onViewScrolledByDrag`);
  the left edge stops at beat 0. The timer runs only while a drag is held in a zone
  (`TimelineSectionStripEdgeScroll.cpp`).
- **Keyboard:** the strip takes focus on a click or Tab (the first Tab stop after the panel root, [focus](focus.md#clip-keyboard-mode)) and the selected block carries the accent focus ring; with no block selected the whole row is ringed.
  Left/Right (the `timelineClipPrevious` / `timelineClipNext` actions, shared with the clip lane) and Home/End move
  between blocks, selecting just the one they land on; Shift+Left/Right extend the selection a block at a time;
  Cmd+A (the `timelineSelectAllTracks` chord, shared with the track row) selects every block; Return or the Rename
  key renames; Cmd+D (the `timelineDuplicateFocusedTrack` chord, shared with the track row) places the selected
  sections again after the last of them; Option+Left/Right (`timelineClipMoveEarlier` / `timelineClipMoveLater`,
  the move-this-item chord) move one block one section earlier or later with make-room (later puts it right after
  the next block, whatever their lengths), and a group past the neighbouring unselected block (later: the first
  one after the last selected; earlier: the last one before the first), keeping their gaps and taking the blocks between
  along as a drag does, so Option+Right then Option+Left restores the song, doing nothing at either end;
  Delete or Backspace deletes the selected blocks and their bars; Shift+F10 opens the focused block's menu (the
  strip is a `KeyboardContextMenuProvider`). With no block selected Return starts nothing. Esc cancels a drag in
  flight.
- **Block menu** (right-click a block): Rename, Change colour, Place again after this, Place again at the end,
  Make unique (only when the section plays more than once), Select its clips, Remove (keep the music), Delete
  and bars, close up what touches it. "Place again after this" names what it will do with the setting: "(insert)" or "(replace the sections there)"; "Place again at the end" has nothing to cover. Each choice is
  one undo step. The headless seams are the protected `openSectionContextMenu` and the public
  `applySectionMenuChoice`. A **Variation** submenu follows Make unique, with the same items as the chip's menu.
- **Variation chip:** every block shows a rounded chip at its right end with the letter it plays (A, B...),
  drawn on the block's colour darkened and brightened under the pointer. It is hidden when the block would leave
  under 24 px for its name (the block menu still reaches every action). A click on the chip (not a drag, which
  still moves the block) selects the block and opens the **variation menu** at the chip; V on a focused block
  opens it too. The menu has one radio row per variation ("A", "B (Lead)", "C (Lead, Bass)": the tracks a
  variation owns, at most three then "+N", none for A or a full copy; the placement's own is ticked) which
  switches only this repeat, then "New variation (copy of B)", "Play B in every Verse" (only when the section
  plays more than once and some repeat plays another letter) and "Remove variation B" (not on A). Each choice
  is one undo step. Switching, playing everywhere, adding and removing outline the rewritten placements like a
  stamp. The chip's tooltip reads "Variation B of Verse. Click to switch, add or play it everywhere." and a block
  of a section with several variations adds " - variation B". Code: `TimelineSectionStripVariations.cpp`, seams
  `applyVariationMenuChoice` and the protected `openVariationMenu`.
- **Clip menu, New variation of this part:** on a clip that starts inside a placement, after Make unique. The
  clip's track (and any the placement's variation already owns) becomes the placement's own in a new variation
  and the placement plays it; one undo step, an outline on the placement and a toast such as "Verse B: Lead has
  its own part now. Switch with the B chip." (`TimelineClipLaneArea::onVariationAdded`).
- **Strip menu** (right-click empty strip): Make sections from markers.
- **From a range:** with the Range tool, the range menu's **Make section** (after "Delete range and close
  gap") or Shift+Cmd+J (`timelineMakeSection`, rebindable) turns the range into a section.
- **The key from anywhere on the timeline:** Shift+Cmd+J works with focus on the strip, a lane or the panel
  (the panel handles it; MainComponent forwards it while the timeline is shown): it makes a section from the Range
  tool's bars, else from the loop while looping is on (`MakeSectionKey.h`, `makeSectionKeySpan`). With neither, a toast says
  "Select bars with the Range tool or set the loop, then press it again to make a section"; over a placed section
  a toast says the bars already hold one. The empty strip's tooltip ("Drag across bars to make a section (follows Snap; hold Shift to place it freely)") adds
  ", or press Shift+Cmd+J to make one from the loop" (or "the selected bars") only when the key would make one.
  The key does nothing with a clip editor open.
- **Clips follow:** a clip inside a section that plays more than once draws the link mark; its tooltip reads
  "Follows Chorus (plays 3 times). Edit any Chorus to change them all." and a screen reader adds ", follows
  Chorus". A clip on a track its variation owns reads "Belongs to Verse B (plays 2 times). Edit any Verse B
  to change them all." (plays: the placements on that variation; "Belongs to Verse B, the only one." when it plays
  once, which draws no mark), a screen reader adds ", belongs to Verse B", and a shared track counts every
  placement that shares it.
- **Edge lines:** each block's start and end draw as a faint line down the lanes.
- **Settings (Preferences > Timeline):** "show the Sections strip" (`timelineShowSectionsStrip`, default on) and
  "dropping a section on other music" (`timelineSectionsCoverOnDrop`; a Label + ComboBox, "Insert (the music after
  moves later)" by default, or "Replace the sections under it"; a hint under it says Shift does the other one, that time only).
  The words for both ways of landing (insert = make room, replace = cover) are one struct, `SectionDropWords`
  (`SectionDropWords.h`), read by the drop hint, the block menu item and this row so they cannot drift.
- **Drop hint:** while a block is dragged, a small pill says what a release does, from the same mode the release
  commits (the setting, flipped while Shift is held): "Release to insert here" or "Release to replace the sections
  under it"; with Option (a copy) "Release to insert a copy here" or "Release to replace them with a copy". It sits in
  the band just under the strip (above it only when the window ends before the pill would fit), centred on the drop
  line and kept inside the strip's width, so it never covers a section, the ghost or the line. It is painted in the
  theme's inverse (the primary text colour as its fill, the page background as its words, an accent rim), so it stands
  out from the strip and the clip lane and never reads as a section block in any theme. Its rect and colours come from
  `SectionDropHintLook.h` (`dropHintPillRect`, `dropHintColours`), pure functions the guard enumerates. The strip clips its own
  paint, so the pill is drawn on a click-through layer (`SectionDropHintOverlay`, a child of the window's top-level
  component) that exists only while the hint shows. A make-room move of a group with unselected blocks between its
  members adds ", the section between comes too" (or ", the N sections between come too"). The words are true for
  earlier and later drops and for groups. The pill is sized by the longest phrase the drag can show, so the Shift
  cross-fade never resizes it. Hidden when the pointer is above the strip or after Esc. Its opacity follows the lift;
  when Shift flips the mode the old words fade into the new over 120 ms (`SectionStripMotion::startHintSwap`). While a
  block is carried, the original keeps no selection outline or focus ring (its ghost wears them), so Shift's cover
  mode, which leaves the dimmed original in place, shows no stray box there. Test seam `getDropHintTextForTest`;
  guards `TimelineSectionStripDropHintTest.TheWordsNeverIntersectASectionTheGhostOrTheDropLine` and
  `SectionDropHintLook.*` (every line position and window edge; contrast against the strip, the lane and every grey
  section in all four themes).
- **After a delete:** the section after the deleted one (else the one before it) is selected and carries the focus
  ring, so the keys keep working from where they were (`SectionDeleteSurvivor`); a delete of several picks one
  survivor, and only when the strip is empty does the whole-strip ring show. Undo restores the old selection.
- **Motion** (`SectionStripMotion`, one `AnimationDriver` per effect, nothing running at rest): a new block grows
  in over 160 ms and a removed block's picture shrinks away over 180 ms, each from or toward its origin (see
  [Motion](#motion), "Origin"); a dragged block lifts over 100 ms
  and settles over 140 ms; the drop hint's words cross-fade over 120 ms; when `getSectionStampRevision()` changes, every placement in
  `getLastStampedPlacements()` shows a 1 px accent outline that fades over 400 ms. Undo of a delete grows the block
  back; a project load grows nothing. Reduce Motion turns the grow and shrink into an 80 ms fade and the outline
  into an 80 ms fade, and the lift lands at once (a move has no fade form); Animations: Off, or a strip that is not
  on screen, starts nothing.
- **Accessibility:** the strip is a group titled "Sections" described as "Sections, N sections"; each block is
  a button titled like "Chorus, bars 13 to 20, 1 of 3" (", variation B" added when the section has more than one),
  described with its tooltip and "Shift+F10: menu. V: variations." and offering the screen reader's show-menu action,
  which opens the variation menu. The chip is not a separate focus stop. Block tooltips read "Chorus - 8 bars - plays 3 times".
- **Test mode:** a `timelineSections` probe publishes `sections` (`name:start:length:ordinal/count`, joined
  by `;`), `selectedSection` and `selectedSections` (every selected block's start beat, joined by `;`); `variations` lists the letter each placement plays (`A*;B*;A*`, the star only
  when its section has more than one variation) and `sectionVariationCount` counts the selected block's section's
  variations (0 with nothing selected). While a block drag shows its words, `dropHint` holds them and `dropHintBelow`
  says the pill sits under the strip.

## Model

- `Section { id, name, colourArgb, lengthBeats, variations }` and `SectionPlacement { id, section, startBeat, variation }` live on
  `TimelineDoc` (`getSections()`, `getPlacements()`). Sections are in creation order, placements sorted by
  `(startBeat, id)`. Section and placement ids share one counter (`nextSectionId`).
- Names are trimmed, non-empty and at most `kMaxMarkerTextLength`. Caps: `kMaxSections` 256,
  `kMaxPlacements` 1024.
- After every mutation and every load: lengths are finite and above 0, starts finite and at or above 0,
  placements never overlap (touching is fine), every placement names a section, and a section with no
  placement is removed (`finishMutation`, like the lone link groups).

## Variations

A section has variations A, B, C... (`Section::variations`, never empty, `[0]` is letter 0 = A, at most
`kMaxVariations` 26, creation order). Each placement plays one (`SectionPlacement::variation`). Placements that
play the same variation stay in step. A variation other than A records the tracks it owns (`ownTracks`); every
other track is shared with A. So a Verse B whose only own track is the lead has the drums and bass of every
Verse A, and an edit to those reaches all of them. A variation no placement plays keeps its music in
`SectionVariation::stored` (all tracks for A, own tracks only otherwise); the sync refreshes `stored` from a
placement that plays it after every change.

**Sync key.** For placement P (variation v) and track T the key is `v` when v is not A and owns T, otherwise 0.
Placements of one section with the same key for T form a group for T's clips and T's lanes' points
(`variationKey`). The sync runs per (section, track, key) group with the rule above: the changed placement with
the lowest start is stamped onto the rest of its group, on that track only. One mutation is still one revision,
one notification and one undo step, and `getLastStampedPlacements()` lists a placement once however many tracks
were rewritten.

**Operations** (each validates first and changes nothing when it rejects):

- `addVariation(placement)`: a full, independent copy of the placement's music (owns every non-Automation
  track) that the placement then plays. Returns the lowest unused letter, or -1.
- `addVariationForTrack(clip)`: "New variation of this part". Owns the placement's current own tracks plus the
  clip's track; the placement plays it. -1 when the clip is in no placement or at 26.
- `setPlacementVariation(placement, letter)`: rewrites only that placement, on each track whose key changes,
  with the target's music (a live placement of the same key, else `stored`). The switch is not seen as an edit.
- `playVariationEverywhere(section, letter)`: the same for every placement, as one mutation.
- `removeVariation(section, letter)`: placements on it switch to A first; A cannot be removed.
- `placeSection(..., letter)`: the new placement plays `letter`. `makePlacementUnique`: the new section's A is the
  placement's current music; the old section keeps its variations.
- Removing a track or lane erases it from every `ownTracks` and `stored`.
- `getVariation(section, letter)`, `forEachStoredVariationClip(visit)` (clips of unplayed variations only).

**Audio files.** An audio clip held only by an unplayed variation is on no track, so `AssetManager` visits
`forEachStoredVariationClip` too when it collects unused files and when it adopts recordings; its files are kept.

**File keys** (additive, `kFormatVersion` stays 1). A section with variations beyond A writes
`"variations": [{"letter", "ownTracks": [ids], "stored"}]` for letters 1..25 (A is implied); `"stored"` is written
only for a variation no placement plays, as `{"clips": {"<trackId>": [clip...]}, "points": {"<laneId>": [point...]}}`
in the tracks' clip and breakpoint dialect. When no placement plays A, the section's own `"stored"` holds A's music.
A placement writes `"variation": n` only when n is not 0. `fromVar` and `validateTimeline` reject a bad or repeated
letter, letter 0 in the list, a placement naming an absent letter, an unknown key, an `ownTracks` or stored key
naming a track or lane the file lacks (or that the variation does not own), a stored clip or point that fails the
normal checks or starts outside the section, and more than `kMaxStoredVariationClips` stored clips. The validator
also refuses a stored clip that names an audio asset and counts stored notes in the document budget. An unplayed
variation without `"stored"` loads empty; an old file loads as A only.

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

A clip made by a gesture (an empty-lane double-click or the Draw tool) is cut at the end of the placement it
starts in (`TimelineDoc::capToPlacementEnd`), so a new clip never crosses into the next section for this sync to
rewrite. Dragging an existing clip's right edge past its section's end, or its left edge before the section's start, stops there and, on release, toasts "To make this clip longer, make the section longer first." (`TimelineClipLaneArea::resizeHeldBySection_`). Moving, copy-dragging, nudging by keyboard, pasting or duplicating a clip so it would run past its section's end trims the clip's end to it (`TimelineDoc::capClipToPlacement`, same undo step) and toasts "This clip now ends at its section's end. Hold Option to make the section longer instead." (linked duplicates are not trimmed). An Option copy-drag or Paste and Grow Section (Option+Cmd+V) instead makes the section longer (`TimelineDoc::growSectionsToFit`: to whole bars, every repeat by the same amount, later music and blocks shift, one undo step) and the clip keeps its length; the strip's blocks ease their right edge to the new length (`TimelineSectionStrip::lengthGlide_`, same time and curve as a block gliding to a new start; Reduce Motion and Animations Off follow the shared tween). In the piano roll, Extend (the prompt after resizing notes past the clip end) stops at the section's end with the same "To make this clip longer, make the section longer first." toast, and a clip already ending there gets the toast instead of the prompt.

One edit is one revision, one notification and one undo step. `getSectionStampRevision()` and
`getLastStampedPlacements()` (runtime only) tell a UI which placements were rewritten.

A load never stamps: a file whose repeats differ keeps them as they are until one is edited, then that one
wins. Compound ops hold a `TimelineDoc::SectionEditScope`, which suspends the sync so a half-finished edit
never stamps; the last scope to end runs one sync, and only when it would change something.

## Operations

`makeSection`, `placeSection`, `movePlacement` (the beat passed is the start the placement gets after the edit;
`SectionDropMode::MakeRoom` takes a moved placement's bars out first, closing its gap, then shifts everything from
the target later; `Cover` overwrites what is under the drop), `removePlacementKeepMusic`, `deletePlacementAndBars` (deletes the bars with their full shift, then
opens the same length again where the touching run ends, so only that run moved; one undo step),
`makePlacementUnique` (moves one placement to a new section; rejected for a section's only placement),
`movePlacementsTogether` and `placeSectionsTogether` (the group forms: each member keeps its distance from the first one's start, the first
at the beat given, make room opening the span once; a make-room group move takes the whole span out and puts it back at
the landing with the sections, loose clips, points and markers between the members, `moveSpanTogether`; one id behaving exactly as `movePlacement` / `placeSection`; the copies read each section's music before
anything changes, so a copy that covers another member's original still places its music),
`renameSection`, `setSectionColour` and `makeSectionsFromMarkers` (markers stay; a repeated label joins one
section only when its span has the same length and identical content, otherwise it becomes "Chorus 2").
`insertTime` and `deleteTime` edit the whole song: clips, lane points, markers and placements all move; a
placement that strictly contains an insert point stops being a placement and keeps its music. Every op
validates first and changes nothing when it rejects. Ops that may split or trim audio take `secondsPerBeat`.

## Motion

A section edit moves the music with the blocks instead of popping it, in the clip lanes and on the strip alike.

- **Same clips.** `movePlacement` and `movePlacementsTogether` stamp the moved music back with the clips' and notes'
  own ids (`stampContent(..., keepFreeIds)`, checked against every id in the doc so none is ever shared); a copy
  (`placeSection`, `placeSectionsTogether`) and a repeat stamp still get fresh ids. A moved clip is therefore the same
  clip before and after, and the lane glides it like any other moved clip.
- **One change per edit.** A compound section op notifies once per step (delete the old bars, open the new ones, stamp
  the music). `isSectionEditUnfinished()` is true from the op's first step to its last, and the last step bumps
  `getSectionEditRevision()` (move, place, group move, group place, delete with bars; not `makeSection`, `insertTime`
  or `deleteTime`; runtime only, never saved). The lane and the strip skip a notification that is unfinished with an
  unchanged revision, so they diff the finished edit against the state before it.
- **Clip lanes** (`ClipLaneEnterExit`, `TimelineClipLaneSectionGlide.cpp`): a restore and a finished section edit glide
  every clip that changed; a repeat stamp glides only the clips in the placements it rewrote (never the one being
  edited); any other edit lands at once. A clip that arrives or leaves with a section block grows from and shrinks
  toward that block's origin (Origin, below); one that comes or goes on its own grows from its left edge and shrinks
  toward its centre where it was (180 ms, drawn by the real `paintClip`). A clip on a new or deleted track
  goes with its row, a doc replaced with no clip surviving (a load) moves nothing (an undo or redo is never a load, so
  redoing the delete of the only clip's section still shrinks it away), more than 64 arriving or leaving
  clips land at once, and a clip that arrives exactly where one left draws as the same clip. Reduce Motion: moves land
  and fade in, leaving clips fade over 80 ms; Animations Off or off screen: nothing.
- **Origin** (`SectionMotionOrigins`, one rule the strip and the clip lane each run on the same doc changes). Blocks
  that come or go together (a run touching end to start) share one origin, `MotionOrigin` Left, Right or Centre,
  anchored on the run's edge or each block's own middle. A run that repeats, in order, the sections of the blocks it
  touches (Cmd+D, Place again after, an Option-drag copy dropped beside its original) grows out of that neighbour's
  edge; a make-drag grows from the side it was anchored on; a menu make and a copy dropped away from its original grow
  from the middle. A forward delete or cover shrinks toward the middle. An undo or redo replays the edit backwards:
  every placement keeps the origin it last grew from or shrank to, so Cmd+D then Cmd+Z shrinks the copy back into the
  original's right edge and delete then Cmd+Z grows the block from the middle it shrank to (a history from before the
  strip first saw an edit falls back to the copy rule). Clips that arrive or leave with a block scale toward the
  block's anchor in the same time and curve (a clip that comes or goes on its own, in a repeat stamp, still grows from
  its left edge and shrinks toward its middle). When blocks leave, the blocks and clips closing up behind them glide
  over the exit's 180 ms, and an edge exit uses their `easeOutCubic`, so the copy's far edge and the returning
  neighbour stay one line, the copy painted under it. An undo or redo while a grow or shrink still runs starts the
  reverse from the size it is drawn at. Reduce Motion: a plain 80 ms fade; Animations Off: at once.
- **Strip.** `trackChangesForMotion` is the one place a block leaves: every menu delete, Delete key, cover drop, undo
  and redo shrinks each removed block exactly once, from its picture cached at the last change, gated like the grow
  (nothing for a load, a replace its `RestoreWatch` does not see as an undo or redo, nor for more than 8 at once). A block whose start changed glides there.
  Leaving blocks are painted under the blocks that stay, so a cover drop's incoming block always lands on top of the one
  it covers, and that one's exit starts at the dimmed opacity the drag showed it with (`Ghost::alpha`), never at full.
- **Settle.** A dropped block is drawn by its ghost until the lift reaches 0, then by the real block. The ghost hands
  over as the same picture: its lifted look (raise, shadow, 1 px outline) goes with the lift, and the at-rest look the
  real block will have (the 2 px selection outline, the focus ring on the anchor) comes in with `1 - lift`, so no frame
  of the landing pops.

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
in the next palette colour from `SectionPalette.h`; a name the project already uses becomes "Chorus 2") and `placeSection` (`name`, `beat`: play it again, covering the empty bars), then any
`writeLane` ops. Both run inside the batch's one undo step (`TimelineOpsSections.cpp`); a name made twice in the batch, a
`placeSection` naming no section, an overlap or a cap rejects the whole batch. A `writeLane` that comes after is
written over every bar, then the sync copies the first placement's points onto each repeat, so every repeat ends
up with the automation of the first one. See [timeline ops](../ai/timeline-ops.md).

Tests: `Tests/Timeline/TimelineOpsSectionTests.cpp`, `Tests/Timeline/TimelineDoc/TimelineDocSectionsTests.cpp`, `TimelineDocSectionFileTests.cpp`.
