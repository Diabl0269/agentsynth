# Filter Drive

Drive (1-10, default 1) on the Filter module is one stage shared by every type that has it: a gain plus a soft clip
(`synth::CustomFilterDrive`, `Source/Modules/Filter/CustomFilterDrive.{h,cpp}`) that runs **ahead of** the filter. It
acts on the six ladder types (LPF24, LPF12, HPF24, HPF12, BPF24, BPF12) and on Custom, with the same curve. The
filter then works on the driven signal, so a high-pass or band-pass is driven before it removes anything.

- **Curve**: small-signal gain per knob position (the level change of a quiet signal; a loud one is squashed by the
  stage's ceiling of 1.1 sooner). Fitted to a commercial reference synth's low-pass drive; the law and the fit are in
  [`custom-filter.md`](custom-filter.md#drive).

  | Knob | Travel | Small-signal gain |
  |---|---|---|
  | 1 | 0 % | 0 dB |
  | 3.25 | 25 % | +7.7 dB |
  | 5.5 | 50 % | +14.1 dB |
  | 7.75 | 75 % | +19.4 dB |
  | 10 | 100 % | +24.0 dB |

- **Drive 1 is untouched**: the stage is skipped and the samples are bit for bit what the filter alone made, so a
  patch that never turned Drive sounds as before (`FilterLadderDrive.DriveOneIsBitForBitWhatTheLadderAlwaysDid`).
- **Notch has no Drive**: it ignored the knob before and still does. The knob's readout and CV still follow.
- **The ladder's own drive is fixed**: the ladder (`synth::ResonantLadder`) is set to Drive 1 once in
  `prepareToPlay` and never changed, so the knob is not applied twice. The ladder's headroom scaling therefore stays at
  its Drive 1 value, and Resonance behaves the same at every Drive.
- **Per block, per channel**: the knob and its CV (`drive + 9 * cv`, clamped to 1-10; ch3 in mono, ch10 in poly) are
  read once per block and ramped linearly across it; the knob goes through the 5 ms smoother. Both legs and every
  audible poly voice have their own oversampler; a silent channel costs nothing. History is cleared on entering a
  ladder type or changing the voice count (`FilterModule::applyLadderDrive`, which drives a second `CustomFilterDrive` owned by `CustomFilterEngine`
  so `FilterModule.h` includes no new header). No allocation on the audio thread.
- **Response view**: the ladder types draw their curve raised by the stage's small-signal gain
  (`customfilterdrive::smallSignalGain`) for the modulated Drive; Notch draws no lift.
