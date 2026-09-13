# Mixer

**Status: DECIDED 2026-09-10 (founder sign-off on D1-D4). P9-2 implemented, P9-3's audio-track,
instrument-track, MIDI-track-auto-channel and existing-project "Create channels" flows implemented
(T173a, T183, T184, T173e/FRO26), the Oscillator/Wavetable instrument track now gets an envelope +
VCA ahead of the chain (P9-3i, FRO43), and a poly Oscillator/Wavetable instrument track gets a TRUE
per-voice envelope instead of a shared mono one (P9-3j, FRO46)** — the
`ChannelStrip` and `Master` nodes, the engine-owned solo gate and the Master splice exist (engine
only; §8 item 1 records how), "+ Track -> Audio Track" builds the factory default channel end to
end (§8 item 2), "+ Track -> Instrument" does the same ahead of a chosen instrument (§8 item 2,
T183) — see §5.2's table note for why that one stays a `TrackKind::Midi` track rather than the "(new
track kind)" this doc originally called for — connecting a MIDI track's cable to an unchanneled
instrument auto-creates a channel at the point the audio reaches the output (§8 item 2, T184; §5.2's
"main workflow" paragraph), and the "+ Track" menu's "Create Channels" entry sweeps every
channel-less track in the open project into its own channel in one shot (§5.13, §8 item 2, FRO26).
"Make channel" (a single track/instrument, on demand) is still a follow-up, and there is no mixer
panel yet (P9-5). This document
records the decided design;
§8 is the implementation order that turns it into code. The visual
proposal that led to this decision (canvas diagram, mixer panel mock, the four decision cards)
lived in a separate page shown to the founder, not in this repo.

---

## 1. What exists today

Timeline tracks already own **internal-only** source nodes: `TimelineMidiSource` ("Track In") and
`TimelineAudioSource` ("Track Audio") — `ModuleType` entries in `Source/Modules/ModuleBase.h`,
created by the timeline-track flow rather than the module library, with the same three exclusions
every internal-only node gets (no library row, no replace-menu entry, never model-authorable).

