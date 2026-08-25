# FX Modules Reference

Technical documentation for the Agent Synth effects suite.

## Stereo I/O (Dual I/O toggle)

Every FX module (and Voice Mixer outputs) processes a fixed **raw** stereo pair on channels 0/1.
That is always stereo DSP — Dual I/O is **not** a mono/stereo switch, and turning it off does not
sum to mono or drop a channel. JUCE freezes the bus layout at construction, so the UI cannot grow
or shrink the channel count — it only changes how many jacks you see:

| Dual I/O | Visible audio jacks | Wiring |
|---|---|---|
| **Off (default)** | One `"Audio"` in and out | Dragging onto the jack fans both raw L/R legs (`polyVoiceSpan == 2`). A mono source is duplicated onto both legs. |
| **On** | Separate `"Left"` / `"Right"` | Independent cables per leg — for mid/side tricks, dual-mono patches, or feeding only one side. |

CV jacks (Drive, Rate, …) keep their **raw** channel indices so presets and the AI schema stay stable;
in single-jack mode they simply shift one slot down in the visible column.

### The toggle is inherited, not registered

**A module never opts in.** `ModuleBase`'s constructor adds the `dualIO` parameter itself when the
module's channel shape says stereo — `ModuleBase::hasStereoOutputPairShape`: **≥ 2 inputs and exactly
2 outputs**, i.e. audio on raw ch0/ch1 with any further inputs being CV. That shape also gets the
collapsing output jack for free: `hasCollapsibleOutputPair()` drives `ModuleBase`'s default
`getOutputPortLabel` / `getVisibleOutputPortCount` / `mapOutputChannel`, so **a new stereo FX needs no
Dual I/O code whatsoever** — declare `ModuleBase("Foo", 4, 2)` and the toggle, the `"Audio"`/`Left`/
`Right` labels and the two-leg fan are all there.

Deliberately **output-side only**. Whether ch0/ch1 are an *input* pair cannot be inferred from the
shape — Voice Mixer's ch0-7 are eight voice inputs and the Ring Modulator's ch0/ch1 are Carrier and
Modulator, two unrelated mono jacks — so an input pair stays an explicit declaration through
`mapStereoPairInput` / `stereoInputLabel` / `stereoVisibleInputCount`.

The exceptions are stated in the constructor call, and there are only three kinds:

| `ModuleBase::StereoAudio` | Modules | Meaning |
|---|---|---|
| `Auto` (the default) | every FX, Voice Mixer, Ring Modulator | shape decides; toggle ships **collapsed** |
| `Declared` | Oscillator, Wavetable, Filter, VCA, Sampler | a second audio leg the shape cannot see — its own `kRightBase` block, or a ch0/ch1 pair alongside further outputs. Toggle ships **split**; the module owns its jack maps. |
| `None` | Comparator, Rec Tap | the shape matches by accident. Comparator's ch0/ch1 are Signal + Threshold CV in and Gate + inverted Gate out (no audio output at all); Rec Tap is a hidden recording tap whose two channels are the take's capture pair, wired by the record flow and never patched. |

Why it moved: `addDualIOParameter()` was a call you could forget, and the Ring Modulator did ship a
stereo output pair without it — no header toggle, no Preferences row, and nothing red.
`StereoDeclaration.EveryFactoryModuleFollowsTheShapeRuleOrADocumentedException` now sweeps every
factory module against the rule plus those two tables, so a new module that needs an exception fails
the build's test run until it is listed with a reason (and
`…TheExceptionTablesHaveNoStaleEntries` fails if a listed module is renamed or stops matching the
shape). `StereoDeclaration.ANewModuleWithTheStereoShapeInheritsTheWholeToggle` pins the
zero-code-required claim, and `…RingModulatorGetsTheTogglePurelyByInheritance` pins the module the bug
was reported on.

`dualIO` is consequently parameter **index 1** on every module that carries it. Saved patches do not
care — parameters are keyed by `paramID` on both the `ModuleBase::getStateInformation` and the
`AIStateMapper` paths, and neither the id nor any module's default changed
(`StereoDeclaration.APatchSavedBeforeTheMoveLoadsWithTheSameLayout` loads a patch authored before the
move and checks every layout, including the Ring Modulator falling to its default because the patch
predates its toggle). Positional `getParameters()[n]` lookups did care, which is exactly why
[the guide](Module_Development_Guide.md) forbids them: a shifted index resolves to the wrong
parameter *silently*.

