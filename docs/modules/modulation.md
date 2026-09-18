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
[`architecture_timeline.md`](../architecture_timeline.md#automationrecorder-record-modes-and-gesture-capture).

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
[`architecture_plugin_layer.md`](../architecture_plugin_layer.md#automation-lanes-on-hosted-plugin-parameters).

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

**Right-click any knob → "Automate '\<Param\>'"** opens that parameter's automation lane in the
timeline panel's automation strip (creating its lane/track on first use) — see
[`timeline/automation.md`](../timeline/automation.md#the-knob-entry-point) for the full path
(`ModuleComponent` → `GraphEditor::onAutomateParameterRequested` → `MainComponent::automateParameter`).

## Drag-to-knob modulation

A cable released **on a knob** connects the source to that parameter's CV jack. This is the primary
way to patch modulation — aiming at a jack in the gutter still works, but on a module with sixteen CV
inputs it is the slow path.

- `ModuleComponent::getModTargetPortForPoint()` resolves a point to the visible rotary under it, then
  maps that knob's `getComponentID()` (the parameter's display name) through `getModulationTargets()`
  to a channel index, and reports it as the input `Port` its CV jack would be.
- `GraphEditor::endConnectionDrag()` consults it **only as a fallback**, so an actual jack under the
  cursor still wins, and **only for a cable dragged from an output** — a mod source drives a
  destination, and honouring the reverse would wire it backwards.
- From there it rejoins the normal drop path, so the connection still goes through
  `AudioEngine::addModRouting()` and still gets an attenuverter auto-promoted onto it. Nothing about
  the routing model changes; this is purely a second way to name the destination.
- `GraphEditor::dragConnection()` arms the knob under the cursor via `setModDropTargetChannel()`,
  which rings it while the cable is in flight, and `endConnectionDrag()` clears every card's
  highlight. Without the ring the drop is a guess.
- It is deliberately **not** folded into `getPortForPoint()`: that method also decides what a
  mouse-down starts, and a knob has to keep starting a value drag there rather than a cable.

Guarded by `GraphEditorTest.DroppingACableOnAKnobCreatesAModRouting` (full LFO output → Position knob
→ routing, including the highlight arming and clearing) and
`KnobDropIsIgnoredForACableDraggedFromAnInput`.

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
