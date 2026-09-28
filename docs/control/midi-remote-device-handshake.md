# Device handshake

FRO339. A profile switching its own controller into "remote" mode. Some controllers ship two
operating modes: a standalone/MIDI mode where most of the
surface is inert (or fixed to a factory layout), and a "DAW mode" that only activates once a host
tells the device to enter it — usually one SysEx message on connect and its inverse on
disconnect. The Novation Launch Control XL 3 is the concrete case this shipped for: its Page,
Track, Record, Play, Solo/Arm and Mute/Select buttons send nothing at all in standalone mode
(Launch Control XL 3 Programmer's Reference Guide, p.7), and only work after the app sends the
DAW-mode enable SysEx (p.8) — exactly what Ableton Live 12's own Launch Control XL 3 script does
on connect, sending the disable SysEx back on unload/quit/unplug.

## Decision

A `ControllerProfile` may carry an optional `handshake` (`RemoteModel.h`):

```text
handshake : { open: byte[], close: byte[] }   // both default empty == "no handshake"
```

`open`/`close` are raw MIDI bytes (a `juce::MidiMessage` constructed straight from them — SysEx or
a short message, whichever the device wants), sent once when the app starts treating this
profile's device as open, and once when it stops. Empty (the default on both fields) means no
handshake at all; written to JSON only when non-empty, so a pre-FRO339 profile round-trips
byte-identical (same convention as `Control::focusBank`). `applyControllerTemplate`
(`ControllerTemplates.cpp`) copies a template's handshake into the merged profile only when the
profile doesn't already have one — there is only one per profile, so "existing wins" here means
the whole handshake stays or goes, not a per-byte dedup like controls/actions.

**Why not a second port field instead of overloading `input`:** a handshake's target port and a
profile's control-surface `input` are conceptually different things, but `ControllerProfile` has
exactly one `input` and no per-control port override (see the Launch Control XL 3 caveat below for
why that matters for THIS device specifically). Rather than growing the schema with a
vendor-specific "handshake port" field before a second real device needs one, the handshake is
sent to **the MIDI output whose identifier/name matches the profile's own `input`** (identifier
first, name fallback — same convention `output` already uses for controller feedback). A template
author who needs the handshake on a different physical port than the rest of the surface points
the whole profile's `input` at that port instead (see the Launch Control XL 3 template's own
`"source"` note for exactly this trade-off).

## `ControllerHandshakeCoordinator` (`Source/MidiRemote/ControllerHandshake.h`)

Core, not app layer — it only depends on `RemoteFeedbackSink` (the same interface
`MidiRemoteFeedbackOutputs` already implements for FRO139 controller feedback) and
`RemoteModel.h`, so it's headless-testable with a fake sink and needs no `juce_audio_devices` of
its own. It tracks, per profile id, whether that profile's `open` bytes have been sent:

- `reconcile(profiles, openSourceKeys)` — sends `open` for every profile with a non-empty
  handshake whose `input.identifier` is in `openSourceKeys` and isn't already tracked as open
  (idempotent: the device latches, so re-sending on an unrelated reconcile is pointless, not just
  wasteful). Sends `close` for anything tracked as open that no longer qualifies — profile
  removed, handshake cleared, or its device no longer open — using the LAST state seen for it
  (a removed profile is no longer in `profiles` to re-derive the bytes from).
- `shutdownAll()` — sends `close` for everything still open, then forgets it all. App-quit only.

**Threading:** message thread only, exactly like `RemoteEngine::drain()`'s own feedback send
(`Source/CLAUDE.md`'s MIDI Remote threading rule) — never called from a MIDI/audio thread.

## Lifecycle wiring (`MidiLearnController`)

`MidiLearnController` owns a `std::optional<ControllerHandshakeCoordinator>`
(`MidiLearnControllerHandshake.cpp`), empty until `setHandshakeFeedbackSink()` runs — called once
from `MainComponent::wireMidiRemoteEngine()`, guarded by the same `!audioEngine.isHosted()` check
`remoteEngine.setFeedbackSink()` already uses (`HostMode::Hosted` never opens hardware MIDI, so
there is nothing to send a handshake to; every coordinator method is then simply unset and every
call above it a no-op).

Two call sites reconcile it, covering every way a profile or the open-device set can change:

- **`refreshSources()`** — a device opening or closing (replug, an Audio-tab MIDI-input tick, app
  startup) needs the same reconcile as a profile change, since `reconcile()`'s "is this profile's
  device open" check depends on it.
- **`setProfilesAndReconcileHandshakes()`** — the shared tail every `profiles_`-mutating method
  (`updateProfile`, `addProfile`, `deleteProfile`, template application, undo/redo, ...) now calls
  instead of `remoteEngine_.setProfiles(profiles_)` directly, so a profile that just gained a
  handshake (or lost its device) is reconciled immediately rather than waiting for an unrelated
  device-list change.

`MainComponent`'s destructor calls `midiLearnController_.shutdownHandshakes()` as its very first
statement — before anything else tears down — so `close` reaches the device while
`remoteFeedbackOutputs_` and every MIDI output are still fully alive. **A crash never runs this**:
if the app dies without reaching its destructor (or the process is killed), the device stays in
DAW mode until another host disables it or it is power-cycled — the same best-effort-only
guarantee Ableton's own script gives, and the reason this is a "goodbye", not a keep-alive.

## The Launch Control XL 3's own port question

The Launch Control XL 3 exposes two USB MIDI port pairs — "MIDI In/Out" (Custom Modes, p.5) and
"DAW In/Out" (DAW mode's transport/feature controls, p.8) — and the guide is genuinely ambiguous
about whether a Custom Mode's encoders/faders ALSO move onto the DAW port once DAW mode is
enabled, or stay on the MIDI port regardless of which "surface mode" is selected (p.9's own
wording admits both readings; see the template's `"source"` field for the specific sentences).
Since a profile has only one `input`, this template assumes they move together — pick the Launch
Control XL 3's **DAW** port, not its MIDI port, as this controller's `input` — matching how
Novation's own "DAW Control Mode" mirrors a Custom Mode's default layout over that port for
exactly this kind of generic DAW integration, and matching Ableton's own script. **This has not
been confirmed against real hardware** — see docs/control/midi-remote.md's own testing story, or
the FRO339 report, for what still needs a real device.

## Related

- [`midi-remote.md`](midi-remote.md#data-model) — the full data model `handshake` sits in, and
  [Controller feedback](midi-remote.md#controller-feedback) (FRO139) for the sibling FRO that
  established `RemoteFeedbackSink`/`MidiRemoteFeedbackOutputs`, reused here rather than adding a
  second `juce::MidiOutput`-opening path.
- [`midi-remote-ui.md`](midi-remote-ui.md#templates-and-importexport) — how a template's
  handshake reaches a real profile, and the Launch Control XL 3's specific template.
