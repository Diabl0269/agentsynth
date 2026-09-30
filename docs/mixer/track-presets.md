# Track Presets

A saveable, reusable channel: what a track preset holds, where it is saved from, how a new track
starts from one, and the keys that must never survive a round trip through disk. What a channel is
lives in [`docs/mixer/mixer.md`](mixer.md).

---

## What a track preset is

**"Channel template" and "track preset" are one concept: the track preset.** A track preset is
`{ track kind, channel chain, strip settings, optional clips }`. Every track kind — **Audio**, and
**Instrument** (which also covers a MIDI track that alone drives an instrument, per the link rule in
[`docs/mixer/mixer.md`](mixer.md#auto-creating-a-channel-on-connect)) — has its own default preset.

`synth::TrackPresetManager` (`Source/Mixer/TrackPresetManager.h`) is a thin wrapper over
`SnippetManager::extractSnippet`/`insertSnippet`, adding only what a track preset needs beyond a
plain snippet: the outside-modulator walk below, and the key scrub below that.

## A third kind: Bus

**FRO297: a bus can be saved and re-added as a preset too**, alongside Audio and Instrument.
`TrackPresetKind::Bus` is stored at the same `"trackPresetKind"` preset-level field the other two
use ("bus" on disk) — there is no separate storage mechanism for it.

A bus preset's own macro is its ENTIRE identity: unlike Audio/Instrument, a Bus preset creates **no
timeline track at all** on insert — a bus has none — just the bus chain, which the mixer shows as a
BUS column exactly like a freshly built one. Two consequences follow:

- **Saving one has no track header to reach it from.** A bus's own channel macro right-click menu
  ("Save Track as Preset...", gated on `synth::isChannelMacro` same as any channel) is the only save
  path — there is no bus equivalent of the track header's own menu.
- **A bus has no per-type Preferences -> Mixer default.** "Set as Default Track Preset" is omitted
  entirely from a bus macro's own menu (`synth::isBusMacro`, same "omit, don't disable" precedent
  the header's own gates use) — a bus preset can still be saved and inserted at any time, it just
  never becomes what a plain "+ Bus" builds.

**Inserting one re-flags `"isBus"` the same way "Add bus" does, AFTER the scrub-respecting insert
runs.** The scrub itself is unchanged (see below): a Bus preset's saved JSON carries no `"isBus"` any
more than an Audio or Instrument one does, so `TrackPresetManager::insertTrackPreset` alone never
produces a bus-flagged strip. `MainComponent::insertBusFromPresetVar` calls
`ChannelStripModule::setIsBus(true)` on the freshly inserted strip immediately afterward — the exact
mechanism `synth::buildBusChannel` uses for a strip built from scratch — so the scrub's own hazard
(an imported `isBus=true` badging an ordinary track channel) never reopens: a Bus-kind preset is the
only kind whose insert path ever calls `setIsBus(true)` at all.

**Naming**: the inserted macro takes whatever name its own capture already carries (a user-renamed
source bus keeps that name). **A captured name that comes back empty, or that another macro in the
project already carries, falls back to the numbered `synth::busFallbackName` default ("Bus N")**, the
same name "Add bus" would give a fresh one, so inserting a preset saved from "Bus 1" next to "Bus 1"
never shows two columns with the same name.

`"+ Track"`'s own **Bus** submenu ([`docs/timeline/add-track.md`](../timeline/add-track.md#track-presets))
lists every saved Bus preset the same way the Audio/Instrument ones are listed, and **"Insert Track
Preset from File..."** dispatches to the same no-timeline-track insert path when the file's own
`"trackPresetKind"` reads `"bus"`.

## Why the product says track and not lane

The product says **track** everywhere, in the UI and in these docs. "Lane" stays reserved for what it
already means in this codebase: the clip and automation lanes *inside* a track
([`docs/timeline/clips.md`](../timeline/clips.md), [`docs/timeline/automation.md`](../timeline/automation.md)).

## Saving and setting a default

**"Save Track as Preset..."** and **"Set as Default Track Preset"** are reachable from the track
header's right-click menu and from the channel macro's own menu, both gated on
`synth::isChannelMacro` — **omitted entirely, not merely disabled, for an ordinary non-channel
macro**, the same as the header's own disabled-not-hidden gate when the track has no channel yet.
Preferences -> Mixer lists each type's current default in a dropdown.

**Saving a preset never changes a default** unless the user also picks "Set as default".

The two per-type defaults are read from Preferences -> Mixer's `mixerDefaultTrackPresetAudio` and
`mixerDefaultTrackPresetInstrument` settings keys and are **consulted by "+ Track -> Audio Track" and
"+ Track -> Instrument Track" before the factory chain is built**
([`docs/mixer/mixer.md`](mixer.md#the-factory-default-chain)).

## Creating a track from a preset

Defaults only decide what a plain `+ Track -> Audio` or `+ Track -> Instrument` creates. **Any saved
preset can be inserted at any time regardless of the defaults:** `+ Track` lists every saved preset
grouped by type as its own submenu entries, and **"Insert Track Preset from File..."** loads one saved
anywhere on disk (`TimelinePanelTrackHeaders.cpp`).

**Both resolve against a menu-open-time snapshot**
(`audioTrackPresetMenuSnapshot_`/`instrumentTrackPresetMenuSnapshot_`), never a live re-query — the
same reason the plugin list submenu already uses one: a preset can be saved or deleted between the
menu opening and the click landing.

## What a saved preset carries beyond the box

**Saving or exporting a track's channel also captures every module OUTSIDE the channel's macro that
feeds it through a port.** A shared LFO is the running example
([`docs/mixer/mixer.md`](mixer.md#make-channel-and-shared-modules)).

`extractTrackPreset`'s walk (`collectOutsideModulatorsForTrackPreset`) goes **upstream transitively
through modulation, CV and MIDI inputs from the macro's ports, and stops the moment it reaches another
channel's strip** — a shared module that itself terminates in a different channel is that channel's
business, not this preset's.

**On import or load, everything gathered this way arrives as fresh copies** placed beside the track's
box, wired to the same ports the original was — so the imported track sounds identical, with no
dangling port left unfed and no cross-project aliasing of an original module.

A channel macro's child macros (nested macros) travel with the preset: extraction walks the channel
macro's own members plus every descendant's (`MacroSet::descendantMembers`), the outside-modulator
walk counts a child's modules as inside the channel, and applying the preset recreates the child
macros with their parent links.

## Loading a preset

**The same trusted and untrusted pairing every disk-sourced patch data uses.**
`insertTrackPreset` is `SnippetManager::insertSnippet` with `trustedPayload=false`, so
`validatePatch(..., trusted=false)` still runs as a separate gate on every disk-sourced preset before
a trusted `applyJSONToGraph` — the `SnippetManager::insertSnippet` / `ProjectBundle::load` reference
pairing the root `CLAUDE.md` names.

**`"timeline"` stays a reserved field, refused on the untrusted path.** `prepareForInsert` only ever
copies `"nodes"`, `"connections"` and `"macros"`, so a smuggled `"timeline"` key has no effect at all.
A malformed preset is refused whole, never partially applied. Two inserts of the same preset never
collide.

## Scrubbed keys

**`extractTrackPreset` scrubs `"solo"`, `"isBus"` and `"sends"` from the captured Channel Strip's
extra state before the preset is ever written to disk.** Each one is a real hazard, not hygiene:

- an imported `soloed_=true` would **silence the whole mix render-wide** the moment the file loads
  (`Source/CLAUDE.md` invariant);
- an imported `isBus=true` would **badge an ordinary track channel as BUS** wherever the preset is
  inserted;
- captured `"sends"` slot state would restore **with no re-resolved cable target**, because a send's
  target is a graph edge and is never stored
  ([`docs/mixer/sends-and-buses.md`](sends-and-buses.md#what-is-a-parameter-and-what-is-state)), so it
  would show "No target" rows. Re-resolving a captured slot's target by name on insert is out of
  scope.

## Related

- [`docs/mixer/mixer.md`](mixer.md) — what a channel is, the link rule, the factory default chain.
- [`docs/mixer/panel.md`](panel.md) — the Preferences -> Mixer surface these defaults live on.
- [`docs/mixer/sends-and-buses.md`](sends-and-buses.md) — why `"sends"` and `"isBus"` are scrubbed.
- [`docs/layout/snippets-clipboard.md`](../layout/snippets-clipboard.md) — `SnippetManager`, and the
  trusted-versus-untrusted apply rule.
- [`docs/timeline/add-track.md`](../timeline/add-track.md) — the "+ Track" menu that lists presets.
- [`docs/ai/patch-safety.md`](../ai/patch-safety.md) — `validatePatch` and the untrusted path.