Toggling Dual I/O **does not rewire existing cables away**. Raw ch0/ch1 stay connected; only jack
visibility changes. If only the left output was patched (common when the next node is Audio Output),
toggling Dual I/O on completes the pair (`L→L` / `R→R`) so the Right jack is not left hanging.
Splitting only one end of a cable still draws the Right jack: the far end's collapsed `"Audio"` jack
owns both raw legs, so the extra wire lands on that same jack rather than waiting for both modules
to split. The control is a header icon (`Icon::ModuleDualIO`, a Y-fork into two jacks) with an
on/off tooltip — not a labelled checkbox.

**Splitting hands the right leg over; collapsing hands it back.** The two directions are mirror
images, and both are `GraphEditor::completeStereoPairConnections`:

| Direction | What happens to the cable |
|---|---|
| **Split (off → on)** | The peer's right leg is wired to our new `Audio R`, **and the collapsed jack's duplicate of `Audio L` is removed from it**. While we were collapsed, one drawn cable fanned `L` onto both of the peer's raw legs; leaving that edge behind after the split sums `L+R` into the peer's right leg — +6 dB on one side, invisible, because both edges draw as the same cable. |
| **Collapse (on → off)** | A cable on a hidden right block is **re-pointed onto the surviving left leg** (same offset in the left block, so poly voice *v* pairs with voice *v*), not deleted — wherever the far end still exposes that channel. If it does not (the peer collapsed its own split block), the cable is dropped as before. |

The re-point is what keeps a collapse level across the stereo field. Without it, collapsing the
default patch's VCA dropped `VCA kRightBase → Distortion ch1` and every right channel downstream of
it rendered silence — the mix jumped left with nothing on screen to explain it. It re-points at
**unity**: `ModuleBase::panGains` keeps a centred module's left leg at full level, so the surviving
mono leg is exactly as loud as it was, and no gain compensation is applied (an equal-power law
would have made every collapse 3 dB quiet). The input side is deliberately **not** symmetric — our
left input is normally fed by the same upstream already, and re-pointing there too would sum `L+R`
into one mono jack — so an input-side cable only moves when the left leg has nothing at all.

Two rules a wiring call site has to respect, both enforced by `GraphEditor::rightAudioLegOf`:
ask `ModuleBase::rightAudioLegChannel()` for the channel (never assume ch1), and then require it to
be **reachable from a visible jack** (`GraphEditor::audioChannelReachableFromJack`). A collapsed
split-block module still reports `PortRole::Audio` on its hidden block, so the role alone is not
enough — wiring it would create a cable that is audible and impossible to unplug. A collapsed FX
pair still passes, because its one `Audio` jack owns raw ch1 through `voiceSpan == 2`.

### Splitting a split-block module that was wired mono

The table above covers a peer that *has* a right leg. When it does not — a collapsed split-block
neighbour, whose one `"Audio"` jack is its left leg alone — `rightAudioLegOf` returns -1, and the
split used to leave the toggled module's right jacks dangling. That is the reported Filter-mid-chain
case: `Audio L` in and out stayed wired, `Audio R` in and out came up empty. A module the user just
split has to arrive with **both legs wired**:

| Side | Peer has a right leg | Peer is mono-only |
|---|---|---|
| **Input** (`Audio R` in) | wire it: collapsed FX pair → its raw1, dual peer → its own `kRightBase` (never ch1) | **broadcast** — copy the channel that already feeds `Audio L` onto `Audio R` |
| **Output** (`Audio R` out) | wire it, and drop the left leg's stand-in copy | **sum** — a second cable into the same mono jack `Audio L` feeds |

Both input-side entries assume the left jack has exactly **one** feed; when it has two, the migration
rule below runs instead.

The two sides get there by different means, and the distinction matters:

- Feeding two of *our* legs from one source channel **copies** a signal — the source drives one more
  destination at the same level, so the mix cannot move. (It is the same mono→pair fan
  `resolvePolyLink` already performs for an adjacent pair, applied here to a non-adjacent
  `kRightBase` block.)
- Feeding one *peer* channel from two of our legs **sums** them. While both legs still carry the
  identical signal — the usual state right after a split — that sum is **+6 dB**. It is a transient
  jump, lasting only until the legs differ, which is the point of splitting; and it is exactly what
  dragging both legs onto that jack by hand produces, since `connectPorts` has always allowed summed
  inputs and stereo-into-mono summing is the standard convention. Wiring both jacks won out over
  avoiding the jump.

**The hidden block is still off limits.** The other candidate for the mono-only output case — wiring
onto the peer's hidden `kRightBase` — remains forbidden: the cable would be audible and impossible to
unplug. The summed cable instead targets the channel `Audio L` is already wired to, which is visible
by construction.

