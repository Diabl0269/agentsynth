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
  (root `CLAUDE.md` tripwire);
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