Track mute/solo is **source gating**, not mixer gating. Both source modules compute the identical
condition — `track->muted || (snapshot->anySoloed && !track->soloed)` — and suppress themselves
when it's true: `Source/Modules/TimelineMidiSourceModule.h` line 235, `Source/Modules/
TimelineAudioSourceModule.h` line 108. `TimelineSnapshot::anySoloed`
(`Source/Timeline/TimelineSnapshot.h`) is computed once per snapshot and published to the audio
thread via `EpochExchange`, alongside every other timeline field.

**Consequence:** solo stops notes/clips at the source. Anything downstream of the source that is
*shared* with other tracks, or that has a tail (reverb, delay), is not isolated by today's solo —
the signal it already emitted keeps ringing, and a shared downstream processor still hears
whatever other unsoloed tracks fed it before playback stopped. This is the same limitation stem
export (P9-8) already has to design around: there is no point in the graph, today, where "just
this track's contribution" is actually isolated.

The graph has exactly one **Audio Output** node, a `juce::AudioGraphIOProcessor` that cannot host
DSP of its own. `MainComponent::ensureMasterRecordTap()` (`Source/MainComponent.cpp`) already
splices a singleton `Rec Tap` node in front of it on demand, re-routing every audio connection
that fed the output through the tap, as one compound undo step (`docs/architecture.md`). This is
the closest existing precedent for what a `Master` node splice would look like (§5.1).

Macros (`docs/macros.md`) are a **presentation-only layer over a flat graph**: a macro adds no
processing of its own, and its ports are proxy nodes (`MacroInlet`/`MacroOutlet`/
`MacroMidiInlet`/`MacroMidiOutlet`) — themselves internal-only, non-authorable, excluded the same
three ways.

Relevant existing modules: `VoiceMixerModule` (sums a poly chain to one signal),
`ParametricEQModule`, `CompressorModule`, `LimiterModule` (all under `Source/Modules/FX/`). There
is **no Gate module** — see §7 (D3) / §8 (P9-11).

---

## 2. The problem

The graph is free-form: any node can wire to any other, and nothing about a node's position or
type says "this is a channel." Unlike a DAW with one fixed channel per track, AgentSynth's mixer
has to answer, from an arbitrary tangle of nodes, where a channel *begins and ends*. The mapping
between timeline tracks and instruments is also many-to-many — several MIDI tracks can drive one
sampler, one track can layer two synths — so "one channel per track" does not hold either.

---

## 3. Candidates

### Candidate A — the macro is the channel

Treat an existing macro as the channel unit directly: no new node type, reuse the box that P8-12
already ships.

**Where it breaks:** a macro and a channel would then have two-part identity that can disagree.
Ungrouping (per `docs/macros.md` fix G7) removes a macro's *ports* but never its ordinary members
— so ungrouping a channel macro would leave the fader/pan/solo controls with no box around them,
still alive, still processing, just no longer presented as a channel. A box with no strip is just
a macro; a strip with no box is still a channel. Tying channel identity to a single node avoids
the split.

### Candidate B — a hidden per-track sub-graph

Give every track its own nested `juce::AudioProcessorGraph`, DAW-style.

**Where it breaks:** this is exactly Candidate A from the macro I/O design (`docs/macros.md` §3),
already rejected there, for the same two reasons: it moves member nodes out of uuid-addressable
space (orphaning timeline bindings and automation lanes that point at them), and it opens a second,
nested latency-compensation domain on top of the one hosted plugins already strain.

### Candidate C — anything wired to Audio Output

Define a channel implicitly as any node with a cable reaching the output.

**Where it breaks:** there's no fixed location to put a fader, mute or solo control — a signal
"wired to the output" is just a cable, not a node with state. Worse, once two such signals have
summed together upstream of the output, solo can no longer separate them; there's nothing left to
silence independently.

### Candidate D (chosen) — the `ChannelStrip` node is the channel

A new node type sits at the end of a signal chain and *is* the channel. A macro remains the
optional box drawn around the chain that feeds it — useful for organization, but not what defines
channel identity.

---

## 4. Decision

> **The `ChannelStrip` node is the channel; a macro is the box you keep it in.**

The mixer panel enumerates `ChannelStrip` nodes in the live graph, plus a synthetic **Direct**
column (§5.1) and **Master**. Nothing else is a channel.

---

## 5. The design

### 5.1 Node types

Two additions to `ModuleType`: **`ChannelStrip`** and **`Master`**. Both are internal-only, with
the same three exclusions as `TimelineMidiSource` / `Rec Tap` / the macro port types: no library
row, no replace-menu entry, never model-authorable (`kNonAuthorableModuleTypes`,
`Source/AI/AIStateMapper.cpp`). Adding them to the factory without adding them to that set will
fail `AIStateMapperTest.AuthorableModuleTypesGolden` — that failure is intended and is how the
golden test is meant to catch this exact addition (see `docs/macros.md` §6 for the identical
pattern with the macro port types).

`Master` is spliced in front of Audio Output **when the first channel is created** — a user
action, following `ensureMasterRecordTap()`'s existing pattern: singleton by construction, one
compound undo step, every audio connection that fed the output re-routed through it. Ordering
becomes:

```
... channel strips ... -> Master -> Rec Tap -> Audio Output
```

`Master` exposes a **Direct** input block that receives whatever cables previously went straight
to the output — the same re-routing `ensureMasterRecordTap()` already does for the record tap,
one level upstream of it.

### 5.2 Channels follow audio, not tracks

| Track kind | Own channel? | What gets created |
| --- | --- | --- |
| Audio track | Yes, automatic | Track Audio -> default modules -> strip, boxed as one channel when the track is added. |
| Instrument track | Yes, automatic | Track In -> the chosen instrument -> default modules -> strip, in one step. |
| MIDI track into an existing instrument | No | Nothing new is created. Its notes play into that instrument's channel; the strip's UI lists the track under its "tracks" line. |

Audio and instrument tracks each automatically get a channel; a MIDI track routed to an instrument
another track already drives does not get a second one — it feeds the existing channel. Splitting
a shared sampler three ways (or triple-counting its CPU/sound) if every track insisted on its own
channel is the failure this avoids; Cubase draws the same line (MIDI tracks carry no audio channel
of their own, instrument tracks do).

**T183 implemented "Instrument track" as `TrackKind::Midi`, not a new kind.** This table originally
called it "(new track kind)" — a `TrackKind::Instrument` enum value plus format/serialization and
kind-badge UI changes. T183's own scope never asked for that, and an instrument track today is
exactly what this row above ("MIDI track into an existing instrument") already describes once the
user draws the cable by hand: a Track In feeding exactly one instrument. "+ Track -> Instrument"
just draws that cable and builds the channel automatically instead of making the user do both steps
— see `MainComponent::addInstrumentTrack`'s own comment. A future `TrackKind::Instrument` would
need to migrate every track this flow has already created.

**The main workflow: connecting a MIDI track to an unchanneled instrument — DONE (T184).** A
track's own mute/solo controls only mean anything once a channel exists for them to drive, so the
common path — add a MIDI track, wire it to a macro or a bare instrument whose audio has never
reached a channel — has to *create* the channel, not just wait for the user to run "Make channel"
separately. When a connection like that makes some audio newly reach a `ChannelStrip`-less path to
Audio Output, a channel is **auto-created at the point that audio reaches the output**: one undo
step, gated by a Preferences toggle (`mixerAutoCreateChannelOnConnect`) that defaults to **ON**.
Turning the toggle off restores today's behaviour (wire freely, no channel appears until asked
for). §8 item 2's own T184 bullet has the implementation detail.

**The link rule.** A track and a channel are **linked** exactly when the track is the channel's
**only** source — an audio track, an instrument track, or a MIDI track that alone drives an
instrument. Linked means:

- **(a) Names sync both ways.** Renaming the track renames the channel (strip + macro), and vice
  versa.
- **(b) Colour syncs live.** The track colour, the channel's macro colour and the mixer column
  colour always match, and they update **while the colour picker is still being dragged**, not
  only on commit. `synth::ui::ColourPickerPopup` (`Source/UI/ColourPickerPopup.h`,
  `docs/theming.md` §13) already separates a live-preview callback (fires on every drag/favourite
  click, writes straight into its target with **no undo step**) from a commit-once callback (fires
  once, on close, as the real edit). A linked track/channel fans the SAME preview write out to all
  three destinations — track header swatch, macro colour, mixer column — on every drag frame; a
  cancel restores all three to their original colour, exactly like today's single-target case;
  a commit is **one** undo step covering all three, not three separate edits, matching the existing
  "one `Cmd+Z` undoes a dozen preview colours" semantics `docs/theming.md` §13 already documents,
  now fanned out.
- **(c) The track header's M/S drive the strip.** Not note gating — the linked channel's mute/solo.

A channel fed by **more than one** track (Kick/Snare/Hats into one sampler) is **not** linked: it
keeps its own independently-chosen name and colour, lists every track that feeds it, and each of
those tracks' M/S controls keep today's note-gating behaviour (§1) rather than touching the strip.
The link **breaks** the moment a second track starts feeding a previously-linked channel, and
**re-forms** if the channel goes back down to exactly one source track.

**The channel chip.** Every track header whose notes/audio play into a channel — linked or not —
shows a small **channel chip**: the channel's name plus a compact meter. Clicking it reveals that
channel's column in the mixer. No extra audio track is ever created by having a chip; the chip is
a pointer, not a new signal path. As the design brief for this puts it: **the timeline holds what
you play; the mixer holds what you hear.**

### 5.3 Solo: a render-time gate, not a parameter

Solo is a **switch applied during playback**, never something you automate, and it must not be
implemented by fanning `setMuted()` across other strips — `setMuted()` is a
`setValueNotifyingHost` parameter write: undoable, host-visible, and would silently clobber
whatever mute state the user had already set on those strips.

Each `ChannelStrip` carries its own solo flag: **not** an `AudioParameter`, **not** host-visible,
**not** automatable, persisted only in the strip's trusted extra state
(`ModuleBase::setExtraState`, applied on the trusted path only, per the root `CLAUDE.md`
invariant). A shared "is anything soloed?" count, owned by the engine, is readable from the audio
thread with no locks and no allocation. While the count is nonzero, every non-soloed strip **and**
Master's Direct input output silence.

Solo acts **after** each strip's own inserts, so a shared reverb or a decaying tail on a
non-soloed channel is genuinely silenced rather than continuing to ring into a mix that other
channels can still hear — the thing today's source-level solo gating (§1) cannot do.

**Open implementation detail, not a design fork:** whether the shared count is an engine-owned
atomic the audio thread reads directly, or something published through the existing
`EpochExchange` snapshot pattern timeline data already uses, is left to implementation. Either way
is allocation-free and lock-free on the audio thread; the choice doesn't change anything in this
document.

### 5.4 Mono/stereo

A strip's channel shape is fixed **at creation**, derived from the chain it terminates — never
inferred later from what gets plugged into it. This is the same rule macro ports follow
(`docs/macros.md` §5.3: "a port's shape is chosen at creation and then fixed"), applied to strips.

Strip output is always stereo. Pan is a balance law with unity gain at centre; a mono strip feeds
both output legs from a single input jack. The right leg follows the `kRightBase` convention
(`Source/Modules/CLAUDE.md`) — never ch1, which stays reserved for CV on the split-block voice
modules.

Changing a strip's shape means **replacing the strip**, one undo step, not widening a live node —
the same invariant that governs every fixed-channel-count module in this codebase. When "Make
channel" (§5.8) wraps a chain that ends poly, it inserts a `VoiceMixerModule` ahead of the strip
so the strip itself only ever receives a summed signal.

### 5.5 Bypass vs mute

The strip follows the root `CLAUDE.md` two-branch bypass/mute contract like every other
signal-processing module. The mixer exposes **no bypass control on the strip itself** — bypass is
not a mixer-level idea.

A channel macro's existing **Bypass** fan-out (`docs/macros.md` §5.6: `setBypassed(true)` across
every member) skips the source node and the strip when it fans out over a channel macro's
members — this is what "bypass inserts" means on the mixer: the chain's own effects go dry, but
the source keeps producing and the strip keeps passing signal through. The macro's **Mute**
fan-out, by contrast, includes the strip — muting the macro mutes the whole channel, strip
included.

### 5.6 Inserts in a free-form graph

A mixer column lists its channel's modules **in signal order**, read off the chain between the
source and the strip. When that chain is a straight line, the column supports adding, reordering
and removing modules directly in the mixer. When it branches — more than one path between source
and strip, or a shared node feeding more than one channel — the column instead shows the module
list read-only with an "Edit on canvas" link, rather than presenting a branching chain as if it
were a reorderable list.

### 5.7 Track presets

**Naming.** The product says **track** everywhere, in the UI and in this doc. "Lane" stays reserved
for what it already means in this codebase: the clip and automation lanes *inside* a track
(`docs/timeline_panel_clips_automation.md`).

**"Channel template" and "track preset" are one concept: the track preset.** A track preset is
`{ track kind, channel chain, strip settings, optional clips }`. Every track kind — **Audio**;
**Instrument** (which also covers a MIDI track that alone drives an instrument, per the link rule
in §5.2) — has its own default preset.

- **Saving.** "Save track as preset..." and "Set as default for `<type>`" are reachable from the
  track header and from the channel's own menu. Preferences -> Mixer lists each type's current
  default in a dropdown.
- **Creating a track.** The `+ Track` control lists available presets grouped by type, so a new
  track can start from any saved preset, not only the type's default.
- **Reusing a saved preset.** Defaults only decide what a plain `+ Track -> Audio` /
  `+ Track -> Instrument` creates. Any saved preset can be inserted at any time regardless of the
  defaults: `+ Track` lists every saved preset grouped by type, and "Insert track preset from
  file..." loads one saved anywhere on disk. Saving a preset never changes a default unless the
  user also picks "Set as default".
- **What a saved preset carries beyond the box.** Saving or exporting a track's channel must also
  capture every module **outside** the channel's macro that feeds it through a port — a shared LFO
  is the running example (§5.8). The save walks upstream transitively through modulation/CV/MIDI
  inputs from the macro's ports and stops the moment it reaches another channel's strip (a shared
  module that itself terminates in a different channel is that channel's business, not this
  preset's). On import/load, everything gathered this way arrives as **fresh copies** placed beside
  the track's box, wired to the same ports the original was — so the imported track sounds
  identical, with no dangling port left unfed and no cross-project aliasing of an original module.
- **Loading.** The same trusted/untrusted pairing every disk-sourced patch data uses:
  `validatePatch(..., trusted=false)` as a separate gate before a trusted `applyJSONToGraph`, the
  `SnippetManager::insertSnippet` / `ProjectBundle::load` reference pairing the root `CLAUDE.md`
  names. `"timeline"` stays a reserved field, refused on the untrusted path, exactly as it is
  today.

### 5.8 Make channel / shared modules

"Make channel" gathers the chain from a track's source to the output into a channel macro plus
strip. Some of the modules in that chain may also feed other tracks (a shared LFO, a shared
reverb) — **these stay shared**:

- Modules used **only** by this track move into the new channel's box.
- A module still feeding another track too (an exclusively-shared modulator, for example) **stays
  outside** and reaches in through an auto-created macro port — the same auto-porting mechanism
  `docs/macros.md` §7 item 7 already built for a cable crossing a macro's boundary at group time.
  The sound does not change at the moment of conversion. (The same outside-module accounting
  applies when *saving* a channel, not just when creating one — see §5.7's "what a saved preset
  carries beyond the box.")
- Where two tracks **merge** into one shared effect before that effect continues downstream, the
  effect becomes its **own bus channel** rather than being duplicated or arbitrarily assigned to
  one track.
- A **"Duplicate into this channel"** right-click action exists for the case where the user does
  want their own independent copy of a shared module.

### 5.9 Mixer panel & windows

Panel placement is a **Preferences setting** with three options: **Tab beside the Timeline**
(default), **Own panel**, or **Window**. Whichever is chosen, detaching a panel into its own
window uses **one mechanism shared by Timeline and Mixer**, not two — the reference
implementation is the existing hosted-plugin pattern,
`Source/Plugin/Hosting/HostedPluginEditorWindow.{h,cpp}`, including its `addToDesktop=false`
construction path, which is the headless-testable seam that lets a window be built and inspected
in a test with no real desktop window ever created.

The detach control itself is **icon-only**, with a tooltip that reads "Open in window" when docked
and "Dock back" when detached. The Timeline gets the identical control through the identical
mechanism — detaching the Timeline and detaching the Mixer are the same code path applied to
different panels, not two separate features.

In the plugin build, a detached window sets its **own** `LookAndFeel` instance; it never calls
`Desktop::setDefaultLookAndFeel`, which is process-global inside the host and would repaint
everything else the host owns (root `CLAUDE.md` / `Source/UI/CLAUDE.md` invariant). Keyboard focus
is scoped per window, consistent with what T158 already expects for app-wide keyboard focus
arbitration.

### 5.10 What the mixer shows

Strips, Direct, and Master. Nothing else — never an arbitrary module's output. To put something in
the mixer, make it a channel.

### 5.11 Hosted plugin

Nothing channel-specific. The graph stays flat in both host modes, so a `Master` splice sits in
front of the output in the app and the plugin alike, and the mixer panel renders inside the plugin
editor the same way `GraphEditor` already does. The existing rule that an over-wide hosted plugin
is **refused, never truncated** (`Source/Modules/CLAUDE.md`) is unaffected — a channel strip does
not change how a hosted plugin's channel count is checked.

### 5.12 Stem export

Every strip is a natural tap point: "single render, parallel writers," the shape stem export
already needs. One render pass writes one file per strip. Test: the resulting stems sum back to
the pre-Master mix.

### 5.13 Existing projects

Open unchanged — no automatic migration on load. **DONE (FRO26, T173e).** Since there is no mixer
panel yet (P9-5), the action lives on the "+ Track" menu (`TimelinePanelComponent::
kCreateChannelsMenuId`) alongside every other channel-creating entry (Audio Track, Instrument
Track) rather than a per-track context menu or a not-yet-built mixer column — it acts on every
track in the project at once, so it belongs beside the project-wide actions, not off a single
track header. "Create Channels" wraps every channel-less track's chain into a strip, as one undo
step covering all of them at once: `MainComponent::createChannelsForExistingTracks()` walks
`TimelineDoc::getTracks()`, resolves each track's own bound node (`bindingUuid` — a "Track Audio"
or "Track In"), and hands every one of them to `GraphEditor::createChannelsForUnchanneledTracks()`,
which runs T184's own per-node builder (`maybeAutoCreateChannelAfterConnect` — same
`synth::findUnchanneledOutputFeeds`/`buildChannelForFeeds` pair T184 uses) once per track inside
ONE `AppUndoManager::recordGraphTimelineAndMacroChange` transaction. A track that already reaches
a `ChannelStripModule` is silently skipped (same "exits.empty()" early return as T184's
connect-triggered case) — untouched, not re-channeled. The menu entry is disabled, and the action
is a no-op pushing nothing to the undo stack, when no track needs a channel
(`hasTracksNeedingChannels()`). Two channel-less tracks that share one unchanneled instrument (D1,
§7) still come out of the sweep as exactly one channel: the first track's
`maybeAutoCreateChannelAfterConnect` call builds it and removes the exit edges, so the second
track's call sees `findUnchanneledOutputFeeds` already empty for that instrument and does nothing —
free of any extra bookkeeping, since it's the same per-node builder T184 already runs on live
connects.

### 5.14 New Patch always has an Audio Output (T187)

`GraphEditor::newPatch()` seeds a fresh Audio Output immediately after `graph.clear()`, as part of
the same `recordStructuralChange` undo step — a brand-new empty project is never left with nothing
for `spliceMasterNode` (§5.1) to target. Before this, a bare Track Audio added to a fresh New Patch
was silently unheard until the user manually added an Audio Output; a second track added after that
manual fix then auto-spliced Master and re-routed the first track's manual wire into it, which read
as a surprise. Scoped to `newPatch()` only — `AIStateMapper::applyJSONToGraph`'s own `graph.clear()`
(preset/project load) is unaffected: it always replays a full node set from JSON right after
clearing, so it is self-correcting as long as the source JSON has an Audio Output (every factory
preset does).

On the very first channel, `MainComponent::addAudioTrack()` also relocates that Audio Output (or
any bare Audio Output the user dropped manually before adding a track) to sit immediately right of
the newly-spliced Master, once it knows this call is the one that splices Master (checked via
`synth::findMasterNode` before building the chain). Master already lands right of the chain by
design (T173a, §5.2) so the row reads `Track Audio -> EQ -> Compressor -> Strip -> Master ->
Audio Output` left to right; without the move, Audio Output stayed wherever it started (the newPatch
seed's canvas origin) while Master jumped to the far side of the chain, and the output cable had to
run back across the whole canvas — caught live via computer-use testing while verifying T187. Only
fires the first time Master is created; once it exists, later tracks don't reshuffle the canvas.

---

## 6. AI authorability

`ChannelStrip` and `Master` are internal-only and join `kNonAuthorableModuleTypes`
(`Source/AI/AIStateMapper.cpp`) — a model cannot author either directly, the same rule that
already governs `TimelineMidiSource`, `Rec Tap` and the macro port types. `validatePatch` does not
change; per the root `CLAUDE.md` invariant, it is never relaxed to raise an AI pass rate.

If a future goal is "the AI can build a channel," the correct shape is an app-side **tool/action**
— "create channel" — that the model invokes and the app executes against a validated node set,
never a patch key the model writes directly. This mirrors the same resolution `docs/macros.md` §6
already reached for "the AI can build a macro."

---

## 7. Founder decisions (2026-09-10)

All four decisions from the P9-1 proposal are settled. Each is recorded here with the option
chosen and its refinement; the design in §5 already reflects these in full. The rejected
alternatives are kept as one line each for the record.

**D1 — Should every track be a channel? Chosen: channels follow audio (§5.2).** Audio and
instrument tracks get a channel automatically; a MIDI track playing a shared instrument uses that
instrument's channel instead of getting its own. Refined with the auto-create-on-connect workflow,
the track/channel link rule (name sync, live colour sync, M/S drive), and the channel chip.
*Rejected: every track owns a channel (duplicates shared instruments); every track gets a column
(two kinds of column to learn).*

**D2 — What happens to modules a track shares with others when it becomes a channel? Chosen: keep
them shared (§5.8).** Exclusive modules move into the box; a shared module stays outside via an
auto-created port; a merge point becomes its own bus channel. Refined so that saving or exporting a
channel also captures every outside module that feeds it, walked upstream transitively and stopped
at another channel's strip, arriving as fresh copies on import (§5.7). *Rejected: duplicate
everything shared (breaks phase/CPU/shared-reverb sanity the instant you click); ask each time (a
dialog on every conversion).*

**D3 — What goes in a new channel by default? Chosen: EQ -> Compressor, added bypassed.** Refined
by merging "channel template" and "track preset" into one **track preset** concept — per-track-kind
default, saved and set from the track header or the channel menu, listed in Preferences -> Mixer
and in the `+ Track` control (§5.7). *Rejected: EQ -> Compressor live at neutral settings (CPU cost
on every channel regardless of use); empty (no baseline at all).*

**D4 — Where does the mixer live? Chosen: a tab beside the Timeline, as the default.** Refined into
a three-way Preferences placement setting (Tab beside Timeline / Own panel / Window) sharing one
detach mechanism with an icon-only control, and the Timeline gains the identical detach control
through that same mechanism (§5.9). *Rejected (as the default; still available as alternate
settings): its own dock; window-only.*

**Accessibility.** Mixer keyboard navigation and screen-reader support move into their own
Accessibility epic (P9 side track T181, §8/§9) rather than shipping inside P9.

---

## 8. Implementation order and Tests

Main line, in dependency order:

1. **P9-2 (T172) — `ChannelStrip` + `Master` + the solo gate.** Engine only. **DONE.** How it
   landed (module detail in [`docs/modules.md`](modules.md), engine detail in
   [`docs/architecture.md`](architecture.md) § Mixer solo gate):
   - *Strip layout*: 5 raw channels a side, Left ch0 / Right `kRightBase` = 4, ch1–3 reserved;
     params `gain` (dB), `pan`, `muted`; shape + solo in extra state.
   - *Master layout*: Mix L/R on ch0/1, Direct L/R on ch2/3; Direct summed in before the fader;
     bypass is a unity sum that keeps Direct.
   - *§5.3's open detail, resolved*: an engine-owned atomic count, recounted on the message
     thread by scanning the graph inside `publishTimeline` (so a deleted/undone soloed strip
     cannot leave the mix stuck), carried per render pass on `TransportService` like the
     input-monitoring flag.
   - *Solo applies to a bypassed strip too* — bypass is the strip's own gain/pan going dry, the
     gate is layered on top; otherwise the strip's card bypass would leak into a soloed mix.
   - *The splice* is `synth::ensureMasterNode` (Core, `Source/Mixer/MasterSplice.h`); nothing
     calls it yet — P9-3's channel creation does.
   - `Tests/ChannelStripTests.cpp`: pan balance law is unity at centre for both mono and stereo
     shapes; bypass/mute follow the two-branch contract; shape is fixed at construction and
     rejects a later width change.
   - `Tests/MixerSoloTests.cpp`: soloing one strip silences every other non-soloed strip and the
     Direct input; un-soloing the last soloed strip restores every other strip; solo never mutates
     another strip's mute parameter; `Master` splice is one undo step and undoes cleanly.

2. **P9-3 (T173) — Channel creation flows.** Audio track auto-channel, the Instrument track,
   MIDI-track auto-channel-on-connect (§5.2's main workflow, Preferences-gated), "Make channel",
   "Create channels" for existing projects, the factory default chain (EQ -> Compressor, bypassed).
   - Tests: an audio/instrument track creation produces exactly one channel in one undo step; a
     MIDI track wired to an unchanneled instrument auto-creates a channel in one undo step when
     the preference is ON, and creates none when it is OFF; "Create channels" on a project with N
     channel-less tracks is one undo step that creates N channels.
   - **T173a (audio track) — DONE.** "+ Track -> Audio Track" builds the whole channel — Track
     Audio -> Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel Strip (Stereo) ->
     Master (Mix) — in ONE undo step (`AppUndoManager::recordGraphTimelineAndMacroChange`), via a
     new Core helper `synth::buildDefaultAudioChannel` (`Source/Mixer/ChannelFlows.h`) that splices
     Master (`synth::spliceMasterNode`, reusing the existing singleton after the first channel) and
     wires the strip into it. `{Track Audio, EQ, Compressor, Strip}` are boxed into ONE collapsed
     macro named after the track (`GraphEditor::addMacroForMembers`) — **Master stays outside the
     macro, and the Strip -> Master cable is left a plain graph edge, deliberately never a macro
     port**: `spliceMasterNode`/`ensureMasterNode` classify a re-routed feed as Mix vs Direct by
     checking whether the connection's SOURCE NODE is itself a `ChannelStripModule`; a
     `MacroOutlet` sitting between the strip and Master would make the source node a `MacroOutlet`
     instead and defeat that check, which the "Create channels" flow (T173e, DONE — see below,
     "N channel-less tracks") depends on.
   - **T183 (instrument track) — DONE.** "+ Track -> Instrument" opens a submenu of audio-producing
     MIDI instruments (Oscillator, Wavetable, Sampler — deliberately NOT every
     `isMidiInstrumentType()` member: Poly MIDI/Sequencer/Poly Sequencer generate CV/MIDI, not
     audio) and builds Track In -> the chosen instrument -> the same
     `synth::buildDefaultAudioChannel` chain T173a uses, in ONE undo step
     (`MainComponent::addInstrumentTrack`). `{Track In, instrument, [Voice Mixer if poly], EQ,
     Compressor, Strip}` are boxed into one collapsed macro the same way T173a's are; Master stays
     outside it for the same `spliceMasterNode` reason. `buildDefaultAudioChannel` gained a
     trailing `sourceRightChannel` parameter (default 1, Track Audio's contiguous pair unchanged)
     so a split-block source can pass its own `ModuleBase::rightAudioLegChannel()` instead of
     assuming ch1 — Oscillator/Wavetable's right leg is never ch1 (Source/Modules/CLAUDE.md). A new
     `synth::addVoiceMixerForPolyInstrument` (§5.4/§5.8's "poly output needs a Voice Mixer ahead of
     the strip" rule) sums a poly instrument's ch0-7 into a Voice Mixer first — gated on the
     instrument's own live `poly` parameter, never forced on: a factory-created instrument defaults
     to poly OFF, so this branch is a no-op on the golden path today and exists for whenever it
     isn't (`Tests/ChannelFlowTests.cpp`'s `PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel`
     exercises it directly). See §5.2's table note for the `TrackKind::Midi` scope decision.
     `Tests/ChannelFlowTests.cpp`.
   - **P9-3i (Oscillator/Wavetable envelope) — DONE.** Oscillator and Wavetable have no envelope of
     their own — a held (or even released) note droned forever. "+ Track -> Instrument ->
     {Oscillator/Wavetable}" now inserts an ADSR + VCA ahead of the rest of the chain:
     `Track In --MIDI--> ADSR --Env--> VCA's Gain CV`, `chainSource -> VCA Audio -> EQ`
     (`synth::addEnvelopeAndVCAForRawInstrument`, `Source/Mixer/ChannelFlows.h`/`.cpp`). Inserted
     AFTER any Voice Mixer stage, never before it, and both nodes are forced non-poly regardless of
     the instrument's own `poly` parameter: `ADSRModule`'s poly branch is CV-gate-only (it never
     reads the MIDI note-on/off fallback that drives its non-poly branch), so a poly ADSR fed only
     Track In's MIDI would output a permanent zero envelope. ADSR's `sustain` (stock default 0.0)
     is overridden to 0.7 so a held note actually sustains instead of plucking-and-dying after
     ~0.25s; VCA's `gain` (stock default 0.5) is overridden to 1.0 so the envelope alone governs
     level. Sampler is untouched — it already has its own one-shot playback envelope, out of scope.
     `{Track In, instrument, [Voice Mixer if poly], ADSR, VCA, EQ, Compressor, Strip}` join the same
     one collapsed macro. See `Tests/ChannelFlowTests.cpp`'s
     `InstrumentTrackOscillatorEnvelopeActuallySilencesAfterNoteOff` for the render-level proof (not
     just topology) that the envelope actually gates audio.
   - **P9-3j (poly Oscillator/Wavetable envelope) — DONE (FRO46).** P9-3i's ADSR+VCA were forced
     non-poly even when the instrument's own `poly` parameter was on: `ADSRModule`'s poly branch is
     CV-gate-only, it never reads the MIDI note-on/off fallback that drives its non-poly branch, so
     a poly ADSR fed only raw MIDI would output a permanent zero envelope. Fixed by inserting a
     **Poly MIDI** node (the codebase's existing per-voice MIDI-to-CV converter, §"Poly MIDI Module"
     in `docs/modules.md`) between Track In and the instrument instead of raw MIDI, when the
     instrument's `poly` parameter is on at instrument-track creation time — whether set
     programmatically or, since FRO48/P9-3k below, via the "(Poly)" menu entry (same "poly handled
     correctly wherever it arises" precedent T183's `addVoiceMixerForPolyInstrument` established):
     ```
     Track In --MIDI--> Poly MIDI --Pitch(ch0-7)--> instrument's poly Pitch CV in (ch0-7)
                         Poly MIDI --Gate(ch8-15)--> ADSR's poly Gate CV in (ch0-7)
     ADSR poly Env (ch0-7) --> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase)
     instrument's poly Audio L (ch0-7) --> VCA's poly Audio L in (ch0-7)
     ```
     Both ADSR and VCA are now genuinely poly (`synth::addPolyEnvelopeAndVCAForInstrument`,
     `Source/Mixer/ChannelFlows.h`/`.cpp`), giving each voice its own independent envelope instead
     of one shared mono envelope gating the whole poly-voice sum. No separate Voice Mixer is
     inserted for this case — the poly VCA already sums all 8 gated voices to a stereo-shaped pair
     itself (ch0 = left sum, ch1 = its own legacy duplicate), so `addVoiceMixerForPolyInstrument` is
     skipped entirely when this branch fires; it still fires for a poly instrument this doesn't
     apply to (e.g. a poly Sampler). The instrument's R-octet is deliberately NOT wired into the
     VCA's own Audio R poly block (ch16-23) — same known stereo limitation
     `addVoiceMixerForPolyInstrument`'s own comment documents for the non-envelope poly path.
     `{Track In, instrument, Poly MIDI, ADSR, VCA, EQ, Compressor, Strip}` join the same one
     collapsed macro. See `Tests/ChannelFlowTests.cpp`'s
     `PolyEnvelopeAndVCAWiresPerVoicePitchGateAndAudioWithNoVoiceMixer` for the wiring proof and
     `Tests/PolyMidiModuleTests.cpp`'s `PolyMidiToAdsrToVcaTest.ReleasingOneVoiceLeavesAnother-
     HeldVoiceUntouched` for the render-level proof that releasing one voice's note leaves another
     held voice's envelope untouched — the thing a single shared mono envelope could never do.
   - **P9-3k (poly Oscillator/Wavetable UI entry) — DONE (FRO48).** The "+ Track > Instrument" menu
     now offers "Oscillator (Poly)" and "Wavetable (Poly)" entries (Sampler has no poly parameter,
     so it has no poly entry) that set the new instrument's "poly" AudioParameterBool via the new
     `synth::setProcessorPoly()` write counterpart to `isProcessorPoly()` before the poly-envelope
     branch check runs, making P9-3j's poly-envelope auto-wire reachable from the UI for the first
     time. See `ChannelFlowTest.AddInstrumentTrackMenuOscillatorPolyWiresPolyEnvelopeAndVCA` /
     `...WavetablePolyWiresPolyEnvelopeAndVCA` in `Tests/ChannelFlowTests.cpp`.
   - **T184 (MIDI-track auto-channel-on-connect) — DONE.** Dragging a MIDI cable from a Track In
     node (`ModuleType::TimelineMidiSource`) onto an instrument or macro whose audio reaches Audio
     Output/Rec Tap/Master's Direct bus with no `ChannelStrip` anywhere on that path auto-builds a
     channel there, as ONE undo step with the connection itself
     (`AppUndoManager::recordGraphAndMacroChange`, `GraphEditor::endConnectionDrag`). An instrument
     that already has a channel on its path gets nothing new — connecting a second MIDI track just
     wires straight in, same as any other MIDI-track-into-an-existing-instrument case (§5.2's
     table). Gated by a Preferences toggle, `mixerAutoCreateChannelOnConnect`, default **ON**; OFF
     restores exactly today's behaviour (wire freely, no channel appears).
     - *Core* (`Source/Mixer/ChannelFlows.h`/`.cpp`, no AppUI/GraphEditor/AppUndoManager
       dependency): `synth::findUnchanneledOutputFeeds(graph, start)` does a forward BFS from the
       just-connected node over audio AND MIDI edges — never expanding past a
       `ChannelStripModule` (already channeled: stop, no exit), a `RecordTapModule`, a
       `MasterModule`, or the `AudioGraphIOProcessor` named "Audio Output" (all three are
       terminals), and never traversing INTO a hidden `AttenuverterModule` (a mod/CV leg is not an
       audio-reaching-the-output path). An "exit" is an edge landing on Audio Output ch0/1, Rec Tap
       ch0/1, or Master's `kDirectLeft`/`kDirectRight` — `kMixLeft`/`kMixRight` are deliberately
       NOT exits, since only a `ChannelStripModule`'s own output legitimately lands there.
       `synth::buildChannelForFeeds(graph, exits, layout)` removes every exit edge FIRST (so
       `spliceMasterNode`'s own "sweep everything already feeding Audio Output/Rec Tap into Master"
       behaviour, run as part of building the chain when no Master exists yet, never re-captures an
       edge this call is about to own), then builds EQ(bypassed) -> Compressor(bypassed) -> Strip
       (Stereo) -> Master via the SAME internal builder `buildDefaultAudioChannel` now shares
       (`buildChannelChain`), generalized to accept arbitrary left/right feed lists instead of one
       fixed stereo pair (multiple feeds landing on the same side is fine — `AudioProcessorGraph`
       sums them).
     - *GraphEditor hook* (`Source/UI/GraphEditor.h`/`.cpp`): `endConnectionDrag` gates on the drag
       being MIDI and the real source node being `ModuleType::TimelineMidiSource`
       (`nodeIsTimelineMidiSource`), covering both the direct-jack path and the collapsed-macro-card
       "existing port jack" path — not the `createMacroPortFromDroppedCable` fallback (dropping a
       cable on a macro's body with no jack under it mints a port with no interior leg yet, so
       there is nothing to search from and T184 never applies there). One undo transaction wraps macro-port
       auto-creation (T148, if the drag also crosses a macro boundary), the connection itself, and
       the auto-channel build — `maybeAutoCreateMacroPortsForDrag` gained a trailing
       `recordUndo = true` parameter so a caller already inside its own transaction can pass
       `false` and hoist `updateComponents()` out to run once, after every mutation, instead of the
       nested-transaction double-repaint a naive wrap would produce (docs/macros.md §7 item 9 has
       the signature detail). Layout mirrors T173a/T183's own pattern: `estimateModuleSize()` per
       node type plus a 40px gap constant (`kAutoChannelCardGapX`, GraphEditor.cpp's own copy of
       `MainComponent.cpp`'s `kChannelCardGapX` — duplicated rather than shared, since Core/UI
       layering keeps GraphEditor.cpp from reaching into MainComponent.cpp). **Boxing rule:** the
       new EQ/Compressor/Strip join the SAME macro as the instrument only when every distinct exit
       source node is already an ORDINARY member of ONE common macro (not a port, not split across
       macros, not un-macroed) — otherwise the new chain nodes are left unboxed on the canvas
       rather than guessing which container they belong to.
     - Tests: `Tests/ChannelFlowTests.cpp` — Core-level (`ChannelFlowAutoChannelCore`):
       `FindUnchanneledOutputFeedsOnInstrumentToOutputFindsTwoExits`,
       `FindUnchanneledOutputFeedsOnStripChanneledInstrumentFindsZero`,
       `FindUnchanneledOutputFeedsNotReachingOutputFindsZero`,
       `FindUnchanneledOutputFeedsIgnoresModulationBranchAndSweepsTheUnrelatedPathOntoMasterDirect` (a mod/CV
       leg through a hidden Attenuverter is neither traversed nor counted, and an unrelated
       pre-existing direct-to-output feed gets correctly swept onto Master's Direct bus by the
       same-transaction `spliceMasterNode` splice, exactly like any other pre-existing feed would
       be), `BuildChannelForFeedsRemovesExitEdgesAndWiresThroughToANewMaster`,
       `BuildChannelForFeedsReusesAnExistingMasterAndClearsDirectFeeds`,
       `BypassedEQAndCompressorReportZeroLatencyAfterPrepare` (latency-zero gate: neither module
       calls `setLatencySamples`). Real-mouse-path (`ChannelFlowTest`, `MainComponent` fixture,
       synthesized `juce::MouseEvent`s driven straight into `ModuleComponent`, docs/testing.md's
       "test the real mouse path"): `AutoChannelOnConnect_ToggleOnBuildsOneChannelAsOneUndoStep`,
       `AutoChannelOnConnect_ToggleOffOnlyConnectsNoChannel`,
       `AutoChannelOnConnect_AlreadyChanneledInstrumentGetsNoNewStrip`,
       `AutoChannelOnConnect_NewChainNodesJoinTheInstrumentsExistingMacro`. Plus
       `Tests/PreferencesSettingsTabTests.cpp`: default ON, persists `"0"`/`"1"` under
       `mixerAutoCreateChannelOnConnect` and round-trips, and pushes to a live `GraphEditor` via
       `setGraphEditor`/on toggle, mirroring every T148 toggle test exactly.
   - **T173e (existing projects, "Create channels") — DONE (FRO26).** The "+ Track" menu's new
     "Create Channels" entry (`TimelinePanelComponent::kCreateChannelsMenuId`) — see §5.13 for why
     it lives there. `GraphEditor::createChannelsForUnchanneledTracks(trackSourceNodeIds)` is a thin
     public wrapper: for each id, it calls the SAME private `maybeAutoCreateChannelAfterConnect`
     T184's `endConnectionDrag` hook already uses, reusing its exit-finding, chain-building, T187
     Master-relocation and same-macro boxing logic unchanged rather than a parallel implementation.
     `MainComponent::createChannelsForExistingTracks()` gathers every track's own bound node
     (`TimelineDoc::Track::bindingUuid`) first, then wraps the whole per-track sweep in ONE
     `AppUndoManager::recordGraphTimelineAndMacroChange` transaction, so N channel-less tracks
     becoming N new channels (sharing one `Master` after the first) is a single Cmd+Z.
     `hasTracksNeedingChannels()` — the same `synth::findUnchanneledOutputFeeds` query per track,
     short-circuiting on the first hit — backs both the menu's enabled state and the action's own
     no-op guard, so a project where every track already has a channel changes nothing and pushes
     no undo step. Both methods are non-pure `TrackHeaderHost` virtuals with inert defaults
     (`Source/UI/TimelineTrackHeaderComponent.h`), the same pattern every other "+ Track" action
     uses, so existing test stubs keep compiling untouched.
     - Tests (`Tests/ChannelFlowTests.cpp`, `ChannelFlowTest` fixture):
       `CreateChannelsWrapsEveryChannellessTrackAsOneUndoStep` (two legacy tracks — a bare
       "Track Audio" and a "Track In" -> Oscillator, both wired straight to the output, the
       pre-P9-3 shape a real old project still loads as — both get their own strip sharing one
       `Master`, their old straight-to-output feeds are gone, and ONE undo/redo round-trips the
       whole sweep byte-for-byte against the graph/timeline/macro JSON snapshots taken right
       before the action), `CreateChannelsLeavesAlreadyChanneledTracksUntouched` (one track already
       channeled via `addAudioTrack()` plus one legacy track — the pre-existing strip's node and
       its wiring to Master are untouched; only the legacy track gets a new strip),
       `CreateChannelsIsANoOpWhenNothingNeedsAChannel` (every track already channeled — no new
       nodes, and undoing once removes the earlier `addAudioTrack()` step itself, proving "Create
       Channels" pushed no undo step of its own),
       `CreateChannelsGivesTwoTracksSharingOneInstrumentJustOneChannel` (D1, §7: two MIDI tracks
       wired into one shared channel-less Oscillator come out of the sweep with exactly one
       channel, not two, and one undo removes it).

3. **P9-4 (T177) — Track/channel link.** Name sync, live colour sync across track/macro/column
    through `ColourPickerPopup`'s preview/commit split, M/S driving the strip, the channel chip.
   - Tests: renaming either side of a linked track/channel renames the other; a colour drag
     previews on all three surfaces with no undo step per frame and commits as one; adding a
     second source track to a linked channel breaks the link (name/colour become independent,
     M/S reverts to note gating) and removing it back down to one source re-forms the link;
     clicking a channel chip reveals the right mixer column.

4. **P9-5 (T174) — Mixer panel, tab beside the Timeline.** Columns, faders, meters, insert lists,
   track header M/S wired to strips (§5.6/§5.9).
   - Tests: track header M/S toggles the bound strip's mute/solo state and nothing else; a
     branching insert chain renders read-only with "Edit on canvas" rather than a reorderable
     list.

5. **P9-6 (T175) — Detachable windows for Timeline + Mixer, and the placement preference.** One
   mechanism for both, icon-only detach control, keyboard focus scoped per window (T158).
   - Tests: a detached mixer window built with `addToDesktop=false` behaves identically to the
     docked panel in a headless test; keyboard focus in one detached window does not leak into the
     other; switching the placement preference moves the panel without losing its state.

6. **P9-7 (T176) — Track presets.** Save/set-default from the track header and channel menu,
   Preferences -> Mixer defaults, `+ Track` preset listing, the outside-module walk-and-copy rule
   (§5.7).
   - Tests: a track preset round-trips through `validatePatch(trusted=false)` before a trusted
     apply; a hand-edited preset containing a `"timeline"` key is refused; a preset whose channel
     depends on an outside shared module imports with a fresh copy of that module wired to the
     same ports, not a reference to the original.

Side tracks (each independent of the main line beyond its own listed dependency):

- **P9-8 (T123) — Stem export.** Needs only P9-2. `Tests/StemExportTests.cpp`: the written stems
  sum back to the pre-Master mix within a tolerance; a soloed strip during export does not affect
  which strips get written.
- **P9-9 (T178) — Sends and group buses.** After P9-5; needs a short design pass of its own before
  implementation (a send is a tap on a strip feeding a bus channel, per §9, but the mechanism
  itself isn't specified here).
- **P9-10 (T179) — EQ curve thumbnail on mixer columns.** After P9-5.
- **P9-11 (T180) — Gate module.** No dependency on the rest of P9; only needed if a default track
  preset (§5.7/§7 D3) should include one.
- **T181 — Mixer accessibility**, in the Accessibility epic: column navigation, the existing
  rebindable M/S keys acting on the focused column, fader nudge, screen-reader labels for faders
  and meters, alongside T158's app-wide keyboard focus work.

---

## 9. Out of scope

- **Sends and group buses** — P9-9 (T178), after the mixer panel ships and its own short design
  pass is done.
- **An EQ curve thumbnail on a channel column** — P9-10 (T179), after the mixer panel ships.
- **A Gate module** — P9-11 (T180), independent of the rest of P9.
- **Mixer accessibility** (keyboard navigation, screen-reader labels) — T181, in the Accessibility
  epic rather than P9, alongside T158's app-wide keyboard focus work.

---

## Related

- [`docs/macros.md`](macros.md) — the macro container a channel is optionally boxed in; the
  proxy-port and internal-only-node precedents this design follows.
- [`docs/timeline_panel_core.md`](timeline_panel_core.md) — track headers, M/S controls, the
  timeline side of the track/channel relationship.
- [`docs/architecture.md`](architecture.md) — `ensureMasterRecordTap()`, `EpochExchange`, the
  bypass/mute contract, plugin host modes.
- [`docs/modules.md`](modules.md) — `kRightBase`, the stereo-pair conventions a `ChannelStrip`'s
  output follows, `VoiceMixerModule`.
- [`docs/theming.md`](theming.md) §13 — `ColourPickerPopup`'s preview/commit split, the mechanism
  the track/channel colour link (§5.2) fans out over three targets.
