# Mixer

What a mixer channel is, how one comes into existence, and the rules every channel obeys. This doc
is the area hub; the topics with a surface of their own live beside it:

- [`docs/mixer/panel.md`](panel.md) — the mixer panel, its columns, placement, detachable windows,
  the EQ thumbnail and keyboard navigation.
- [`docs/mixer/track-presets.md`](track-presets.md) — what a track preset is, how it is saved,
  loaded and defaulted.
- [`docs/mixer/sends-and-buses.md`](sends-and-buses.md) — sends as strip-owned output legs, group
  buses, and the per-leg solo mask.
- [`docs/mixer/stem-export.md`](stem-export.md) — one render pass, one file per strip.
- [`docs/mixer/meters.md`](meters.md) — the peak latch, the dB scale, ballistics and colour zones.
- [`docs/mixer/fader.md`](fader.md) — the fader's taper and drag conventions.

---

## What a channel is

> **The `ChannelStrip` node is the channel; a macro is the box you keep it in.**

The mixer panel enumerates `ChannelStrip` nodes in the live graph, plus a synthetic **Direct** column
and **Master**. Nothing else is a channel.

**Why a node and not the macro around it.** The graph is free-form: any node can wire to any other,
and nothing about a node's position or type says "this is a channel". Tying channel identity to a
single node is what makes the question answerable at all. Three alternatives were weighed and each
breaks somewhere specific:

- **The macro is the channel.** A macro and a channel would then have a two-part identity that can
  disagree. Ungrouping removes a macro's *ports* but never its ordinary members, so ungrouping a
  channel macro would leave the fader, pan and solo controls with no box around them, still alive,
  still processing, just no longer presented as a channel. A box with no strip is just a macro; a
  strip with no box is still a channel.
