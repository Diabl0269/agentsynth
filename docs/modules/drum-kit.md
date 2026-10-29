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
  - An `instrument` pad is kept in the state but is silent for now.
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
  - Otherwise a one-line hint.
- **Controls.**
  - The picked pad's dot, name and "plays note D1 (38)".
  - Then Level, Tune and Decay, plus Start for a file pad. The panel's height is the same for every pad.
  - Decay dims while the pad plays a recording.
  - The knobs re-bind to the pad's own parameters, so a drag is one undo step like any card knob.
  - Switching pads cross-fades the panel: out over 80 ms, in over 160 ms. Under Reduce Motion it is an 80 ms fade in;
    with Animations Off it is instant.
- **Undo.** A count change is one undo step, and the card resizes to match.
- **Accessibility.**
  - The stepper reads "Pads, 16 of 32 maximum".
  - The grid is one Tab stop after the stepper: group "Drum pads". Each pad is a child button named "Snare pad,
    built-in sound, 2 of 8", which a screen reader can press.
  - A pick announces "Snare picked".
  - The panel is the group "Snare controls", and its knobs read "Level: Snare".
  - Escape on a knob returns focus to the grid.
- **Test mode.** The grid (`drumPads`) reports `padCount`, `pickedPad` and `lastPlayedPad` (1-based, 0 for none).
  Scenario: `Tools/ui-verify/scenarios/drum-kit-pads.yaml`.

Not built yet:

- The panel knobs are not MIDI-learnable: the learnable registry binds one parameter per control and has no remove.
- The per-pad Sound menu and reset to the kit sound.
- Instrument pads that play.
- The Shown switch.
- Dropping a browser sound onto a pad.
