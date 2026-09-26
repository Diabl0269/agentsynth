# Modulation System

How modulation routing is modelled, how the engine derives it from the graph, the logical-port API
that collapses per-voice wires onto one visible jack, and how the routings reach the canvas.

Per-module channel maps are in [`modules.md`](modules.md); the poly channel table is in
[`poly-channel-layout.md`](poly-channel-layout.md).

## Automation writes the base value; CV stacks on top

Timeline automation and CV modulation are **not** competing for the same slot, and neither one is
layered on top of the other by any code that knows about both — the layering is a consequence of how
modules already work.

- **Automation is a very precise knob turn.** `synth::AutomationApplier` stores the lane's value
  straight into the target parameter (`setValue`, denormalised → normalised, clamped into the
  parameter's own range) once per render pass, before the graph runs. Nothing distinguishes that
  store from the user dragging the knob: it changes the parameter's **base value**, permanently,
  until something else changes it. For one of our own modules that target is a
  `juce::RangedAudioParameter` and "the parameter's own range" is its `NormalisableRange`; for a
  hosted plugin's own parameter (below) there is no `NormalisableRange` to clamp into, so the lane's
  own `RangeSnapshot` — always `{0, 1, default}` for a hosted parameter — plays that role instead.
- **CV is added per sample, on top of whatever the base value currently is.** Every module reads
  `param->get()` fresh at the top of its own `processBlock` and then adds the CV it finds on the
  corresponding input channel (scaled by the attenuverter on the cable). It never writes the
  parameter back.
- **So the two compose without either knowing about the other**: automation moves the base, CV
  wiggles around wherever the base happens to be. Automating a filter's cutoff while an envelope
  also modulates it gives "the envelope, riding the automated curve" — which is what a user drawing
  an automation lane on a modulated parameter expects.

Two consequences worth stating explicitly:

- **Automation is visible to the user as a moved knob**, because it really is one. That is why the
  applier deliberately does *not* call `setValueNotifyingHost` — pushing a listener notification per
  automated parameter per block from the audio thread is the wrong mechanism for that; UI reflection
  is a message-thread concern.
- **Automation only writes while the transport is playing.** Stopped, the knob is the user's again
  and the applier writes nothing, so a paused session never fights a mouse drag. CV, by contrast,
  keeps flowing whenever its source module is producing signal — a stopped transport does not
  silence an LFO.

### Recording a knob

The reverse direction is the same idea run backwards, and it has one contract that governs every
knob in the app: **turning a knob only records automation while a real gesture is in flight.**
`synth::AutomationRecorder` listens for `beginChangeGesture` / `setValueNotifyingHost` /
`endChangeGesture` — the trio every JUCE slider attachment produces — and captures a value change
**only** while a capture span is open, which only a gesture (or, for a `Write` lane, the transport
starting to play) can open.

**Why the gesture and not the value change.** A preset load, an AI patch apply and an undo restore
all move parameters exactly like a knob drag does, and they all do it without a gesture. Keying on
the gesture is what stops any of them from silently overwriting every armed lane.

While a gesture *is* in flight, the hand wins: the parameter is "claimed", the applier skips it, and
the automation lane stops fighting the mouse for as long as the button is down. Let go and playback
resumes on the very next block — for a `Touch` lane; a `Latch` lane keeps writing until the
transport stops. Full mode table and the commit/thinning rules:
[`architecture/timeline.md`](../architecture/timeline.md#automationrecorder-record-modes-and-gesture-capture).

### Hosted plugin parameters as automation lanes

A [hosted plugin](modules.md#load-ux)'s own parameters live on the *inner*
`juce::AudioPluginInstance`, discovered at load — never on `HostedPluginModule`'s own
`getParameters()` (that carries only `muted`). Two things follow from "discovered at load": the
parameter set is not `juce::RangedAudioParameter` the way our own modules' parameters are, and it
can change **shape** between versions of the same plugin. A lane therefore has to survive an update
that reshuffles parameters without ever landing on the wrong one. The full keying/fallback/orphan
rule:

1. **Exact id match wins.** The lane's `paramId` is matched against the live instance's
   `juce::HostedAudioProcessorParameter::getParameterID()` — the interface VST3/AU/LV2 wrappers
   implement for a persistent string identity. This always resolves the lane when the id still
   exists, at whatever index it now sits at.
2. **A stored index hint is a narrow rescue, never a guess.** Every lane also carries
   `paramIndexHint` (captured once, at creation, from the parameter's index at that moment). If the
   exact id match fails, the hint is consulted **only** to check whether the plugin format has no
   stable ids at all (the parameter at that index itself has no id — see
   [`modules.md`](modules.md#load-ux)'s table). If the
   hinted index instead names a *different*, still-identified parameter — a plugin update having
   moved the parameter set under us — that is exactly the case this rule exists to catch, and the
   lane **orphans** instead of silently binding to whatever is there now.
3. **Anything else orphans too**: no live instance at all (still loading, unloaded, or the plugin
   isn't installed), or a hinted index that no longer exists. Orphaned lanes are retained and
   re-bindable, never auto-deleted or silently repointed.

Non-plugin modules are entirely unaffected: their lanes resolve by `paramID` match, and a miss is
merely unbound, never orphaned.

Every lane-resolution call site — the audio-thread binding build, `TimelineReconciler`, the AI-tool
`writeLane` path (`TimelineOps`/`TimelineValidator`), and the automation recorder's rebind — goes
through the one shared resolver, `synth::resolveLaneParameter`
(`Source/Timeline/AutomationBinding.h`), so this rule cannot drift between the audio path, the
AI-tool path and the UI path. The resolver's shape and the normalised-range note are in
[`architecture/plugin-layer.md`](../architecture/plugin-layer.md#automation-lanes-on-hosted-plugin-parameters).

The automation strip's lane picker offers **"Add lane…"** entries for a hosted plugin's
not-yet-automated instance parameters (there is no `ModuleComponent` knob to right-click for them —
the plugin has its own editor). Choosing one creates a lane with a `RangeSnapshot` of
`{0, 1, default}` (a hosted parameter's native domain is always 0..1) and captures `paramIndexHint`
from the parameter's current index.

## Routing kinds

Every CV connection in the graph resolves to one of three routing kinds, defined in `AudioEngine.h`:

```cpp
enum class RoutingKind { AttenuverterChain, DirectCV, PolyBus };
```

| Kind | What it is | How it arises |
|------|-----------|---------------|
| `AttenuverterChain` | A mono source signal passes through an `AttenuverterModule` node before reaching the destination's CV input. This is the standard user-adjustable modulation path. | Created by `AudioEngine::addModRouting()`. The engine automatically inserts an `AttenuverterModule` between source and destination. Amount is set to 1.0 at creation. |
| `DirectCV` | A source output connects directly into a destination's CV/ModCV input channel, with no intermediary attenuverter. | Any non-attenuverter connection into a mod-CV jack (e.g. env output 0 → oscillator Level ch12). |
| `PolyBus` | N per-voice `DirectCV` connections (one per voice) that share the same source module and destination visible jack. The engine collapses them into one logical bus. | Arises when the source's `mapOutputChannel()` and destination's `mapInputChannel()` both describe a poly fan (e.g. ADSR outputs 0-7 → VCA inputs 8-15 in the Poly Pad preset). Also created directly: dragging a cable between two equally-wide poly jacks, or toggling a module's `poly` parameter, creates/rebuilds all N raw connections at once. A mono modulator dropped on a per-voice `ModCV` fan collapses too, with all N edges leaving the same source channel — see [Creating poly connections](#creating-poly-connections). |

## AttenuverterModule internals

`AttenuverterModule` is the intermediary node inserted by `AudioEngine::addModRouting()`. Key
internals:

- **Constructor default Amount = 0.0.** The `amountParam` (`AudioParameterFloat`, range −1.0 to
  1.0) is initialised to `0.0f`.
- **`addModRouting` sets Amount to 1.0.** After inserting the node, `AudioEngine::addModRouting()`
  finds the amount parameter by index and calls `setValueNotifyingHost(1.0f)`, so a freshly
  connected modulation path immediately passes the full signal.
- **`addEmptyModRouting` leaves Amount at 0.0.** It adds an `AttenuverterModule` node without wiring
  it or adjusting its parameter.
- **UI visualisation atomics.** After each `processBlock`, the module writes two
  `std::atomic<float>` members for the UI to read lock-free: `lastOutputPeak` (peak absolute value
  over the processed block, driving the activity display) and `lastModValue` (the mid-block sample
  value, `audioData[numSamples / 2]`, a near-instantaneous reading for modulation rings). Both are
  exposed via `getLastOutputPeak()` and `getLastModValue()`.
- **Bypass behaviour.** When bypassed, `lastOutputPeak` and `lastModValue` are both set to `0.0f`
  and all channels are cleared — no pass-through; an attenuverter in bypass silences its output.

## The routing snapshot

```cpp
std::vector<ModulationRouting> getModulationRoutings() const;
```

Returns a read-only snapshot of every active modulation routing derived from the
`AudioProcessorGraph`. The struct is:

```cpp
struct ModulationRouting {
    RoutingKind kind;
    NodeID sourceNodeID;
    int    sourceChannelIndex;  // raw graph channel
    int    sourceVisibleJack;   // visible jack index on source
    NodeID destNodeID;
    int    destChannelIndex;    // raw graph channel
    int    destVisibleJack;     // visible jack index on destination
    NodeID attenuverterNodeID;  // valid only for AttenuverterChain
    int    voiceCount;          // 1 for mono routings, N for PolyBus
    float  amount;              // attenuverter amount or 1.0 for direct
    bool   isBypassed;
    bool   hasSource;
    bool   hasDest;
    float  modSignalValue;      // last mid-block sample (UI display)
    float  modSignalPeak;       // peak over last block (UI display)
    PortRole role;
};
```

**This is a derived, read-only view — it is never serialised.** The graph connections and the JSON
preset remain the single source of truth. Because `getModulationRoutings()` is re-derived from the
live graph each time it is called, undo/redo and round-trip preset load/save are unaffected by this
model.

`getActiveModRoutings()` and `getModulationDisplayInfo()` are thin adapters over
`getModulationRoutings()`, retained for legacy call sites.

## Logical-port API

To map raw `AudioProcessorGraph` channel indices onto visible UI jacks, `ModuleBase` provides:

```cpp
enum class PortRole { Audio, ModCV, Pitch, Gate, Midi, Other };

struct LogicalPort {
    int  visibleJackIndex;  // which visible jack a wire anchors to (0..getVisible*PortCount()-1)
    PortRole role;
    bool isPolyGroupHead;   // true only for the lowest raw channel of a poly fan
    int  polyVoiceSpan;     // 1 = mono; N = this is the head of an N-voice fan
};

virtual LogicalPort mapInputChannel(int rawChannel) const;
virtual LogicalPort mapOutputChannel(int rawChannel) const;
```

Poly-capable modules override these to describe fans. The default base implementation clamps any
out-of-range channel to the last visible jack and marks `isPolyGroupHead` based on whether
`rawChannel < getVisible*PortCount()`.

**A module that adds visible jacks must override the mapping for every raw channel it exposes**
rather than inherit that fallback. Any raw channel below `getVisible*PortCount()` that no override
claims is reported as its own `isPolyGroupHead`, so an unclaimed channel becomes a phantom second
head on a real jack and `getJackTargets` hands out a duplicate wire for it.

### JackTarget and getJackTargets

The inverse of `mapInput/OutputChannel` — given a visible jack, returns every poly-group head
anchored to it:

```cpp
struct JackTarget {
    int rawHeadChannel;  // raw channel a wire anchored to this jack should start at
    PortRole role;
    int voiceSpan;       // 1 = mono; N = head of an N-voice fan
};

std::vector<JackTarget> getJackTargets(int visibleJackIndex, bool isInput) const;
```

Normally returns one entry. Poly MIDI's single "Poly Out" jack fronts two fans — Pitch at raw
channel 0 and Gate at raw channel 8 — so it returns both, and the caller
(`GraphEditor::resolvePolyLink`) disambiguates by role. **Never returns empty**: an unmapped jack
falls back to the raw==jack identity, so modules without a logical-port override keep the
pre-logical-port wiring behaviour.

### Port labels

`ModuleBase` also declares virtual port-label accessors:

```cpp
virtual juce::String getInputPortLabel(int channelIndex) const;   // default: "In <N>"
virtual juce::String getOutputPortLabel(int channelIndex) const;  // default: "Out <N>"
```

Modules override these to supply human-readable jack names shown in the UI (for example
`AttenuverterModule` returns `"Signal"` / `"Amount"` for its two inputs and `"Out"` for its output).
The UI reads these labels to annotate jack tooltips and connection indicators with descriptive names
rather than raw channel numbers.

### How GraphEditor uses the logical-port API

1. For every connection in the graph, GraphEditor calls `mapOutputChannel` on the source and
   `mapInputChannel` on the destination to get `LogicalPort` values.
2. The wire is anchored to `sourceVisibleJack` and `destVisibleJack` rather than the raw channel
   number. This collapses N per-voice wires onto a single visible jack. A Dual I/O split on only one
   end is the exception: the split jack is a group head, so its Right (or Left) cable still draws,
   landing on the far end's collapsed `"Audio"` jack.
3. If the engine classifies the routing as `PolyBus`, GraphEditor draws only one wire and overlays
   an "xN" voice-count badge on it (for example "x8").
4. Mono `DirectCV` wires are drawn without a badge.
5. `AttenuverterChain` wires render a draggable midpoint knob for depth control.

### Creating poly connections

`getJackTargets` and `GraphEditor::resolvePolyLink` drive connection *creation*, not just display:

1. `resolvePolyLink(source, sourceVisibleJack, dest, destVisibleJack)` calls `getJackTargets` on both
   ends and pairs one `JackTarget` from each into a
   `PolyLink { sourceRawChannel, destRawChannel, voiceCount, sourceStride }`. Pairings are scored:
   matching `PortRole` is the strongest signal (this is what lets Poly MIDI's single "Poly Out" jack
   send Pitch to an Oscillator and Gate to an Amp Env from the same jack); a `ModCV`/`Other` end is a
   wildcard, since mod inputs accept anything; equal fan widths break remaining ties.
2. `voiceCount = min(sourceSpan, destSpan)` with `sourceStride = 1` — a voice-to-voice fan only forms
   when both ends are equally wide. A poly-to-mono cable degrades to one head-to-head wire.
3. **Broadcast exception.** A mono source landing on a per-voice `ModCV` fan sets
   `voiceCount = destSpan` and `sourceStride = 0`, so one source channel feeds all N destination
   channels — a single LFO shakes every voice, as on a hardware poly synth. Deliberately limited to
   `ModCV` for poly voice fans:
   - Broadcasting `Pitch` or `Gate` would make all N voices sound the same note at the same time.
   - Broadcasting `Audio` onto an N-voice poly audio fan would build a paraphonic instrument (N
     voices filtering an identical signal through a *shared* cutoff CV, so the results are
     bit-identical for N times the DSP). That is a legitimate patch, but an explicit one — not a side
     effect of a single drag.
   - **Stereo bus exception.** A mono source into a collapsed Dual I/O `"Audio"` jack
     (`PortRole::Audio`, `voiceSpan == 2`) *does* broadcast with `sourceStride = 0`, duplicating onto
     raw L and R — the usual mono→FX insert. A poly voice fan is `voiceSpan == 8`; a stereo pair is
     exactly 2.
   - **`kRightBase` stereo is not this mechanism.** The voice modules put `Audio R` on a *dedicated*
     block above their mod-CV inputs rather than on a contiguous ch0/ch1 pair, because ch1 is already
     Position/Waveform/Cutoff CV. Each leg is therefore its own jack and its own poly-bus head
     (`isPolyGroupHead` at `kRightBase`, `polyVoiceSpan == 8` in poly), so there is no span-2 group to
     broadcast into — a mono cable reaches `Audio L` only, and a stereo path means two cables. See
     [`modules.md`](modules.md#oscillator-module).
   - A poly source into a mono jack never broadcasts either; summing N envelopes onto one CV channel
     is not what the user asked for.
4. `GraphEditor::endConnectionDrag` resolves the visible jacks the user dropped a cable between
   through `resolvePolyLink` and adds `voiceCount` connections,
   `{sourceRawChannel + v * sourceStride} -> {destRawChannel + v}` for each voice. A fan is always
   wired direct — an `AttenuverterModule` is only inserted for a single mono mod wire
   (`voiceCount == 1`), because the engine collapses the N raw edges of a fan into one `PolyBus` for
   display.
5. `GraphEditor::disconnectPort` removes every raw channel a visible jack owns (via
   `getJackTargets`), so "Disconnect" on a poly jack does not leave some voices still wired. The same
   call is used by the right-click port menu and, when
   *Settings → Preferences → Double-click port to disconnect* is on (the default), by double-clicking
   a connected jack.
6. Toggling a module's `poly` parameter calls `GraphEditor::rewireForPolyChange`, which re-anchors
   every cable touching that module: mono cables fan out to N voices when both ends go poly, fans
   collapse back to one wire when poly is switched off, and an attenuverter-mediated mod wire is
   recreated (carrying its amount across) or replaced by a direct fan as appropriate. MIDI
   connections are never moved. `ModuleComponent` snapshots the module's raw→`LogicalPort` maps just
   before the toggle, because by the time the parameter listener fires the live mapping already
   reflects the new layout — only the snapshot can say which visible jack an existing raw connection
   used to be anchored to.

## isAutoPromotableModTarget

```cpp
virtual bool isAutoPromotableModTarget(int dstChannel) const;
```

The default implementation returns `true` if `dstChannel` appears in `getModulationTargets()`.
Poly-capable modules override it to return `false` when `poly == true`:

```cpp
bool isAutoPromotableModTarget(int dstChannel) const override {
    if (polyParam->get()) return false;
    return ModuleBase::isAutoPromotableModTarget(dstChannel);
}
```

**Why.** `AIStateMapper` uses `isAutoPromotableModTarget` to decide whether to auto-wrap a connection
in an `AttenuverterModule` when applying JSON patches. When a module is in poly mode, direct CV
connections into the poly shared-CV block must stay as plain `DirectCV` routings rather than being
auto-wrapped, so returning `false` prevents the auto-promotion.

## Envelope modulation in the Poly Pad preset

The Poly Pad factory preset demonstrates two distinct modulation kinds for the same Amp Env module.

**Amp Env → VCA is a `PolyBus`.** The Amp Env module runs in poly mode (8 outputs, one per voice) and
the VCA runs in poly mode (8 CV inputs at channels 8-15, one per voice). The preset connects ADSR
output ch0 → VCA input ch8 (voice 0 envelope → voice 0 gain CV), ch1 → ch9, and so on to ch7 → ch15.
`getModulationRoutings()` sees 8 `DirectCV` connections that share the same source module and
destination visible jack (the VCA CV jack, index 1) and classifies them as a single `PolyBus` with
`voiceCount = 8`. GraphEditor draws one wire with an "x8" badge. Per-voice amplitude is fully
independent: when voice 3 releases, only voice 3's envelope falls.

**Amp Env → Oscillator Level is a `DirectCV`.** The preset also connects ADSR output ch0 (voice 0's
envelope) directly to Oscillator input ch12 (the shared Level CV): a single connection from one raw
channel to one raw channel, without an attenuverter and without a poly fan. `getModulationRoutings()`
returns it as `DirectCV` with `voiceCount = 1` and GraphEditor draws a normal single wire without a
badge. All 8 oscillator voices share the same level modulation (the voice-0 envelope signal).

That combination — `PolyBus` for per-voice gate control, `DirectCV` for a shared timbre parameter —
is a common pattern when building poly patches.

## Modulation rings on knobs

`ModuleComponent` renders modulation rings on knobs for any active modulation targeting that module.
It calls `AudioEngine::getModulationRoutings()` (via the `GraphEditor`'s cached snapshot) to find
which knobs have live modulation and paints a ring overlay proportional to the routed signal value,
giving a real-time visual indication of modulation depth directly on the parameter knob.

**Which knob a ring belongs on is `getModRingSliderIndex()`'s call, and it returns -1 for a knob that
is not visible.** A card can page its controls (the Wavetable tab strip,
[`layout/module-card.md`](../layout/module-card.md)), and a knob on a hidden page keeps the bounds it
had when its page was last laid out — so a ring drawn straight from `sliders[i]->getBounds()` paints
an orange arc over empty card. The rule lives in that one accessor so it can be tested without a
themed LookAndFeel and a live routing.

### The depth band: how far a routing COULD move the knob

Under the live ring, `paintModulationRings` also draws a **depth band**: the arc between the
knob's reachable extremes, visible even while the source is at rest (`modSignalValue == 0`) —
answering "how far could this move", not just "where is it now". One band per routing (two
routings on one knob draw two separate bands, never summed), at `theme.metrics.modDepthBandAlpha`
(30%) over the same `modRingPositive`/`modRingNegative` tokens the live ring uses.

The range comes from `ModuleBase::isModSourceBipolar()` (default `true`) and the routing's
attenuverter `amount` (`AudioEngine::ModulationDisplayInfo::amount`, real range -1..1; `1.0` for a
DirectCV/PolyBus routing, which has no attenuverter):

- **Bipolar** source (an LFO, a Macro not locked unipolar) → `[base - |amount|, base + |amount|]`.
  A negative amount inverts phase, not the reachable range, so the band always straddles the
  knob's current value symmetrically.
- **Unipolar** source (`ADSRModule`, `EnvelopeFollowerModule` — anything only rising from rest) →
  `[base, base + amount]`, or `[base + amount, base]` when `amount` is negative (a unipolar source
  dialled to pull the knob DOWN from rest, which also switches the band to `modRingNegative`).

Both ends clamp to `[0, 1]`. The pure start/end-norm math lives in
`ModuleComponentModBand.h::modDepthBandRange` so it is unit-testable with no `ModuleComponent` or
`LookAndFeel` involved (`ModuleComponentModBandTests.cpp`); the ring and the band it sits under
share ONE angle-mapping helper (`AppLookAndFeel::modRingAngleForNorm`), so they can never drift
apart geometrically.

### Drag the ring to adjust a routing's amount, without touching the knob

A knob with a live **AttenuverterChain** routing (not DirectCV/PolyBus — those have no attenuverter
to adjust) can have that routing's `amount` dragged directly from the card: Alt-drag anywhere on
the knob, or a plain drag starting within the ring's own annulus (+/-5px of its radius). Either
claims the WHOLE gesture through `CardKnobSlider` (`Source/UI/Graph/ModuleComponent/
CardKnobSlider.h`) — the knob's own value never moves while it is active, and an ordinary
centre-of-knob drag (no Alt, not on the ring) still moves the knob exactly as before.

The drag itself goes through `GraphEditor::beginModAmountGesture()` /
`adjustModAmount(attenuverterNodeID, delta)` / `commitModAmountGesture()`
(`GraphEditorModAmount.cpp`) — the SAME path the cable's own midpoint amount knob
(`docs/layout/cables.md`) has always used, so the two gestures can never diverge: same delta/clamp
math, same undo shape (`captureBeforeState` on the first mouseDown, `pushSnapshotFromCapture` on
mouseUp).

### A target binds to its parameter, never to a label

The knob a ring (and a [knob drop](#drag-to-knob-modulation)) resolves to is found through the
target's **bound parameter**, `ModuleBase::parameterForModTarget()`, which `ModuleComponent::
sliderIndexForModTarget()` turns into a knob index:

1. `ModulationTarget::paramId` set → the `RangedAudioParameter` with that `paramID`.
2. Otherwise → the parameter whose display name equals the jack label `name`.
3. Neither → no knob (Oscillator's Pitch CV; Wavetable's Sync input).

A module sets `paramId` whenever its jack label is not the knob's name — the jack is "Rate", the
knob "Rate (Hz)"; the VCA's jack is "CV", its knob "Gain"; Wavetable's "Warp" jack drives
"Warp Amt". Matching the label against the knob's `componentID` (the parameter's display name) was
the pre-`paramId` rule, and it silently found nothing on every FX module whose labels carry no
unit: no ring, and a cable released on the knob fell through to the canvas. `paramId` is a third
aggregate member, so `{"Depth", 3}` still compiles and still resolves by name — only the
mismatched jacks needed touching, and `ModulationTargetBindingTests` sweeps every factory module so
a new mismatch cannot ship (a jack that really has no knob goes in that test's `knoblessTargets()`
list, with a reason).

**Right-click any knob → "Automate '\<Param\>'"** opens that parameter's automation lane in the
timeline panel's automation strip (creating its lane/track on first use) — see
[`timeline/automation.md`](../timeline/automation.md#the-knob-entry-point) for the full path
(`ModuleComponent` → `GraphEditor::onAutomateParameterRequested` → `MainComponent::automateParameter`).

### Hover highlight and the chip

A knob's ring and the cable that drives it are hover-correlated in both directions
(`GraphEditor::HoveredModTarget`, `docs/layout/cables.md#hover`): hovering the `AttenuverterChain`
cable that [lands on this knob](../layout/cables.md#knob-landing) highlights the ring, and hovering
the ring/knob highlights the cable. The ring's highlight is `AppLookAndFeel::drawModulationRing`'s
`hovered` flag — the stroke widens by `Theme::Metrics::modRingHoverWidthBoost` and the colour gets
the same `brighter(0.3)` treatment a hovered cable already gets, no new colour token.

While either side of that correlation is hovered, a small chip appears just under the knob's value
box: `"<source module name> · <signed percent>"`, e.g. `"LFO · +63%"` or `"Env 2 · -40%"`. The
percent is the attenuverter's `amount` (`ModulationDisplayInfo::amount`) rounded to the nearest
whole number; the source name is the routing's source node's `getName()` (the same identity the mod
matrix labels a source with). Formatted by the free function `synth::ui::formatModHoverChipText`
(`ModuleComponentModChip.h`, unit-tested in isolation the same way `modDepthBandRange` is), drawn in
the knob-value-box mono font on a `surfaceHi` chip, clipped to the card.

## Drag-to-knob modulation

A cable released **on a knob** connects the source to that parameter's CV jack. This is the primary
way to patch modulation, and for a knob-bound jack (FRO312, below) it is the ONLY way — the gutter
jack it used to also be reachable from is gone.

### FRO312: a knob-bound gutter jack is hidden

A visible input jack is **knob-bound** when it is a `ModulationTarget` whose knob resolves on this
card right now (`ModuleComponent::isInputJackKnobBound`, `sliderIndexForModTarget(target) >= 0`) —
recomputed live on every call (never cached across a layout), so a poly toggle, Dual I/O change or
Wavetable tab switch that changes which knob is visible changes which jacks are hidden on the very
next repaint. A knob-bound jack:

- draws no gutter dot or label at all (`ModuleComponent::paint`'s input loop iterates
  `drawnInputJackIndices()`, not every visible index);
- is never hit-tested by `getPortForPoint` (same list), so a mouse-down there falls through to the
  knob's own gesture handling instead of starting a jack drag;
- still fully participates in the routing model — an existing or saved connection into that raw
  channel is never torn down, unlike the macro bank's hidden-jack precedent
  (`GraphEditorLayoutTests.ShrinkingTheMacroBankDropsRoutingsOnTheJacksItHides`). Its cable simply
  lands on the knob instead of the (now absent) jack — see [knob
  landing](../layout/cables.md#knob-landing) for how every cable kind resolves this through
  `ModuleComponent::getPortCenter` itself.

The remaining jacks — audio, pitch, gate, MIDI, and any CV jack with no bound knob (Oscillator's
Pitch CV, still a bare jack) — draw packed with no gaps, so a card whose knob-bound jacks were the
only inputs it had gets an empty left column and reserves no dead space for them
(`ModuleComponent::getContentTopY` clears the last DRAWN jack, not the last visible one).
`ModuleComponent::drawnInputJackIndices()` is the one list every caller (paint, hit-testing,
`getInputPortColumns`) reads, so they can never disagree about which jacks are actually on screen.

Dropping a cable directly on the knob still works exactly as described below
(`getModTargetPortForPoint`); picking up or disconnecting an existing knob-landed cable now goes
through the same near-the-landing-dot click the ring-amount-drag gesture already claims clicks
near — see [the landing dot](../layout/cables.md#knob-landing).

- `ModuleComponent::getModTargetPortForPoint()` walks `getModulationTargets()`, resolves each target
  to its knob through the [bound parameter](#a-target-binds-to-its-parameter-never-to-a-label)
  (`sliderIndexForModTarget`, which also says -1 for a knob on a hidden tab page), and reports the
  first visible rotary containing the point as the input `Port` its CV jack would be.
- `GraphEditor::endConnectionDrag()` consults it **only as a fallback**, so an actual jack under the
  cursor still wins, and **only for a cable dragged from an output** — a mod source drives a
  destination, and honouring the reverse would wire it backwards. The `Port` it gets back names
  the jack by **raw channel** (the target's `channelIndex`); the drop path speaks in visible jack
  indices, so it maps through `mapInputChannel().visibleJackIndex` first. On a collapsed stereo
  pair the two differ by one, and before the mapping a Distortion's Drive knob (raw ch2) wired
  visible jack 2 — the Mix jack.
- From there it rejoins the normal drop path, so the connection still goes through
  `AudioEngine::addModRouting()` and still gets an attenuverter auto-promoted onto it. Nothing about
  the routing model changes; this is purely a second way to name the destination.
- `GraphEditor::dragConnection()` arms the knob under the cursor via `setModDropTargetChannel()`,
  which rings it while the cable is in flight, and `endConnectionDrag()` clears every card's
  highlight. Without the ring the drop is a guess.
- It is deliberately **not** folded into `getPortForPoint()`: that method also decides what a
  mouse-down starts, and a knob has to keep starting a value drag there rather than a cable.

Guarded by `GraphEditorTest.DroppingACableOnAKnobCreatesAModRouting` (full LFO output → Position knob
→ routing, including the highlight arming and clearing),
`DroppingACableOnAUnitLabelledKnobCreatesAModRouting` (the Flanger's "Rate (Hz)" knob, reached only
through `paramId`) and `KnobDropIsIgnoredForACableDraggedFromAnInput`; the card-level resolution by
`ModuleComponentModTargetTests`.

The first time a cable drag starts from a modulation source's output at all (before the user has
even found a knob to aim at), `GraphEditor::beginConnectionDrag()` shows a one-time status-bar hint
pointing this out — see [`layout/chrome.md`](../layout/chrome.md#the-mod-drop-hint) for the
mechanism.

## Every continuous parameter is a target

The rule for a module's CV jacks: **every continuous (`AudioParameterFloat`/`Int`) parameter that
shapes the sound gets a CV jack and a `ModulationTarget`.** Choice and toggle parameters do not
(there is no meaningful "half a waveform"), nor does the shared output Level stage (see
[`fx-modules.md`](fx-modules.md#output-level-shared-stage) for why). A user who can turn a knob
expects to be able to drop a cable on it — a knob with no jack reads as a bug, not a design choice.

New jacks are **appended** after the module's existing channels, never inserted: connections
persist by raw channel index, so a saved patch that modulates Pitch Shifter Pitch on ch2 must keep
modulating Pitch after Fine and Window arrive on ch6/ch7. The Compressor, Gate and Limiter went from
no CV inputs to a full set the same way (audio pair on ch0/ch1, jacks from ch2). Delay and Reverb
had listed their targets for a long time — on channels 2-6 of modules that declared two inputs, so
the jacks never existed and the AI schema advertised ports nothing could connect to;
`ModulationTargetBindingTests` now checks every target's channel against the declared input count.

Also covered, appended after each module's existing channels so saved patches keep theirs: the
LFO (Rate ch0, Level ch1, Glide ch2; Rate CV applies in Hz mode only), the ADSR (Attack ch9, Hold
ch10, Decay ch11, Sustain ch12, Release ch13, after Threshold ch8; a tempo-synced stage ignores its
time CV) and every remaining Parametric EQ parameter (B1/B4 Freq+Gain, each band's Q, Output on
ch6-14) — see [`modules.md#lfo-module`](modules.md#lfo-module),
[`modules.md#adsr-envelope-module`](modules.md#adsr-envelope-module) and
[`fx-modules.md#parametric-eq-module`](fx-modules.md#parametric-eq-module). Not yet covered: the
mixer modules (Channel Strip, Voice Mixer, Master), whose gain is driven by the mixer panel rather
than by CV — deliberately out of scope for FRO314's sweep below, since Channel Strip's channel
layout is frozen-once-set with fixed send legs (`Source/Modules/CLAUDE.md`).

**FRO314** closed two gaps this rule had let through silently: Oscillator's targets had no
`paramId` (falling back to fragile jack-label/knob-name string matching — harmless only because
every label happened to already match its knob's name) and its Unison/Detune knobs had **no CV
jack at all**, so a dropped cable was refused outright. It also gave the ADSR's three Curve
amounts (`attackCurve`/`decayCurve`/`releaseCurve`, ch14-16, appended after the stage-time/level
block) a CV jack for the first time — these have no generic rotary knob (they are edited only via
the envelope graph's bend handles, FRO112), so the jack is patchable through the mod matrix and
AI-authored connections, but dropping a cable onto a visible knob for them still needs its own
bend-handle drop anchor (tracked as a UI follow-up, not done as part of FRO314).

`Tests/UI/Graph/ModuleComponent/ModuleComponentKnobCoverageTests.cpp` is the regression guard for
this rule: it iterates every module the factory can create (both voice modes, where applicable)
and asserts every declared target resolves to a bound knob AND every continuous-parameter knob has
a target pointing at it, so a NEW module (or a new knob on an old one) with the same gap fails the
build instead of shipping. Its explicit, commented exclusion list documents which module types are
legitimately out of scope (paged tab cards, port widgets, per-step pattern data, mixer gain
stages, hosted-plugin parameters, and the two ADSR knob-less targets above) rather than absorbing
them silently.

## CV in normalised units

Every jack added under the rule above reads its CV the same way, through two `ModuleBase` helpers:

```cpp
static float blockCV(const juce::AudioBuffer<float>& buffer, int channel);            // first sample, 0 if absent
static float modulateNormalised(const juce::RangedAudioParameter&, float base, float cv);
```

`modulateNormalised` moves `base` by `cv` **in the parameter's own normalised range** and clamps:
+1.0 sweeps the knob from wherever it sits to its maximum, -1.0 to its minimum, and a skewed range
(an LFO rate) moves in the knob's skewed units rather than linearly. This is exactly what the
[modulation ring](#modulation-rings-on-knobs) draws — `baseNorm + modSignalValue` — so a jack
modulated this way rings exactly as far as it moves, and a bipolar LFO through a full-depth
attenuverter swings the knob across its whole travel. `base` is the module's current base value:
the smoothed value for a parameter the module ramps (Compressor Threshold, Chorus Centre Delay),
`*param` for one it does not.

The read is **once per block** (`blockCV` takes the block's first sample) for these jacks because
every one of them lands in a per-block quantity — a `juce::dsp` setter, a smoothing target, a
detector time constant — so a per-sample read would buy nothing and cost a branch per sample.
Jacks that predate the rule keep their own scaling (Flanger Rate adds `cv * 2.5 Hz`, Pitch Shifter
Pitch `cv * 24 semitones`, per sample) so no saved patch changes sound; the convention applies to
what was added, not retroactively.

## Smart cables, poly-bus wires and the mod matrix

The three surfaces that expose a routing's depth, and what each one does:

- **Smart cables.** Every mono CV cable (an `AttenuverterChain` routing) renders a circular knob at
  the midpoint of its bezier curve. Drag up/down to sweep depth from -100% to +100%; double-click to
  delete the connection.
- **Poly-bus wires.** When N per-voice `DirectCV` connections share the same source module and
  destination visible jack, GraphEditor collapses them into a single wire with an "xN" badge. These
  wires have **no** midpoint knob, because there is no attenuverter in the path. Dragging a cable
  between two equally-wide poly jacks creates all N per-voice connections directly, so no preset JSON
  has to hand-author each voice.
- **Mod matrix panel.** Lists every active CV connection as a labelled row with a bipolar slider. The
  sliders and the smart-cable knobs are **bidirectionally synced in real time** off a 30 Hz timer.
  The panel's own geometry, striping and row widgets are in
  [`layout/chrome.md`](../layout/chrome.md#mod-matrix-panel); the toolbar toggle that shows and hides
  it is in the same doc.

## Visual signal flow

`GraphEditor` draws animated signal-flow visualisation on top of all connections:

- **Animated dots on wires.** Three evenly-spaced dots travel along every wire's bezier curve, riding
  the exact same cubic path as the drawn wire. The dot colour always matches its wire's resolved
  colour.
- **Wire colour by routing kind.** In the default *By signal type* mode each routing maps to one
  colour token: `AttenuverterChain` and mono `DirectCV` → `modWire`; `PolyBus` → `polyBusWire`; port
  role `Pitch` → `pitchWire`; role `Gate` → `gateWire`. Role wins over kind, so a poly *pitch* fan is
  pitch-coloured, not poly-bus-coloured. Plain audio edges use `audioWire` and MIDI edges `midiWire`.
  A bypassed modulation cable is drawn at 30% alpha. All of this is resolved in one place — see
  [`layout/cables.md`](../layout/cables.md#colour-resolution), which also covers the *By source
  module* mode and user colour overrides.
- **Pulsing modulation lines.** Modulation wires pulse in brightness based on the live
  `modSignalPeak` from `ModulationRouting`, giving a visual sense of signal activity.
- **Activity glow on modules.** `ModuleComponent` drives an activity glow (painted at 15 Hz) that
  brightens when the module's RMS output changes meaningfully or when it has active incoming
  modulation.
- **Data source and cache rate.** `GraphEditor::timerCallback()` fires at **30 Hz**
  (`startTimerHz(30)`). Each tick it calls `AudioEngine::getModulationRoutings()` and
  `AudioEngine::getModulationDisplayInfo()` and stores the results in `cachedModRoutings` and
  `cachedModDisplayInfo`. The paint pass reads these cached values, so animated dots and pulsing
  wires are driven by up-to-date signal state without hitting the audio engine on every paint frame.