**Expanding a destination MIGRATES the sum, it does not add to it.** A summed cable is aimed at a
jack that stops existing the moment that destination splits, so before anything else is wired, an
audio input carrying **two feeds from the same upstream node** hands the second one to the new
`Audio R` — preferring the feed that comes off the upstream's right leg, so the pair lands
`L→L` / `R→R`. Without that step the sequence "split the source, then split the destination" left
three cables on one pair: `L→L`, the stale `R→L` sum, and a fresh `R→R` on top. The same step covers
two raws of one collapsed upstream jack wired onto the same input.

Two feeds from *different* modules are a mix the user built by hand: migration leaves them alone, and
because the jack is then not single-fed, the right-leg wire and the broadcast are both skipped too —
picking one of the two to pair or copy would choose a winner by cable order and leave the legs
carrying different mixes.

**Prefer a real right leg.** A copy of `Audio L` (input) or a summed second cable (output) only ever
stands in where there is no right leg to be had; the moment the peer has one, the real pair is wired
and the stand-in copy is removed. That single rule is what reconciles this with
`TogglingDualIOKeepsBothStereoLegs`, which pins the removal, and it is pinned from the other side by
`SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast`. Dual→dual is untouched by any of
it: both ends have real legs, so it is a plain `L→L` / `R→R` pair.

Both stand-ins are confined to **split-block** toggled modules, which is what makes the on/off
round-trip exact: their right leg is hidden again on collapse, so `dropHiddenRightLegConnections`
removes the broadcast and the summed cable for free. An FX's raw1 stays part of its collapsed jack,
so the same cable would survive a collapse and be indistinguishable from a hand-drawn one.

Confined to the toggle handler on purpose: flipping Dual I/O is an explicit action on one module,
whereas `resolvePolyLink` governs manual cable drags and smart-connect, where a split-block pair
stays left-leg-only. Nothing here widens that. The rewire rides inside the parameter gesture's own
snapshot, so it is **one undo step** with the toggle
(`SplittingAMidChainVoiceModuleIsOneUndoableStep`).

Wavetable and Sampler used to keep permanent `Audio L` / `Audio R` jacks. Since issue #219 they are
on this toggle too, along with Oscillator, Filter and VCA — see below for what "off" means on a
module whose right leg is not the contiguous ch0/ch1 pair. The Ring Modulator joined the toggle
later, having shipped with permanent `Left`/`Right` output jacks that no preference could reach.

### Split-block modules share the toggle, not the channel layout

Every stereo-capable module has the Dual I/O toggle — the FX, Voice Mixer's output, and (since
issue #219) Oscillator, Wavetable, Filter, VCA and Sampler. What differs is where the right leg
lives, and that changes what "off" means:

| Layout | Modules | Dual I/O off |
|---|---|---|
| **Contiguous pair** (raw ch0/ch1) | FX, Voice Mixer out, Ring Modulator out, Sampler | One `"Audio"` jack **owning both raw legs** — a mono source fans onto L and R. |
| **Split block** (`kRightBase`) | Oscillator, Wavetable, Filter, VCA | One `"Audio"` jack carrying the **left leg only**; the right block is hidden and unpatchable. |

Voice Mixer and Ring Modulator are **output-only**: their input jacks are not a stereo pair (eight
voice inputs; Carrier + Modulator + three CV), so only the output pair collapses. On the Ring
Modulator that also means raw ch1 must keep a non-`Audio` input role — it is the Modulator jack, and
calling it audio would make `rightAudioLegChannel()`'s ch1 look like an input right leg, so the Dual
I/O toggle would wire a neighbour's `Audio R` into the modulator input.

The split-block modules cannot collapse the FX way: a collapsed jack can only fan to *adjacent* raw
channels, and their right leg deliberately sits above the mod-CV inputs because ch1 is Waveform /
Position / Cutoff / gain CV. So on those modules the toggle picks between "one jack, left leg" and
"two jacks". Collapsed, their jack layout is exactly what it was before #219 — a Filter shows
`Audio`, `Cutoff`, `Resonance`, `Drive` again.

Two consequences worth knowing:

- Collapsing a split-block module **unhooks its right leg** (`GraphEditor::dropHiddenRightLegConnections`, reached from `completeStereoPairConnections`), because an invisible jack cannot be unplugged — re-pointing each cable onto the surviving left leg where the far end still exposes it, and dropping it where it does not (see the split/collapse table above). Collapsing an FX module unhooks nothing — its collapsed jack still owns both legs.
- Merge-mode auto-connect (and smart-connection drops onto a mono destination) wires `Audio L` only, matching what Wavetable has always done.

Anything pairing two modules' legs must ask `ModuleBase::rightAudioLegChannel()` rather than
assuming ch1 — on a voice module ch1 is CV, and wiring it as audio corrupts the patch.

