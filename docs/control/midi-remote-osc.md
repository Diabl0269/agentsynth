# MIDI Remote — OSC as a second message source

**Status: design only, nothing here is built.** Companion to [`midi-remote.md`](midi-remote.md)
(the MIDI Remote model and decisions; read it first). There, OSC is a named non-goal of v1; this is
the design it defers to.

## What you'd get

OSC (Open Sound Control) is how phone and tablet controller apps talk to music software: TouchOSC,
Lemur, Open Stage Control and many others. It runs over the network instead of a MIDI cable, and
messages carry names such as `/synth/filter/cutoff 0.42` instead of CC numbers. With this feature:

- Turn on **OSC** in the MIDI Remote panel, point your tablet app at this computer and port, and your
  tablet appears as a controller in the Controllers list.
- **Right-click any knob → MIDI Learn**, move a fader on the tablet, and it is bound. It is the same
  Learn, the same panel and the same Inspector as a hardware controller, just keyed by the OSC address
  instead of a CC.
- The tablet's faders follow the app (mouse edits, automation, undo), because the app sends values
  back to the tablet.
- Everything a hardware knob does, an OSC fader does too: undo per gesture, automation Touch/Latch
  recording, and plugin-host notification.

---

## Where OSC fits in the v1 model

The v1 model separates the **source** (where messages come from) from the **mapping**
(`ControllerProfile` → `Control` → `Assignment` → `Target`) and from the **apply** path (the 60 Hz
message-thread drain with gestures). OSC only changes the source and the key. Everything from the
per-source lane FIFO onwards (drain, takeover, gestures, targets, undo, scopes, orphans) is reused
unchanged. That is why it is "a second `RemoteMessageSink`-style source" and not a new engine.

What does *not* carry over:

- The lookup key. `packLookupKey` packs `(source, type, channel, number)` into 32 bits with a 14-bit
  `number`. An OSC address is a string, so it needs an interned id (see Address key below).
- The thread. There is no `juce::MidiInput` driver thread. OSC arrives on a network thread that JUCE's
  `OSCReceiver` owns.
- Consumption. OSC never reaches the graph, so "consumed vs passed through" does not apply: every OSC
  message is MIDI Remote's.

---

## Design

### 1. Linking `juce_osc`

**Verified:** `juce_osc` is **not** linked today. Neither target's `target_link_libraries` block in
`CMakeLists.txt` (the Core list around line 486 and the AppUI list around line 746) names it. JUCE
itself comes from `FetchContent`, and `juce_osc` is a standard JUCE module that depends only on
`juce_core` and `juce_events`. Adding it costs one line and no new dependency.

**Where it links:** the **app layer**, not Core. The receiver and sender open sockets, and Core stays
free of networking, the same reason `juce::MidiOutput` lives in the app-layer
`MidiRemoteFeedbackOutputs` and not behind `RemoteFeedbackSink`. Core gets only plain values: an
address string, a float and an argument kind.

### 2. Receiving: its own thread, bounded, then the ordinary lane

```text
OSC network thread (juce::OSCReceiver, RealtimeCallback)
  OscRemoteSource::oscMessageReceived(msg)
    validate (below) → pick arg → normalise to float
    RemoteEngine::handleOsc(sourceKey, address, argIndex, value, argKind)   // Core, no MIDI types
      snapshot lookup by address id → slot
      push RemoteEvent onto THIS source's lane (same SPSC ring, same drain)
message thread: RemoteEngine::drain()   // unchanged
```

