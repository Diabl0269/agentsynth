# Drum Kit invariants (Source/Modules/DrumKit/)

One rule per bullet; mechanism lives in [`docs/modules/drum-kit.md`](../../../docs/modules/drum-kit.md).

- **Shown never silences.** `Pad::shown` only decides what the card and the clip editors draw (`padShown`, `shownPads`, `setPadShown`, `showAllPads`); `processBlock` ignores it, so a hidden pad plays its note.
- **The group parameter ids (`kickLevel`, `hatTune`, `closedDecay`, ...) are an AI and automation alias surface**: never rename or drop one. Each writes every pad of its group (the hats split by closed/pedal and open), from the listener on the message thread and from the per-block poll for automation. → [drum-kit](../../../docs/modules/drum-kit.md#the-model)
- **Pads are always 32 slots**: `padCount` only decides what the card shows, every slot answers its note, and lowering the count never clears a pad. → [drum-kit](../../../docs/modules/drum-kit.md#the-model)
- **Group parameters are declared before pad parameters**, so a restore sets a group (and its pads) first and the saved pad values win. → [drum-kit](../../../docs/modules/drum-kit.md#the-model)
- **An untouched kit saves no pad state**, so an old project's JSON and render stay identical; `DrumKitMigrationTests` compares the render against the old engine. → [drum-kit](../../../docs/modules/drum-kit.md#migration)
- **Pad parameters stay out of the AI's view** (`detail::modelVisibleParam`, the catalog, automation targets, patch context, `PatchDiff`); the AI edits groups. → [drum-kit](../../../docs/modules/drum-kit.md#the-model)
- **An instrument pad stores only its module's uuid; `publishSamplerMidiWiring` is the one place that links it** to the module's `NoteInbox`, so any path that changes the graph or a pad's target ends by calling it. The audio thread only pushes into an inbox it already holds; never look a node up there. → [drum-kit](../../../docs/modules/drum-kit.md#instrument-pads)