- **A hidden per-track sub-graph.** This is the container-node option the macro I/O design already
  rejected ([`docs/macros/macros.md`](../macros/macros.md#macro-ports-are-proxy-nodes-on-a-flat-graph)),
  for the same two reasons: it moves member nodes out of uuid-addressable space, orphaning timeline
  bindings and automation lanes that point at them, and it opens a second, nested
  latency-compensation domain on top of the one hosted plugins already strain.
- **Anything wired to Audio Output.** There is no fixed location to put a fader, mute or solo
  control — a signal "wired to the output" is a cable, not a node with state. And once two such
  signals have summed upstream of the output, solo can no longer separate them; there is nothing left
  to silence independently.

**The mapping between timeline tracks and instruments is many-to-many** — several MIDI tracks can
drive one sampler, one track can layer two synths — so "one channel per track" does not hold either.
See [Channels follow audio, not tracks](#channels-follow-audio-not-tracks).

## Node types

Two `ModuleType` entries: **`ChannelStrip`** and **`Master`**. Both are internal-only, with the same
three exclusions as `TimelineMidiSource`, `Rec Tap` and the macro port types: no library row, no
replace-menu entry, never model-authorable (`kNonAuthorableModuleTypes`,
`Source/AI/AIStateMapper/AIStateMapperInternal.h`). **Adding them to the factory without adding them
to that set fails `AIStateMapperTest.AuthorableModuleTypesGolden`** — that failure is intended and is
how the golden test catches this exact addition.

`Master` is spliced in front of Audio Output **when the first channel is created**, following
`ensureMasterRecordTap()`'s existing pattern: singleton by construction, one compound undo step,
every audio connection that fed the output re-routed through it. The ordering becomes:

```
... channel strips ... -> Master -> [Master inserts] -> Rec Tap -> Audio Output
```

The bracketed part is empty until the user adds something to it — see [Master inserts](#master-inserts).

`Master` exposes a **Direct** input block that receives whatever cables previously went straight to
the output — the same re-routing `ensureMasterRecordTap()` already does for the record tap, one level
upstream of it.

**Layouts.** A strip carries 5 raw channels a side: Left ch0, Right `kRightBase` = 4, ch1 to ch3
reserved, with `gain` (dB), `pan` and `muted` as parameters and its shape plus solo flag in trusted
extra state. It declares `kMaxSends` send slots on top of that — see
[`docs/mixer/sends-and-buses.md`](sends-and-buses.md#channel-layout). `Master` carries Mix L and R on
ch0 and ch1 and Direct L and R on ch2 and ch3, sums Direct in before the fader, and under bypass is a
unity sum that still keeps Direct.

The splice itself is `synth::ensureMasterNode` (Core, `Source/Mixer/MasterSplice.h`) wrapped in one
`recordCombinedChange`; the non-undoing `synth::spliceMasterNode(graph, position)` is the mechanism it
and every larger transaction call directly.

## Channels follow audio, not tracks

| Track kind | Own channel? | What gets created |
| --- | --- | --- |
| Audio track | Yes, automatic | Track Audio, default modules, strip, boxed as one channel when the track is added. |
| Instrument track | Yes, automatic | Track In, the chosen instrument, default modules, strip, in one step. |
| MIDI track into an existing instrument | No | Nothing new is created. Its notes play into that instrument's channel; the strip's UI lists the track under its "tracks" line. |

Audio and instrument tracks each automatically get a channel; a MIDI track routed to an instrument
another track already drives does not get a second one — it feeds the existing channel. **Why:**
insisting on a channel per track would split a shared sampler three ways and triple-count its CPU and
its sound. Cubase draws the same line — MIDI tracks carry no audio channel of their own, instrument
tracks do.

**An instrument track is a `TrackKind::Midi` track, not a new track kind.** A `TrackKind::Instrument`
enum value would mean format, serialisation and kind-badge changes, and an instrument track is exactly
what the "MIDI track into an existing instrument" row above already describes once the user draws the
cable by hand: a Track In feeding exactly one instrument. "+ Track -> Instrument" just draws that
cable and builds the channel automatically instead of making the user do both steps. A future
`TrackKind::Instrument` would have to migrate every track this flow has already created.

### Auto-creating a channel on connect

A track's own mute and solo controls only mean anything once a channel exists for them to drive, so
the common path — add a MIDI track, wire it to a macro or a bare instrument whose audio has never
reached a channel — has to *create* the channel rather than wait for the user to run "Make channel"
separately.

When a connection makes some audio newly reach a `ChannelStrip`-less path to Audio Output, a channel
is **auto-created at the point that audio reaches the output**: one undo step, gated by a Preferences
toggle (`mixerAutoCreateChannelOnConnect`) that defaults to **ON**. Turning the toggle off restores
wire-freely behaviour, with no channel appearing until asked for. An instrument that already has a
channel on its path gets nothing new — connecting a second MIDI track just wires straight in.

**The link rule.** A track and a channel are **linked** exactly when the track is the channel's
**only** source — an audio track, an instrument track, or a MIDI track that alone drives an
instrument. Linked means:

- **Names sync both ways.** Renaming the track renames the channel (strip and macro), and the
  reverse.
- **Colour syncs live.** The track colour, the channel's macro colour and the mixer column colour
  always match, and they update **while the colour picker is still being dragged**, not only on
  commit. `synth::ui::ColourPickerPopup` (`Source/UI/Chrome/ColourPickerPopup.h`,
  [`docs/layout/colour-overrides.md`](../layout/colour-overrides.md#colour-picker-popup)) separates a
  live-preview callback — fires on every drag or favourite click, writes straight into its target
  with **no undo step** — from a commit-once callback that fires once, on close, as the real edit. A
  linked track and channel fan the SAME preview write out on every drag frame; a cancel restores
  every destination to its original colour, exactly like the single-target case; a commit is **one**
  undo step covering all of them, not several separate edits, matching the existing "one `Cmd+Z`
  undoes a dozen preview colours" semantics. It fans out over TWO stored targets, not three: **a
  channel's colour IS its macro's colour**, so the mixer column reads the macro rather than a third
  copy on the strip.
- **The track header's M and S drive the strip.** Not note gating — the linked channel's mute and
  solo. A redirect, not a mirror: the track's own `muted`/`soloed` stay false, so there is only one
  copy.

A channel fed by **more than one** track (Kick, Snare and Hats into one sampler) is **not** linked: it
keeps its own independently chosen name and colour, lists every track that feeds it, and each of
those tracks' M and S controls keep plain note-gating behaviour rather than touching the strip. The
link **breaks** the moment a second track starts feeding a previously linked channel, and **re-forms**
if the channel goes back down to exactly one source track.

**The rule is a pure query, never a cached flag.** `synth::resolveTrackChannelLink`
(`Source/Mixer/TrackChannelLink.h`) walks a track's bound node forward to the first
`ChannelStripModule` it reaches, then walks that strip's feeders back out: exactly one feeder, and it
is this track, **is** the link — so break and re-form need no bookkeeping. The backward walk is the
same `upstreamTrackSources` query stem naming asks
([`docs/mixer/stem-export.md`](stem-export.md#stem-naming)), so `channelDisplayName` answers "which
track names this" once for both.

**One interface, one collaborator.** `synth::ui::TrackChannelLinkSurface` is all a track header sees;
`TrackChannelLinkController` implements it, owned by `MainComponent`, which gains only the member and
a one-line `TrackHeaderHost::getChannelLinkSurface()` override (the `GraphCanvasHost` seam pattern).
Every "not linked" answer is false or null, so the header falls through to its existing behaviour
unchanged. Renaming the channel joins `renameMacro`'s own transaction through
`MacroGroupController::recordMacroRenameHook` rather than becoming a second `Cmd+Z`; a linked M or S
is one graph-snapshot step, and solo always goes through the engine entry below.

**Two consequences worth stating.** A channel's colour is its macro's colour and there is no colour
field on the strip, so an **unboxed** linked channel syncs neither name nor colour while its M and S
still drive the strip. And soloing a linked channel silences every other channel, a shared one
included, even when that shared channel's own track is note-gate soloed: that is a DAW mixer solo,
not a bug.

### The channel chip

Every track header whose notes or audio play into a channel — linked or not — shows a small **channel
chip**: the channel's name plus a compact meter. Clicking it reveals that channel's column in the
mixer (`revealChannelForTrack`). **No extra audio track is ever created by having a chip; the chip is
a pointer, not a new signal path.** The timeline holds what you play; the mixer holds what you hear.

ONE 15 Hz `juce::Timer` on `TimelinePanelComponent` ticks every header, never one per row; it idles
while hidden, reads only `getChannelMeterPeak` (a cached strip id, not a graph walk), and each chip
repaints only past a coarse threshold ([`docs/layout/rendering.md`](../layout/rendering.md)).

## Solo is a render-time gate

Solo is a **switch applied during playback**, never something you automate, and it **must not be
implemented by fanning `setMuted()` across other strips** — `setMuted()` is a `setValueNotifyingHost`
parameter write: undoable, host-visible, and it would silently clobber whatever mute state the user
had already set on those strips.

Each `ChannelStrip` carries its own solo flag: **not** an `AudioParameter`, **not** host-visible,
**not** automatable, persisted only in the strip's trusted extra state (`ModuleBase::setExtraState`,
applied on the trusted path only, per the root `CLAUDE.md` invariant). A shared "is anything soloed?"
count, owned by the engine, is readable from the audio thread with no locks and no allocation. While
the count is nonzero, Master's Direct input outputs silence, and every strip silences the output legs
that do not contribute to the soloed path.

**Per-leg, not per-strip.** Sends make "is this strip soloed?" the wrong question, so the count
answers only "is anything soloed?" and a second, per-strip **audible-leg mask** answers "which of
this strip's legs may be written". `synth::computeSoloAudibleLegs`
(`Source/Mixer/SoloAudibleSet.{h,cpp}`) recomputes the whole map on the message thread inside
`AudioEngine::refreshSoloGate`, and each strip reads its own mask once per block. With nothing soloed
every mask is all-ones and the behaviour is byte-for-byte what it was before the mask existed. The
full rule and its fixed-point closure are in
[`docs/mixer/sends-and-buses.md`](sends-and-buses.md#solo-is-a-per-leg-audible-mask).

**Solo acts after each strip's own inserts**, so a shared reverb or a decaying tail on a non-soloed
channel is genuinely silenced rather than continuing to ring into a mix other channels can still
hear.

**Why this is better than the source-level gating the timeline already has.** Track mute and solo are
**source gating**: both `TimelineMidiSourceModule` and `TimelineAudioSourceModule` compute the
identical condition — `track->muted || (snapshot->anySoloed && !track->soloed)` — and suppress
themselves when it is true, with `TimelineSnapshot::anySoloed` computed once per snapshot and
published to the audio thread via `EpochExchange` alongside every other timeline field. That stops
notes and clips *at the source*, so anything downstream that is *shared* with other tracks, or that
has a tail, is not isolated: the signal it already emitted keeps ringing, and a shared downstream
processor still hears whatever other unsoloed tracks fed it. There is no point in the graph, without a
strip, where "just this track's contribution" is actually isolated.

**The shared count is an engine-owned atomic, recounted on the message thread** by scanning the graph
inside `publishTimeline` — so a deleted or undone soloed strip cannot leave the mix stuck — and
carried per render pass on `TransportService`, like the input-monitoring flag. A graph-replacing path
that skips `publishTimeline` calls `AudioEngine::refreshSoloGate()` itself. **Solo applies to a
bypassed strip too:** bypass is the strip's own gain and pan going dry and the gate is layered on top,
or the strip's card bypass would leak into a soloed mix.

## Mono and stereo

A strip's channel shape is fixed **at creation**, derived from the chain it terminates — never
inferred later from what gets plugged into it. This is the same rule macro ports follow
([`docs/macros/ports.md`](../macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed)),
applied to strips.

Strip output is always stereo. Pan is a balance law with unity gain at centre; a mono strip feeds both
output legs from a single input jack. The right leg follows the `kRightBase` convention
(`Source/Modules/CLAUDE.md`) — **never ch1**, which stays reserved for CV on the split-block voice
modules.

**Changing a strip's shape means replacing the strip**, one undo step, not widening a live node — the
same invariant that governs every fixed-channel-count module in this codebase. When a flow wraps a
chain that ends poly, it inserts a `VoiceMixerModule` ahead of the strip so the strip itself only ever
receives a summed signal.

## Bypass and mute

The strip follows the root `CLAUDE.md` two-branch bypass and mute contract like every other
signal-processing module. **The mixer exposes no bypass control on the strip itself** — bypass is not
a mixer-level idea.

A channel macro's **Bypass** fan-out skips the source node or nodes and the strip: "bypass" on a
channel means "bypass the inserts", so the chain's own effects go dry while the source keeps producing
and the strip keeps passing signal through. The macro's **Mute** fan-out, by contrast, includes the
strip — muting the macro mutes the whole channel. See
[`docs/macros/ports.md`](../macros/ports.md#bypass-and-mute).

## Inserts in a free-form graph

A mixer column lists its channel's modules **in signal order**, read off the chain between the source
and the strip. When that chain is a straight line, the column supports adding, reordering and removing
modules directly in the mixer. When it branches — more than one path between source and strip, or a
shared node feeding more than one channel — the column instead shows the module list read-only with
an "Edit on canvas" link, rather than presenting a branching chain as if it were a reorderable list.

**A bus has no source to walk from.** A bus column carries no feeding track, so
`buildBusInsertsForColumn` walks its chain the other way: BACKWARD from the strip along signal
predecessors, collecting the EQ and Compressor "Add bus" builds — or whatever the user has since
rearranged — until it reaches a node with no predecessor of its own. A send into the bus lands on the
same strip input channels an insert's own output would and **is excluded from this walk**: a feeding
strip is a source, never an insert, the same rule `findStripsFeedingStrip` applies walking the
opposite direction. So an active send never marks a bus's own chain "branching" and is never mistaken
for one of its inserts. `MixerColumn::sourceNodeId` therefore stays invalid for a bus, which is also
why **`MixerInsertList::moveRow` refuses to move a row to the very front of a bus's chain** — there is
no external predecessor to splice against, and `reorderInsert`'s second step has no rollback of its
own.

**An empty insert list and the "Edit on canvas" link must not paint on the same row.** The link
anchors at `jmax(1, entries_.size()) * kRowHeight` in both `paint()` and `mouseDown()`, matching
`getPreferredHeight()`'s own maths; anchoring at `entries_.size() * kRowHeight` puts it at row 0 when
the list is empty, which is exactly where the "(no inserts)" placeholder draws.

## Master inserts

**Master has an insert list like a strip's, off by default.** Right-click the Master column's list to add a
Limiter, EQ, Compressor, Gate and the rest of the mixer's add menu; move and remove work the same way,
and each gesture is one undo step. Nothing is added unless the user asks.

**Post-fader, one list.** Master's inserts sit AFTER its fader — the chain is
`strips -> Master (fader inside MasterModule) -> [inserts] -> Rec Tap (if any) -> Audio Output` — the way Pro
Tools, Cubase and Studio One put the limiter and dither, so a fader move can never push the signal past
the ceiling. The graph is flat in both host modes, so the plugin behaves exactly like the standalone app.
The known cost of post-fader: a compressor's effective threshold moves during a master fade-out, since
the level it sees falls with the fader.

**The Limiter module is a level maximiser, not a hard ceiling.** Its threshold carries automatic makeup
gain ([`docs/modules/fx-modules.md`](../modules/fx-modules.md#limiter-module)), so a Master Limiter holds the
signal under full scale but a -6 dB threshold does not hold it at -6 dBFS.

**How the chain is found.** `buildMasterInsertsForColumn` (`Source/Mixer/MixerModel/MixerModelInserts.cpp`)
walks FORWARD from Master's output along signal edges until it reaches the Rec Tap or the Audio Output
node — the terminator, recorded as `MixerColumn::chainEndNodeId` and never itself an insert. The nodes
in between are the inserts, in signal order; `MixerColumn::sourceNodeId` is Master's own node, so the
same `MixerInsertList` splice code that serves a strip runs unchanged (predecessor = the last entry or
Master, successor = the terminator). A node with more than one signal successor, or an insert with more
than one signal predecessor, makes the list read-only with "Edit on canvas", exactly as for a strip. Other
feeders on the terminator itself do NOT: a hand-wired source landing on the Rec Tap or Audio Output does
not stop the user adding a Limiter in front of Master's own connection. A Master whose output never
reaches a terminator has nothing to splice against, so its list is empty and read-only, and "Edit on
canvas" selects Master itself.

**Rec Tap ordering.** The tap is spliced in front of Audio Output whenever it is first needed, and
`ensureMasterRecordTap()` re-routes whatever fed the output into it — so a Limiter added earlier ends
up ahead of the tap, and one added afterwards goes in between Master and the tap. Either way the take
records the limited signal and the list stays exactly what the user added.

**The column's meter shows the level LEAVING the chain** — see
[`docs/mixer/meters.md`](meters.md#the-master-meter-and-master-inserts).

## Make channel and shared modules

"Make channel" gathers the chain from a track's source to the output into a channel macro plus strip.
Some modules in that chain may also feed other tracks (a shared LFO, a shared reverb) — **these stay
shared**:

- Modules used **only** by this track move into the new channel's box.
- A module still feeding another track too **stays outside** and reaches in through an auto-created
  macro port — the same auto-porting mechanism a cable crossing a macro's boundary at group time uses
  ([`docs/macros/auto-ports.md`](../macros/auto-ports.md#auto-creating-ports-when-grouping)). **The
  sound does not change at the moment of conversion.**
- Where two tracks **merge** into one shared effect before that effect continues downstream, the
  effect becomes its **own bus channel** rather than being duplicated or arbitrarily assigned to one
  track.
- A **"Duplicate into this channel"** right-click action exists for the case where the user does want
  their own independent copy of a shared module.

**Entry points.** The track header's right-click **Make Channel** (acting on the track's bound node),
and **Make Channel** in the canvas right-click menu with a selection and in every module card's
right-click menu. `synth::resolveChannelSource` picks the chain the selection belongs to — a selected
track source, else the one track source upstream of it, else the one root upstream of it; no item
when ambiguous. The item is **disabled, not hidden**, once the chain has a channel.

**"Which modules does this track use" is `synth::planMakeChannel`'s reach walk**: every MIDI edge and
every audio edge not landing on a modulation (`PortRole::ModCV`) pin, never through a modulation
attenuverter, through strips and macro ports, stopping at Audio Output, Record Tap and Master. A node
this track reaches that no other track source reaches is **own** and moves in; one another track also
reaches is **shared** and never moves; a node no track reaches (an LFO) moves in only when every
consumer of it, looking through attenuverters, already does — otherwise it stays outside and the
group-time auto-port pass fronts its cable.

The new strip takes over the own region's exits to the output, plus every audio edge into the shared
region carrying that same signal — with no exit at all, every such edge, provided each side carries
one consistent signal; **otherwise the action is refused with a status message**, and a different
signal into a shared module stays a pre-strip send. Each shared merge head that still reaches the
output without a strip gets its own **bus channel** ("`<module>` Bus") holding the shared nodes
downstream of it; the track's own strip feeds the bus's original input pins, **never Master directly,
so nothing is summed twice**. **A node that would move but is already in a macro refuses the whole
action** (the flat model).

**The conversion is sample-identical.** Strip, bypassed Gate, EQ and Compressor, Master and macro ports are
all unity pass-throughs at their defaults, proven offline for the shared-LFO and merge cases. The one
intended exception is a chain ending on a poly jack, which gets a Voice Mixer ahead of the strip
(`addVoiceMixerForPolyInstrument`) and so carries every voice where a bare poly jack wired to a mono
input carried voice 0 only.

**Duplicate into Channel** is offered on a module outside every macro that feeds a channel macro —
directly, through its port, or through a modulation attenuverter — and something else too: one item
naming the channel, or a submenu when it feeds several. The copy carries parameters and extra state
(the Duplicate path), takes over every cable into that channel, inherits the original's inputs (a
modulation into it is re-created with the same amount), and joins the macro; the other consumers stay
on the original. It joins through the incremental membership passes
([`docs/macros/menu-and-membership.md`](../macros/menu-and-membership.md#incremental-port-splicing-on-a-membership-change)).

**Each action is ONE graph plus timeline plus macro undo step, followed by the reconcile pass.** Core
lives in `synth::planMakeChannel` (a pure query: own, shared and side-input regions, exits, strip
crossings, bus heads, `needsChannel`, `refusal`), `synth::buildMakeChannel` (the graph rebuild — the
shared `buildChannelChain` gained a sink so a strip can feed a merge point's input pins instead of, or
besides, Master's Mix) and `synth::resolveChannelSource`, all in `Source/Mixer/ChannelFlows/`.
`MainComponent::makeChannelForNode`/`duplicateIntoChannel` wrap each action and check the refusal or
no-op first, so neither pushes an empty undo step, then run
`reconcileTimelineAfterGraphChange()`.

## Building a channel

Every flow that creates a channel shares one chain builder rather than a parallel implementation.
The factory default chain is **Gate (bypassed) -> Parametric EQ (bypassed) -> Compressor (bypassed)
-> Channel Strip (Stereo) -> Master (Mix)**, added bypassed so a new channel costs no CPU until the
user engages it.

**Why Gate, EQ and Compressor rather than nothing, or live at neutral settings.** Live-at-neutral
means paying CPU on every channel regardless of use; empty means no baseline at all. Bypassed gives
the baseline with neither cost. FRO226 added Gate ahead of EQ as the chain's first stage — a noise
gate belongs upstream of tone-shaping and dynamics, so it sees the source signal directly.

### The factory default chain

`synth::buildDefaultAudioChannel` (`Source/Mixer/ChannelFlows/ChannelFlows.h`) splices Master
(`synth::spliceMasterNode`, reusing the existing singleton after the first channel) and wires the
strip into it. "+ Track -> Audio Track" builds the whole channel — Track Audio, Gate, EQ, Compressor,
Strip, Master — in ONE undo step (`AppUndoManager::recordGraphTimelineAndMacroChange`), and
`{Track Audio, Gate, EQ, Compressor, Strip}` are boxed into ONE collapsed macro named after the track
(`GraphEditor::addMacroForMembers`).

**Master stays OUTSIDE the macro, and the Strip to Master cable is left a plain graph edge,
deliberately never a macro port.** `spliceMasterNode`/`ensureMasterNode` classify a re-routed feed as
Mix or Direct by checking whether the connection's SOURCE NODE is itself a `ChannelStripModule`; a
`MacroOutlet` sitting between the strip and Master would make the source node a `MacroOutlet` and
defeat that check, which the whole-project sweep below depends on.

`buildDefaultAudioChannel` takes a trailing `sourceRightChannel` parameter (default 1, Track Audio's
contiguous pair unchanged) so a split-block source passes its own
`ModuleBase::rightAudioLegChannel()` instead of assuming ch1 — Oscillator and Wavetable's right leg
is never ch1 (`Source/Modules/CLAUDE.md`).

### An instrument track

"+ Track -> Instrument" opens a submenu of audio-producing MIDI instruments — Oscillator, Wavetable,
Sampler, **deliberately not every `isMidiInstrumentType()` member**, since Poly MIDI, Sequencer and
Poly Sequencer generate CV or MIDI rather than audio — and builds Track In, the chosen instrument, and
the same default chain, in ONE undo step (`MainComponent::addInstrumentTrack`).
`{Track In, instrument, [Voice Mixer if poly], Gate, EQ, Compressor, Strip}` are boxed into one
collapsed macro; Master stays outside it for the same classification reason.

`synth::addVoiceMixerForPolyInstrument` sums a poly instrument's ch0 to ch7 into a Voice Mixer first,
gated on the instrument's own live `poly` parameter and **never forced on**: a factory-created
instrument defaults to poly off, so this branch is a no-op on the default path and exists for when it
is not. A poly instrument's raw ch0 to ch7 cannot feed `buildDefaultAudioChannel` directly, which
wants one stereo pair, which is why the Voice Mixer comes first. The full menu, its ids and its
headless seam are in [`docs/timeline/add-track.md`](../timeline/add-track.md).

### Envelope and VCA for a raw instrument

Oscillator and Wavetable have no envelope of their own, so a held — or even released — note drones
forever. `synth::addEnvelopeAndVCAForRawInstrument` (`Source/Mixer/ChannelFlows/ChannelFlows.h`)
inserts an ADSR and a VCA ahead of the rest of the chain:
`Track In --MIDI--> ADSR --Env--> VCA's Gain CV`, then `chainSource -> VCA Audio -> Gate`.

**Inserted AFTER any Voice Mixer stage, never before it**, and both nodes are forced non-poly
regardless of the instrument's own `poly` parameter: `ADSRModule`'s poly branch is CV-gate-only — it
never reads the MIDI note-on and note-off fallback that drives its non-poly branch — so a poly ADSR
fed only Track In's MIDI would output a permanent zero envelope. ADSR's `sustain` is explicitly set to
0.7, independently of its own stock default, so this auto-wired chain settles at a musical level;
VCA's `gain` is overridden to 1.0 so the envelope alone governs level. **Sampler is untouched** — it
already has its own one-shot playback envelope.
`{Track In, instrument, [Voice Mixer if poly], ADSR, VCA, Gate, EQ, Compressor, Strip}` join the same one
collapsed macro.

### A poly instrument gets a per-voice envelope

When the instrument's `poly` parameter is on at instrument-track creation time — set
programmatically, or via the menu's "(Poly)" entries — a **Poly MIDI** node (the existing per-voice
MIDI-to-CV converter, [`docs/modules/modules.md#poly-midi-module`](../modules/modules.md#poly-midi-module))
goes between Track In and the instrument instead of raw MIDI, and both ADSR and VCA are genuinely poly
(`synth::addPolyEnvelopeAndVCAForInstrument`):

```
Track In --MIDI--> Poly MIDI --Pitch(ch0-7)--> instrument's poly Pitch CV in (ch0-7)
                    Poly MIDI --Gate(ch8-15)--> ADSR's poly Gate CV in (ch0-7)
ADSR poly Env (ch0-7) --> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase)
instrument's poly Audio L (ch0-7) --> VCA's poly Audio L in (ch0-7)
```

That gives each voice its own independent envelope instead of one shared mono envelope gating the
whole poly-voice sum — releasing one voice's note leaves another held voice's envelope untouched,
which a single shared envelope could never do.

**No separate Voice Mixer is inserted for this case**: the poly VCA already sums all 8 gated voices to
a stereo-shaped pair itself (ch0 the left sum, ch1 its own legacy duplicate), so
`addVoiceMixerForPolyInstrument` is skipped entirely when this branch fires. It still fires for a poly
instrument this does not apply to, such as a poly Sampler. **The instrument's R-octet is deliberately
NOT wired into the VCA's own Audio R poly block (ch16 to ch23)** — the same known stereo limitation
`addVoiceMixerForPolyInstrument` documents for the non-envelope poly path.
`{Track In, instrument, Poly MIDI, ADSR, VCA, Gate, EQ, Compressor, Strip}` join the same one collapsed
macro. The menu's "Oscillator (Poly)" and "Wavetable (Poly)" entries set the new instrument's `poly`
`AudioParameterBool` via `synth::setProcessorPoly()` before this branch check runs; Sampler has no
poly parameter and therefore no poly entry.

### A hosted plugin as the instrument

"+ Track -> Instrument -> Plugin" lists the scanned INSTRUMENT plugins
(`juce::PluginDescription::isInstrument`; effects are filtered out and never offered here). Choosing
one builds the exact same Track In, instrument, default chain, Master flow, boxed into one collapsed
macro, as ONE undo step — except the instrument is a hosted `HostedPluginModule` rather than a factory
module, and **the load is ASYNCHRONOUS**.

`MainComponent::addInstrumentPluginTrack` stages a bare Hosted Plugin module OFF the graph, starts its
load, and **only opens the undo transaction once `HostedPluginModule::onLoadCompleted` reports
success** — a completion hook that fires once per load attempt, covering all three exits (publish, an
outright backend failure, and the over-max refusal inside `publishInstance()`), none of which
`onInstancePublished`/`onInstanceChanged` alone would catch. **A failed or refused load touches
neither the graph nor the undo stack.** The staged module is owned by
`MainComponent::pendingInstrumentPluginLoads_` — **never by a `shared_ptr` looped back through its own
`onLoadCompleted`, which would keep the object alive forever** — and is torn down on the next
message-loop turn.

**No ADSR and VCA is added** — a hosted synth has its own envelope — and the shared tail gets that for
free: it keys the Oscillator, Wavetable and poly branches off the instrument NODE's own `getName()`
and `poly` parameter, and a `HostedPluginModule` is named "Hosted Plugin" and declares no `poly`
parameter, so every one of those branches falls through to the plain path automatically.

**`ModuleBase::rightAudioLegChannel()` is read only AFTER the load completes**, since the module's
real channel count is not known before then. `HostedPluginModule` overrides it from the published
instance's real output count: ch1 once there are two or more outputs, ch0 duplicated onto both legs
for a genuinely mono instance. The base class's `hasDualIOParameter()`-gated default would read -1
forever, since this module never registers a Dual I/O parameter.

**Four rules the picker itself follows.** A click resolves against a SNAPSHOT of the options list
captured when the menu was built (`TimelinePanelComponent::instrumentPluginMenuSnapshot_`), never by
re-running the collector at click time, because a background `PluginScanService::runScan` finishing
between open and click could otherwise resolve the click against a different plugin than the menu
showed. Every entry's label always carries its format — "Massive (VST3)", "Massive (AU)" — so a VST3
and an AU build of the same product never show as two identical rows. The picker excludes this app's
own VST3 and AU build (matched against `synth::branding::kProductName`/`kCompanyName`) so it never
offers to host itself; the library sidebar is a separate collector and stays unfiltered. And **a load
still in flight when the document is replaced is dropped rather than landing in the FRESH document**:
`MainComponent::documentGeneration_` is bumped once per replacement that actually proceeds — never on
Cancel or a failed Save — `addInstrumentPluginTrack` captures it when the load starts, and
`onLoadCompleted` compares it before building a track.

### Creating channels in an existing project

**Existing projects open unchanged — no automatic migration on load.** "Create Channels" on the
"+ Track" menu (`TimelinePanelComponent::kCreateChannelsMenuId`) wraps every channel-less track's
chain into a strip, as one undo step covering all of them at once.

**Why it lives on the "+ Track" menu.** It acts on every track in the project at once, so it belongs
beside the project-wide channel-creating actions (Audio Track, Instrument Track) rather than off a
single track header or a per-column control.

`MainComponent::createChannelsForExistingTracks()` walks `TimelineDoc::getTracks()`, resolves each
track's own bound node (`bindingUuid`, a "Track Audio" or a "Track In"), and hands every one to
`GraphEditor::createChannelsForUnchanneledTracks()`, which runs the SAME private
`maybeAutoCreateChannelAfterConnect` the connect hook uses — the same
`synth::findUnchanneledOutputFeeds`/`buildChannelForFeeds` pair — once per track inside ONE
`AppUndoManager::recordGraphTimelineAndMacroChange` transaction. **A track that already reaches a
`ChannelStripModule` is silently skipped**, untouched rather than re-channeled. The menu entry is
disabled, and the action is a no-op pushing nothing to the undo stack, when no track needs a channel
(`hasTracksNeedingChannels()`, the same per-track query short-circuiting on the first hit).

**Two channel-less tracks sharing one unchanneled instrument come out as exactly one channel:** the
first track's call builds it and removes the exit edges, so the second track's call finds
`findUnchanneledOutputFeeds` already empty for that instrument and does nothing — no extra
bookkeeping, since it is the same per-node builder live connects already run.

**Reusing that builder unchanged carries over its macro-boxing rule**, and that has a visible
consequence: the new Gate, EQ, Compressor and Strip only join the source's existing macro when every exit
source is an ordinary, non-port member of the SAME macro. A genuinely legacy track's node was never a
macro member at all, so that condition fails and the sweep's new channel ships as loose cards on the
canvas rather than boxed. That is an accepted consequence of reusing the connect-triggered builder as
is rather than teaching it a second, legacy-specific boxing path. **The action is also gated
independently of `autoCreateChannelOnConnectEnabled`** — this is an explicit user action, not a live
connect, so a legacy project with that preference off still gets working channels from this entry.

**The auto-channel core, for both callers.**
`synth::findUnchanneledOutputFeeds(graph, start)` (Core, no AppUI, `GraphEditor` or `AppUndoManager`
dependency) does a forward BFS from the just-connected node over audio AND MIDI edges, **never
expanding past** a `ChannelStripModule` (already channeled: stop, no exit), a `RecordTapModule`, a
`MasterModule` or the `AudioGraphIOProcessor` named "Audio Output", and **never traversing INTO a
hidden `AttenuverterModule`** — a mod or CV leg is not an audio-reaching-the-output path. An "exit" is
an edge landing on Audio Output ch0 or ch1, Rec Tap ch0 or ch1, or Master's
`kDirectLeft`/`kDirectRight`; **`kMixLeft`/`kMixRight` are deliberately NOT exits**, since only a
`ChannelStripModule`'s own output legitimately lands there.
`synth::buildChannelForFeeds(graph, exits, layout)` **removes every exit edge FIRST**, so
`spliceMasterNode`'s own "sweep everything already feeding Audio Output or Rec Tap into Master"
behaviour — run as part of building the chain when no Master exists yet — never re-captures an edge
this call is about to own, then builds the default chain via the same internal `buildChannelChain`,
generalised to accept arbitrary left and right feed lists instead of one fixed stereo pair (multiple
feeds landing on the same side is fine, `AudioProcessorGraph` sums them).

The `GraphEditor` hook gates on the drag being MIDI and the real source node being
`ModuleType::TimelineMidiSource` (`nodeIsTimelineMidiSource`), covering both the direct-jack path and
the collapsed-macro-card "existing port jack" path — **not** the
`createMacroPortFromDroppedCable` fallback, since dropping a cable on a macro's body with no jack
under it mints a port with no interior leg yet, so there is nothing to search from. **One undo
transaction wraps macro-port auto-creation, the connection itself and the auto-channel build**, which
is what `maybeAutoCreateMacroPortsForDrag`'s `recordUndo` parameter exists for
([`docs/macros/auto-ports.md`](../macros/auto-ports.md#auto-creating-a-port-on-a-drag)).

## New Patch always has an Audio Output

`GraphEditor::newPatch()` seeds a fresh Audio Output immediately after `graph.clear()`, as part of the
same `recordStructuralChange` undo step — a brand-new empty project is never left with nothing for
`spliceMasterNode` to target. Without it, a bare Track Audio added to a fresh New Patch was silently
unheard until the user manually added an Audio Output, and a second track added after that manual fix
then auto-spliced Master and re-routed the first track's manual wire into it, which read as a
surprise.

**Scoped to `newPatch()` only.** `AIStateMapper::applyJSONToGraph`'s own `graph.clear()` (preset or
project load) is unaffected: it always replays a full node set from JSON right after clearing, so it
is self-correcting as long as the source JSON has an Audio Output, which every factory preset does.

On the very first channel, `MainComponent::addAudioTrack()` also relocates that Audio Output — or any
bare one the user dropped manually before adding a track — to sit immediately right of the
newly-spliced Master, once it knows this call is the one that splices Master (checked via
`synth::findMasterNode` before building the chain). Master already lands right of the chain by design,
so the row reads `Track Audio -> Gate -> EQ -> Compressor -> Strip -> Master -> Audio Output` left to right;
without the move, Audio Output stayed at the newPatch seed's canvas origin while Master jumped to the
far side of the chain, and the output cable had to run back across the whole canvas. **Only fires the
first time Master is created** — once it exists, later tracks do not reshuffle the canvas.

## Hosted plugin

Nothing channel-specific. The graph stays flat in both host modes, so a `Master` splice sits in front
of the output in the app and the plugin alike, and the mixer panel renders inside the plugin editor the
same way `GraphEditor` already does. The existing rule that an over-wide hosted plugin is **refused,
never truncated** (`Source/Modules/CLAUDE.md`) is unaffected — a channel strip does not change how a
hosted plugin's channel count is checked.

## AI authorability

`ChannelStrip` and `Master` are internal-only and are in `kNonAuthorableModuleTypes`
(`Source/AI/AIStateMapper/AIStateMapperInternal.h`) — a model cannot author either directly, the same
rule that already governs `TimelineMidiSource`, `Rec Tap` and the macro port types. `validatePatch`
does not change; per the root `CLAUDE.md` invariant, it is never relaxed to raise an AI pass rate.

**Why a strip in particular cannot be model-authored.** A strip's meaning is the channel the app built
around it — its shape, its place at the end of a chain, the track it may be linked to — and its solo
flag rides in trusted extra state, where a model-supplied `soloed=true` would silence the whole mix
render-wide. A model-authored second `Master` would split the mix and defeat the solo gate on Direct.

If the goal becomes "the AI can build a channel", the correct shape is an app-side **tool or action**
— "create channel" — that the model invokes and the app executes against a validated node set, never a
patch key the model writes directly. This mirrors the same resolution
[`docs/macros/macros.md`](../macros/macros.md#ai-authorability) reached for "the AI can build a
macro".

## Related

- [`docs/macros/macros.md`](../macros/macros.md) — the macro container a channel is optionally boxed
  in, and the proxy-port and internal-only-node precedents this design follows.
- [`docs/timeline/tracks.md`](../timeline/tracks.md) — track headers, M and S controls, the timeline
  side of the track and channel relationship.
- [`docs/timeline/add-track.md`](../timeline/add-track.md) — the "+ Track" menu and every flow it
  starts.
- [`docs/architecture/audio-engine.md#audioengine`](../architecture/audio-engine.md#audioengine) —
  `ensureMasterRecordTap()`, `EpochExchange`.
  [`docs/architecture/module-base.md#bypassmute-contract`](../architecture/module-base.md#bypassmute-contract)
  — the bypass and mute contract.
  [`docs/architecture/plugin-layer.md#host-modes-audioenginehostmode`](../architecture/plugin-layer.md#host-modes-audioenginehostmode)
  — plugin host modes.
- [`docs/modules/modules.md#channel-strip-module-mixer-channel-hidden`](../modules/modules.md#channel-strip-module-mixer-channel-hidden)
  — `kRightBase`, the stereo-pair conventions a strip's output follows, and
  [`docs/modules/modules.md#voice-mixer-module`](../modules/modules.md#voice-mixer-module).
- [`docs/layout/colour-overrides.md#colour-picker-popup`](../layout/colour-overrides.md#colour-picker-popup)
  — `ColourPickerPopup`'s preview and commit split, the mechanism the track and channel colour link
  fans out.
- [`docs/control/shortcuts.md#locate-master`](../control/shortcuts.md#locate-master) — "Locate Master" and the mixer's own key bindings.