- **`RealtimeCallback`, not `MessageLoopCallback`.** JUCE offers both
  ([`OSCReceiver` docs](https://docs.juce.com/master/classOSCReceiver.html)). The realtime flavour
  runs "directly on the network thread that receives OSC data", and the message-loop flavour posts
  every packet to the message queue. Using the realtime flavour and our own bounded lane means a
  packet flood from the network drops the newest events at a fixed-size ring, instead of growing the
  app's message queue without limit (see Trust boundary).
- **The MIDI-path tripwire is relaxed only as far as it can be.** The network thread is neither the
  audio thread nor a MIDI driver thread. JUCE already allocates `juce::String`s there to parse the
  packet, so allocation on it is not the hazard it is on the MIDI path. Two rules still hold: **no lock
  shared with the audio thread** (the snapshot read uses the same reader-count `SnapshotPublisher`),
  and **the lane is single-producer**. One `OSCReceiver` = one thread = one lane, and its lane index is
  assigned once on the message thread like any other source (it counts against `kMaxRemoteSources`).
- **Source key:** `"osc:<port>"`. One listening port is one source is one `ControllerProfile`.
  Several tablets sending to the same port share that source and profile, which matches how TouchOSC
  users think of "the layout". A second port (a second layout) is a second profile.

### 3. Address key

- `MessageType` gains **`osc`**. `MessageSpec` gains **`address`** (a string, only meaningful for
  `osc`) and **`argIndex`** (0 by default, for multi-argument messages such as an XY pad
  `/xy 0.3 0.8` → two controls, `argIndex` 0 and 1). The project's denormalised assignment copy
  ([`midi-remote.md`](midi-remote.md#where-does-a-mapping-live--global-or-in-the-project)) needs the
  string itself, not a runtime id, so the address is data.
- **Interning at snapshot build (message thread):** every distinct `(address, argIndex)` in the table
  gets a dense id `0..N-1`. The snapshot stores a sorted `std::vector<juce::String>` for the lookup,
  and `number = id` in the packed key, so the existing `findSlot` works unchanged. The 14-bit field
  allows 16 384 addresses per source, far above any layout.
- **Lookup on the network thread:** binary search over the sorted address array by
  `juce::String::compare`. It compares, never constructs, and does not allocate.
- **Matching is exact.** OSC's address *patterns* (`*`, `?`, `[...]`, `{...}`) are for a receiver's
  dispatch. A controller sends concrete addresses. A pattern in an incoming message is rejected (see
  validation), never expanded: a wildcard from the network must not be able to fan out to every mapped
  parameter at once.
- `channel` is always 0 for OSC. `MessageSpec::operator==` gains the address and `argIndex`
  comparison (for non-OSC types these stay empty and 0, so v1 equality is unchanged).

### 4. Argument normalisation

| OSC arg | As | Default mapping to the control's 0..1 |
|---|---|---|
| float32 (`f`) | absolute | `(v − inMin) / (inMax − inMin)`, clamped; `inMin = 0`, `inMax = 1` by default (a common fader default in OSC apps; not verified per app, so Detect proposes a range from the values it sees) |
| int32 (`i`) | absolute | same formula with the control's input range; Detect proposes `0..127` if the first values seen are integers above 1 |
| `T` / `F` (true/false) and 0/1 on a button control | button | press / release, through the existing `buttonLike` decode |
| string, blob, time tag, nil, extra args | — | ignored (activity only) |

- **Per-control input range** (`inMin`, `inMax`) is a new optional `Control` field, only for `osc`,
  shown in the Inspector as "Receives from … to …". Relative encoding does not exist in OSC. An app
  that sends +1/−1 steps can be supported later with the existing `rel*` encodings if one is ever
  needed (not proposed).
- The decoded value becomes the same `RemoteEventKind::absolute` / `buttonPress` / `buttonRelease`
  the MIDI path produces, so takeover (default Scale), range/invert and gestures apply unchanged.
- `RemoteEvent::rawValue` (a 7-bit byte used by encoder auto-detect) is set to `round(value × 127)`
  for OSC. Encoder auto-detect is never offered for OSC controls.

### 5. Learn and Detect by address

The problem: a learn candidate currently rides the 24-byte `RemoteEvent` as `(type, channel, number)`,
and an OSC address does not fit.

| Option | For | Against |
|---|---|---|
| **A. A small side ring of fixed learn slots** (`char address[64]`, argIndex, value), per OSC source, written only while learn/Detect is armed | stays on the one-producer lane discipline; bounded; no lock | a 63-byte address limit for learn (longer ones cannot be learned, and the status bar says so) |
| B. Intern on the fly: the network thread adds new addresses to a lock-protected table | unlimited | a lock shared with the message thread; unbounded growth from a hostile sender |
| C. Learn via a separate `MessageLoopCallback` listener only while armed | simplest code | unbounded message-queue posting during learn; a second delivery path to reason about |

**Recommended: A.** 63 bytes covers every real layout address (`/1/fader12` is ten), and the limit is
visible, never silent. The drain turns a slot into a tally entry keyed by `(address, argIndex)`, and
the 300 ms most-messages rule from [`midi-remote.md`](midi-remote.md#learn-what-does-the-first-message-mean)
applies unchanged. Auto-profile creates the OSC profile's control named after the address
(`/1/fader3`), kind guessed from the argument (float → fader, `T`/`F` → button).

Unassigned traffic also uses side-ring slots (not only during learn) so the panel's live surface and
Detect mode light up, rate-limited to the ring's capacity.

### 6. Feedback: sending to a configured host:port

- The profile stores **`feedback: { host, port }`** (optional, off by default). "Reply to sender" is
  offered as a convenience: it remembers the last sender IP and uses the configured port.
- `RemoteFeedbackSink` today takes a `juce::MidiMessage`. OSC adds a second tiny interface,
  **`RemoteOscFeedbackSink::sendOsc(profileId, address, argIndex, value)`**, implemented in the app
  layer over one `juce::OSCSender` per profile. It keeps Core free of `juce_osc`, and is called from
  the same message-thread feedback pass
  ([`midi-remote.md`](midi-remote.md#controller-feedback)) with the same 250 ms cooldown and the same
  "only parameter-like targets echo" rule.
- The value is sent back in the control's own input range and argument type (float/int), so the
  tablet's fader shows what it would have sent. For a multi-arg control (XY), all of that address's
  args are sent together from their slots' current values.
- **Plugin build:** sending over UDP is fine from the message thread, but multi-instance behaviour
  follows the receiving rules below.

### 7. Trust boundary and network exposure

OSC is **unauthenticated UDP**. Anyone who can reach the port can send anything, and the source IP is
spoofable. The design treats every packet as hostile input:

- **Off by default.** Nothing listens until the user enables OSC.
- **Bind scope is explicit:** "This computer only" binds `127.0.0.1`, and "Local network" binds all
  interfaces. `OSCReceiver::connect(port)` is documented only as "connects to the specified UDP port". It is
  assumed to bind every interface, which the first receiver ticket must confirm. The localhost choice
  therefore binds a
  `juce::DatagramSocket` to `127.0.0.1` itself and hands it to `OSCReceiver::connectToSocket` (both
  documented in [`OSCReceiver`](https://docs.juce.com/master/classOSCReceiver.html); the exact
  `bindToPort(port, "127.0.0.1")` overload is to be confirmed against the pinned JUCE tag). Binding all
  interfaces may trigger the OS firewall prompt on macOS/Windows. The panel says so before the prompt
  appears.
- **Optional sender allow-list** (IP addresses) for "Local network". This is a speed bump, not
  security (UDP source IPs can be spoofed on a LAN), and the UI says exactly that.
- **What a packet can do is bounded by the mapping table.** An unmapped address does nothing but
  light the activity surface. A mapped one can only move the parameter, action or node command the
  *user* bound it to, through the same takeover and range the user set. OSC can never name a
  parameter, file, preset or command directly. There is no "OSC API".
- **Validation, before lookup, on the network thread:**
  - The address must start with `/`, be ≤ 128 bytes of printable ASCII, and contain no pattern
    characters `*?[]{}`.
  - Float args must be finite. NaN/±Inf is rejected; it would otherwise travel toward a parameter the
    way [`Source/CLAUDE.md`](../../Source/CLAUDE.md)'s NaN/Inf scrub rule exists to prevent.
  - Out-of-range values are clamped by the normalisation, never trusted.
  - Bundles are unpacked by JUCE. Their time tags are ignored (applied on arrival), so a far-future
    tag cannot park messages.
  - JUCE's `registerFormatErrorHandler` counts malformed packets; the panel shows the count, never the
    contents.
- **Flood behaviour:** the lane ring (512 events) and side ring drop the newest when full. At worst a
  flood costs one frame of knob movement. It never costs message-queue growth or an audio xrun.
- **No logging of packet contents** on the network thread. A malformed-packet counter only.

### 8. Plugin build

Two plugin instances (or the plugin plus the standalone app) cannot both bind one UDP port.
**Recommended:** OSC is available in the plugin but **off by default per instance**. Enabling it in a
second instance whose port is taken shows *"Port 9000 is in use (another Agent Synth?) — pick another
port"* rather than failing silently. This is a different reason from the Hosted "no hardware" rule
([`midi-remote.md`](midi-remote.md#the-plugin-build-vst3au-inside-a-host)). The host does not own the
network, so nothing forbids OSC inside a host; it is just shared. The receiver thread writes only its
own lane, so the Hosted audio thread is untouched.

### 9. Persistence

```text
ControllerProfile
  + osc : { port: 1..65535, bind: localhost | lan, allow: [ip], feedback: { host, port } | null }
          // present = this profile is an OSC source; `input`/`output` MIDI devices unused
Control.message (MessageSpec)
  + type osc, address: "/1/fader3", argIndex: 0
Control
  + inRange : { min, max }        // osc only; default 0..1
Assignment.spec                   // denormalised copy gains address + argIndex, like every other spec field
```

- Profiles stay one JSON file each under `MidiRemote/Controllers/`, and project assignments stay under
  the `"midiRemote"` key: [`midi-remote.md`](midi-remote.md#persistence-and-the-trust-boundary) is
  unchanged. `MessageSpec::fromVar` must reject an `osc` spec with a missing or invalid address the
  same all-or-nothing way it rejects a bad CC number, and the same address rules as the network
  validation apply on load (an imported profile is untrusted too).
- An older build that loads a profile or project with `type: "osc"` rejects it (unknown enum string is
  a hard failure in `RemoteModelJson.cpp`), and a project with an OSC assignment fails its
  `"midiRemote"` load visibly. **Open question 4.**
- Orphan handling is unchanged. Re-link matches on `(address, argIndex)`.

---

## Tests

| File | Case | Verifies |
|---|---|---|
| `Tests/MidiRemote/RemoteEngineOscTests.cpp` (new) | mapped float address | absolute event, correct slot, takeover applied in drain |
| same | int arg with `inRange 0..127`; `T`/`F` on a button | normalisation and button decode |
| same | NaN, Inf, wildcard address, 200-byte address, non-`/` address | rejected before lookup |
| same | unmapped address | activity side ring only, nothing applied |
| same | learn armed, sweep `/1/fader3` | binds `(address, 0)`; 64-byte address refused with message |
| same | XY `/xy f f` | two controls by `argIndex` |
| same | flood of 10 000 packets | ring caps, no growth, drain survives |
| same | threading (TSan) | network-thread push while the message thread republishes |
| `Tests/MidiRemote/RemoteModelTests.cpp` | `osc` spec/profile round-trip, invalid address on load | all-or-nothing |
| `Tests/MidiRemote/OscRemoteSourceTests.cpp` (new, app layer) | a real `juce::OSCSender` → receiver on `127.0.0.1` | end-to-end loopback; a localhost bind is not reachable on the LAN address (skipped if no non-loopback interface) |
| same | feedback | fake sender records `/1/fader3 0.5` after a mouse edit, not during the cooldown |
| same | port already bound | clear error surfaced, no crash |

All local, no network beyond loopback, and no paid service.

## Phased plan (ticket-sized)

1. **Model + key.** `MessageType::osc`, `MessageSpec.address/argIndex`, `Control.inRange`, JSON and
   validation, snapshot interning and address lookup, `RemoteEngine::handleOsc`. Headless tests only.
2. **Receiver.** Link `juce_osc` in the app layer, `OscRemoteSource` with localhost bind, validation,
   lane push, the Preferences/panel "Enable OSC" with port, and an OSC profile appearing in the
   Controllers list.
3. **Learn and Detect.** Side ring, learn by address, auto-profile/auto-control, and the live surface.
4. **Feedback.** `RemoteOscFeedbackSink`, `OSCSender` per profile, host:port config, and "reply to
   sender".
5. **LAN mode.** The all-interfaces bind, firewall-prompt copy, allow-list and malformed-packet
   counter.
6. **Plugin build.** Per-instance enable, the port-in-use message, and a Hosted smoke test.

## Open questions for the founder

1. **Default bind: localhost or LAN?** The main use case, a tablet, needs LAN. **Recommended:**
   OSC off by default. When the user turns it on, the choice defaults to **"This computer only"**,
   with "Local network (for phones/tablets)" one click away and explained. It is a deliberate, visible
   opt-in to exposure.
2. **Default port?** **Recommended:** `9000` for listening, user-editable. Common OSC app defaults
   were not verified for this doc, so the setup text should show the port rather than assume the app
   already matches.
3. **Learn address length limit of 63 bytes** (Option A above): acceptable? **Recommended:** yes.
4. **Forward compatibility:** an older build refuses projects containing an OSC assignment.
   **Recommended:** accept it. It is the existing all-or-nothing, visible-refusal rule. Softening it
   to "skip unknown assignment types" would be a separate decision across all of `"midiRemote"`.
5. **Plugin build:** OSC available but off per instance (recommended), or standalone only?
   **Recommended:** available but off. The port clash is handled with a clear message, and DAW users
   are the ones most likely to have a tablet layout already.
6. **Sender allow-list in the first LAN release, or later?** **Recommended:** later (phase 5 can
   ship without it). It is a convenience, not a security control, and the mapping table already bounds
   what a packet can do.
