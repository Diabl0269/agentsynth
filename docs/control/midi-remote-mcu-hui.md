# MIDI Remote — Mackie Control (MCU) and HUI protocol surfaces

**Status: design only, nothing here is built.** Companion to [`midi-remote.md`](midi-remote.md)
(the MIDI Remote model and decisions; read it first). There, MCU/HUI is a named non-goal of the
generic learnable design. This doc is the separate integration it defers to.

## What you'd get

Mixing-style controllers such as the Behringer X-Touch, iCON Platform/QCon, PreSonus FaderPort 8/16,
Mackie MCU Pro, SSL UF8 and Softube Console 1 Fader all speak one of two old, fixed "DAW remote"
languages: **Mackie Control** (MCU) or **HUI**. You pick "Mackie Control" or "HUI" as the controller
type, choose its MIDI in/out ports, and it works with no learning:

- **Eight channel strips follow the mixer.** Motor faders jump to each channel's level, mute/solo/select
  lights match the app, the scribble-strip screens show channel names, and the meters bounce. Bank
  left/right moves the eight strips across the mixer. The master fader drives Master.
- **The knobs above the faders** (V-Pots) control pan, sends, or the selected module's parameters
  eight at a time, and their LED rings show the value.
- **Transport and jog** (play/stop/record/loop, rewind/forward and the jog wheel) drive the timeline,
  and the timecode display shows bars/beats.
- Touching a fader is a real "hand on the knob": it records automation in Touch/Latch and is one undo
  step, exactly like a mouse drag.

It is not a generic mapping. The layout is fixed by the protocol, the app drives the motors, lights and
screens continuously, and nothing needs to be learned.

---

## The protocols

### Sources and how far to trust them

Neither protocol has a public vendor specification. Every wire-level number below comes from
community reverse-engineering or from open-source DAW implementations, cross-checked where two
sources agree. Sources, abbreviated in the tables:

