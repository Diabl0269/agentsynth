# Drum Kit pads

The Drum Kit plays **pads**. A pad is one note that plays one sound. The drum voices are listed in
[`modules.md`](modules.md#drum-kit-module), and recorded kits are covered in
[`sampler-instruments.md`](sampler-instruments.md#drum-kit). Code: `Source/Modules/DrumKit/` (`DrumKitPads.h` for the
table, `DrumKitModulePads.cpp` for the message thread, `DrumKitModulePadAudio.cpp` for the audio thread) and
`Source/UI/Graph/ModuleComponent/DrumKitPads/` for the card.

## The model

- **Always 32 slots.** `padCount` (4 to 32 in steps of 4, default 8) only sets how many pads the card shows. Every pad
  answers its note whatever the count. Lowering the count hides pads and never deletes their settings.
- **A pad** has a kind (`kit`, `builtIn`, `file`, `instrument`), a sound (the General MIDI note of the built-in drum it
  plays, or none), the note it answers, a name, and for a file pad the file.
  - Pads 1 to 20 start on the 20 notes the kit always played, each playing its own drum.
  - Pads 21 to 32 start on free notes with no sound, so an old project's silent notes stay silent. When the count first
    shows one of them, it gets a built-in sound (China plays the crash, Tambourine and Splash play crash 2, and so on).
- **Kinds.**
  - A `kit` pad plays the loaded kit's region for its sound, and the built-in drum when the kit has none.
  - A `builtIn` pad plays the synth drum.
  - A `file` pad plays its audio file from `Start` (0 to 1 of the file). `Decay` is how long it holds, then a 50 ms
    fade.
  - An `instrument` pad plays a module on the canvas: see [Instrument pads](#instrument-pads).
  - Picking a kit on the card makes every pad a `kit` pad and turns every Recorded switch on, in the load's undo step.
- **Per-pad knobs.** Each pad has its own `pad<N>Level` (0..1), `pad<N>Tune` (-12..+12 semitones), `pad<N>Decay`
  (0.02..4 s, centred on 0.4 s) and `pad<N>Start` (0..1). These are ordinary automatable parameters. They are hidden
  from the AI's schema, catalog, automation targets and patch diffs.
- **Group parameters** (`kickLevel`, `hatTune`, `closedDecay`, ...) keep their ids. Setting one writes every pad that
  plays a drum of that group. The hats split: `closedDecay` reaches the closed and pedal hats, `openDecay` the open hat.
  The write happens on the message thread from a parameter listener. Automation sets values on the audio thread with
  no listener call, so a per-block poll writes the pads there. On a restore, group parameters come before the pad
  parameters, so saved pad values win.
- **Two pads on one drum both sound.** Each pad has its own voice. A closed or pedal hat chokes every open-hat pad
  (synth, file or recording).
- **State.**
  - `"padCount"` and `"pads"` (one object per slot: `kind`, `sound`, `note`, `name`, `shown`, `instrument`, `pitch`),
    plus `"padFiles"` (32 paths, registered in `ModuleFileRefs` so a project collects them).
  - All of it is written only when the table differs from the defaults, so an untouched kit saves what it saved before.
  - Everything is clamped on read, and a file pad without a file reads as a built-in pad.
  - File pads decode on `DeferredAssetLoads` while a project opens.
- **Threads.** The table and the decoded samples reach the audio thread as one immutable `PadSnapshot` under a
  try-lock (as with `InstrumentPlayer`). Replaced snapshots and samples wait in message-thread lists until the audio
  thread lets go of them.

## Migration

A project saved before pads opens as 8 pads carrying its group values, and renders the same samples as the old kit.
`DrumKitMigrationTests` proves this:

- It saves a kit with every group off its default through `ProjectBundle`.
- It strips the pad parameters and state, so the file looks like an old save.
- It reopens the file and compares a 22-hit beat against `LegacyDrumKitRender.h`, a copy of the old engine (max
  difference under 1e-4). A second beat, on a fresh load, plays the other note of each shared voice.
- The old kit is handed the 7-bit velocity a note-on carries, as the module reads it.
- Another test shows the comparison catches a kit that ignored the saved values.

One intended difference: two notes that shared an old voice (35/36, 38/40, 42/44, 49/57, 51/59) now overlap instead
of restarting each other, and the second one starts from a fresh voice instead of the filter state the first left.

## The card

The card is 280 px wide. From the top:

1. The kit picker.
2. The master Level.
3. The **Pads** stepper:
   - "-" and "+", or Up/Down, Left/Right, + and -, step it by four.
   - Typing a number and Return sets it, rounded up to a multiple of four.
4. The pad grid.
5. A caption line.
6. The picked pad's controls.

The group tabs and the Recorded switches are gone from the card (layout revision 3). Every Drum Kit parameter except
`level` is `isEditedElsewhere`.

- **Grid.**
  - Four pads a row, pad 1 top left. Pads are 44 px tall up to 16 pads, 38 px above.
  - Each pad shows a colour dot (`drumLaneForPitch`, the drum machine's lane colours) and a kind mark in the accent: a
    wave for a file, a box for an instrument.
  - The pad's name sits under them, cut with an ellipsis.
  - The picked pad has an accent ring that slides 120 ms to a new pick.
- **Mouse and keys.**
  - A click plays the pad (a 120 ms flash) and picks it.
  - Shift+click and the arrow keys pick it without a sound. The caption then says "Closed hat picked (no sound)" for
    about two seconds.
  - Home/End go to the first and last pad. Space and Return play the picked pad.
  - Cmd, Ctrl and Alt keys pass on to the app's shortcuts.
- **Caption.**
  - "3 hidden pads keep their sounds" while pads beyond the count hold a setting of their own: a changed note, name,
    sound or file, or a knob off its default.
  - While a sound is dragged from the Sounds view: "Drop on a pad to play this sound there", or "Drop to make Snare
    play this sound" over a pad.
  - Otherwise a one-line hint: "Click to play, right-click for a sound".
- **Controls.**
  - The picked pad's dot, name and "plays note D1 (38)".
  - The **Sound** row: what the pad plays ("Snare, built-in", "kick.wav, audio file", "Pluck 1, instrument") on a
    button that opens the Sound menu, plus Open for an instrument pad. See [A pad's sound](#a-pads-sound).
  - Then Level, Tune and Decay, plus Start for a file pad or Plays pitch for an instrument pad. The panel's height is
    the same for every pad.
  - Decay dims while the pad plays a recording or an instrument.
  - The knobs re-bind to the pad's own parameters, so a drag is one undo step like any card knob.
  - Switching pads cross-fades the panel: out over 80 ms, in over 160 ms. Under Reduce Motion it is an 80 ms fade in;
    with Animations Off it is instant.
- **Shown.** Each pad can be hidden: see [Hiding a pad](#hiding-a-pad).
- **Undo.** A count change is one undo step, and the card resizes to match. So is every Sound choice, a reset, a
  pitch change and a drop.
- **Accessibility.**
  - The stepper reads "Pads, 16 of 32 maximum".
  - The grid is one Tab stop after the stepper: group "Drum pads". Each pad is a child button named "Snare pad,
    built-in sound, 2 of 8", which a screen reader can press.
  - A pick announces "Snare picked".
  - The panel is the group "Snare controls", and its knobs read "Level: Snare".
  - Escape on a knob returns focus to the grid.
  - The Sound button reads "Sound for Snare, Pluck 1, instrument"; Return or Space opens its menu. Open reads "Open
    Pluck 1 on the canvas". Plays pitch is a stepper like Pads.
  - Shift+F10 (or the context-menu key) on the grid opens the picked pad's menu.
- **Test mode.** The grid (`drumPads`) reports `padCount`, `pickedPad`, `lastPlayedPad` (1-based, 0 for none),
  `pickedName`, `pickedKind`, `pickedSound`, `visiblePads` (the shown ones), `soundOutline` (a sound drag is over the card) and `dropTargetPad`.
  The Sound row is `drumPadSound`, Open is `drumPadOpenInstrument`, Plays pitch is `drumPadPitch`.
  Scenarios: `Tools/ui-verify/scenarios/drum-kit-pads.yaml`, `Tools/ui-verify/scenarios/drum-kit-pad-sound.yaml`, `drum-kit-pad-shown.yaml`.

## A pad's sound

The Sound row's button opens the **Sound menu** (`DrumPadSoundMenu`). Right-click on a pad, or Shift+F10 on the grid,
opens the **pad menu**: Play, Replace from file..., Reset to kit sound, Hide pad (H; Show pad when the pad is hidden),
Show all pads (enabled while a pad is hidden), then the same groups inline. The current choice
has a tick.

- **Audio file**: Choose a file..., and Recent files (pad files chosen this session plus the files this kit's pads
  play, newest first, at most eight).
- **Built-in drum sounds**: Kick, Snare and Clap, and a "Hats, toms and cymbals" submenu with the other 17.
- **Instruments on your canvas**: every instrument module except a Drum Kit, a Sequencer or a Poly Sequencer
  (`DrumKitModule::canPlayFromPad`).
- **Use the kit's sound**: a submenu, named after the loaded kit, of the sounds it has a recording for.
- **Reset to kit sound** (Sound menu, after a line): the pad's default for the kit's Recorded setting. The pad keeps
  its note, name position and shown state.

Each choice is one undo step. Choosing a built-in drum on a file pad replaces the file without asking; Undo brings it
back.

### Instrument pads

An instrument pad sends a note to its module instead of playing a sound of its own. Choosing "Note only" was
deliberate: the graph does not change, and the module plays wherever its cables already go.

- **The note.** Pitch is the pad's Plays pitch (default C3, 60) plus Tune rounded to semitones; velocity is Level
  times the hit's velocity. Decay does not apply. The note-off is sent when the hit's note ends, or 0.25 s after a
  click on the card (`kAuditionHoldSeconds`). A second hit releases the first note before it starts.
- **Delivery.** Each module has a `NoteInbox` (`Source/Modules/NoteInbox.h`): a lock-free FIFO that
  `ModuleBase::processBlock` drains into the block's MIDI before the module runs, at most 32 events a block. A full
  inbox drops note-ons but never note-offs. The note lands in the module's current or next block.
- **The link.** A pad stores only its module's `uuid`. `publishSamplerMidiWiring` (run after every graph change, a
  project load and an AI patch) resolves it to the module's inbox and republishes the pads, and counts a Sampler a pad
  plays as MIDI-fed. A pad whose module is gone is silent, and its row reads "Pluck 1, removed, silent".
- **Open** selects the module's card and centres the canvas on it (or on its collapsed macro).
- Bypass, stop and all-notes-off release every held instrument note.

### Dropping a sound on a pad

A sound dragged from the Sounds view onto the pad grid goes to the pad under the pointer only, as one undo step,
and the pad becomes a file pad. While the drag is over the grid every pad has a faint accent outline, the pad under
the pointer a full one, and the caption says what the drop will do. A drop elsewhere on the card falls through to the
canvas (a new Sampler). One audio file dragged in from Finder lands on a pad the same way. After the drop the pad
is named after the file and the caption says "Snare now plays kick.wav" for a moment. A sound never opens an
insert gap on the canvas, so the card stays put while the pointer travels to the pads
([layout](../layout/layout.md#making-room-for-a-module-dropped-between-others)).

Not built yet:

- The panel knobs are not MIDI-learnable: the learnable registry binds one parameter per control and has no remove.

## Hiding a pad

- Each pad has a **Shown** switch under the picked pad's knobs ("Shown: Cymbal"), and **H** flips it for the picked
  pad while the grid has focus (Cmd/Ctrl/Alt+H pass on to the app).
- A hidden pad leaves the card grid and the pads after it move up. The pad shrinks over 110 ms and the others slide
  over 160 ms; Reduce Motion is an 80 ms fade and Animations Off is instant. The card height follows the rows.
- A hidden pad stays picked if it was, so the switch can bring it back. Arrow keys, Home and End skip hidden pads.
  A pad's name for a screen reader counts the shown pads ("Cowbell pad, kit sound, 7 of 7").
- The caption line reads "N hidden ·" followed by a **Show all** link (Tab-reachable, focus ring, named "Show all
  hidden pads"). Show all switches on every pad, also those beyond the count, as one undo step. A pick hint
  replaces the link for its two seconds.
- **Hiding never mutes.** A hidden pad keeps its sound and note, so its hits still play. Hiding only decides what the
  card, and the [drum machine](../timeline/drum-machine.md#a-drum-kits-pads-as-rows), draw.
- A hidden pad's hits are not drawn in the timeline clip's note preview either, and the other rows close up.
- Hiding a pad whose drum has hits in a clip of a track playing the kit asks first: "Cowbell has 1 hit; hide it
  anyway?", with a "Don't ask again" box (user setting `drumKitAskBeforeHidingPad`). Showing never asks.
- The pad menu (right-click, Shift+F10) has the same two actions: Hide pad or Show pad, and Show all pads.
- Each switch, and Show all, is one undo step. `shown` is saved per pad; a kit with every pad shown writes nothing
  new, so older projects load as before.