**Preferences → "Split Left/Right jacks"** applies in both directions to modules already on the
canvas *and* to anything created afterwards, overriding each module's own default. Scoping it to new
modules only made it look broken — the obvious way to check a setting is to flip it and watch the
patch in front of you, which never changed.

**Preferences → "Per-module I/O defaults..."** sits next to the toggle above (same row) and opens a
popup listing every module type from the table above (FX plus Oscillator, Wavetable, Filter, VCA,
Sampler and Voice Mixer). That list is **derived, never hand-written**:
`AIStateMapper::dualIOCapableModuleTypes()` probes the module factory once and keeps every type whose
module answers `ModuleBase::hasDualIOParameter()`, and `PreferencesSettingsTab::getDualIOModuleTypes()`
is a thin wrapper over it. It used to be a literal list, and the Ring Modulator was missing from it —
the module had a stereo output pair, the popup had no row for it, the global toggle skipped it, and
nothing in the build noticed. A module that gains the toggle now appears with no second edit — and
since the toggle itself is inherited from the channel shape (below), a new stereo FX reaches this
popup without anyone writing a line of Dual I/O code;
`PreferencesSettingsTabTest.DualIOModuleTypesIsDerivedFromTheModulesThemselves` and
`…EveryDualIOCapableTypeReachesBothThePopupAndTheNewModulePath` walk the factory and check both
consumers (the popup rows and `applyDefaultDualIOForNewModule`) for every type on it, and
`ModuleComponentTest.DualIOHeaderButtonOnEveryStereoCapableModule` checks the header control.
Each row is a 3-state choice — Follow global (the default), Always on,
Always off — that overrides the global toggle for just that type. Unlike the global toggle, this is
**new-modules-only**: it does not re-lay modules already on the canvas, and there is no per-module
counterpart to `applyDualIOToExistingModules`. Both are read from the same place,
`GraphEditor::applyDefaultDualIOForNewModule`, called once from `addModuleAtCanvasPosition` when a
module is created — an override wins when the two disagree; a type with no override falls through
to the global toggle. Persisted as one `ApplicationProperties` key,
`"dualIOPerModuleDefaults"`, holding a compact JSON object (`{"Reverb": true}`) — a type absent
from the object follows the global default. `PreferencesSettingsTab::loadDualIOPerModuleOverrides`
is the one parser, used by the tab itself and by `MainComponent` to push the map into the real
`GraphEditor` at startup, the same as it re-reads `"defaultDualIOForNewModules"` there.

It is also applied once at startup, to the patch the app opens with — `MainComponent::
applyStoredDualIOPreferenceToPatch()`. That has to run **after** `AudioEngine::initialise()`, which
is what builds the default preset: the preset loader constructs its modules knowing nothing about
preferences, so they come up on their constructor defaults, and the voice modules default to dual.
Restoring the preference without this step left a user who had chosen single jacks with a split
Oscillator and Filter on every launch.

Two paths deliberately do NOT re-lay the patch: `PreferencesSettingsTab::setGraphEditor` (called
every time the Settings window opens — otherwise it would collapse jacks the user had just split by
hand), and the plugin, whose graph holds a host-restored session where each module carries its own
saved `dualIO` value. A patch the user saved carries that value too, so reloading their own work
keeps its layout; the preference governs the factory patch.

Known rough edge: collapsing frees a jack row, so a card gets ~20px shorter (and taller again when
split). Making the toggle height-neutral means reserving the dual-layout gutter in both states,
which would add a row of blank gutter to all twelve FX — they default to collapsed — and force
another rebake of the factory preset rows. `ModuleBase::getReservedInputPortCount` /
`getReservedOutputPortCount` compute the reserved counts if a follow-up wants to make that trade.

