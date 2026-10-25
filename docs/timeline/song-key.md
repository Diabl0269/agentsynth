# Song key

One key for the whole song, set once in the transport bar. Sequencers, the piano roll and new clips will follow it
until they are given their own key; changing the song key changes what follows it, and never rewrites a note.

## The Key field

The transport bar shows a **Key** field after the tempo and time signature (`SongKeyField`, a `KeyField` box). A new
song reads **No key**, so nothing snaps until a key is chosen. Clicking the field, or Tab to it and Space / Return /
Down, opens the key picker (`Layout/KeyPicker/KeyPicker`) in a call-out that grows out of the field. A mouse click on
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
- **Hover to hear.** A key the pointer or keyboard rests on plays its scale once up the octave, quietly, on the first
  selected MIDI track that plays somewhere (else the first such track; never a drum kit). The track is latched for
  the run, so the last note-off always reaches the track its note-on went to; closing the picker ends it.
- **Relative** switches between a major key and its relative minor (and the two pentatonics).
- **Detect from notes** suggests the major or natural minor key that best fits every unmuted note on the MIDI tracks
  (drum kits left out), by note length (`synth::songPitchClassWeights`, `synth::detectKey`, Krumhansl-Kessler
  profiles). It highlights the suggestion; Return picks it.
- **No key** turns the song key off.

## Saved, undone, never rewriting

The key is part of the transport's document state (`TransportDoc::songKey`, `{root, mask, name}`), saved in the
project's `"transport"` object as an optional `"songKey"`. Older projects have none and load as No key; a malformed
one is dropped to No key rather than rejecting the tempo and loop beside it. `TransportService::setSongKey` changes
only that message-thread state and fires `onSongKeyChanged`; it posts nothing to the audio thread and moves no note.
The transport poll in `MainComponent` records the change as one undo step, like a tempo edit, and undo restores it
through `applyDocumentState`. New Patch resets it to No key.
