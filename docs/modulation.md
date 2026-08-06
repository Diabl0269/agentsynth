# Modulation System Reference

This document describes the modulation architecture: how routing is modeled, how the engine derives it from the graph, the logical-port API used to draw collapsed poly-bus wires, and how the Poly Pad preset illustrates envelope modulation.

---

## Routing Kinds

Every CV connection in the graph resolves to one of three routing kinds, defined in `AudioEngine.h`:

```cpp
enum class RoutingKind { AttenuverterChain, DirectCV, PolyBus };
```

| Kind | What it is | How it arises |
|------|-----------|---------------|
| `AttenuverterChain` | A mono source signal passes through an `AttenuverterModule` node before reaching the destination's CV input. This is the standard user-adjustable modulation path. | Created by `AudioEngine::addModRouting()`. The engine automatically inserts an `AttenuverterModule` between source and destination. Amount is set to 1.0 at creation. |
| `DirectCV` | A source output connects directly into a destination's CV/ModCV input channel, with no intermediary attenuverter. | Any non-attenuverter connection into a mod-CV jack (e.g. env output 0 -> oscillator Level ch12). |
| `PolyBus` | N per-voice `DirectCV` connections (one per voice) that share the same source module and destination visible jack. The engine collapses them into one logical bus. | Arises when the source's `mapOutputChannel()` and destination's `mapInputChannel()` both describe a poly fan (e.g. ADSR outputs 0-7 -> VCA inputs 8-15 in the Poly Pad). Also created directly: dragging a cable between two equally-wide poly jacks, or toggling a module's `poly` parameter, creates/rebuilds all N raw connections at once — see [Creating Poly Connections](#creating-poly-connections) below. |

---

## AttenuverterModule Internals

`AttenuverterModule` is the intermediary node inserted by `AudioEngine::addModRouting()`. Key internals:

- **Constructor default Amount = 0.0.** The `amountParam` (`AudioParameterFloat`, range −1.0 to 1.0) is initialised to `0.0f`.
- **`addModRouting` sets Amount to 1.0.** After inserting the node, `AudioEngine::addModRouting()` finds the amount parameter by index and calls `setValueNotifyingHost(1.0f)`. This means a freshly connected modulation path immediately passes the full signal.
- **`addEmptyModRouting` leaves Amount at 0.0.** This method adds an `AttenuverterModule` node without wiring it or adjusting its parameter. The amount stays at the constructor default of `0.0`.
- **UI visualization atomics.** After each `processBlock`, the module writes two `std::atomic<float>` members for the UI to read lock-free:
  - `lastOutputPeak` — peak absolute value over the processed block (used to drive activity display).
  - `lastModValue` — the mid-block sample value (`audioData[numSamples / 2]`), providing a near-instantaneous signal reading for modulation rings.
  Both are exposed via `getLastOutputPeak()` and `getLastModValue()`.
- **Bypass behavior.** When bypassed, `lastOutputPeak` and `lastModValue` are both set to `0.0f` and all channels are cleared (no pass-through — an attenuverter in bypass silences its output).

---

## `AudioEngine::getModulationRoutings()`

```cpp
std::vector<ModulationRouting> getModulationRoutings() const;
```

Returns a read-only snapshot of every active modulation routing derived from the `AudioProcessorGraph`. The struct is:

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

**This is a derived, read-only view — it is never serialized.** The graph connections and JSON preset remain the single source of truth. Because `getModulationRoutings()` is re-derived from the live graph each time it is called, undo/redo and round-trip preset load/save are unaffected by this model.

`getActiveModRoutings()` and `getModulationDisplayInfo()` are thin adapters over `getModulationRoutings()` and are retained for legacy call sites.

---

## Logical-Port API

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

Poly-capable modules override these to describe fans. The default base implementation clamps any out-of-range channel to the last visible jack and marks `isPolyGroupHead` based on whether `rawChannel < getVisible*PortCount()`.

### `JackTarget` / `getJackTargets`

The inverse of `mapInput/OutputChannel` — given a visible jack, returns every poly-group head anchored to it:

```cpp
struct JackTarget {
    int rawHeadChannel;  // raw channel a wire anchored to this jack should start at
    PortRole role;
    int voiceSpan;       // 1 = mono; N = head of an N-voice fan
};

std::vector<JackTarget> getJackTargets(int visibleJackIndex, bool isInput) const;
```

Normally returns one entry. Poly MIDI's single "Poly Out" jack fronts two fans — Pitch at raw channel 0 and Gate at raw channel 8 — so it returns both, and the caller (`GraphEditor::resolvePolyLink`) disambiguates by role. Never returns empty: an unmapped jack falls back to the raw==jack identity, so modules without a logical-port override keep the pre-logical-port wiring behaviour.

### Port Labels

`ModuleBase` also declares virtual port-label accessors:

```cpp
virtual juce::String getInputPortLabel(int channelIndex) const;   // default: "In <N>"
virtual juce::String getOutputPortLabel(int channelIndex) const;  // default: "Out <N>"
```

Modules override these to supply human-readable jack names shown in the UI (e.g. `AttenuverterModule` returns `"Signal"` / `"Amount"` for its two inputs and `"Out"` for its output). The UI reads these labels to annotate jack tooltips and connection indicators with descriptive names rather than raw channel numbers.

### How GraphEditor uses the logical-port API

1. For every connection in the graph, GraphEditor calls `mapOutputChannel` on the source and `mapInputChannel` on the destination to get `LogicalPort` values.
2. The wire is anchored to `sourceVisibleJack` and `destVisibleJack` rather than the raw channel number. This collapses N per-voice wires onto a single visible jack.
3. If the engine classifies the routing as `PolyBus`, GraphEditor draws only one wire and overlays an "xN" voice-count badge on it (e.g. "x8").
4. Mono `DirectCV` wires are drawn without a badge.
5. `AttenuverterChain` wires render a draggable midpoint knob for depth control.

### Creating Poly Connections

`getJackTargets` and `GraphEditor::resolvePolyLink` drive connection *creation*, not just display:

