# Mixer

**Status: PROPOSED (P9-1).** The model below is the recommendation from the P9-1 design proposal
(founder review, 10 Sep 2026). Founder decisions D1-D4 (§7) are open. Nothing in this document is
implemented — no `ChannelStrip` node, no `Master` node, no mixer panel exist in the codebase yet.
This doc exists to record the proposal precisely enough to build from once D1-D4 are settled; the
visual mock-up (canvas diagram, mixer panel mock, the four decision cards) lives in a separate
page shown to the founder, not in this repo.

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
export (T123) already has to design around: there is no point in the graph, today, where "just
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
is **no Gate module** — see §7/§9.

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

This is **D1**, open (§7): the recommendation is that audio and instrument tracks each
automatically get a channel, but a MIDI track routed to an instrument another track already
drives does not get a second one — it feeds the existing channel. Splitting a shared sampler
three ways (or triple-counting its CPU/sound) if every lane insisted on its own channel is the
failure this avoids; Cubase draws the same line (MIDI tracks carry no audio channel of their own,
instrument tracks do).

On a track that has a channel, the track header's M/S buttons drive that channel's strip — there
is one mute per channel, not one on the track and a separate one on the strip. A plain MIDI track
with no channel keeps today's note-gating behaviour (§1) unchanged.

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

### 5.7 Default modules = channel templates

A new channel is seeded from a **channel template** — an ordinary snippet, inserted the same way
any snippet is (`SnippetManager::insertSnippet`). Preferences -> Mixer holds the user's own
default template, with **separate defaults for audio and instrument channels**. The factory
default is **D3** (§7), open; the recommendation is EQ -> Compressor, added switched off (visible
and one click from live, costing nothing while off) rather than on at neutral settings or omitted
entirely. There is no Gate module today (§9); a gate in the default template needs that module
built first.

### 5.8 Make channel / shared modules

"Make channel" gathers the chain from a track's source to the output into a channel macro plus
strip. Some of the modules in that chain may also feed other lanes (a shared LFO, a shared
reverb). What happens to those is **D2** (§7), open. The recommendation:

- Modules used **only** by this lane move into the new channel's box.
- A module still feeding another lane too (an exclusively-shared modulator, for example) **stays
  outside** and reaches in through an auto-created macro port — the same auto-porting mechanism
  `docs/macros.md` §7 item 7 already built for a cable crossing a macro's boundary at group time.
  The sound does not change at the moment of conversion.
- Where two lanes **merge** into one shared effect before that effect continues downstream, the
  effect becomes its **own bus channel** rather than being duplicated or arbitrarily assigned to
  one lane.
- A **"Duplicate into this channel"** right-click action exists for the case where the user does
  want their own independent copy of a shared module.

### 5.9 Mixer panel & windows

**D4** (§7), open: where the mixer panel lives. Whatever is decided, detaching a panel into its
own window uses **one mechanism shared by Timeline and Mixer**, not two. The reference
implementation for a detached window is the existing hosted-plugin pattern,
`Source/Plugin/Hosting/HostedPluginEditorWindow.{h,cpp}` — including its `addToDesktop=false`
construction path, which is the headless-testable seam that lets a window be built and inspected
in a test with no real desktop window ever created.

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

### 5.12 Saving a lane

Today, a channel's macro already saves as a **snippet** (strip settings included) through the
existing `SnippetManager` path. A **Track Preset** — saving the track itself alongside its
modules, optionally with its clips — is a later addition. Loading one goes through the same
trusted/untrusted pairing every disk-sourced patch data does:
`validatePatch(..., trusted=false)` as a separate gate before a trusted `applyJSONToGraph`, the
`SnippetManager::insertSnippet` / `ProjectBundle::load` reference pairing the root `CLAUDE.md`
names. `"timeline"` stays a reserved field, refused on the untrusted path, exactly as it is today.

### 5.13 Stem export

Every strip is a natural tap point: "single render, parallel writers," the shape T123 already
names for stem export. One render pass writes one file per strip. Test: the resulting stems sum
back to the pre-Master mix.

### 5.14 Existing projects

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

## 7. Open founder decisions

Four decisions are outstanding; everything else in this document is the proposed model, not yet
signed off. Each option below carries the recommendation from the P9-1 proposal.

**D1 — Should every lane be a channel?**
- **A (recommended). Channels follow audio.** Audio and instrument tracks get a channel
  automatically; a MIDI track playing a shared instrument uses that instrument's channel. This is
  the model §5.2 describes.
- **B. Every track owns a channel.** A MIDI track routed to a shared instrument gets its own copy
  of it. One rule to learn, but CPU multiplies per copy and "one drum sampler, many lanes" stops
  working.
