# Factory Preset Positions

Factory preset module positions follow a fixed column and row grid, authoritative in
`Source/PresetManager.cpp`. They are hand-placed coordinates, not an `autoArrange` result — see
[layout](layout.md#auto-arrange) for the algorithm that arranges a live patch.

**The column x-positions are not uniformly strided.** They reflect actual module widths plus a
clear gap of at least 12 px:

| Column | x | Contents |
|---|---|---|
| Col 0 | 10 | IO nodes, Sequencer, MIDI Keyboard |
| Col 1 | 350 | Oscillator |
| Col 2 | 650 | Filter |
| Col 3 | 950 | VCA |
| Col 4 | 1250 | FX chain (Distortion / Delay / Reverb) |
| Col 5 | 1560 | Audio Output |

The stride between columns is roughly 300 px and variable — **not** the uniform 360 px that
auto-arrange advances a single-width column by. The two are separate concepts.

| Row | y | Contents |
|---|---|---|
| Signal row | 10 | Osc, Filter, VCA, FX chain |
| Sequencer row | 560 | Sequencer (bottom edge 940) |
| Modulator row | 600 | AmpEnv, FilterEnv, LFO |
| Keyboard row | 960 | MIDI Keyboard |

## Sequencer-adjacent envelopes

Presets 0, 1 and 5 contain a Sequencer at x=10, whose right edge is 570 at
`kDoubleWidth` = 560 (see [module-card](module-card.md#width-buckets)). The envelopes beside it are
positioned to clear it:

- **AmpEnv**: x = **584** (570 + a 12 px gap + 2 px grid ceiling)
- **FilterEnv**: x = **880** (584 + 280 + a 12 px gap + 4 px grid ceiling)

Presets 2, 3, 4 and 6 have no Sequencer-adjacent envelope and need no such offset. The
`AllFactoryPresetsLoadWithoutOverlap` test and the `estimateModuleSize` mirror in
`Tests/Project/PresetManagerTests.cpp` are updated atomically with any preset data change, so the
positions above and the widths they assume can never drift apart.

## Poly Pad routing

The Poly Pad factory preset routes **Amp Env to the VCA's per-voice CV** (PolyBus, VCA ports 8-15)
only. There is **no** Amp Env to Osc Level (ch12) DirectCV connection in this preset.