1. `resolvePolyLink(source, sourceVisibleJack, dest, destVisibleJack)` calls `getJackTargets` on both ends and pairs one `JackTarget` from each into a `PolyLink { sourceRawChannel, destRawChannel, voiceCount }`. Pairings are scored: matching `PortRole` is the strongest signal (this is what lets Poly MIDI's single "Poly Out" jack send Pitch to an Oscillator and Gate to an Amp Env from the same jack); a `ModCV`/`Other` end is a wildcard, since mod inputs accept anything; equal fan widths break remaining ties.
2. `voiceCount = min(sourceSpan, destSpan)` — a fan only forms when both ends are equally wide. A poly-to-mono cable degrades to one head-to-head wire.
3. `GraphEditor::endConnectionDrag` resolves the visible jacks the user dropped a cable between through `resolvePolyLink` and adds `voiceCount` connections, `{sourceRawChannel + v} -> {destRawChannel + v}` for each voice. A fan is always wired direct — an `AttenuverterModule` is only inserted for a single mono mod wire (`voiceCount == 1`), because the engine collapses the N raw edges of a fan into one `PolyBus` for display.
4. `GraphEditor::disconnectPort` removes every raw channel a visible jack owns (via `getJackTargets`), so "Disconnect" on a poly jack does not leave some voices still wired.
5. Toggling a module's `poly` parameter calls `GraphEditor::rewireForPolyChange`, which re-anchors every cable touching that module: mono cables fan out to N voices when both ends go poly, fans collapse back to one wire when poly is switched off, and an attenuverter-mediated mod wire is recreated (carrying its amount across) or replaced by a direct fan as appropriate. MIDI connections are never moved. `ModuleComponent` snapshots the module's raw->`LogicalPort` maps just before the toggle, because by the time the parameter listener fires the live mapping already reflects the new layout — only the snapshot can say which visible jack an existing raw connection used to be anchored to.

---

## `isAutoPromotableModTarget`

```cpp
virtual bool isAutoPromotableModTarget(int dstChannel) const;
```

The default implementation returns `true` if `dstChannel` appears in `getModulationTargets()`. Poly-capable modules override it to return `false` when `poly == true`:

```cpp
bool isAutoPromotableModTarget(int dstChannel) const override {
    if (polyParam->get()) return false;
    return ModuleBase::isAutoPromotableModTarget(dstChannel);
}
```

**Why this matters:** AIStateMapper uses `isAutoPromotableModTarget` to decide whether to auto-wrap a connection in an `AttenuverterModule` when applying JSON patches. When a module is in poly mode, direct CV connections into the poly shared-CV block must stay as plain `DirectCV` routings rather than being auto-wrapped. Returning `false` prevents the auto-promotion.

---

## How Envelope Modulation Works in the Poly Pad

The Poly Pad preset (preset index 6) demonstrates two distinct modulation kinds for the same Amp Env module:

### Amp Env -> VCA (PolyBus)

The Amp Env module runs in poly mode (8 outputs, one per voice). The VCA runs in poly mode (8 CV inputs at channels 8-15, one per voice). The preset connects:

- ADSR output ch0 -> VCA input ch8  (voice 0 envelope -> voice 0 gain CV)
- ADSR output ch1 -> VCA input ch9
- ...
- ADSR output ch7 -> VCA input ch15

`AudioEngine::getModulationRoutings()` sees 8 `DirectCV` connections that share the same source module (Amp Env) and destination visible jack (VCA CV jack, index 1). It classifies them as a single `PolyBus` with `voiceCount = 8`. GraphEditor draws one wire with an "x8" badge. Per-voice amplitude is fully independent: when voice 3 releases, only voice 3's envelope falls.

### Amp Env -> Oscillator Level (DirectCV)

The preset also connects ADSR output ch0 (voice 0's envelope) directly to Oscillator input ch12 (the shared Level CV). This is a `DirectCV` routing: a single connection from one raw channel to one raw channel, without an attenuverter, and without a poly fan. `getModulationRoutings()` returns it as `DirectCV` with `voiceCount = 1`. GraphEditor draws it as a normal single wire without a badge. All 8 oscillator voices share the same level modulation (the voice-0 envelope signal).

This combination — PolyBus for per-voice gate control, DirectCV for a shared timbre parameter — is a common pattern when building poly patches.

---

## Modulation Rings on Knobs (ModuleComponent)

`ModuleComponent` renders Serum-style modulation rings on knobs for any active modulation targeting that module. It calls `AudioEngine::getModulationRoutings()` (via the `GraphEditor`'s cached snapshot) to find which knobs have live modulation and paints a ring overlay proportional to the routed signal value. This gives a real-time visual indication of modulation depth directly on the parameter knob.

---

## Visual Signal Flow

`GraphEditor` draws animated signal-flow visualisation on top of all connections:

- **Animated dots on wires.** Three evenly-spaced dots travel along every wire's bezier curve, riding the exact same cubic path as the drawn wire. Audio connection dots use the `audioWire` theme colour token (a light near-white in built-in themes); modulation connection dots use the `modWire` colour token (a bright cyan-blue in the Obsidian and Neon themes). Wire colours are theme-dependent — the dot colour always matches its wire's theme colour.
- **Pulsing modulation lines.** Modulation wires pulse in brightness based on the live `modSignalPeak` from `ModulationRouting`, giving a visual sense of signal activity.
- **Activity glow on modules.** `ModuleComponent` drives an activity glow (painted at 15 Hz) that brightens when the module's RMS output changes meaningfully or when it has active incoming modulation.
- **Data source and cache rate.** `GraphEditor::timerCallback()` fires at **30 Hz** (`startTimerHz(30)`). Each tick it calls `AudioEngine::getModulationRoutings()` and `AudioEngine::getModulationDisplayInfo()` and stores the results in `cachedModRoutings` and `cachedModDisplayInfo`. The paint pass reads these cached values so animated dots and pulsing wires are driven by up-to-date signal state without hitting the audio engine on every paint frame.
