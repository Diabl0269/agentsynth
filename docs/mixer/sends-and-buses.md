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
uses the exact same `ModuleBase::panGains` balance law the strip's own `pan` parameter does (unity on
both legs at centre), smoothed over the same `kSmoothingSeconds` as level — so a send pan drag never
zippers, and a centred, still-stereo send renders bit-identical to before FRO294.

**The target is never stored.** Node ids are reassigned on every rebuild-from-JSON, so a stored id
goes stale on undo. "Which bus does slot *k* feed?" is answered by walking forward from slot *k*'s own
output channel to the first strip (`synth::findSendTarget`), which therefore also resolves through a
module the user inserted on the send path, and returns nothing for a cut cable.

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
send's own pan law (`ModuleBase::panGains`, the same balance law the strip's own `pan` uses): centre
leaves both legs at unity, hard left/right zeroes the leg you pan away from. **A centred, still-
stereo send is therefore bit-identical to pre-FRO294 output** — `panGains(0.0f)` returns exactly
`1.0f` on both legs, no rounding, so nothing new is introduced when neither control is touched. Pan
is smoothed the same way level is (its own `SmoothedValue`, `kSmoothingSeconds`, advanced/skipped in
lockstep with the level smoother in every branch `writeSendLegs`/`skipSendSmoothers` touch) so a pan
drag never zippers. Mono is a plain per-sample sum with no smoothing of its own — flipping it can pop
exactly the way flipping the strip's own Mono/Stereo shape can, which is an accepted, existing class
of transition in this codebase.

**Under bypass the strip's own gain and pan are off, so pre and post coincide.**

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
mutation uses. A mono row shows a small "M" marker painted (not a component) at the left edge of the
target-name area, the same "painted, not a child" idiom `PRE`/`POST` already use, so the row's
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
`juce::AudioProcessorGraph::addConnection` is **not** a backstop here, as measured: it checks node
existence, channel bounds and "not already connected", and **accepts a cycle without complaint**.
`FeedbackGuardTests`' render-time guard is what catches an audible runaway if one is ever wired by
hand on the canvas. **A refusal changes nothing at all, so no empty undo step is recorded** — and the
same holds for every other refusal (a non-strip target, self, out of slots).

**"Add bus"** — `+ Bus` on the dock's tab strip, and "New bus..." in every send menu — builds a
bypassed Gate, bypassed EQ, bypassed Compressor, Stereo Strip, Master(Mix) chain via the shared chain
builder, boxed in a macro named "Bus N". A boxed bus takes its macro's name.

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
