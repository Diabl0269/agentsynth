# MIDI Remote — MPE (per-note expression) design

**Status: design only, nothing here is built.** Companion to [`midi-remote.md`](midi-remote.md)
(the MIDI Remote model and decisions; read it first) and [`midi-input.md`](midi-input.md) (the note
path MIDI Remote sits in front of).

## What you'd get

MPE controllers (ROLI Seaboard/LUMI, Linnstrument, Sensel Morph, Osmose, Continuum and so on) send
each finger on its own MIDI channel. That is how they carry **per-note** pitch glide, pressure and
"slide" (forward/back, CC 74) instead of one bend wheel for the whole keyboard. With this feature:

- Plug in an MPE controller and play. Each voice of a Poly MIDI patch bends, swells and brightens
  on its own, and chords stay in tune while one finger slides.
- The patch sees two new per-voice CV fans on Poly MIDI, **Pressure** and **Timbre**, which you
  cable wherever you like (VCA, filter cutoff, wavetable position). Per-note bend is folded into the
  existing Pitch output, so current patches glide with no rewiring.
- MIDI Remote stops fighting MPE. Per-note messages are never mapped to knobs by accident and never
  swallowed by an "any channel" mapping. The controller's own global knobs and faders, sent on its
  manager channel, still map as usual.
- In the plugin build it works on whatever MPE stream the host sends.

---

## The protocol, briefly

Facts below come from JUCE's MPE implementation, which follows the MIDI Association MPE
specification. The spec PDF itself is members-only and was not read directly.