- **[TouchMCU]** — [NicoG60/TouchMCU `mackie_control_protocol.md`](https://github.com/NicoG60/TouchMCU/blob/main/doc/mackie_control_protocol.md),
  a reverse-engineered MCU/Logic Control protocol write-up.
- **[Ardour]** — Ardour's MCU implementation, `libs/surfaces/mackie/`:
  [`surface.cc`](https://github.com/Ardour/ardour/blob/master/libs/surfaces/mackie/surface.cc),
  [`device_info.cc`](https://github.com/Ardour/ardour/blob/master/libs/surfaces/mackie/device_info.cc),
  [`pot.cc`](https://github.com/Ardour/ardour/blob/master/libs/surfaces/mackie/pot.cc),
  [`jog.cc`](https://github.com/Ardour/ardour/blob/master/libs/surfaces/mackie/jog.cc),
  [`meter.cc`](https://github.com/Ardour/ardour/blob/master/libs/surfaces/mackie/meter.cc).
- **[DBM]** — DrivenByMoss's HUI implementation (Bitwig extension):
  [`HUIControlSurface.java`](https://github.com/git-moss/DrivenByMoss/blob/master/src/main/java/de/mossgrabers/controller/mackie/hui/controller/HUIControlSurface.java),
  [`HUIDisplay.java`](https://github.com/git-moss/DrivenByMoss/blob/master/src/main/java/de/mossgrabers/controller/mackie/hui/controller/HUIDisplay.java),
  [`HUIControllerSetup.java`](https://github.com/git-moss/DrivenByMoss/blob/master/src/main/java/de/mossgrabers/controller/mackie/hui/HUIControllerSetup.java),
  and its [HUI user doc](https://github.com/git-moss/DrivenByMoss-Documentation/blob/master/Mackie/Mackie-HUI.md).
- **[Apple]** — Logic Pro's *Control Surfaces Support* guide: behaviour only, no wire-level numbers
  ([V-Pots](https://support.apple.com/guide/logicpro-css/v-pots-ctls722275cd/mac),
  [Faders](https://support.apple.com/guide/logicpro-css/faders-ctls72224aae/mac),
  [HUI setup](https://support.apple.com/guide/logicpro-css/hui-setup-ctls73678434/mac)).
- **[Wiki]** — [Human User Interface Protocol](https://en.wikipedia.org/wiki/Human_User_Interface_Protocol) (history and resolution).

Anything marked **unverified** was seen in at most one weak source, or in none. The implementation
ticket must confirm it on real hardware before relying on it. Channel numbers below are 1-based, as a
user would say them. Hex status bytes are 0-based (`0xE0` = pitch bend on channel 1).

### Mackie Control Universal (MCU)

| Element | Wire format | Source |
|---|---|---|
| SysEx header | `F0 00 00 66 <dev>`: `66` = Mackie; `dev` `14` = MCU, `15` = MCU extender (XT), `10`/`11` = Logic Control / its XT | [Ardour], [TouchMCU] |
| 8 channel strips per unit | fixed; Ardour's default strip count is 8 | [Ardour] `device_info.cc` |
| Faders | **pitch bend, 14-bit (0–16383)**, channel 1–8 = strips 1–8, **channel 9 = master**. Same message both ways: the surface sends it when moved, the host sends it to drive the motor | [Ardour], [TouchMCU] |
| Fader touch | note on/off, notes **104–112** (0x68–0x70), strip 1–8 then master; velocity > 64 = touched | [Ardour], [TouchMCU] |
| V-Pot rotation | CC **16–23** (0x10–0x17), relative: bit 6 = direction (set = counter-clockwise), bits 0–5 = tick count | [Ardour] `surface.cc`, [TouchMCU] |
| V-Pot LED ring | CC **48–55** (0x30–0x37), host → surface. Value: bit 6 = centre LED, bits 4–5 = mode (0–3), bits 0–3 = position (0–11) | [Ardour] `pot.cc`, [TouchMCU] |
| V-Pot ring mode names | 0 = single dot, 1 = boost/cut, 2 = wrap, 3 = spread | **unverified for MCU** (same names DBM uses for HUI rings; Ardour names only `spread`) |
| Strip buttons | notes: REC **0–7**, SOLO **8–15**, MUTE **16–23**, SELECT **24–31**, V-Pot push **32–39** | [Ardour] `device_info.cc`, [TouchMCU] (REC/SOLO/MUTE/SELECT) |
| Assignment buttons | notes Track 40, Send 41, Pan 42, Plug-in 43, EQ 44, Instrument/Dyn 45 | [Ardour] |
| Banking | Bank ◀ 46, Bank ▶ 47, Channel ◀ 48, Channel ▶ 49, Flip 50 | [Ardour] |
| Transport | Rewind 91, Fast-fwd 92, Stop 93, Play 94, Record 95; Cycle 86, Marker 84, Nudge 85 | [Ardour], [TouchMCU] |
| Cursor / zoom / scrub | notes 96–99 (up/down/left/right), Zoom 100, Scrub 101 | [Ardour] |
| Button LEDs | host sends the same note number back: velocity `7F` = on, `00` = off | [TouchMCU], [Ardour] |
| LED blink | velocity `01` = blink | **unverified** (TouchMCU only) |
| Jog wheel | CC **60** (0x3C), same sign-magnitude relative encoding as a V-Pot | [Ardour] `jog.cc`, `surface.cc` |
| LCD (scribble strips) | `F0 00 00 66 14 12 <offset> <ASCII…> F7`: a 2 × 56-character buffer, line 1 at offset `00`, line 2 at `38` (56). Each strip is 7 characters wide | [Ardour], [TouchMCU] |
| Timecode / assignment 7-segment | CC **64–75** (0x40–0x4B): 64–73 = the ten timecode digits, 74–75 = the two-character assignment display. Value bit 6 = decimal point, bits 0–5 = character | [Ardour], [TouchMCU] |
| 7-segment digit order | which CC is the leftmost digit | **unverified** (Ardour sends only changed digits, order not confirmed) |
| Meters | channel pressure `D0 <strip<<4 \| level>`: level 0–13 (0x0–0xD, Ardour scales to 13 segments); low nibble `E` = set overload, `F` = clear overload | [Ardour] `meter.cc`, [TouchMCU] |
| Connection sysex | message types: `01` device ready / connection challenge, `02` host reply, `03` connection confirmed, `04` denied. Also `08` reset, `09` fader recalibrate, `0A` backlight, `0E` touch sensitivity | [Ardour] `surface.cc`, [TouchMCU] |
| Host device query | host sending `00` to ask the surface to announce itself | **unverified** |
| Challenge/response | Logic Control units expect the host to answer a challenge; whether an MCU in Mackie mode requires it | **unverified**. Ardour answers it; TouchMCU describes the 3-step exchange but not the algorithm's authority |
| Extenders (XT) | each XT is its own MIDI port pair, the same messages with header `15`, 8 more strips, no master/transport. The host decides which 8 mixer channels each unit shows | [Ardour], [TouchMCU]; Apple mentions "XT and C4 units" as expansions |

Behaviour ([Apple]): faders are motorised and "generally used to control the channel level"; there is
a master fader; **Flip** swaps the fader and V-Pot functions; the V-Pot ring shows the value and the
V-Pot push resets to default or toggles.

### HUI (Mackie/Digidesign, 1997)

HUI predates MCU. It was created by Mackie and Digidesign for Pro Tools in 1997 [Wiki]. Its Pro
Tools heritage is why many controllers still offer a HUI mode.

| Element | Wire format | Source |
|---|---|---|
| Strips | 8 | [DBM] |
| Keep-alive | host sends **note-on 0 velocity 0** (`90 00 00`) **every second**; without it the surface goes offline | [DBM] code (1000 ms task) and user doc |
| Ping reply | surface answers `90 00 7F` | **unverified** (secondary summary only) |
| Buttons in (surface → host) | two CCs: **CC 15 (0x0F) = zone**, then **CC 47 (0x2F) = port**, value ≥ `0x40` = press, else release. Button index = zone × 8 + port | [DBM] |
| Buttons/LEDs out (host → surface) | **CC 12 (0x0C) = zone**, then **CC 44 (0x2C) = port + `0x40` if on** | [DBM] |
| Strip zones | zones 0–7 = strips 1–8; ports 0 = fader touch, 1 = select, 2 = mute, 3 = solo, 4 = auto, 5 = V-sel, 6 = insert, 7 = rec arm | [DBM] |
| Transport | zone 14 (0x0E): port 1 rewind, 2 fast-fwd, 3 stop, 4 play, 5 record. Bank ◀/▶ = zone 10, ports 1/3 | [DBM] (button ids 113–117, 81, 83) |
| Faders | **CC 0–7 = high byte, CC 32–39 = low byte** per strip, value = hi × 128 + lo; same CCs both ways (host drives the motor) | [DBM] |
| Fader resolution | 10-bit (1024 steps) carried in that 14-bit pair | [Wiki] |
| V-Pot rotation | CC 64–71 (0x40–0x47) | [DBM] (CC numbers only) |
| V-Pot encoding | direction/magnitude bit layout | **unverified** |
| V-Pot LED ring | CC 16–23 (0x10–0x17), host → surface, value = mode × 16 + position + 1 | [DBM] |
| Jog | CC 13 (0x0D), two's-complement relative | [DBM] |
| Meters | poly aftertouch (`A0`) per strip, left/right side packed into the message | [DBM]. **Exact byte packing unverified** |
| Scribble strips | `F0 00 00 66 05 00 10 <cell 0–8> <4 ASCII chars> F7`: nine 4-character cells (8 strips + one extra) | [DBM] `HUIDisplay.java` |
| Character set | original HUI uses a modified ASCII (umlauts etc.); emulations generally only plain ASCII | [DBM] |
| Main 40×2 display, timecode display | sysex commands | **unverified** (not in any source fetched) |
| Discovery | "HUI control surface devices don't support automatic scanning"; added manually with an in and out port | [Apple] |

### MCU vs HUI in one paragraph

Both give 8 motor-fader strips with mute/solo/select, knobs with LED rings, transport and small
screens. MCU is newer and simpler to implement. Every control has its own fixed note or CC, faders are
full 14-bit pitch bend, the screens are one 2 × 56-character sysex write, and extenders just repeat
the protocol on another port. HUI is older and chattier. Buttons arrive as a two-message "zone then
port" pair, faders are split over two CCs, the host must ping it every second or it drops offline,
and the screens are 4-character cells. Build MCU first. It covers most hardware sold today, since
most HUI-capable controllers also offer an MCU mode.

---

## Why this is not a generic learnable profile

The generic design (`ControllerProfile` + learned `Control`s + `RemoteEngine`) assumes the hardware
is a set of independent controls, each an absolute or relative value you bind one at a time. A
protocol surface breaks every one of those assumptions:

1. **Fixed layout, not detected.** The meaning of note 16 (mute strip 1) is fixed by the protocol.
   Learning it would only let a user get it wrong.
2. **The mapping moves.** A fader is not bound to one parameter; it is bound to "the gain of whichever
   channel sits in slot 3 of the current bank". Banking, the V-Pot assignment mode (pan/send/plug-in)
   and Flip all re-target every control at once. An `Assignment` is a static control → target pair.
3. **Bidirectional state, not an echo.** The host must drive motors, LEDs, rings, a 112-character
   LCD, 12 seven-segment digits and meters continuously, whether or not any control moved. v1 feedback
   only echoes a mapped parameter's value
   ([`midi-remote.md`](midi-remote.md#controller-feedback)).
4. **The whole port belongs to the surface.** Standalone opens every MIDI input and forwards unmapped
   messages to the graph ([`midi-input.md`](midi-input.md)). An MCU's buttons are **note-ons on channel
   1**, so today pressing Mute on strip 3 (note 18) would play F#0 in the patch. Its connection replies and HUI's
   ping replies are **sysex**, which `detail::classifyMessage` drops as ineligible before any sink logic
   runs. A generic profile only consumes *assigned* controls, so it cannot hold this line.
5. **Touch is a gesture boundary.** Fader touch notes say exactly when a hand lands and leaves, which is
   better than v1's 250 ms idle guess. A generic profile has nowhere to put that pairing.

So a protocol surface is a **sibling of `RemoteEngine` behind the same seam**, not a profile inside it.
It still reuses the parts that are generic: the lock-free MIDI-path discipline, the message-thread
apply-with-gestures path, `RemoteFeedbackSink` / `MidiRemoteFeedbackOutputs` for output, the action
and node-command invokers, and the panel's Controllers list.

---

## Design

### 1. One seam, routed by source

`AudioEngine` keeps exactly one `RemoteMessageSink*`. The app layer installs a **`RemoteSinkRouter`**
(Core, implements `RemoteMessageSink`) in place of `RemoteEngine`:

```text
RemoteSinkRouter::handleMessage(sourceKey, msg)      // MIDI thread (standalone only; see Plugin build)
  owner = ownedSources.find(sourceKey)                // immutable table, SnapshotPublisher discipline
  owner != null → owner->handleMessage(sourceKey, msg); return true   // consume EVERYTHING from the port
  otherwise     → return remoteEngine.handleMessage(sourceKey, msg)   // v1 path, unchanged
```

- The owned-source table is published exactly like `RemoteMappingSnapshot`, using an atomic pointer,
  a reader count and a message-thread retire list (`SnapshotPublisher`, `RemoteMappingSnapshot.h`). It
  is not a second mechanism.
- A port bound to a protocol surface is **never** a generic source, even for learn. Its messages never
  reach the collector, `ExternalMidiModule` or a recording take.
- `AudioEngine`'s existing `ScopedRemoteSinkCall` teardown guard covers the router. The router itself
  must be torn down after its owned surfaces are detached (same ordering as `RemoteEngine`).

### 2. `ProtocolSurface`: MIDI path half

`ProtocolSurface` is an abstract Core class with `McuSurface` and `HuiSurface` implementations. Each
bound port (an MCU, each XT, a HUI) is one instance with its own pre-allocated SPSC ring of a small POD:

```text
SurfaceEvent { kind: fader | touch | encoderDelta | button | jog | sysex, unit, index, value, sysexLen, sysex[20] }
```

- The generic `RemoteEventFifo` is refactored into a `SpscRing<T>` template, keeping the same
  release/acquire proof and TSan test. Both rings use it.
- **Sysex on the MIDI path** is copied into a fixed 20-byte slot. The MCU connection replies are well
  under that. Anything longer (a surface echoing its own LCD, a firmware dump) is dropped there, with
  no allocation and no logging.
- **HUI's zone/port pairing and fader hi/lo pairing** are per-port producer-thread state, the same
  shape as `LaneState`'s paired-CC cache. A port message without a preceding zone is dropped.
- Nothing is applied here. There are no locks, no allocation and no logging, per the MIDI-path
  tripwire in [`midi-remote.md`](midi-remote.md#threading-the-mapping-table-crosses-threads).

### 3. `SurfaceController`: message-thread half

`SurfaceController` lives in the app layer, because it needs the mixer model, the transport and the
selection. It drains every surface's ring from the **same 60 Hz drain tick** `RemoteEngine` uses. It
adds no second timer, and it is gated on "a protocol surface is bound".

- **Layout model.** `buildMixerSnapshot(graph, doc, macros)` (`Source/Mixer/MixerModel/MixerModel.h`)
  already produces the mixer's columns in panel order. Strip slots are `Kind::Strip` + `Kind::Bus`
  columns, `Direct` is skipped (it has no fader), and `Master` goes to the master fader. It is rebuilt
  from the existing reconcile funnel after any graph change, never per tick.
- **Bank offset.** Bank ◀/▶ moves by 8 (by 8 × units with extenders), Channel ◀/▶ moves by 1. The
  offset is clamped so the last bank is full where possible. Units are ordered by the user
  (left-to-right) in the controller settings.
- **Targets.** Every resolved control uses paths that already exist:

| Control | Target | Path reused |
|---|---|---|
| Fader | strip `ChannelStripModule` `"gain"` (dB −60…+12) | parameter gesture path |
| Master fader | Master's gain, via the `masterVolume` continuous target's `ContinuousParameterLookup` | [`midi-remote.md`](midi-remote.md#continuous-targets) |
| Mute | strip's mute parameter | parameter gesture path |
| Solo | `RemoteActionInvoker::invokeNodeCommand(node, toggleSolo)` | [`midi-remote.md`](midi-remote.md#node-command-targets), one undo step per press |
| Select | select that column in the mixer / its node on the canvas | new small app-layer call |
| REC | arm the track feeding that strip (`MixerColumn::feedingTracks`) | Open question 3 |
| V-Pot, **Pan** mode | `"pan"` | parameter gesture path, relative |
| V-Pot, **Send** mode | `send<N>Level` for the chosen send slot N | parameter gesture path, relative |
| V-Pot, **Plug-in** mode | the **selected module's** parameters, 8 at a time, paged by Channel ◀/▶ | parameter gesture path; order = the card's visible parameters (or a hosted plugin's chosen card knobs, [`plugin-card-layout.md`](plugin-card-layout.md)) |
| Flip | swaps fader and V-Pot targets for the current mode | — |
| Transport | the command-dispatched Play, Stop, Record, Loop, Return-to-start, cursor-by-bar actions | [`midi-remote.md`](midi-remote.md#action-targets) |
| Jog | the `playhead` continuous target, relative | [`midi-remote.md`](midi-remote.md#continuous-targets) |
| F1–F8 / User | user-assignable to any action (Open question 4) | the profile's global `actions[]` |

- **Gestures.** Fader touch-down calls `beginChangeGesture`, moves call `setValueNotifyingHost`, and
  touch-up calls `endChangeGesture`. That is one undo step and a real automation Touch/Latch pass. A
  surface that never sends touch notes (seen after the first moves with no touch) falls back to v1's
  `kGestureIdleMs` idle end. V-Pot gestures use the idle end.
- **Takeover is always Jump** for motor faders: the motor has already put the fader where the value is.
  Non-motor MCU-mode controllers are Open question 2.

### 4. The surface renderer (feedback)

A per-unit **`SurfaceRenderer`** keeps a mirror of what the hardware currently shows and sends only
differences, through `RemoteFeedbackSink::sendFeedback(outputDevice, message)`. The sink already takes a
bare output-device `ControllerProfile::Input`, so `MidiRemoteFeedbackOutputs` is reused verbatim,
including its cached `juce::MidiOutput` per device and remembered open failures.

| Element | When sent | Notes |
|---|---|---|
| Motor faders | on change, **suppressed while that fader is touched**, final value sent on release | reuses v1's "never fight a moving hand" rule; no 250 ms cooldown needed when touch is present |
| Button LEDs | on change | mute/solo/select/rec per strip, assignment mode, transport state, Flip |
| V-Pot rings | on change | pan uses boost/cut-style centre, levels use wrap. Mode names unverified (see table) |
| LCD | on change, coalesced to ≤ 10 Hz | line 1 = channel names (7 characters, truncated), line 2 = value while a control moves, else the mode name. A full rewrite is ~120 bytes |
| 7-segment | on change, ≤ 10 Hz | bars.beats.ticks from the transport; assignment display = mode (`PN`, `S1`, `PL`) |
| Meters | every drain tick while playing | reads peak via a **new `MeterReader::ControlSurface`** enumerator; the Modules rule forbids reusing the mixer's slot (`Source/Modules/CLAUDE.md`) |
| HUI ping | every 1 s while bound | `90 00 00`; the surface is "online" once anything arrives |
| MCU connect | on bind and on device reconnect | answer the device-ready message; challenge handling per Open question 5 |

- **Bandwidth.** A USB surface is not the constraint. A 5-pin DIN MCU is: MIDI 1.0 runs at 31 250 baud,
  about 3 KB/s. Nine faders at 3 bytes, a few LEDs and 8 meter bytes per 60 Hz tick is ~2 KB/s worst
  case. Hence meters only while playing, the LCD capped at 10 Hz, and full resync only on connect or
  bank change.
- **Resync.** On bind, reconnect, bank change or mode change, the mirror is marked dirty and redrawn
  in full.

### 5. Plugin build

**Standalone only.** In `HostMode::Hosted` the app opens no MIDI ports, and `AgentSynthAudioProcessor`
declares `producesMidi() == false`, so there is no path out to motors or screens. A surface on the
host's MIDI stream would also be driving the host's own MCU support at the same time. The plugin panel
lists MCU/HUI controllers as *standalone only* and inert, the same treatment real-device profiles
already get ([`midi-remote.md`](midi-remote.md#the-plugin-build-vst3au-inside-a-host)). Inside a DAW,
the DAW's own MCU support already drives the plugin's parameters through host automation.

### 6. How a user enables it

- Controllers list → **+ Add controller** → type: **Generic (learn)** (today's default), **Mackie
  Control**, **Mackie Control Extender**, **HUI**. Then choose input port, output port (required for
  these types) and, for an extender, its position left/right of the main unit.
- The Surface area draws a **bespoke fixed layout** (8 strips with fader, V-Pot, four buttons and
  scribble text; transport block) from the protocol, not a detected grid. Detect mode and Learn are
  hidden for these profiles. Controls light live from the same activity path.
- Optional convenience (Open question 6): when a newly seen port's name contains "MCU", "Mackie",
  "X-Touch" or "HUI", the Add popover pre-selects that type.

---

## Data model

```text
ControllerProfile
  + protocol : generic | mcu | mcuExtender | hui         // absent = generic
  + protocolSettings : { unitOrder: int, motorFaders: bool (default true),
                         hasDisplay: bool (default true), vpotDefaultMode: pan | send | plugin }
  controls[] : empty for a protocol profile (the layout is code, not data)
  actions[]  : F-key / User-button assignments only (global, as today)
```

- The bank offset, V-Pot mode and Flip are **session state**. They are neither in the project nor in
  the profile. **Open question 7** asks whether to persist them.
- Older builds ignore unknown keys (`ControllerProfile::fromVar` reads named properties only), so an
  older build would load an MCU profile as an empty generic one. That is harmless, since it has no
  controls. The panel's orphan/Re-link flows skip protocol profiles.
- No project-document change: everything a surface drives is resolved live from the mixer model.

## Threading summary

MIDI thread: router lookup, protocol decode into `SurfaceEvent`, and a fixed sysex copy. There are no
locks, allocations or logging. Message thread: drain, apply with gestures, and the renderer diff/send.
It is the same drain tick as v1, and feedback never leaves the message thread, which is the v1 rule.

## Tests

| File | Case | Verifies |
|---|---|---|
| `Tests/MidiRemote/McuDecodeTests.cpp` (new) | pitch bend ch 1–9, touch notes 104–112, V-Pot sign-magnitude, jog CC 60, strip/transport notes | decode to `SurfaceEvent` |
| `Tests/MidiRemote/HuiDecodeTests.cpp` (new) | zone/port pairs, port without zone, fader hi/lo, V-Pot CCs | producer-state machine |
| `Tests/MidiRemote/RemoteSinkRouterTests.cpp` (new) | a note, a CC and a sysex from an owned port | always consumed, never reach `RemoteEngine`; unowned port unchanged |
| same | sysex longer than the slot | dropped, no allocation |
| same | threading (TSan) | router republish during traffic from two ports |
| `Tests/MidiRemote/SurfaceControllerTests.cpp` (new) | banking over 11 strips + bus, extender unit order | slot → column mapping |
| same | touch → move × N → release | exactly one begin/end gesture, one undo step |
| same | no touch notes | idle-timeout fallback |
| same | Solo press | `invokeNodeCommand` once, one undo step |
| `Tests/MidiRemote/SurfaceRendererTests.cpp` (new) | fake `RemoteFeedbackSink` | exact LCD sysex bytes, diff-only sends, touch suppresses motor echo, full resync on bank change, HUI ping cadence with a fake clock |
| `Tests/MidiRemote/MidiRemoteWorkflowE2ETests.cpp` | add an MCU profile, drive a fader through a fake port | strip gain moves, automation Touch records |

## Phased plan (ticket-sized)

1. **Router + MCU input core.** `SpscRing<T>` refactor, `RemoteSinkRouter`, `McuSurface` decode, consume
   everything from the bound port, faders, touch gestures, mute/solo/select for one unit, bank ◀/▶, and
   the motor-fader and button-LED renderer. Includes a minimal "Mackie Control" choice in Add
   controller. This is the first shippable slice.
2. **V-Pots and master.** Rings, Pan/Send modes, Flip, the master fader, transport buttons and jog.
3. **Screens and meters.** The LCD, 7-segment timecode/assignment display, and meters (new
   `MeterReader`).
4. **Extenders.** Multiple units, unit ordering, banking by 8 × units.
5. **Plug-in mode.** V-Pots on the selected module's parameters, paged, with names on the LCD.
6. **HUI.** `HuiSurface` decode, ping, 4-character scribble cells, and LED/ring output. Verify the
   unverified rows above against hardware first.
7. **Panel polish.** Bespoke surface drawing, port-name type suggestion, and F-key action assignment.

## Open questions for the founder

1. **MCU first, HUI later, or both at once?** **Recommended:** MCU first (phases 1–5), HUI as phase 6.
   Most hardware offers an MCU mode, and HUI's unverified details need real hardware to pin down.
2. **Controllers in MCU mode without motor faders** (many budget ones)? **Recommended:** a
   "Motor faders" checkbox, default on. When off, faders use the Preferences takeover (default Scale),
   exactly like a generic fader.
3. **What should REC do?** Arm the timeline track feeding that strip, or nothing? **Recommended:** arm
   the feeding track when exactly one track feeds the strip; otherwise light nothing and do nothing.
4. **Should the F-keys and User buttons be user-assignable** to any action? **Recommended:** yes. They
   reuse the profile's existing global `actions[]`, so it costs little and it is the one place users
   expect customisation.
5. **Implement the Logic Control challenge/response?** **Recommended:** no. Support Mackie-mode MCU
   only (header `14`/`15`) and answer the device-ready message. Revisit only if a real unit refuses to
   come online. The algorithm is unverified and Logic-Control-only hardware is rare.
6. **Auto-suggest the protocol from the port name?** **Recommended:** suggest in the Add popover, never
   auto-bind. A port named "X-Touch" may be in generic MIDI mode.
7. **Persist bank position and V-Pot mode** across launches? **Recommended:** no. Start at bank 1 / Pan
   every launch, which is what users of other DAWs expect.
