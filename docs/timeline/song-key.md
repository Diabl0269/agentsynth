# Song key

One key for the whole song, set once in the transport bar. Sequencers, the piano roll and new clips will follow it
until they are given their own key; changing the song key changes what follows it, and never rewrites a note.

## The Key field

The transport bar shows a **Key** field after the tempo and time signature (`SongKeyField`, a `KeyField` box). A new
song reads **No key**, so nothing snaps until a key is chosen. Clicking the field, or reach it with the keyboard and press Space or
Return (Down moves on to the next control), opens the key picker (`Layout/KeyPicker/KeyPicker`) in a call-out that grows out of the field. A mouse click on
the field never takes keyboard focus, so the next copy or paste still goes to the clips.

The **Set Song Key** command (`setSongKey`, default Shift+Cmd+K, rebindable in Settings) shows the Timeline and opens
the same picker from anywhere.

## The key picker

- **Search.** Type a root and a scale: `d dor` lists D Dorian first. A first word that is a note name (`d`, `F#`,
  `bb`, `Eb`) sets the root; the remaining words filter the scales with the shared search (`SearchMatch.h`). With no
  root typed, the picker keeps its current root (`blues` shows Blues at the current root).
- **Keyboard strip.** Two octaves light the highlighted key's notes, the root brightest. Clicking a key there moves
  the root; the highlight stays on the same scale.
- **List.** Grouped as Common, Modes, Pentatonic and blues, More, then **Your scales** (the user scales saved from
  the piano roll's Scale Assist). Chromatic is not a key. Up / Down move, Return picks, Escape closes unchanged.
- **Hover to hear.** A key the pointer or keyboard rests on plays its scale; [Hearing a key](#hearing-a-key).
- **Relative** switches between a major key and its relative minor (and the two pentatonics).
- **Detect from notes** suggests the major or natural minor key that best fits every unmuted note on the MIDI tracks
  (drum kits left out), by note length (`synth::songPitchClassWeights`, `synth::detectKey`, Krumhansl-Kessler
  profiles). It highlights the suggestion; Return picks it.
- **No key** turns the song key off.

## Hearing a key

Every key picker (the Song key, the piano roll's scale Key, a Sequencer's Key and a Follow field) plays the key the
pointer or keyboard highlight rests on, after a 150 ms pause (`KeyPicker::onAudition`, one `Layout/KeyPicker/KeyAuditioner`
per owner). The run goes up one octave from the root at middle C's octave (root, each scale note, the root again), a
note every 140 ms, quietly. Leaving the key or closing the picker ends it, and the last note is always switched off
where its note-on went.

- **Where it plays.** The Song key picker plays on the track chosen in its **Preview sound** row (**Default sound**, or
  any instrument track; a drum kit is never offered). The choice lasts for the session and the track list is read when
  the picker opens; a track that is gone falls back to the default sound. The piano roll plays on the open clip's
  track, a Sequencer through its own note output (one short note at a time, about 300 ms each, as its step grid does).
- **Default sound.** With no instrument to play on (no track chosen, a track with no instrument, a Sequencer that is
  bypassed or has no MIDI cable) the run is a built-in sound rendered by `KeyAuditionSound` and played by the engine's
  sound preview ([audio engine](../architecture/audio-engine.md#sound-preview)), so a key is always audible, even in
  an empty project. It shares the preview's gain with the Sounds view and is never in a bounce. It plays as one run, so
  closing the picker cuts it only while it is still ringing.
- **Switch.** Settings, Timeline, **Play the scale when browsing keys** (on by default,
  `playScaleWhenBrowsingKeys`; [settings](../layout/settings-preferences.md)) turns hover-to-hear off everywhere. It is
  read each time an audition starts.

## Saved, undone, never rewriting

The key is part of the transport's document state (`TransportDoc::songKey`, `{root, mask, name}`), saved in the
project's `"transport"` object as an optional `"songKey"`. Older projects have none and load as No key; a malformed
one is dropped to No key rather than rejecting the tempo and loop beside it. `TransportService::setSongKey` changes
only that message-thread state, a lock-free mirror the audio thread reads (`getSongKeyPacked()`: valid bit, root,
mask), and fires `onSongKeyChanged`; it queues no transport command and moves no note.
The transport poll in `MainComponent` records the change as one undo step, like a tempo edit, and undo restores it
through `applyDocumentState`. New Patch resets it to No key.

## Following the song key

Things that use a key follow the song key by default and can be given their own. Each one's Key field is a
`Layout/FollowKeyField`: "Song key · D Dorian" in the muted colour while following, "A Phrygian · own" otherwise
(screen reader: title "Key, following song key" or "Key, own key", value the text shown). It opens the key picker with the
Follow song key row on top (ticked while following, the song key at its right). When the key a follower plays changes
under it (a new song key, an undo), the field glows once for 300 ms; picking or following is the user's own change
and does not glow, and reduced motion only changes the text. Built for the Sequencer and Poly Sequencer
([sequencer-design.md](../modules/sequencer-design.md#key)).

## Piano roll clips

Each MIDI clip follows the song key unless it has its own (`Clip::scale`); the piano roll's scale panel shows which
(**Song key · D Dorian**, or the own key) and its key picker has the **Follow song key** row. The key in force drives
highlighting, snapping new notes, Generate and the note range; nothing plays differently. `synth::resolveClipKey`
(`Source/Timeline/ClipKey.h`) is the one query. Details: [scale-assist](scale-assist.md#following-the-song-key-or-its-own).
Fit to key (the piano roll chip, Option+Shift+Q, or the clip's right-click menu) moves notes to the key; setting a key never moves notes.

`KeyField::setFollowing` gives any key field that can follow the follow display: the muted **Song key ·** prefix, the
tooltip ("Following the song key. Click to give this its own key." / "Own key. Choose Follow song key to follow
again.") and the accessible title ("Key, following song key").
