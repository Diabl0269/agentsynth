# Render to audio track

Turns what a MIDI or Audio track plays over a stretch of time into a real audio clip on a new Audio track right under it, the
way Bounce to New Track, Bounce in Place and Render in Place do in other DAWs. There is no options dialog.

## Three ways in, one code path

- The Range tool's right-click menu over a selection: **Render to audio track** (every covered track).
- A clip's right-click menu: **Render to audio track**. With several clips selected, each selected clip's track is rendered over
  the union of the selected clips on that track.
- The rebindable shortcut **Render Range or Selected Clips to Audio Track** (`timelineRenderToAudioTrack`, default Shift+Cmd+R;
  Ctrl+Alt+Shift+R off the Mac, where Ctrl+Shift+R is Repeat) acts on the live range, else on the selected clips, when the clip lanes have focus. Both menu items
  name it as their shortcut. The menu items are reached the way every context menu is (right-click, Shift+F10, Return on the focused row).

`TimelineClipLaneArea` works out the spans (`RenderSpan`: track plus beats) and hands them to `MainComponentRenderToAudio::request`;
the lane area never sees the graph. It asks the host which tracks qualify (`renderableTracks`).

## Which tracks

Every MIDI or Audio track in the spans whose sound reaches a mixer channel (`synth::resolveTrackChannelLink`). A track with no
channel is skipped; when none qualify the menu item is greyed with "Render to audio track (no mixer channel)" and the shortcut
reports "Nothing to render: no track in the selection has a mixer channel". A render is refused with a status-bar message while
recording or while an export or another render runs.

## What is rendered

The source channel's sound over the span exactly as heard from that channel: the instrument, its inserts, fader and pan. It does
not depend on the channel's mute or on a solo elsewhere: the render arms the strip's live tap (after mute, before the solo gate)
and temporarily un-mutes a muted source, restoring the mute afterwards ([stem export render options](../mixer/stem-export.md#render-options)).

The file is a 24-bit stereo WAV at the engine's current sample rate with a 2 second tail kept in the FILE: the clip ends at the
span, and dragging its right edge reveals the tail (clip resize has no cap at the file's length). All sources render in one pass over
the union of the spans; each clip starts at its own offset in that file (`sourceStartSeconds`).

The clip length in beats is the span at the project tempo. A tempo change inside the span is rendered as heard, but the clip then
plays the audio as-is: it does not follow later tempo changes (no warping).

## Where the files go

Like takes (`MainComponent::chooseTakeFiles`, stem `render`): a saved project gets `<bundle>/Audio/render-N.wav` and
`<bundle>/Peaks/render-N.agpk`; an unsaved one gets `Recordings/render-N.wav` and `.agpk` in app data with the `Recordings/` ref
prefix, which the first save adopts. The peaks file is written with `PeaksFile::writeForAudioFile`, the same accumulator a take uses, so the
waveform draws at once.

## The edit

ONE undo step (`recordGraphTimelineAndMacroChange`, queued with `callAsync` when the render completes because it rebuilds the clip
lanes):

- for each source track, a new Audio track directly below it, built like **+ Track > Audio Track** (a macro-boxed channel), named
  "<source> audio" in the source's colour with input No input, holding one clip at exactly the span, named after the source;
- the source's clips are split at the span's edges (`TimelineDoc::splitClipsAtRangeEdges`, the same as **Split at range edges**) and
  every clip now inside the span is muted.

Undo removes the new tracks and their channels and un-splits and un-mutes the source; redo restores them. The status bar says
"Rendered 2 tracks to audio".

## While it runs

The render is chunked on the message thread (`StemRunner`), so the UI never freezes. A small modal window (`RenderProgressDialog`,
"Rendering to audio") shows a progress bar and a **Cancel** button (named "Cancel render", the initially focused control; Escape does
the same). `isBounceInProgress_` is set exactly as for an export, so autosave, recording and graph edits wait. Cancel or a failure
deletes every temporary and rendered file, adds no track and restores the engine, transport and mutes; the status bar says
"Render cancelled" or why it failed.
