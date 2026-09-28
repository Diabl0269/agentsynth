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
handshake : { open: byte[], close: byte[], port: string }   // all default empty == "no handshake"
```

`open`/`close` are raw MIDI bytes (a `juce::MidiMessage` constructed straight from them — SysEx or
a short message, whichever the device wants), sent once when the app starts treating this
profile's device as open, and once when it stops. Empty (the default on all three fields) means no
handshake at all; written to JSON only when non-empty, so a pre-FRO339 profile round-trips
byte-identical (same convention as `Control::focusBank`). `applyControllerTemplate`
(`ControllerTemplates.cpp`) copies a template's handshake into the merged profile only when the
profile doesn't already have one — there is only one per profile, so "existing wins" here means
the whole handshake stays or goes, not a per-byte dedup like controls/actions.

**Where the handshake is actually sent, and why it isn't a second full port field:** a handshake's
target and a profile's control-surface `input` are conceptually different things, but
`ControllerProfile` has exactly one `input` and no per-control port override. The handshake is sent
to whichever MIDI **output** `resolveHandshakeOutput()` (`Source/MidiRemote/ControllerHandshake.h`)
resolves for the profile's own `input` — see the next section for the exact rule. `port` is not a
second device-selecting field; it's a small **hint** (a word like `"DAW"`) resolution and the UI use
to bridge a device whose input/output ports are named differently from each other, and to warn the
user when they picked the wrong one — see "The Launch Control XL 3's own ports" below for the
concrete, now-confirmed case this shipped for.

## `resolveHandshakeOutput()` — matching a profile's `input` to a MIDI output

CoreMIDI (and other backends) can give the two directions of the SAME physical port on a device
different names entirely — an `input` device (what a profile's `input` field names) is not
guaranteed to share an identifier, or even a name, with any `juce::MidiOutput`. Sending "to whatever
matches the input" therefore only ever worked by accident, for a device whose in/out ports happen to
be named identically. `resolveHandshakeOutput(input, portHint, availableOutputs)` tries, in order:

1. **Exact identifier match** — `input.identifier` equals some output's identifier. Never touched by
   `portHint` (an identifier carries no port wording of its own).
2. **Exact name match** — `input.name` equals some output's name (the simple-device case above).
3. **The input's name with a trailing "Out" swapped to "In"** (case-sensitive whole word, e.g.
   `"LCXL3 1 DAW Out"` → `"LCXL3 1 DAW In"`) — the asymmetric-port case.
4. **None** — the coordinator skips sending and reports an issue (see below).

If `handshake.port` is set, step 2 and step 3 run against the input's name **after**
`applyHandshakePortHint()` retargets it — it replaces the token right before the name's trailing
`"In"`/`"Out"` word with the hint, e.g. `"LCXL3 1 MIDI Out"` + hint `"DAW"` → `"LCXL3 1 DAW Out"` —
so a profile still pointed at the wrong sibling port still resolves the right output.

## Visible feedback when it can't resolve, or the wrong port was picked

`describeHandshakeIssue(input, portHint, resolved)` words the ONE status line the panel shows for a
profile, checked in this order:

1. `portHint` is set and `input.name` doesn't already contain it — the profile's `input` is the
   WRONG sibling port. This fires even when `resolved` is true (the hint swap still found an output
   for the handshake itself), because every OTHER message on the right port — Play/Record, encoders,
   whatever the template maps — never reaches the app while `input` points at the wrong one.
   Wording: `This template needs the device's <hint> port: pick "<hinted input name>" as the input.`
2. `resolved` is false — no output matched at all. Wording: `This controller's handshake couldn't
   find a matching MIDI output for "<input.name>".`

`ControllerHandshakeCoordinator::getHandshakeIssue(profileId)` exposes the current issue (or `""`);
`MidiLearnController::getHandshakeIssueForProfile()` is the app-layer forwarder
`MidiRemotePanelComponent::refreshPortHint()` (`Source/UI/MidiRemote/MidiRemotePanel/
MidiRemotePanelHandshake.cpp`) calls after every rebuild/selection change, showing it as its own
warning row under the Surface toolbar's button row (`ControllerSurfaceToolbar::setPortHint()`) for
whichever controller is currently selected. A mismatch is also logged (`DBG`) the moment it first
appears, so it shows up in a debug build's console even with the panel closed.

The **Add-controller popover** (`Source/UI/MidiRemote/AddController/AddControllerPopover.cpp`)
avoids the mismatch at the source: choosing a template whose `handshake.port` is set
(`TemplateInfo::handshakePort`, mirrored from the loaded template by `listControllerTemplates()`)
preselects the first free device row whose name already contains that word — e.g. picking
**Template: Launch Control XL 3** preselects `"LCXL3 1 DAW Out"` over `"LCXL3 1 MIDI Out"` — through
the same path a manual device pick uses, so the name field re-prefills too.