See [`modules.md § Oscillator`](modules.md#oscillator-module) and
[`§ Filter`](modules.md#filter-module) for the channel maps.

## Output Level (shared stage)

Modules whose output is **audio** carry a `Level` parameter (`outputLevel`, linear 0.0–1.0, default 1.0) so their output can be scaled without patching a VCA into the chain. Provided by `ModuleBase` as three opt-in pieces:

| Piece | Where to call it |
|---|---|
| `addOutputLevelParameter()` | in the ctor, **after** all of the module's own `addParameter()` calls |
| `prepareOutputLevel(sampleRate)` | in `prepareToPlay` — sets up the 10 ms anti-click ramp |
| `applyOutputLevel(buffer, numAudioChannels)` | at the **end of the normal `processBlock` path** |

Adopting modules: **Distortion, Delay, Reverb, Chorus, Phaser, Flanger, Filter, Bitcrusher, Pitch Shifter, Ring Modulator**.

**This is a standing rule, not a one-off.** Every *new* module whose output carries audio must have a level control — the shared stage here, or its own `level`/`gain` parameter. `Tests/ModuleAdoptionTests.cpp` enforces it: it classifies every module the factory can build into one of three buckets (shared stage / own parameter / no level by design, with a rationale), and a new module that nobody classified fails `EveryFactoryModuleIsClassified`. A renamed or deleted module fails `ClassificationTableHasNoStaleEntries`. So the decision cannot be skipped — only made explicitly.

Rules that make this safe:

- **Opt-in, never in the `ModuleBase` ctor.** "Gain" is wrong on pitch/gate CV outputs — scaling a V/oct pitch CV transposes it, and scaling a gate drops it under the `> 0.5f` trigger threshold that ADSR uses. Sequencer, ADSR, LFO, Poly MIDI, MIDI Keyboard and External MIDI therefore do **not** adopt it. Attenuverter does not either — it already *is* a gain/polarity stage.
- **Added last in the parameter list among value params.** Appending keeps every existing index pointing at the same parameter, so Level stays the last continuous control (only `muted` follows it). The Dual I/O toggle is *not* in this list any more — `ModuleBase`'s constructor adds it at index 1 (see § Stereo I/O), which is what made the repo-wide rule "look parameters up by ID, never `getParameters()[n]`" concrete. Pinned by `OutputLevelTests.LevelParameterIsAddedLast` and `…AttenuverterKeepsAmountAtParameterIndexOne`.
- **Modules that already have a level/gain parameter do not get a second one** — Oscillator, LFO, Noise and Voice Mixer have `level`; VCA has `gain`; Limiter has `inputGain`; Compressor has `makeupGain`.
- **`numAudioChannels` excludes CV.** Only the leading audio channels are scaled; CV input channels are left for the module's own clearing logic. Filter passes `8` in poly mode and `1` in mono.
- **Bypass/mute contract is unchanged.** `applyOutputLevel` is never reached on the bypass branch (dry pass-through stays untouched, so Level cannot silence a bypassed module) nor on mute (already cleared). See [`architecture.md`](architecture.md).
- **It is an output stage, not an insert.** Delay applies it after the feedback write, so lowering Level does not starve the repeats; Reverb applies it after the wet/dry mix, so Wet/Dry still sets balance and Level sets absolute loudness.

CV control of Level is deliberately **not** implemented — it would need a new input channel on every module, and CV channel indices are positional and hard-coded across `getModulationTargets`, `mapInputChannel`, the AI patch schema and the tests. Mono CV connections already route through an Attenuverter, which provides per-connection gain.

## Distortion Module
- **Algorithm**: Three waveshaper types selectable via the Type parameter:
  - **Soft** (type 0): Rational waveshaper `f(x) = x * (1 + k) / (1 + k * |x|)` where `k = drive - 1`. Gain-neutral at `k=0`; increasingly compressed at higher drive values. This is NOT a tanh curve.
  - **Hard** (type 1): Hard clipper — scales input by drive then clips to a threshold that tightens as drive increases.
  - **Foldback** (type 2): Folds the signal back at a fixed threshold of ±0.8 after scaling by drive.
- **Oversampling**: Configurable oversampling mode (Off, 2x, 4x) using FIR equiripple half-band filters (`filterHalfBandFIREquiripple`) to reduce aliasing. A latency-compensation delay line keeps the dry signal aligned for wet/dry mixing. Default is 2x (backward-compatible).
- **Makeup Gain**: Automatic dynamic makeup gain (computed per-block from RMS ratio of dry vs. wet, clamped 0.01–1.0 linear, 50 ms smoothing). Not a user-visible parameter.
- **Parameters**: Drive (1–20), Mix (0–1), Type (Soft/Hard/Foldback), Oversampling (Off/2x/4x), Level (0–1). Level is applied after the wet/dry mix and before the scope push, so the visualiser shows the real output.
- **CV Inputs**: Drive (ch2), Mix (ch3).

## Ring Modulator Module
- **Algorithm**: Parker diode-ring (DAFx-11). Four parallel piecewise-quadratic diode approximations
  `out = d(m + c/2) + d(-m + c/2) - d(m - c/2) - d(-m - c/2)`. This is **not** a clean multiply — Math's `Mult` output already covers that. The diode dead-zone is what gives the metallic, gated, bell-like character.
- **Oversampling**: Same real-time-safe scheme as Distortion. `Off` / `2x` / `4x` (default `2x`); both oversamplers are pre-allocated in `prepareToPlay` and swapped via an `AudioProcessorParameter::Listener`. A latency-compensation delay line keeps the dry carrier aligned for wet/dry mixing. Oversampling is excluded from `getModulationTargets()`.
- **I/O**: Carrier (ch0), Modulator (ch1), Mix CV (ch2), Drive CV (ch3), Character CV (ch4). Stereo out is the mono ring-mod result duplicated to Left/Right. Dry is the unprocessed carrier. No internal carrier oscillator — patch an Oscillator into Carrier.
- **Dual I/O**: **output-only** (the Voice Mixer's shape, not the usual FX one — the input pair is Carrier + Modulator, two unrelated mono jacks). Off by default like every other FX: one `"Audio"` jack owning raw ch0/ch1, split into `Left`/`Right` when on. Purely a jack-layout change — both legs already carry the same wet signal, so `RingModulatorModuleTest.DualIODoesNotChangeWhatItRenders` pins the DSP as untouched. The input map is deliberately left on `ModuleBase`'s default so ch1 (Modulator) never reports `PortRole::Audio`; see § Stereo I/O for why that matters.
- **Parameters**: Drive (0.5–8, default 1), Mix (0–1, default 1), Character (0–1, default 0.5), Oversampling (Off/2x/4x), Dual I/O (layout toggle), Level (0–1). Character maps the diode forward-bias / linear-region breakpoints (`vb` / `vl`) from near-clean multiply (`vb≈0.02`, `vl≈0.05`) to hard gated (`vb≈0.5`, `vl≈1.0`). Parker's typical `vb≈0.2` / `vl≈0.4` sits at the default.
- **CV Inputs**: Mix (ch2), Drive (ch3), Character (ch4). Every continuous parameter has a CV jack; Oversampling does not.

## Delay Module
- **Type**: Stereo feedback delay.
- **Technique**: Fractional delay line with linear interpolation for smooth "time" parameter changes.
- **Parameters**: Time (ms), Feedback, Mix, Level (0–1). Level sits outside the feedback path — the delay line stores the pre-level signal.
- **Smoothing**: Glide-on-time prevents pitch glitches during modulation.

## Reverb Module
- **Type**: Algorithmic stereo reverb.
- **Implementation**: Uses standard Schroeder/Freeverb-inspired techniques for lush acoustic simulation.
- **Parameters**: Room Size, Damping, Wet, Dry, Width, Level (0–1). Wet/Dry set the balance; Level scales the summed result.

## Chorus Module
- **Implementation**: `juce::dsp::Chorus<float>`.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.1–10 Hz), Depth (0–1), Centre Delay (1–30 ms), Feedback (-1–1), Mix (0–1), Level (0–1).
- **Smoothing**: Centre Delay is smoothed over 50 ms (a block at a time) — `juce::dsp::Chorus` adds it straight onto the modulated read position, so a per-block step jumps the read head by `delta_ms × fs / 1000` samples. Feedback and Mix are already ramped inside `juce::dsp::Chorus` (a per-channel `SmoothedValue` and a `DryWetMixer`); smoothing them again here would only add lag.

## Phaser Module
- **Implementation**: `juce::dsp::Phaser<float>`.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.1–20 Hz), Depth (0–1), Centre Freq (200–10 000 Hz), Feedback (-1–1), Mix (0–1), Level (0–1).
- **Smoothing**: Centre Freq is smoothed multiplicatively over 50 ms (a block at a time) — it is the allpass bank's cutoff, so stepping it swaps every stage's coefficients at once. Feedback and Mix are already ramped inside `juce::dsp::Phaser`.

## Compressor Module
- **Implementation**: `juce::dsp::Compressor<float>`.
- **Makeup Gain**: Manual, user-controlled. Range: -20 to +40 dB, default 0 dB. Applied per-sample with 5 ms smoothing after the compressor. There is no automatic gain compensation.
- **CV Modulation**: None — no CV input channels.
- **Parameters**: Threshold (-60–0 dB), Ratio (1–20), Attack (0.1–200 ms), Release (10–1000 ms), Makeup Gain (-20–+40 dB).
- **Smoothing**: Threshold and Ratio are both smoothed over 10 ms (a block at a time — `juce::dsp::Compressor` only takes them through setters); both set the gain computer, so stepping either steps the applied gain reduction. Attack and Release are detector time constants and are deliberately not smoothed.

## Flanger Module
- **Implementation**: `juce::dsp::Chorus<float>` configured for flanger character by constraining the centre-delay range to 1–5 ms (versus Chorus's 1–30 ms). Default centre delay is 2 ms.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.05–5 Hz), Depth (0–1), Centre Delay (1–5 ms), Feedback (-1–1), Mix (0–1), Level (0–1).
- **Smoothing**: same as Chorus — Centre Delay smoothed over 50 ms a block at a time; Feedback and Mix left to `juce::dsp::Chorus`'s own ramps.

## Limiter Module
- **Implementation**: Brickwall `juce::dsp::Limiter<float>`.
- **Input Gain**: Pre-limiter drive parameter. Range: -20 to +20 dB, default 0 dB. Applied per-sample with 5 ms smoothing before the limiter stage.
- **CV Modulation**: None — no CV input channels.
- **Parameters**: Threshold (-20–0 dB, default -1 dB), Release (1–500 ms), Input Gain (-20–+20 dB).
- **Smoothing**: Threshold is smoothed over 10 ms (a block at a time) — it is where the gain computer starts pulling the signal down, so stepping it steps the gain reduction. Release is a detector time constant and is deliberately not smoothed.

## Bitcrusher Module
- **Implementation**: Downsampling and bit-depth quantization effect with dither.
- **Quantization**: Rounds input signal to `2^depth` discrete levels with `std::round` (clamped to ±1.0) to eliminate DC quantization bias.
- **Sample Rate Reduction**: Holds sample values for `rate` sample clocks (scaled relative to 44.1 kHz for device independence).
- **CV Modulation**: Rate (ch2), Depth (ch3), Mix (ch4). CV presence is detected via non-zero sample checking.
- **Parameters**: Rate (1–50), Depth (1–24 bits), Mix (0–1), Dither (0–1).

## Parametric EQ Module

Modelled on a traditional DAW channel EQ (Cubase's, specifically): four fixed band slots, **all disabled by default**, with points added and shaped directly on the response curve.

- **Bands**: Four slots with fixed types — `1` Low Shelf, `2` Peak (bell), `3` Peak (bell), `4` High Shelf. Every slot exposes Freq / Gain / Q uniformly, so the same gestures work on all of them; only the shape differs. Q defaults to 1/√2 (the RBJ "S = 1" case — maximally flat, no shelf overshoot) but is user-adjustable on shelves too.
- **Disabled by default**: a freshly dropped EQ is a straight wire and the curve starts empty. `processBlock` skips a disabled band's filter loop entirely, and its coefficients are written as a literal unity biquad, so "off" really is bypassed rather than "gain happens to be 0".
- **Implementation**: RBJ cookbook biquads via `juce::dsp::IIR::Filter<float>`, one filter per channel per band (2 × 4). Coefficients are computed in-house by `ParametricEQModule::writeBiquad()` rather than `IIR::Coefficients::makePeakFilter()` et al., because the module also needs the matching *analog prototype* magnitude for the visualiser — see below.
- **No audio-thread allocation**: the four `Coefficients` objects are allocated once in `prepareToPlay` and their raw values rewritten in place each block. The two channels share each band's `Coefficients` object; `juce::dsp::IIR` keeps filter state in the `Filter`, not the `Coefficients`, so sharing is safe (this is what `ProcessorDuplicator` does internally).
- **Coefficient update rate**: once per block, from 20 ms-smoothed values. **Every band's Freq / Gain / Q is smoothed, not just the two CV-driven bells** — they all end up in `writeBiquad`, and swapping a biquad's coefficients wholesale puts a step in the output. The per-block step is small enough that a knob drag, or any automation curve with a sane slope, is inaudible while keeping the inner loop a plain biquad. It is *not* enough to absorb an instantaneous jump across the full ±24 dB gain range — that would need per-sample coefficient interpolation, and the 20 ms cannot simply be lengthened because the two bells are CV targets that have to track an LFO. `AutomationZipperTests` records that exception against `bandNGain` rather than hiding it. Output gain is applied per sample so its smoothing is genuinely continuous.
- **Analytic response**: `bandMagnitudeDb()` / `responseDb()` are pure static functions evaluating the analog prototypes the digital coefficients are derived from (`H(s)` forms for peak / low shelf / high shelf); `responseDb` sums only the *enabled* bands. `EQCurveComponent` draws the curve with them, so the display and the DSP agree — `ParametricEQAudio.MeasuredResponseTracksTheAnalyticCurve` asserts they stay within 1 dB of each other at real audio frequencies.
- **Snapshot semantics**: `getBandSnapshots()` reads the *parameters*, not values cached during `processBlock`, so the curve tracks a dragged point even with no audio flowing. The two bell bands have their live CV-modulated frequency/gain overlaid, but only while CV is actually driving them.
- **Channels**: 6 in / 2 out — `0-1` stereo audio, `2` B2 Freq CV, `3` B2 Gain CV, `4` B3 Freq CV, `5` B3 Gain CV. Only the two bells take CV, following the rest of the FX suite (CV on the parameters worth modulating, not one jack per knob); the shelves are parameter-only.
- **CV mapping**: Freq CV is exponential over the full 20 Hz – 20 kHz range — `+1` sweeps to 20 kHz, `-1` to 20 Hz, matching FilterModule's cutoff-CV feel. Gain CV maps linearly onto the full ±24 dB range and adds to the knob value. Both are sampled once per block and RMS-gated, so an unpatched jack reads as exactly 0.
- **Parameters** (per band N = 1–4): `bandNOn` (bool, default off), `bandNFreq` (20–20 000 Hz; slot defaults 100 / 500 / 3000 / 8000), `bandNGain` (±24 dB, default 0), `bandNQ` (0.1–10, default 0.707). Plus `outputGain` (±24 dB). Frequency and Q ranges are skewed (`setSkewForCentre`) so the knobs feel logarithmic, and all three carry `stringFromValue` formatters because the raw skewed values read as `2999.9` / `0.7071` — see the UI note below for the exact strings.
- **Slot selection**: `findBandForNewPoint(freqHz)` returns the disabled slot whose home frequency is nearest on a log axis, so a click down low lands on the low shelf and one up top on the high shelf; `-1` once all four are in use.
- **UI**: double-width card (560 × 592) — response curve set between the port-label gutters, then a row of [Show Spectrum] [Open EQ Window] with the Output trim at its right, then one column per band (type-labelled on/off checkbox above Freq / Gain / Q). Knob geometry is deliberately identical to the generic auto-UI (70 × 60 slider, 20 px label, 50 px text box) so EQ knobs are the same size as every other module's; the columns are wider than a knob, so each is centred in its column. Value formatters are correspondingly compact ("3.2k", "-9.0", "0.71") to fit that shared 50 px box. See [`docs/layout_visuals_animation.md` §1](layout_visuals_animation.md) for `EQCurveComponent` (interactive curve) and `EQWindow` (pop-out editor).
## Pitch Shifter Module
Two engines behind one Mode switch, because they answer the same question with opposite characters: Pitch mode multiplies frequencies (harmonic), Frequency mode adds to them (inharmonic).

- **Pitch mode**: Two-tap crossfaded delay line. The read heads sweep across one window half a window apart; the phase advances at `(1 - ratio) / windowSamples` per sample, which is the condition for the read position to advance at `ratio`. Taps are summed with an equal-power window (`sin`/`|cos|` of the phase) so each tap's wrap-around lands exactly where its own gain is zero. Reads use cubic Hermite interpolation — linear interpolation audibly dulls a transposed signal.
  - **Unity dead zone**: when `|ratio - 1| < 1e-4` (about 0.0017 semitones) the two taps would sit at fixed, different delays and comb-filter the input, so the module emits the signal untransposed instead. This makes the default (Pitch 0, Mix 1) bit-transparent. Entering and leaving that state swaps between two genuinely different signals, so it is **crossfaded over 5 ms, not switched** — a slow Pitch/Fine sweep parks inside the epsilon window for tens of samples and the hard switch clicked there. The fade is time-based rather than ratio-based so a fast sweep cannot outrun it, and it is snapped at `prepareToPlay`, so a render that starts at (or away from) unity is unchanged. Regression: `AutomationZipperTests` `Pitch_Shifter_fine`.
  - **Window**: trades artifacts against latency. Short windows chop the signal at a higher rate (audible as AM sidebands at `|1 - ratio| / window` Hz); long windows smear transients. 50 ms default.
- **Frequency mode**: Single-sideband modulation. A Hilbert transform pair (two cascaded 4-section 2nd-order allpass chains, `H(z) = (a² - z⁻²)/(1 - a²z⁻²)`, using Olli Niemitalo's wideband 90° coefficients) feeds a quadrature oscillator: `out = I·cos(ωt) + Q·sin(ωt)`. Measured rejection of the unwanted sideband is ~55 dB. A negative Shift runs the oscillator backwards — no separate code path. Harmonics stop being integer multiples of the fundamental, which is what produces the "alien voice" / metallic timbre.
- **Feedback**: routes the shifted output back into the input, so each pass is shifted again — cascading octaves in Pitch mode, barber-pole / Shepard-tone illusions in Frequency mode. Soft-clipped with `tanh` so the loop stays bounded at the 0.95 maximum.
- **CV Modulation**: Pitch (ch2, ±24 semitones), Shift (ch3, ±1000 Hz), Mix (ch4, ±1), Feedback (ch5, ±0.95). CV is added per-sample to the smoothed parameter value and clamped; Window and Fine have no CV input.
- **Parameters**: Mode (Pitch/Frequency), Pitch (-24–+24 semitones), Fine (-100–+100 cents), Shift (-1000–+1000 Hz), Window (10–100 ms), Feedback (0–0.95), Mix (0–1, default 1).