- A **zone** is one **manager channel** plus a run of **member channels**. The **lower zone**'s
  manager is channel 1 and its members count up from 2. The **upper zone**'s manager is channel 16
  and its members count down from 15. Source: [JUCE `MPEZoneLayout`](https://docs.juce.com/master/classMPEZoneLayout.html).
- A zone is announced with the **MPE Configuration Message (MCM)**: **RPN 6**, sent on the manager
  channel (1 or 16), whose data value is the number of member channels. A value of 0 removes the zone.
  Sources: [JUCE `MPEMessages`](https://docs.juce.com/master/classMPEMessages.html) (`zoneLayoutMessagesRpnNumber` = 6) and
  [`juce_MPEMessages.cpp`](https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_basics/mpe/juce_MPEMessages.cpp)
  (`setLowerZone` generates RPN 6 on channel 1, `setUpperZone` on channel 16, data = `numMemberChannels`).
- **Pitch-bend range** is RPN 0, per channel. JUCE's defaults are 48 semitones on member channels
  and 2 on the manager ([`MPEZoneLayout::setLowerZone`](https://docs.juce.com/master/classMPEZoneLayout.html) defaults).
- The three expression dimensions on a member channel are **pitch bend** (X), **channel pressure** (Z)
  and **CC 74** (Y, "timbre"). Source: [JUCE `MPEInstrument`](https://docs.juce.com/master/classMPEInstrument.html).
- Messages on the **manager** channel apply to the whole zone (sustain pedal, a global bend,
  a controller's own knobs).
- Many controllers do not send an MCM at all and just expect the receiver to be in MPE mode (a fixed
  lower zone of 15 members). JUCE calls the non-MPE fallback "legacy mode". This is why detection needs
  a manual override (Open question 1).

---

## What v1 does today (verified in code)

- **No MPE awareness at all.** `midi-remote.md`'s Learn section used to claim that learn ignores
  "any message on a channel the profile marks as MPE member channels". That was wrong:
  `ControllerProfile` (`Source/MidiRemote/RemoteModel.h`) has no such field, and neither
  `detail::classifyMessage` (`RemoteEngine/RemoteEngineInternal.h`) nor `RemoteEngine::handleMessage`
  (`RemoteEngine/RemoteEngineDecode.cpp`) filters by channel. The sentence now points here.
- What *is* true: poly (per-note) aftertouch, sysex, clock and realtime bytes are ineligible, so they
  are dropped before learn or lookup. A note-off shares its note-on's `MessageSpec`, so during learn it
  only adds to the same tally.
- **The real hazard is the "any channel" fallback.** `RemoteMappingSnapshot::findSlot` tries the exact
  channel first, then channel 0 ("any"). An assignment such as "pitch bend, any channel → filter
  cutoff", or "CC 74, any channel → something", therefore matches every member channel's expression.
  Unless `passMapped` is on it **consumes** that expression, silently stripping it from the synth.
  During learn, the most frequent key in the 300 ms window wins. A pressure-heavy MPE performance
  therefore easily learns "channel pressure on channel 7", a channel that belongs to whichever finger
  landed there.
- **The voice path ignores channels.** `PolyMidiModule::processBlock` reads only note-on, note-off
  and all-notes-off. It has no pitch bend, no pressure, no CC, and does not look at the channel.
  It has a fixed 16 output channels: 8 pitch fans (in Hz) and 8 gate fans. Poly aftertouch and
  channel pressure fall on the floor.
- `ExternalMidiModule`'s `channel` parameter is 0 = all or a single channel 1–16. Set to one channel,
  it drops every other member channel on that path. The graph-input collector path has no filter.

---

## Design

### 1. Zone detection lives in the MIDI path, per source

A new `MpeZoneState` (plain memory) sits in each source's `LaneState` (`RemoteLaneState.h`), next to
the paired-CC and NRPN caches, and follows the same rules: written by that source's producer thread
only, and never read by the drain.

- It watches for **RPN 6** on channel 1 or 16. The RPN select pair CC 101 = 0 / CC 100 = 6, followed by
  data entry CC 6, updates that zone's member count. The existing NRPN state machine treats CC 101/100
  as "cancel NRPN". That stays, but an RPN select now also arms an RPN address, and CC 6 under
  RPN 6 or RPN 0 is routed to the zone state instead of being dropped as a plain CC. RPN 0 records the
  member-channel bend range for display only; the voice path gets it from its own copy, below.
- **The RPN bytes are never consumed.** They always continue to the graph, because the voice path
  needs to see them too.
- Detection publishes: the lane writes the new zone layout into a small `std::atomic<std::uint32_t>`
  (lower member count, upper member count, and a "detected" bit). The drain reads it on the message
  thread and republishes it to the panel. No FIFO event is needed, and a torn read is impossible
  because it is one word.
- The **per-profile override** (`ControllerProfile::mpe`, below) wins over detection. `off` means
  never MPE, even if an MCM arrives. `lower15` / `upper15` / `custom` force a layout. `auto` means
  detect.

### 2. Coexistence rule: member channels are the synth's, not MIDI Remote's

Once a source has an active zone (detected or forced):

1. **Member-channel pitch bend, channel pressure, CC 74 and notes never match a channel-0 ("any")
   slot.** `findSlot` gains a `memberChannelMask` argument. The snapshot stores each source's forced
   layout, and the lane state stores its detected one. When the incoming channel is a member channel,
   the fallback lookup is skipped. An *exact-channel* assignment on a member channel is still
   honoured: the user explicitly asked for it, and the panel warns (see UI below).
2. **Learn ignores member channels entirely.** No learn candidate is pushed. The status bar hint says
   *"MPE controller: move a knob or fader, not a key"* if the only traffic in the window was member
   traffic. This makes the old doc sentence true.
3. **The manager channel behaves exactly as today.** Its knobs, faders, sustain and global bend map,
   are consumed, and pass through according to the profile's rules.
4. **Unmapped traffic** (all member-channel traffic, and an unmapped manager channel) flows to the
   collector and `ExternalMidiModule` exactly as today. `MidiRecorder` records it unchanged, because
   it taps the collector-merged buffer ([`midi-input.md`](midi-input.md#recording-the-recorder-taps-one-of-these-two-paths-only)).

This is the only change MIDI Remote itself needs. Everything expressive happens downstream in the
graph, sample-accurately, which is where [`midi-remote.md`](midi-remote.md#how-does-a-hardware-value-reach-a-parameter)
already says sample-accurate control belongs.

### 3. The voice path: `PolyMidiModule` becomes MPE-aware

Per-note routing belongs in the graph, **not** in MIDI Remote's mapping path. MIDI Remote applies on
the message thread at 60 Hz with undo gestures. Per-note expression is audio-rate performance data
that must not be recorded as undo steps. It is note data, like velocity.

- **Channel ownership.** A voice remembers the channel its note-on arrived on. Pitch bend, channel
  pressure and CC 74 on a member channel update only the voice(s) holding that channel, at the event's
  sample position. `processBlock` already chunk-renders at every event, so this is the same loop with
  three more branches. Manager-channel pitch bend applies to every voice in its zone and adds to the
  per-note bend. Note-off matches on **(channel, note)**, not note alone, because two fingers can play
  the same note on different channels in MPE.
- **Pitch.** Per-note bend is folded into the existing Pitch fan (Hz), so **no new port** is needed
  and every existing patch glides. `freq = mtof(note + bend × range)`, where the range comes from the
  last RPN 0 seen on that channel (default 48 member / 2 manager). The existing 5 ms pitch smoother
  stays.
- **Pressure and Timbre are new output fans.** `Source/Modules/CLAUDE.md` fixes a module's channel
  count for its lifetime, so the module declares its **maximum up front**: 32 channels (8 pitch, 8 gate,
  8 pressure, 8 timbre). An "MPE" bool parameter (default off) varies only
  `getVisibleOutputPortCount()` from 1 to 3 visible jacks, and hidden channels are cleared every block.
  This follows the same visible-count pattern Macro bank and Audio Input use.
  - **Why not a new module:** a user who plugs in a Seaboard should not have to rebuild their patch
    around a different node, and the pitch path is shared anyway.
  - **Cost:** widening a module's declared shape changes its raw channel layout. The patch format
    stores cables by raw channel, pitch stays 0–7 and gate stays 8–15, so existing cables are
    untouched. The new fans take raw 16–31. `mapOutputChannel` gains the two new fans, and
    `getJackTargets` must not duplicate wires, as the Modules rule warns. Golden preset tests must stay
    byte-identical with MPE off (Tests section).
  - Pressure is 0..1 CV (channel pressure / 127). Timbre is 0..1 CV (CC 74 / 127); whether 0.5 means
    "centre" is up to the patch. At note-on, a voice's pressure resets to 0 and its timbre to 0.5, unless
    a value for that channel arrived just before the note-on. Controllers commonly send the initial
    CC 74 and pressure *before* the note-on, so the last per-channel value seen is latched onto the new
    voice. The exact spec wording on initial values was not read (the spec is members-only), so the
    first implementation ticket confirms it against real hardware.
- **Voice count.** Poly MIDI has 8 voices and an MPE zone usually has 15 member channels. Channel
  ownership is still per voice, and a 9th finger steals using the existing `voiceSteal` policy. Raising
  the voice count is a separate, larger change to every per-voice consumer and is not proposed here.
- **Not `juce::MPEInstrument`.** It is the obvious off-the-shelf tracker, but it holds a
  `CriticalSection` ([JUCE docs](https://docs.juce.com/master/classMPEInstrument.html)) and allocates
  listener callbacks. Inside `PolyMidiModule` the lock would be uncontended (audio thread only), but
  its note model (per-note `MPENote`, dimension tracking modes) is broader than 8 voices need. A
  ~100-line in-module tracker (per-channel bend/pressure/timbre arrays + voice→channel map) is smaller
  and trivially real-time safe. `MPEZoneLayout` (a value type with no lock) *is* reused for parsing
  RPN 6 / RPN 0 in both places.
- **Zone awareness in the module.** The module parses MCM and RPN 0 from its own MIDI buffer, because
  the RPNs pass through MIDI Remote untouched. It is therefore self-contained and works identically for
  timeline-played MPE clips, the plugin build, and live input. In "MPE off" the module ignores channels
  as it does today (legacy behaviour, byte-identical).

### 4. Plugin build

The host delivers MPE on the MIDI buffer handed to `processHostBlock`, and the "Host MIDI"
pseudo-controller ([`midi-remote.md`](midi-remote.md#the-plugin-build-vst3au-inside-a-host)) sees it.
Zone detection, the coexistence rule and the voice path all work unchanged.

**To verify at implementation:** `AgentSynthAudioProcessor` does not override
`juce::AudioProcessor::supportsMPE()` (`Source/Plugin/PluginProcessor.h` overrides only
`acceptsMidi`/`producesMidi`). Some hosts, and JUCE's VST3 wrapper for note expression, key off that
flag before sending per-note data. The first ticket checks Bitwig, Logic (AU, "MPE" track setting)
and Ableton Live 11+ with and without the override, and adds it if any host needs it.

### 5. Timeline

Recorded MPE takes keep their channels (`MidiRecorder` records the raw merged buffer). **To check at
implementation:** whether the piano roll and clip model preserve per-note channel and
pitch-bend/pressure/CC74 events on edit. A clip edit that drops channels would flatten an MPE take.
This is out of scope here beyond "do not destroy it". Editing per-note expression is its own feature.

---

## Data model changes

```text
ControllerProfile
  + mpe : { mode: auto | off | lower | upper | both, lowerMembers: 1..15, upperMembers: 1..15 }
          // absent = { mode: auto }; auto = use detected MCM, else no zone
```

- The profile `version` stays 1. The field is optional. `ControllerProfile::fromVar`
  (`RemoteModelJson.cpp`) reads named properties and checks `version == 1`, but it does not reject
  unknown keys. An older build therefore loads a profile carrying `mpe` and drops the field on its
  next save. That is acceptable for an optional override, so no version bump is needed. A malformed
  `mpe` value is rejected all-or-nothing, like any other field.
- **No project-document change.** Assignments are untouched, and the coexistence rule is derived from
  profile + detection at snapshot-build time.
- `RemoteMappingSnapshot::SourceEntry` gains `forcedMemberMask` (a 16-bit channel mask) and `mpeMode`.
  The MIDI path reads these; they are immutable once published.
- `PolyMidiModule` gains the `mpe` bool parameter (default false). It is a plain parameter, so it
  persists, undoes and is AI-visible like any other. `AIStateMapper::kMaxPortIndex` (64) already
  admits raw channels 16–31, but its comment ("poly buses top out at 16 — see PolyMidiModule") and the
  Poly MIDI entry in the AI module docs must be updated for the two new fans.

## Threading

- MIDI thread: zone parsing in `LaneState` (producer-only), a mask check in `findSlot` (no
  allocation), and one relaxed atomic word published for the panel. Nothing new crosses the tripwire in
  [`midi-remote.md`](midi-remote.md#threading-the-mapping-table-crosses-threads).
- Audio thread: `PolyMidiModule`'s tracker is fixed arrays, with no lock and no allocation.
- Message thread: the panel reads the published zone word in the drain and draws a small **MPE** badge
  on the controller in the Controllers list ("MPE lower zone, 15 channels").

## UI (small)

- Controllers-list right-click → **MPE…**: Auto (detected: lower 15) / Off / Lower / Upper / Both,
  with member counts.
- An exact-channel assignment on a member channel shows a warning in the Inspector: *"Channel 5 is an
  MPE finger channel on this controller — this mapping will steal expression from one note."*
- Poly MIDI card: the "MPE" toggle; when on, the Pressure and Timbre jacks appear.

---

## Tests

| File | Case | Verifies |
|---|---|---|
| `Tests/MidiRemote/RemoteEngineMpeTests.cpp` (new) | MCM on ch 1 with data 15 | lane detects lower zone 2–15; RPN bytes return `consumed=false` |
| same | "any-channel" pitch-bend assignment + MPE active | member-channel bend not consumed; manager-channel bend consumed |
| same | exact-channel assignment on a member channel | still honoured |
| same | learn armed, only member traffic | no learn candidate; timeout cancels |
| same | profile `mpe.mode = off` + MCM | no zone, v1 behaviour byte-identical |
| same | threading (TSan job) | concurrent MCM + mapped traffic on two sources |
| `Tests/MidiRemote/RemoteModelTests.cpp` | `mpe` round-trip + malformed values | all-or-nothing load |
| `Tests/Modules/PolyMidiMpeTests.cpp` (new) | two fingers, same note, different channels | independent voices, note-off by (channel, note) |
| same | member bend ±48 st | pitch fan Hz at the event sample |
| same | manager bend + member bend | additive |
| same | CC 74 before note-on | latched as the voice's initial timbre |
| same | MPE off | output byte-identical to today (golden) |
| existing preset golden tests | unchanged | the widened declared shape does not move any saved cable |

## Phased plan (ticket-sized)

1. **Fix the doc and add the override field.** `ControllerProfile::mpe` + JSON + the Controllers-list
   MPE menu, and `findSlot`'s member-mask skip driven by the *forced* layout only. Small, and it already
   stops the "any channel steals expression" bug for users who tick it.
2. **Detection.** RPN 6 / RPN 0 parsing in `LaneState`, the published zone word, auto mode, the panel
   badge, and learn ignoring member channels.
3. **Poly MIDI per-note pitch.** Channel ownership, (channel, note) note-off, and bend folded into
   Pitch behind the `mpe` parameter. There is no new port yet, so no port-map risk.
4. **Poly MIDI Pressure and Timbre fans.** The declared 32-channel shape, visible-count variation, the
   port map, and AI port-map updates, plus the golden-preset guard.
5. **Plugin verification.** The `supportsMPE()` host matrix and the fix if needed.
6. **(Later, separate)** Piano-roll preservation or editing of per-note expression.

## Open questions for the founder

1. **Auto-detect or explicit toggle by default?** Many controllers never send an MCM.
   **Recommended:** `auto` (use the MCM when seen) plus a one-click override in the Controllers menu.
   Do not guess MPE from traffic shape.
2. **Extend Poly MIDI or add a separate "MPE MIDI" module?** **Recommended:** extend Poly MIDI behind
   an "MPE" toggle (off by default), so existing patches keep working when the user plugs in an MPE
   controller.
3. **8 voices for a 15-channel zone: acceptable for now?** **Recommended:** yes. Voice stealing covers
   the rare 9th finger, and a voice-count increase is its own project.
4. **Should an exact-channel mapping on a member channel be allowed at all?** **Recommended:** allow
   it with a warning, never silently block something the user explicitly learned or typed.
5. **Should MPE traffic record into timeline clips before the piano roll can edit it?**
   **Recommended:** yes, record it raw; losing a performance is worse than not being able to edit its
   expression yet.