## `ControllerHandshakeCoordinator` (`Source/MidiRemote/ControllerHandshake.h`)

Core, not app layer — it only depends on `RemoteFeedbackSink` (the same interface
`MidiRemoteFeedbackOutputs` already implements for FRO139 controller feedback) and
`RemoteModel.h`, so it's headless-testable with a fake sink and needs no `juce_audio_devices` of
its own (`resolveHandshakeOutput()`/`describeHandshakeIssue()` are likewise pure, free functions in
the same header). It tracks, per profile id, whether that profile's `open` bytes have been sent:

- `reconcile(profiles, openSourceKeys, availableOutputs)` — sends `open` for every profile with a
  non-empty handshake whose `input.identifier` is in `openSourceKeys` and isn't already tracked as
  open, to `resolveHandshakeOutput(profile.input, profile.handshake.port, availableOutputs)`'s
  result — never to `profile.input` itself (idempotent: the device latches, so re-sending on an
  unrelated reconcile is pointless, not just wasteful). A profile whose output can't be resolved is
  skipped, not asserted. Sends `close` for anything tracked as open that no longer qualifies —
  profile removed, handshake cleared, or its device no longer open — to the SAME output it opened on
  (never re-resolved at close time: the device may already be gone), using the LAST state seen for
  it (a removed profile is no longer in `profiles` to re-derive the bytes from). Also recomputes
  every open profile's `getHandshakeIssue()` on every call, independent of the open/already-sent
  bookkeeping, so a still-mismatched port keeps reporting even once the device has latched.
- `shutdownAll()` — sends `close` for everything still open, then forgets it all. App-quit only.
- `getHandshakeIssue(profileId)` — see above.

`availableOutputs` (every MIDI output the app can currently see) is real `juce::MidiOutput`
enumeration in production (`MidiLearnControllerHandshake.cpp`) — an app-layer concern, same as
`MidiRemoteFeedbackOutputs`'s own opener — with a test seam
(`MidiLearnController::setAvailableOutputsQueryForTest`) exactly like
`ControllersListComponent::queryFeedbackOutputs`, since a headless test process crashes calling that
enumeration directly (no CoreMIDI entitlement/bundle).

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

## The Launch Control XL 3's own ports

The Launch Control XL 3 exposes two USB MIDI port pairs — "MIDI In/Out" (Custom Modes, p.5) and
"DAW In/Out" (DAW mode's transport/feature controls, p.8). **Confirmed on hardware 2026-09-28**
(a connected Launch Control XL 3): in DAW mode, Play/Record and every encoder/fader all arrive on
the **DAW Out** source with exactly Mode 16's CC numbers (Play = CC116 ch1, Record = CC118 ch1,
fader 1 = CC5 ch16, an encoder = CC29 ch16, ...), nothing on **MIDI Out** — so the template's
Mode-16 numbers were right, and a Custom Mode's encoders/faders DO move onto the DAW port once DAW
mode is enabled, matching Novation's own "DAW Control Mode" (p.10) and Ableton's own script.

The same session found CoreMIDI naming the device's four ports asymmetrically: the **sources**
(this app's `input` choices) are `"LCXL3 1 MIDI Out"` / `"LCXL3 1 DAW Out"`; the **destinations**
(what a handshake actually writes to) are `"LCXL3 1 MIDI In"` / `"LCXL3 1 DAW In"` / two
`"LCXL3 1 To DIN Out"` ports — no destination shares either source's exact name. Sending
`F0 00 20 29 02 15 02 7F F7` to `"LCXL3 1 DAW In"` puts the device into DAW mode; it echoes the
same bytes back on `"DAW Out"`. This is exactly the case `resolveHandshakeOutput()`'s trailing
`"Out"` → `"In"` step exists for, and exactly why this template sets `handshake.port` to `"DAW"` —
so a user who picks `"LCXL3 1 MIDI Out"` as the input (the popover no longer preselects the wrong
one, but a hand-edited or imported profile still could) gets a visible warning instead of a
controller that silently never leaves standalone mode.

## Related

- [`midi-remote.md`](midi-remote.md#data-model) — the full data model `handshake` sits in, and
  [Controller feedback](midi-remote.md#controller-feedback) (FRO139) for the sibling FRO that
  established `RemoteFeedbackSink`/`MidiRemoteFeedbackOutputs`, reused here rather than adding a
  second `juce::MidiOutput`-opening path.
- [`midi-remote-ui.md`](midi-remote-ui.md#templates-and-importexport) — how a template's
  handshake reaches a real profile, and the Launch Control XL 3's specific template.

The template binds **Play** to `transportTogglePlayStop` (press once to start, again to stop, like the app's own Play button) and **Record** to `transportRecord`.
