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
channel-less track in the open project into its own channel in one shot (§5.13, §8 item 2, FRO26),
and "Make channel" — the last P9-3 flow — turns one track (header menu) or a selected chain (canvas /
module menu) into a channel on demand, keeping shared modules shared, turning a merge point into its
own bus channel and offering "Duplicate into Channel" for an independent copy (§5.8, §8 item 2,
P9-3d/FRO25). There is no mixer panel yet (P9-5) — until it exists, "Locate Master" (Cmd+Shift+M / canvas right-click, P9-3g/FRO45,
[`shortcuts.md`](shortcuts.md#locate-master-fro45)) is the lightweight, canvas-only stopgap for
finding Master (or Audio Output) after auto-arrange or a drag leaves it off-screen. This document
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
DSP of its own. `MainComponent::ensureMasterRecordTap()` (`Source/MainComponent/MainComponentTimeline.cpp`) already
splices a singleton `Rec Tap` node in front of it on demand, re-routing every audio connection
that fed the output through the tap, as one compound undo step (`docs/architecture.md`). This is
the closest existing precedent for what a `Master` node splice would look like (§5.1).

Macros (`docs/macros.md`) are a **presentation-only layer over a flat graph**: a macro adds no
processing of its own, and its ports are proxy nodes (`MacroInlet`/`MacroOutlet`/
`MacroMidiInlet`/`MacroMidiOutlet`) — themselves internal-only, non-authorable, excluded the same
three ways.

Relevant existing modules: `VoiceMixerModule` (sums a poly chain to one signal),
`ParametricEQModule`, `CompressorModule`, `LimiterModule`, `GateModule` (all under
`Source/Modules/FX/`) — see §8 (P9-11).

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
Ungrouping (per `docs/macros_implementation.md` §7 item 8, fix G7) removes a macro's *ports* but never its ordinary members
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
`Source/AI/AIStateMapper/AIStateMapperInternal.h`). Adding them to the factory without adding them to that set will
fail `AIStateMapperTest.AuthorableModuleTypesGolden` — that failure is intended and is how the
golden test is meant to catch this exact addition (see `docs/macros_implementation.md` §6 for the identical
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
  only on commit. `synth::ui::ColourPickerPopup` (`Source/UI/Chrome/ColourPickerPopup.h`,
  `docs/theming.md` §13) already separates a live-preview callback (fires on every drag/favourite
  click, writes straight into its target with **no undo step**) from a commit-once callback (fires
  once, on close, as the real edit). A linked track/channel fans the SAME preview write out to all
  three destinations — track header swatch, macro colour, mixer column — on every drag frame; a
  cancel restores all three to their original colour, exactly like today's single-target case;
  a commit is **one** undo step covering all three, not three separate edits, matching the existing
  "one `Cmd+Z` undoes a dozen preview colours" semantics `docs/theming.md` §13 already documents,
  now fanned out over TWO stored targets, not three: a channel's colour IS its macro's colour, so
  the mixer column reads the macro rather than a third copy on the strip (§8 item 3).
- **(c) The track header's M/S drive the strip.** Not note gating — the linked channel's mute/solo.
  A redirect, not a mirror: the track's own `muted`/`soloed` stay false, so there is only one copy.

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
(`docs/macros_ports.md` §5.3: "a port's shape is chosen at creation and then fixed"), applied to strips.

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

A channel macro's existing **Bypass** fan-out (`docs/macros_ports.md` §5.6: `setBypassed(true)` across
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

**As implemented (P9-7, FRO13).** `synth::TrackPresetManager` (`Source/Mixer/TrackPresetManager.h`)
is a thin wrapper over `SnippetManager::extractSnippet`/`insertSnippet`, adding only what a track
preset needs beyond a plain snippet: `extractTrackPreset` walks outside modulators
(`collectOutsideModulatorsForTrackPreset`, the §5.7 "stops at another channel's strip" rule) and
scrubs `"solo"` from the captured Channel Strip's extra state before the preset is ever written to
disk — an imported `soloed_=true` would otherwise silence the whole mix render-wide (root
`CLAUDE.md` tripwire); `insertTrackPreset` is `SnippetManager::insertSnippet` with
`trustedPayload=false`, so the untrusted `validatePatch` gate still runs on every disk-sourced
preset. Entry points: the track header's right-click menu and the channel macro's own menu both
offer **"Save Track as Preset..."**/**"Set as Default Track Preset"**, gated on
`synth::isChannelMacro` (omitted entirely — not merely disabled — for an ordinary, non-channel
macro, same as the header's own disabled-not-hidden gate when the track has no channel yet); the
two per-type defaults are read from Preferences -> Mixer's `mixerDefaultTrackPresetAudio`/
`mixerDefaultTrackPresetInstrument` settings keys and consulted by `+ Track -> Audio Track` /
`Instrument Track` before the factory EQ -> Compressor -> Strip chain is built; `+ Track` also
lists every OTHER saved preset by type as its own submenu entries, and "Insert Track Preset from
File..." loads one saved anywhere on disk (`TimelinePanelTrackHeaders.cpp`), both resolved against
a menu-open-time snapshot (`audioTrackPresetMenuSnapshot_`/`instrumentTrackPresetMenuSnapshot_`,
same "resolve against the snapshot, not a live re-query" reason the plugin list submenu already
uses) since a preset can be saved or deleted between the menu opening and the click landing. Tests:
`Tests/Mixer/TrackPreset/TrackPresetTests.cpp` (round-trip render identity, two inserts never
collide, a smuggled `"timeline"` key has no effect because `prepareForInsert` only ever copies
`"nodes"`/`"connections"`/`"macros"`, a malformed preset is refused whole),
`TrackPresetCaptureTests.cpp` (the outside-modulator walk, the other-channel-strip stop rule, the
solo scrub, and the macro-menu gate), `TrackPresetDefaultsTests.cpp` (the per-type default actually
consulted by `+ Track -> Audio Track`), and four cases in
`Tests/UI/Timeline/TimelineTrackHeaderContextMenuTests.cpp` for the header menu's own wiring.

### 5.8 Make channel / shared modules

"Make channel" gathers the chain from a track's source to the output into a channel macro plus
strip. Some of the modules in that chain may also feed other tracks (a shared LFO, a shared
reverb) — **these stay shared**:

- Modules used **only** by this track move into the new channel's box.
- A module still feeding another track too (an exclusively-shared modulator, for example) **stays
  outside** and reaches in through an auto-created macro port — the same auto-porting mechanism
  `docs/macros_implementation.md` §7 item 7 already built for a cable crossing a macro's boundary at group time.
  The sound does not change at the moment of conversion. (The same outside-module accounting
  applies when *saving* a channel, not just when creating one — see §5.7's "what a saved preset
  carries beyond the box.")
- Where two tracks **merge** into one shared effect before that effect continues downstream, the
  effect becomes its **own bus channel** rather than being duplicated or arbitrarily assigned to
  one track.
- A **"Duplicate into this channel"** right-click action exists for the case where the user does
  want their own independent copy of a shared module.

**As implemented (P9-3d, FRO25).** Entry points: the track header's right-click **Make Channel**
(the track's bound node), and **Make Channel** in the canvas right-click menu (with a selection)
and in every module card's right-click menu — `synth::resolveChannelSource` picks the chain the
selection belongs to (a selected track source, else the one track source upstream of it, else the
one root upstream of it; no item when ambiguous). The item is disabled, not hidden, once the chain
has a channel. "Which modules does this track use" is `synth::planMakeChannel`'s reach walk: every
MIDI edge and every audio edge not landing on a modulation (`PortRole::ModCV`) pin, never through a
modulation attenuverter, through strips and macro ports, stopping at Audio Output / Record Tap /
Master. A node the track reaches that no other track source reaches is **own** and moves in; one
another track also reaches is **shared** and never moves; a node no track reaches (an LFO) moves in
only when every consumer of it (looking through attenuverters) already does — otherwise it stays
outside and the group-time auto-port pass fronts its cable ([`macros_implementation.md`](macros_implementation.md) §7 item 7).
The new strip takes over the own region's exits to the output, plus every audio edge into the
shared region carrying that same signal (with no exit at all, every such edge, provided each side
carries one consistent signal — otherwise the action is refused with a status message; a
different signal into a shared module stays a pre-strip send). Each shared merge head that still
reaches the output without a strip gets its own **bus channel** ("<module> Bus"), holding the shared
nodes downstream of it; the track's own strip feeds the bus's original input pins, never Master
directly, so nothing is summed twice. Strip, bypassed EQ/Compressor, Master and macro ports are all
unity pass-throughs at their defaults, so the converted patch renders sample-identically
(`Tests/ChannelFlowTests.cpp` proves it offline for the shared-LFO and merge cases) — the one
intended exception is a chain ending on a poly jack, which gets a Voice Mixer ahead of the strip
(§5.4, `addVoiceMixerForPolyInstrument`) and so carries every voice where a bare poly jack wired to
a mono input carried voice 0 only. A node that would move but is already in a macro refuses the
whole action (flat model). **Duplicate into Channel** is offered on a module outside every macro
that feeds a channel macro (directly, through its port, or through a modulation attenuverter) and
something else too — one item naming the channel, or a submenu when it feeds several: the copy
(parameters and extra state carried, the Duplicate path) takes over every cable into that channel,
inherits the original's inputs (a modulation into it is re-created with the same amount), and
joins the macro; the other consumers stay on the original. Each action is ONE graph + timeline +
macro undo step followed by the reconcile pass.

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

**EQ curve thumbnail (P9-10, T179).** When a strip's insert chain contains a Parametric EQ, its
column shows a small frequency-response curve (Cubase's top-mixer-row idiom) — computed from the
EQ's own parameters, never audio, and cached so a busy graph never pays for an unconditional
per-tick repaint (root `CLAUDE.md`). Clicking it selects that EQ on the canvas through the same
`onEditOnCanvas` seam the insert list's own "Edit on canvas" link and a column header click use. A
bypassed EQ draws dimmed; a strip with no EQ insert shows no thumbnail; a strip with two shows the
thumbnail for only the first one in signal order. See
[`docs/mixer_implementation.md`](mixer_implementation.md) for how it landed.

### 5.11 Hosted plugin

Nothing channel-specific. The graph stays flat in both host modes, so a `Master` splice sits in
front of the output in the app and the plugin alike, and the mixer panel renders inside the plugin
editor the same way `GraphEditor` already does. The existing rule that an over-wide hosted plugin
is **refused, never truncated** (`Source/Modules/CLAUDE.md`) is unaffected — a channel strip does
not change how a hosted plugin's channel count is checked.

### 5.12 Stem export

**DONE (P9-8, T123).** Every strip is a natural tap point: "single render, parallel writers," the
shape stem export already needs. One render pass writes one file per strip. Test: the resulting
stems sum back to the pre-Master mix.

`ChannelStripModule` carries an opt-in, non-owning stem tap (`setStemTapBuffer`): a
message-thread-armed pointer to a preallocated stereo buffer, null outside an export. When armed,
the strip copies its FINAL output — post gain, pan, mute AND solo, exactly what it hands to
Master — into the tap at the end of every `processBlock` exit path. No allocation, no locks: an
atomic pointer swap and, when armed, one `copyFrom` per leg.

`synth::StemExporter::exportStems` (`Source/Transport/StemExporter.{h,cpp}`) drives ONE render
pass through the exact same offline path `BounceExporter::bounce` uses — same `BounceOptions`
(range, tail, sample rate, bit depth, format), same suspend/reprepare/restore choreography, same
progress/cancel semantics — via a sibling session class, `synth::StemSession`
(`Source/Transport/StemSession.{h,cpp}`), and a sibling chunked runner, `synth::StemRunner`
(`Source/Transport/StemRunner.{h,cpp}`), mirroring `BounceSession`/`BounceRunner` so the message
thread stays responsive exactly like Export Audio. The two shared pieces both paths actually
duplicate no logic for (`synth::validateBounceOptions`, and the metronome/external-MIDI RAII
guards) are factored into `Source/Transport/BounceGuards.h`, so `BounceExporter`'s own behaviour
and tests are unchanged. Strips are enumerated from the graph in ascending node-id order (the
"else node id" fallback — that ORDER has no dependency on the timeline/track model) and named
`"NN - <name>.<ext>"`.

**Stem naming (FRO55).** `<name>` is the ONE track — `TimelineMidiSource`/`TimelineAudioSource`
("Track In"/"Track Audio") — whose signal feeds that strip, not the strip's own graph-node
instance name (pre-FRO55 this was `"Channel Strip"`, identical across every strip in a patch and
therefore useless as a stem name once "Create channels" makes more than one). `StemSession`
walks the graph upstream from the strip, along signal edges only (mirroring
`synth::ChannelFlows`'s `isSignalEdge` rule: never through an `AttenuverterModule`, never through
a `PortRole::ModCV` input — a modulation cable from an unrelated track must not make that track
"feed" the strip), transitively through the instrument/macro chain, stopping at another
`ChannelStripModule` (that strip already terminates its own track's chain). Exactly one track
found this way contributes its `TimelineDoc` name (e.g. `"Bass"`, `"Audio 1"`); zero or several
tracks — or no `TimelineDoc` at all, e.g. a headless caller — fall back to `"Channel N"` (`N`
matching the strip's own `NN` position, so it agrees with the file's own number and is unique on
its own even when a `TimelineDoc` isn't available). `ChannelStripModule` has no user-given name
field of its own yet; when the mixer-UI work adds one, stem naming is expected to prefer it ahead
of the track walk. `StemSession`'s constructor takes an optional `const TimelineDoc*` for this —
null is exactly the pre-FRO55 "no timeline" behaviour (every strip falls back to `"Channel N"`).

**Two decisions, both load-bearing for "the stems sum back to the mix":**

- **Every strip ALWAYS gets a file — muted or soloed-out included.** The tap sits AFTER the
  strip's own bypass/mute/solo logic, so a muted or non-soloed strip's stem is simply silent for
  exactly the blocks it was silent, never absent. A soloed strip during export therefore never
  changes *which* strips get written, only what most of them contain — silence sums to zero, so
  the mix identity holds in every solo/mute combination.
- **Master's Direct input (cables that bypass every strip) is NOT a stem.** Direct is not a
  channel (§5.10 "what the mixer shows"): summing the stems reproduces the pre-Master MIX bus, not
  the whole signal Master receives. A patch that also uses Direct will not find it isolated in any
  stem — by design, the same way it isn't a mixer column either.

Both decisions assume every enumerated strip is actually routed into Master's Mix bus. Strips are
enumerated by scanning the graph for `ChannelStripModule` nodes regardless of routing (see
`collectStemStrips`), so an orphaned strip (wired to nothing) or one wired somewhere other than
Master's Mix inputs still gets a stem file — the same caveat as Direct above, just the mirror
image: that stem is not part of what sums back to the pre-Master mix, exactly because it never
fed Master's Mix bus in the first place.

No strips in the patch: `StemExporter::hasChannelStrips` lets the UI show a clear message ("No
mixer channels yet - use Create channels in the mixer first") before even opening the dialog,
and `StemSession`'s own setup fails with the identical message as a defense-in-depth backstop.
"Export Stems..." sits immediately after "Export Audio..." everywhere that action is offered
(today: the File menu only), opening `ExportAudioDialog` in a stems mode — destination is a
FOLDER (default `"<project name> Stems"` inside the same `Exports/` base Export Audio uses), same
range/tail/format/rate/bit-depth controls and progress page, no destination-exists collision
prompt (a stems folder is a re-exportable container, not a one-shot file).

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

Reusing `maybeAutoCreateChannelAfterConnect` unchanged also carries over its macro-boxing rule:
the new EQ/Compressor/Strip only join the source's existing macro when every exit source is an
ordinary (non-port) member of the SAME macro (§5.2's boxing paragraph). A genuinely pre-P9-3
track's own node was never a macro member at all, so this condition fails and the sweep's new
channel ships as loose cards on the canvas rather than boxed like T173a/T183/T184's own channels
— an accepted consequence of reusing the connect-triggered builder as-is rather than teaching it
a second, legacy-specific boxing path; also gated independently of `autoCreateChannelOnConnectEnabled`
(the T184 Preferences toggle), since this is an explicit user action, not a live connect — a
legacy project with that preference OFF still gets working channels from this menu entry.

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

### 5.15 Keyboard navigation and accessibility (FRO18)

The mixer panel is its own T159 keyboard focus region (Left/Right walk columns, Up/Down nudge the
focused fader, Enter selects the focused column's macro on canvas, and the rebindable Mute/Solo/
Arm Focused Track actions act on it — the same action ids the Timeline track-header row already
binds), and every fader/pan/meter/M/S control carries a JUCE `AccessibilityHandler` name and value
so VoiceOver can read the mix (e.g. "Lead 1 fader, -3.0 dB"). Full key table, the region's open
predicate, and the accessibility handler details live in
[`docs/shortcuts.md`](shortcuts.md#mixer-column-navigation) (§ Focus regions / § Mixer column
navigation) — this section only cross-links it, per this doc's own "one topic per doc" rule.

---

## 6. AI authorability

`ChannelStrip` and `Master` are internal-only and join `kNonAuthorableModuleTypes`
(`Source/AI/AIStateMapper/AIStateMapperInternal.h`) — a model cannot author either directly, the same rule that
already governs `TimelineMidiSource`, `Rec Tap` and the macro port types. `validatePatch` does not
change; per the root `CLAUDE.md` invariant, it is never relaxed to raise an AI pass rate.

If a future goal is "the AI can build a channel," the correct shape is an app-side **tool/action**
— "create channel" — that the model invokes and the app executes against a validated node set,
never a patch key the model writes directly. This mirrors the same resolution `docs/macros_implementation.md` §6
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

Moved to [`docs/mixer_implementation.md`](mixer_implementation.md) (one topic per doc) — dependency
order, what shipped and how, and the test list for each P9 item: P9-2 engine/solo gate, P9-3
channel creation flows, P9-4 track/channel link, P9-5 mixer panel, P9-6 detachable windows, P9-7
track presets; side tracks P9-8 stem export, P9-9 sends/group buses, P9-10 EQ curve thumbnail,
P9-11 Gate module, T181 mixer accessibility. Item numbers there match every existing
"mixer.md §8 item N" citation elsewhere in the repo.

---

## 9. Out of scope

- **Sends and group buses** — P9-9 (T178), after the mixer panel ships and its own short design
  pass is done.
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