- **C. Every track gets a column.** MIDI-only tracks get a slim column with just M/S controlling
  note gating. Lines the mixer up 1:1 with the track list, at the cost of two different kinds of
  column to learn.

**D2 — When a lane becomes a channel, what happens to modules it shares with other lanes?**
- **A (recommended). Keep them shared.** Exclusive modules move into the box; a shared modulator
  stays outside via an auto-created port; a merge point becomes its own bus channel. Sound never
  changes at conversion time. "Duplicate into this channel" is available for an explicit opt-out.
- **B. Duplicate everything shared.** Every channel is fully self-contained, but the sound changes
  the moment you click: a duplicated free-running LFO drifts out of phase with the original, a
  duplicated reverb runs twice, and editing one copy no longer touches the other.
- **C. Ask each time.** A preview lists each shared module with Keep shared / Duplicate,
  defaulting to Keep. Most control, at the cost of a dialog on every conversion.

**D3 — What goes in a new channel by default?**
- **A (recommended). EQ -> Compressor, switched off.** Always present like Cubase's built-in
  strip, but costs nothing and doesn't colour the sound until turned on with one click.
- **B. EQ -> Compressor, on at neutral settings.** Live immediately, at the cost of a flat EQ and a
  1:1 compressor burning CPU on every channel regardless of use.
- **C. Empty.** Cleanest default; anyone who wants more saves a channel template (§5.7).

There is no Gate module yet (§9); if the default strip should include one, it needs to be built
first.

**D4 — Where does the mixer live?**
- **A (recommended). A tab beside the Timeline.** The bottom dock gets Timeline | Mixer tabs — one
  dock to learn, and opening the mixer never further shrinks canvas space. Either can still detach
  to view both at once.
- **B. Its own dock.** A second panel so Timeline and Mixer can both be visible in the main window
  at once, at the cost of canvas space on smaller screens.
- **C. Window only.** The mixer always opens as a separate window — good on two screens, fiddly on
  a single-monitor laptop.

Every option under D4 shares the same detachable-window mechanism (§5.9) regardless of which is
picked.

---

## 8. Implementation order and Tests

1. **`ChannelStrip` + `Master` nodes and the solo switch.** Engine only.
   - `Tests/ChannelStripTests.cpp`: pan balance law is unity at centre for both mono and stereo
     shapes; bypass/mute follow the two-branch contract; shape is fixed at construction and
     rejects a later width change.
   - `Tests/MixerSoloTests.cpp`: soloing one strip silences every other non-soloed strip and the
     Direct input; un-soloing the last soloed strip restores every other strip; solo never mutates
     another strip's mute parameter; `Master` splice is one undo step and undoes cleanly.

2. **Channel flows and templates.** Audio tracks auto-channel; the new Instrument track type;
   "Make channel"; "Create channels" for pre-existing projects; channel templates in
   Preferences -> Mixer.
   - Tests: an audio/instrument track creation produces exactly one channel in one undo step; a
     MIDI track into an existing instrument creates no new channel; "Create channels" on a project
     with N channel-less tracks is one undo step that creates N channels.

3. **Mixer panel, docked.** Columns, faders, meters, insert lists, track header M/S wired to
   strips.
   - Tests: track header M/S toggles the bound strip's mute/solo state and nothing else; a
     branching insert chain renders read-only with "Edit on canvas" rather than a reorderable
     list.

4. **Detachable windows for Timeline and Mixer.** One mechanism for both, keyboard focus scoped
   per window (T158 already expects this).
   - Tests: a detached mixer window built with `addToDesktop=false` behaves identically to the
     docked panel in a headless test; keyboard focus in one detached window does not leak into the
     other.

5. **Track presets.** Save and load a lane with its modules (§5.12).
   - Tests: a track preset round-trips through `validatePatch(trusted=false)` before a trusted
     apply; a hand-edited preset containing a `"timeline"` key is refused.

6. **Stem export (T123).** One render, one writer per strip.
   - `Tests/StemExportTests.cpp`: the written stems sum back to the pre-Master mix within a
     tolerance; a soloed strip during export does not affect which strips get written.

---

## 9. Out of scope

- Sends and group buses. The model already accommodates them later — a send is a tap on a strip
  feeding a bus channel — but they are left out of the first mixer.
- An EQ curve thumbnail on a channel column (Cubase-style), suggested as a later polish pass.
- A Gate module. Only needed if D3 wants one in the default channel template.
- An accessibility epic covering the mixer (column navigation, rebindable M/S on the focused
  column, fader nudge, screen-reader labels for faders/meters) alongside T158's app-wide keyboard
  focus work — proposed as its own epic rather than folded into P9, to keep P9 about mixing.

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
