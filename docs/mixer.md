# Mixer

**Status: DECIDED 2026-09-10 (founder sign-off on D1-D4).** Nothing is implemented yet — no
`ChannelStrip` node, no `Master` node, no mixer panel exist in the codebase yet. This document
records the decided design; §8 is the implementation order that turns it into code. The visual
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
| Instrument track (new track kind) | Yes, automatic | Track In -> the chosen instrument -> default modules -> strip, in one step. |
| MIDI track into an existing instrument | No | Nothing new is created. Its notes play into that instrument's channel; the strip's UI lists the track under its "tracks" line. |

Audio and instrument tracks each automatically get a channel; a MIDI track routed to an instrument
another track already drives does not get a second one — it feeds the existing channel. Splitting
a shared sampler three ways (or triple-counting its CPU/sound) if every track insisted on its own
channel is the failure this avoids; Cubase draws the same line (MIDI tracks carry no audio channel
of their own, instrument tracks do).

**The main workflow: connecting a MIDI track to an unchanneled instrument.** A track's own
mute/solo controls only mean anything once a channel exists for them to drive, so the common path
— add a MIDI track, wire it to a macro or a bare instrument whose audio has never reached a
channel — has to *create* the channel, not just wait for the user to run "Make channel"
separately. When a connection like that makes some audio newly reach a `ChannelStrip`-less path to
Audio Output, a channel is **auto-created at the point that audio reaches the output**: one undo
step, gated by a Preferences toggle that defaults to **ON**. Turning the toggle off restores
today's behaviour (wire freely, no channel appears until asked for).

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

### 5.7 Lane presets

**"Channel template" and "track preset" are one concept: the lane preset.** A lane preset is
`{ track kind, channel chain, strip settings, optional clips }`. Every track kind — **Audio**;
**Instrument** (which also covers a MIDI track that alone drives an instrument, per the link rule
in §5.2) — has its own default preset.

- **Saving.** "Save track as preset..." and "Set as default for `<type>`" are reachable from the
  track header and from the channel's own menu. Preferences -> Mixer lists each type's current
  default in a dropdown.
- **Creating a track.** The `+ Track` control lists available presets grouped by type, so a new
  track can start from any saved preset, not only the type's default.
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

Open unchanged — no automatic migration on load. The mixer offers a single **"Create channels"**
action that wraps every channel-less track's chain into a strip, as one undo step covering all of
them at once.

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

**D1 — Should every lane be a channel? Chosen: channels follow audio (§5.2).** Audio and
instrument tracks get a channel automatically; a MIDI track playing a shared instrument uses that
instrument's channel instead of getting its own. Refined with the auto-create-on-connect workflow,
the track/channel link rule (name sync, live colour sync, M/S drive), and the channel chip.
*Rejected: every track owns a channel (duplicates shared instruments); every track gets a column
(two kinds of column to learn).*

**D2 — What happens to modules a lane shares with others when it becomes a channel? Chosen: keep
them shared (§5.8).** Exclusive modules move into the box; a shared module stays outside via an
auto-created port; a merge point becomes its own bus channel. Refined so that saving or exporting a
channel also captures every outside module that feeds it, walked upstream transitively and stopped
at another channel's strip, arriving as fresh copies on import (§5.7). *Rejected: duplicate
everything shared (breaks phase/CPU/shared-reverb sanity the instant you click); ask each time (a
dialog on every conversion).*

**D3 — What goes in a new channel by default? Chosen: EQ -> Compressor, added bypassed.** Refined
by merging "channel template" and "track preset" into one **lane preset** concept — per-track-kind
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

1. **P9-2 (T172) — `ChannelStrip` + `Master` + the solo gate.** Engine only.
   - `Tests/ChannelStripTests.cpp`: pan balance law is unity at centre for both mono and stereo
     shapes; bypass/mute follow the two-branch contract; shape is fixed at construction and
     rejects a later width change.
   - `Tests/MixerSoloTests.cpp`: soloing one strip silences every other non-soloed strip and the
     Direct input; un-soloing the last soloed strip restores every other strip; solo never mutates
     another strip's mute parameter; `Master` splice is one undo step and undoes cleanly.

2. **P9-3 (T173) — Channel creation flows.** Audio track auto-channel, the new Instrument track,
   MIDI-track auto-channel-on-connect (§5.2's main workflow, Preferences-gated), "Make channel",
   "Create channels" for existing projects, the factory default chain (EQ -> Compressor, bypassed).
   - Tests: an audio/instrument track creation produces exactly one channel in one undo step; a
     MIDI track wired to an unchanneled instrument auto-creates a channel in one undo step when
     the preference is ON, and creates none when it is OFF; "Create channels" on a project with N
     channel-less tracks is one undo step that creates N channels.

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

6. **P9-7 (T176) — Lane presets.** Save/set-default from the track header and channel menu,
   Preferences -> Mixer defaults, `+ Track` preset listing, the outside-module walk-and-copy rule
   (§5.7).
   - Tests: a lane preset round-trips through `validatePatch(trusted=false)` before a trusted
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
- **P9-11 (T180) — Gate module.** No dependency on the rest of P9; only needed if a default lane
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
