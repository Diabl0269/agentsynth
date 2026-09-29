# Sends and Group Buses

A send is a **strip-owned output leg**, and a bus is an **ordinary Channel Strip** whose inputs are
other strips' outputs. **Neither is a new node type** — there is no `SendModule` and no bus node type.
What a channel is lives in [`docs/mixer/mixer.md`](mixer.md).

---

## A bus is a Channel Strip

Three mechanisms already treat a bus as one: "Make channel"'s merge-point buses build the same
Gate, EQ, Compressor, Strip, Master chain
([`docs/mixer/mixer.md`](mixer.md#make-channel-and-shared-modules)), `collectStemStrips` scans for
`ChannelStripModule` ([`docs/mixer/stem-export.md`](stem-export.md)), and the mixer's orphan-strip pass
already renders one as a column.

**Why one type.** Keeping a bus a `ChannelStrip` leaves the solo gate, the stem tap, the column model
and `spliceMasterNode`'s `Strip -> Master(Mix)` classification all unchanged.

`MixerColumn::Kind::Bus` is **display only**, set when the strip carries `"isBus": true` in its
trusted extra state — written by "Add bus" and by the merge-point buses — **or**, as a structural
fallback for patches built before that flag existed, when one of its signal predecessors is another
strip. A cable into a Compressor or Gate **Key** input is not a signal predecessor
(`PortRole::Sidechain`, [`fx-modules.md`](../modules/fx-modules.md#key-inputs-sidechain)), so a bass
channel keyed from a kick channel stays a channel. **The flag is what a freshly added, still-unfed bus has to go on.** A track preset scrubs
`"isBus"` from every captured strip
([`docs/mixer/track-presets.md`](track-presets.md#scrubbed-keys)), so inserting one never badges an
ordinary track channel as BUS. **FRO297: a bus can itself be saved and re-added as a preset** (a
third `TrackPresetKind`, alongside Audio and Instrument) — inserting one re-applies this same flag
right after the scrub-respecting insert, the same mechanism "Add bus" uses, so the scrub's own
hazard never reopens; see [`docs/mixer/track-presets.md#a-third-kind-bus`](track-presets.md#a-third-kind-bus).

## A send is an output leg

`ChannelStripModule` declares `kMaxSends = 4` fixed slots, each a stereo pair of real output channels,
and **a send is a plain `AudioProcessorGraph` connection** from those channels into the target strip's
`ch0`/`kRightBase`.

**Why a real graph edge and not a module.** Only a real edge is visible to the three things that must
see it: JUCE's parallel-path delay compensation, stem export, and the canvas cable renderer. On the
canvas a send is therefore an ordinary cable on the strip's new visible jacks — no new cable concept.

**A send across a macro boundary goes through macro ports (FRO354).** A track's strip sits inside its
channel macro, so with "Auto-create macro ports when a cable or mixer send crosses a boundary" on (the default) a send between two channels leaves
the source's macro through an outlet port and enters the target's through one Stereo inlet — the same
as a hand-drawn cable. Removing or retargeting the send removes the ports it no longer uses, in the
same undo step. `MixerSendList` runs every row mutation through
`MacroGroupController::applyProgrammaticConnectionChange`; the Core flows themselves stay
macro-agnostic. Off, the send is the two direct cables it always was. See
[`docs/macros/auto-ports.md`](../macros/auto-ports.md#programmatic-connections).

## Channel layout

| | channels |
|---|---|
| inputs (unchanged) | `0` = In/Left, `1..3` reserved, `kRightBase = 4` = Right, so `kNumInputs = 5` |
| main out | `0` = Left, `1..3` reserved, `4` = Right, `5..7` reserved |
| send L block | `kSendBase = 8`, slot *k* at `8 + k` |
| send R block | slot *k* at `kSendBase + kMaxSends + k` |
| | `kNumOutputs = 16` |

**Each send's right leg is on its own block, so the "right leg never on ch1" invariant
(`Source/Modules/CLAUDE.md`) holds per send.** `hasStereoOutputPairShape(5, 16)` is false, so no Dual
I/O toggle is inherited.

## What is a parameter and what is state

Level is four `juce::AudioParameterFloat`s, `send1Level` to `send4Level`, ranged -60 to +12 dB with a
default of **0 dB** so a new send is audible rather than looking broken. **FRO294: pan is likewise
four `juce::AudioParameterFloat`s, `send1Pan` to `send4Pan`, ranged -1 to +1 with a default of 0
(centre)**, appended AFTER every other parameter (including `muted`) so nothing already registered
renumbers. **All eight (four level, four pan) are added in the constructor unconditionally** —
adding one later renumbers the host-visible layout and detaches saved host automation. A send's pan
uses the exact same pan-law choice the strip's own `pan` parameter does
([`docs/mixer/mixer.md#pan-law`](mixer.md#pan-law): FRO325's per-project Balance/Compensated on a
Mono-shaped strip, always Balance on a Stereo one), smoothed over the same `kSmoothingSeconds` as
level — so a send pan drag never zippers, and a centred, still-stereo send under the Balance law
renders bit-identical to before FRO294 (the Compensated law gives centre a hair under unity, a float
cosine/sine rounding, not a bug — see `ModuleBase::panGainsCompensated`).

**The target is never stored.** Node ids are reassigned on every rebuild-from-JSON, so a stored id
goes stale on undo. "Which bus does slot *k* feed?" is answered by walking forward from slot *k*'s own
output channel to the first strip (`synth::findSendTarget`), which therefore also resolves through a
module the user inserted on the send path, and returns nothing for a cut cable. A send can also feed
a Compressor/Gate Key input instead of a strip; see [Sending to a Key input](#sending-to-a-key-input).

Which slots exist, each slot's pre or post setting, (FRO295) each slot's **mute**, and (FRO294) each
slot's **mono** are **trusted extra state**, `"sends": [{"slot", "pre", "mute", "mono"}]`, the same
path as `"shape"` and `"solo"` — never an `AudioParameter`. `"mute"`/`"mono"` are each written
unconditionally, same as `"pre"`; an entry with no `"mute"`/`"mono"` key at all (every project saved
before FRO295/FRO294 respectively) reads as unmuted/stereo, so an old project loads unchanged.
**Mono is state, not a parameter**, because it is a binary routing choice (sum L/R before the pan
law) rather than a continuous value a fader or automation lane would drive — the identical "state,
not parameter" call FRO295 already makes for mute. A track preset scrubs `"sends"` from every
captured strip ([`docs/mixer/track-presets.md`](track-presets.md#scrubbed-keys)) — pan, being an
ordinary parameter like level, needs no special case there: `extractTrackPreset` captures every
parameter generically and pan rides along exactly as level already does.

## Slots are sparse

**Removing a middle send clears its bit and its cable and leaves every higher slot on its own raw
channels** — no cable is re-wired and no parameter value is copied, so host automation stays attached
to the right send. Only the VISIBLE jack indices renumber; the jack LABEL keeps naming the slot, so a
jack, its mixer row and its `sendNLevel` parameter always agree.

## Reordering sends

**FRO296: reordering swaps the REAL slots, not a display-only order on top of them** — the user wants
"Send 1" to always be the TOP ROW everywhere (the knob's title, a lane's name, the target menu), so
dragging a row to a new position is `synth::swapSends`/`synth::moveSendRow`
(`Source/Mixer/MixerSends/MixerSends.h`) actually exchanging the CONTENT of two slot numbers, headless
and with no undo of its own, exactly like `retargetSend`. Everything slot-keyed moves together, in one
call:

- its **cables** — exactly the connections leaving that slot's own raw L/R output channels, re-landed
  on the other slot's raw channels; a module the user inserted on the send path (the same case
  `findSendTarget`'s own walk handles) moves with it, since the cable is followed by channel index,
  never by what it eventually reaches;
- its **active/pre-fader/mute/mono bits** — a plain field swap;
- its **level and pan parameter VALUES** — `sendNLevel`/`sendNPan` are fixed per-slot identities
  (`"send1Level"` always names slot 0, so host automation on it always means "the top row's level"),
  so swapping slots swaps what those two parameter OBJECTS hold, never the objects themselves;
- its **automation lane**, if it has one — `TimelineDoc::swapLaneParams` (paramId-only rebind, no
  delete+recreate, so points and record mode survive) retargets a lane from `sendALevel`/`sendAPan` to
  `sendBLevel`/`sendBPan` and back, replayed by the caller (`MixerPanelComponent::moveSendRow`, which
  owns the `TimelineDoc`) against the SAME sequence of slot swaps the Core flow applied — see
  [`docs/timeline/automation.md`](../timeline/automation.md#a-lane-follows-its-send-through-a-reorder-fro296);
- its **MIDI Learn mapping**, if it has one — keyed the same way a lane is, `(nodeUuid, paramId)`
  (`Source/MidiRemote/RemoteModel.h`'s `Target::Parameter`), so `moveSendRow` replays the identical
  slot-swap sequence a third time against `MidiRemoteProjectDoc::swapParameterAssignments`, right
  beside the `TimelineDoc::swapLaneParams` calls — see
  [`docs/control/midi-remote.md#undo`](../control/midi-remote.md#undo).

**A visible row's POSITION is `moveSendRow`'s `fromRow`/`toRow`, into the active-slot-ordered list
(`MixerModelSends.cpp`'s `buildSendsForColumn`), not a raw slot number** — dragging row *i* to
position *j* becomes a walk of adjacent `swapSends` calls through that list from *i* to *j*, one per
step, each exchanging the two slots CURRENTLY sitting at two neighbouring visible positions. A sparse
gap (an inactive slot between two active ones) is never one of the positions a step touches: slots 0
and 2 active, moving row 1 above row 0, is exactly one `swapSends(0, 2)` — slot 1 stays untouched.
`moveSendRow` reports the exact sequence of `(slotA, slotB)` pairs it applied, which is what lets the
caller replay the identical sequence against a lane and a MIDI mapping. The whole gesture — the slot
swap(s), any lane rebind, and any MIDI Learn remap — lands in ONE
`AppUndoManager::recordGraphTimelineAndMacroChange` transaction (extended to optionally also carry a
`MidiRemoteProjectDoc` domain), so a single Cmd+Z reverts cables, values, bits, lane binding AND the
MIDI mapping together. `MixerPanelComponent::moveSendRow` also republishes the live Controllers
assignment cache (`MidiLearnController::publishAssignments()`) right after its own edit and again as
the undo/redo postRestore, so a hardware control already resolves against the send's new slot with no
separate reconcile step.

## Pre and post, mute and bypass

**Pre-fader is tapped after the hygiene and mono duplication and before gain and pan; post-fader
after them**, which is exactly what the strip hands Master, and before the solo gate.

**Mute silences all sends, pre and post.** The mute branch stays `buffer.clear()` per the root
`CLAUDE.md` two-branch contract; keeping a pre-fader cue alive under mute would mean making that
branch selective, which is the erosion the invariant exists to prevent. This is a deliberate departure
from DAWs that do keep pre-fader cue sends alive under mute.

**Under bypass the strip's own gain and pan are off, so pre and post coincide.**

**FRO295: a single send can also be muted on its own**, independent of the strip's own mute above and
of every other slot. Per-send mute is non-parameter trusted state (`isSendMuted`/`setSendMuted`),
exactly like the pre/post bit next to it — never an `AudioParameter`, so the send's own
`sendNLevel` is never touched: unmuting always restores the exact level the knob was left at. In
`processBlock`, a muted slot's L/R legs are silenced the same way a solo-gated-shut leg is —
`writeSendLegs` skips writing over the silence the unconditional hygiene pass already wrote, one
relaxed atomic read alongside `preMask_`'s own. Removing a send (`synth::removeSend`) clears the
mute bit along with the active/pre-fader bits, so a slot a later "+ Send" reuses always starts
unmuted. **Mute does not remove the send as a structural path** — `findSendTarget` and
`computeSoloAudibleLegs` (below) don't look at it at all, so a muted send still keeps its source
classified as feeding whatever bus it targets; it simply carries silence instead of signal while
muted.

**FRO294: a send can be panned, and independently switched to mono.** In `writeSendLegs`, mono runs
FIRST: the tapped L/R signal is summed to `(L+R)*0.5` on BOTH legs, replacing the stereo image with
identical content on each leg. The result — mono or the original stereo pair — then goes through the
send's own pan law, exactly like the strip's own `pan` ([`docs/mixer/mixer.md#pan-law`](mixer.md#pan-law)):
`ModuleBase::panGains` on a Stereo-shaped strip, or FRO325's per-project Balance/Compensated choice
on a Mono one. Under the Balance law, centre leaves both legs at unity and hard left/right zeroes
the leg you pan away from — **a centred, still-stereo send under Balance is bit-identical to
pre-FRO294 output**, since `panGains(0.0f)` returns exactly `1.0f` on both legs with no rounding, so
nothing new is introduced when neither control is touched (the Compensated law's centre is a hair
under `1.0f`, a float rounding artefact of its cosine/sine formula, not a regression). Pan
is smoothed the same way level is (its own `SmoothedValue`, `kSmoothingSeconds`, advanced/skipped in
lockstep with the level smoother in every branch `writeSendLegs`/`skipSendSmoothers` touch) so a pan
drag never zippers. Mono is a plain per-sample sum with no smoothing of its own — flipping it can pop
exactly the way flipping the strip's own Mono/Stereo shape can, which is an accepted, existing class
of transition in this codebase.

**Every branch writes every send channel** — the reserved `5..7` and all of `8..15` are cleared
unconditionally up front — or a stale block from the previous callback leaks into a bus.

## Latency across the parallel path

**Nothing new is written.** `juce::AudioProcessorGraph`'s built-in delay compensation already aligns
`strip -> Master` against `strip -> bus -> Master`. Adding a send is a **topology** change, so the
graph schedules its own rebuild and alignment is restored as soon as the message loop runs: **no send
flow calls `MainComponent::rebuildGraphForLatencyChange()`**, and a test asserting that adding a send
schedules its own rebuild is what keeps that true. Measured with an impulse and a negative control,
not assumed.

## Solo is a per leg audible mask

**A strip's output leg is audible if and only if (a) the strip is itself soloed, (b) it is downstream
of a soloed strip, or (c) that leg lies on a signal path reaching such a strip.** Everything else is
silenced.

**Why a single whole-strip flag is not enough.** Soloing a reverb bus would give the source dry
**plus** the source through the bus, and a group bus only works because its sources' MAIN legs stay
open.

`synth::computeSoloAudibleLegs` computes the map on the message thread — every graph change already
reaches `refreshSoloGate` via `publishTimeline` — reusing `synth::isSignalEdge` for every walk and
stopping at the first strip and at the terminals, exactly as `findStripFedByTrackSource` does.

**One walk is not enough: the set of contributing strips is closed to a fixed point.** A single
first-strip-stopping walk answers only "does this leg reach the soloed set in ONE hop?", which is
narrower than rule (c). With nested buses — source, inner bus, soloed outer bus — it closes the
source's main leg, so the audible inner bus is fed silence and soloing the outer bus produces nothing
at all. So **the soloed set is first grown by the same leg walk, repeatedly, until no further strip
joins**: a strip with any open leg feeds the soloed path, and so does a strip whose leg reaches it.

**Feeding the path is not the same as being downstream of a solo** — a contributing strip still gets a
**per-leg** mask, not all-ones, so a source that reaches a soloed bus only through its send keeps its
dry main leg closed no matter how many buses sit in between.

**`refreshSoloGate` publishes in two passes** — pass 1 ORs the new mask in, pass 2 assigns — so a
render pass landing between them sees a strip momentarily *more* audible, never wrongly silent. **A
strip's default mask is 0**, so a strip the engine never published for behaves exactly as a
whole-buffer clear did; **a strip that is itself soloed short-circuits the mask entirely.**

**Documented limitation.** The gate is per-*leg*, not per-*edge*: a main leg that feeds both Master
and a soloed bus stays open, so that strip's dry signal is still heard. Splitting it would need a
delay-compensated per-edge mute node, which is out of scope.

## The send and bus UI

A bus column is an ordinary strip column with three differences: a **"BUS" badge** instead of the
linked badge, a source line listing the feeding strips' names instead of tracks, and no track chip or
colour link. Its insert list works exactly like any other column's — including the bypassed Gate, EQ
and Compressor "Add bus" builds — via the backward walk
[`docs/mixer/mixer.md`](mixer.md#inserts-in-a-free-form-graph) describes for a column with no feeding
track. **Buses sit after the track-driven strips and before Direct**, which is exactly where the
existing orphan-strip append puts them.

On a source column a compact `MixerSendList` sits under the insert list: one row per active slot — a
target-bus button, a rotary level knob attached straight onto `sendNLevel`, a rotary **pan knob**
(FRO294) attached straight onto `sendNPan`, an **"M" mute toggle** (FRO295), a `PRE`/`POST` toggle and
an `x` — plus a `+ Send` row while a slot is free. **FRO301: a screen reader names each level knob by
its target** — "Send to Bus 1", or "Send 2 (no target)" once its cable is cut — and speaks its value
the same "-6.0 dB" format as the fader and pan knob
([`docs/mixer/panel.md`](panel.md#keyboard-navigation-and-accessibility)); the pan knob follows the
same naming convention ("Send pan to Bus 1" / "Send 2 pan (no target)") and speaks its value the same
"Center"/"50% left"/"50% right" text the strip's own pan knob uses (shared via
`Source/UI/Mixer/MixerPanAccessibilityText.h`). **Each mutation is ONE
`recordGraphAndMacroChange`** around `Source/Mixer/MixerSends`' Core flows, and **the rows unbind
before a graph-replacing undo frees their parameters**
([`docs/mixer/panel.md`](panel.md#unbinding-before-a-graph-change)).

**FRO294: Mono has no row button of its own** — the row's width budget has no room left for one — so
it is a ticked "Mono" item at the top of the row's existing target menu (`showTargetMenu`), toggled
through `synth::setSendMono` inside the same one-`recordGraphAndMacroChange` shape every other row
mutation uses. A mono row shows a small filled dot painted (not a component) at the left edge of
the target-name area — a dot, not a letter, because an "M" there read as a second mute button — and
its pan knob's accessible title gains "(mono)"; the same "painted, not a child" idiom `PRE`/`POST` already use, so the row's
component budget stays exactly level knob + pan knob + M mute button + real-click PRE/POST/remove
hit areas.

**FRO295: the M button is a real `juce::TextButton`, not painted text like PRE/POST** — it reuses the
exact button type/convention `MixerColumnComponent`'s own strip-level mute button uses
(`setClickingTogglesState(false)` plus a manual `setToggleState` kept in step by `rebuildKnobs()`),
so its on/off colouring comes from `AppLookAndFeel`'s `TextButton::buttonOnColourId` (the theme's
accent) with no bespoke paint, and it gets a real hit area and a real `AccessibilityHandler` (a
`juce::Button`'s own, unlike the proxy components the paint-only rows need). Its accessible title is
"Mute send to \<target\>" / "Mute send N (no target)", the same naming the level knob's own title
uses. Clicking it calls `synth::setSendMuted` inside the same one-`recordGraphAndMacroChange` shape
every other row mutation uses.

**FRO296: drag a row by its target-name area to reorder it.** The name area is the same region a
plain click already used to open the target menu, so `MixerSendList` defers the click-vs-drag
decision to a small pixel threshold (`kRowDragThreshold`, the same value/reasoning
`TimelineTrackHeaderComponent`'s own row-reorder drag uses) — a press that never crosses it is a
plain click and still opens the menu; one that does becomes a drag. While dragging, a thin insertion
line (the theme's accent colour, no new colour) is painted at the hovered drop boundary. Releasing
calls `MixerPanelComponent::moveSendRow` — the one place graph, `TimelineDoc` and macros are all
reachable together — which does the real swap and lane rebind (see "Slots are sparse" above) as ONE
undo step. The knobs, M button, PRE/POST and `x` all keep working exactly as before: only a press that
lands on the name area itself, past the menu-vs-drag threshold, is claimed by the drag.

**FRO292: an active send's level can carry its own automation lane.** Right-click the send knob
→ **Automate 'Send N Level'** creates the lane and opens the automation strip on it (the same
`onAutomateParameterRequested` route a canvas knob uses — the only way in from a track with no lanes
yet, since the track header's `A` button only appears once one exists). Once the strip is open, its
lane picker also offers an "Add lane…" entry per
active, not-yet-automated slot, labelled by `synth::describeSendSlotLabel` with the SAME "Send to
\<target\>" / "Send N (no target)" text the knob's own accessible title uses, so the picker entry and
the knob it drives always read the same thing. A `sendNLevel` is an ordinary `RangedAudioParameter`
(unlike a hosted plugin's), so the created lane's range comes from its real `NormalisableRange`
(-60..+12 dB), not the hosted `{0, 1}` convention. Removing the slot leaves an existing lane bound to
now-inert extra state — no orphan logic runs, since the parameter itself never goes away (see
[`docs/timeline/automation.md`](../timeline/automation.md) and
[`docs/modules/modulation.md`](../modules/modulation.md#hosted-plugin-parameters-as-automation-lanes)).

**The forward cycle walk is the ONLY cycle defence.** Cyclic targets are excluded from the menu by a
forward walk from the candidate back to this strip, and `synth::addSend` applies the same check.
**FRO318: that walk follows EVERY connection, not just `isSignalEdge`'s signal edges** — a Key edge, a
ModCV cable or a hidden attenuverter leg is as much a render dependency as an audio input, so a bus
whose output keys a Compressor on the source's own chain is no longer offered as the source's send
target (see [Sending to a Key input](#sending-to-a-key-input)).
`juce::AudioProcessorGraph::addConnection` is **not** a backstop here, as measured: it checks node
existence, channel bounds and "not already connected", and **accepts a cycle without complaint**.
`FeedbackGuardTests`' render-time guard is what catches an audible runaway if one is ever wired by
hand on the canvas. **A refusal changes nothing at all, so no empty undo step is recorded** — and the
same holds for every other refusal (a non-strip target, self, out of slots).

**"Add bus"** — `+ Bus` on the dock's tab strip, and "New bus..." in every send menu — builds a
bypassed Gate, bypassed EQ, bypassed Compressor, Stereo Strip, Master(Mix) chain via the shared chain
builder, boxed in a macro named "Bus N". A boxed bus takes its macro's name.

## Sending to a Key input

**FRO318: a send can also feed a Compressor's or Gate's Key input** — the detector key
([`fx-modules.md`](../modules/fx-modules.md#key-inputs-sidechain)) — so a kick track can duck a bass
track straight from the mixer. It is still just a send: the same slot, the same cables, the same
level, pan, mono, mute and pre/post, all applied to the slot's output leg exactly as for a bus.

**Two target kinds, one value type.** `synth::SendTarget { node, key }`
(`Source/Mixer/MixerSends/MixerSends.h`): a strip (`key == false`, wired into the strip's
`ch0`/`kRightBase` as before) or a module's Key input (`key == true`, wired into the module's first
two `PortRole::Sidechain` inputs, found by querying its own input map — nothing is hard-coded to
Compressor or Gate, so any module that grows a Key pair becomes a target for free). `addSend` and
`retargetSend` take either kind with the same all-or-nothing rollback; the old strip-only overloads
forward to them. Reordering needs nothing new: `swapSends`/`moveSendRow` remap cables by source
channel, so a Key send moves like any other.

**The target is still never stored.** `synth::resolveSendTarget` walks forward from the slot's raw
left output: an edge landing on a Sidechain input — looking through any macro ports in between, so a
ported Key send still resolves — makes that module a Key target (checked *before*
the `isSignalEdge` filter, which drops key edges by design); otherwise the first strip reached, as
`findSendTarget` always did. The nearest hit wins. `findSendTarget` stays the strip-only view and
reports a Key send as no strip at all, so every bus/solo/stem caller keeps its meaning.

**Keying follows the cable.** Adding or removing a Key send is an ordinary topology change, so
`synth::publishSidechainConnections` — run on every graph change — switches the module's detector
to the key and back. Muting the send silences the key; it does not unplug it, so the detector stays
on the (now silent) key and the ducking simply stops. **A bypassed Compressor/Gate ignores its key**
— the ones Make Channel builds start bypassed, so switch it on to hear the send.

**Labels.** In the send menu, after the bus/strip targets, a separator and one
"Key: \<module\> on \<channel\>" entry per legal Key target; `<module>` is the card title (its
custom name, else the auto-numbered "Compressor 2"), `<channel>` is `sendTargetName` of the first
strip the module's own output reaches (`synth::findKeyTargetChannel`), and a module that reaches no
strip reads plain "Key: \<module\>". The row itself names the channel the way its column header
does (`stripColumnName`, so the track's name), which can read richer than the menu's doc-less
`sendTargetName` — the same asymmetry strip targets already have. The knob's FRO301 title and the
lane picker's `describeSendSlotLabel` read "Send to Key: Compressor 2 on Bass".

**Cycles.** A Key target is illegal when the module's own output reaches the source strip, along
ANY edge — including another Key edge: a cycle through a key is still a real render cycle. So the
Compressor on the source's own channel is never offered. See the cycle-walk paragraph above.

**Solo.** A Key send's leg stays **open while the channel it keys is audible** (soloed, downstream of
a solo, or contributing): soloing the bass keeps it ducking. Only the send bit opens — the kick's
main leg is still decided by its own walk, so the kick itself stays silent. A Key edge never makes
the keyed channel "downstream" of a soloed kick either (`collectDownstreamStrips` keeps ignoring key
edges). The open Key leg does make the kick a contributing strip in the fixed point, which keeps
whatever feeds the kick feeding its key.

**Stems.** A Key send contributes to no stem: `collectStemStrips` lists strips only, the stem tap
copies a strip's main legs only (never a send leg), and a Key edge never makes the keyed channel a
bus, so its stem keeps its own name. The keyed channel's stem does carry the ducked audio, because
that is what its strip outputs.

## Stems

Buses get stems for free and sources' stems stay pre-send — see
[`docs/mixer/stem-export.md`](stem-export.md#buses-are-stems-too).

## Related

- [`docs/mixer/mixer.md`](mixer.md) — what a channel is, the solo gate, insert walking.
- [`docs/mixer/panel.md`](panel.md) — the column the send list sits in.
- [`docs/mixer/stem-export.md`](stem-export.md) — how a bus and a pre-send source are written.
- [`docs/mixer/track-presets.md`](track-presets.md) — why `"sends"` and `"isBus"` are scrubbed.
- [`docs/modules/modules.md#channel-strip-module-mixer-channel-hidden`](../modules/modules.md#channel-strip-module-mixer-channel-hidden)
  — the strip's own channel conventions.
