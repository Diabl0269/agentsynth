#Timeline Operations

`synth::TimelineOps` (`Source/Timeline/TimelineOps.h/.cpp`) is the **write** half of the timeline
seam: discrete, validated, previewable operations a model may ask for, applied to `TimelineDoc` as
one undo step. [Timeline safety](timeline-safety.md) is the gate they go through;
[arrangement context](arrangement-context.md) is the read-only context that lets a model know what
to ask for in the first place.

**Trust statement for `addInstrumentTrack`:** the model may bind only the `Track In` that op
creates. There is no `bindingUuid` and no plugin identity in the grammar, and `Track In` stays
non-authorable in patches (`kNonAuthorableModuleTypes`).

## The envelope

```json
{
    "timelineOps" : [
        {
            "op" : "addInstrumentTrack",
            "name" : "Lead",
            "instrument" : "Oscillator",
            "inserts" : [ {"type" : "Filter", "params" : {"cutoff" : 800}} ],
            "envelope" : {"params" : {"decay" : 0.3, "sustain" : 0}}
        },
        {"op" : "addTrack", "kind" : "midi", "name" : "Bass"}, {
            "op" : "placeClips",
            "track" : "Bass",
            "clips" : [ {
                "startBeat" : 0,
                "lengthBeats" : 4,
                "name" : "A",
                "notes" : [ {"startBeat" : 0, "lengthBeats" : 1, "pitch" : 36, "velocity" : 100, "channel" : 1} ]
            } ]
        },
        {
            "op" : "writeLane",
            "nodeUuid" : "...",
            "paramId" : "cutoff",
            "points" : [ {"beat" : 0, "value" : 800, "tension" : 0, "curve" : 1} ]
        },
        {"op" : "placeMidiClip", "track" : "Bass", "startBeat" : 0, "midBase64" : "<base64-encoded Standard MIDI File>"}
    ]
}
```

This is the **client** half of the capability. The private backend repo owns the **server** half —
the capability schema the model emits against, the counterpart of `getPatchSchema` for patches.
Nothing here trusts that schema: an envelope is re-validated locally whatever produced it.

| Op | What it does | What it deliberately does not do |
| --- | --- | --- |
| `addTrack` | Creates the **doc** track. `kind` is `"midi"` or `"automation"`. | No graph node, no Track In wiring — binding a track to a module is a routing decision about the user's own patch, so it stays a user gesture. The new track is unbound and the preview says so. `"audio"` is not offered: an audio track needs an asset, and assets are trusted-only. |
| `addInstrumentTrack` | Builds a **bound, playing** MIDI track exactly like "+ Track -> Instrument": `Track In -> instrument -> [Voice Mixer / ADSR+VCA as that flow decides] -> inserts -> Gate -> EQ -> Compressor -> Channel Strip -> Master`, boxed into one macro named after the track, palette colour. `name` (required, `addTrack`'s rules, and **new** — no existing track may have it, since later ops address it by name); `instrument` (required: `kAuthorableInstrumentTypes` = Oscillator, Wavetable, Sampler); `poly` (optional bool, Oscillator/Wavetable only); `instrumentId` (optional int: inert to `TimelineOps` itself, an in-response node reference inside an [edit plan](#one-edit-plan)); `inserts` (optional, at most `kMaxInstrumentInserts` = 8, each `{type, id?, params?}`, `id` likewise: an authorable module that is not a MIDI instrument or MIDI source and takes audio in and out, its `params` checked by `validatePatch`'s own `validateNodeParams` and applied through the untrusted apply path);
`instrumentParams` (
    optional object : the instrument 's own params, e.g. `{"waveform": "Saw"}`, checked against the instrument type' s real params and applied to the built instrument; `poly` is
        refused there); `envelope` (optional object `{
    id ?, params ?}`, Oscillator/Wavetable only - see [the track's own envelope](#the-tracks-own-envelope)). Needs a host — see [below](#addinstrumenttrack). | No `bindingUuid`, no plugin identity, no default-track-preset lookup (the preview must describe what gets built). Never binds anything but the `Track In` it creates. |
| `placeClips` | Places clips, and their clip-relative notes, on a MIDI track targeted by exact name or `{
    "index" : N}`. | A name matching no track, or more than one, rejects the whole batch rather than guessing. |
| `writeLane` | Find-or-creates the lane for `(nodeUuid, paramId)` on the document's Automation track, creating that track if there is none (it has no graph, so it cannot pick the owning track the way `MainComponent::automateParameter` does; the next project open moves the lane), then REPLACES every point in the written span (min to max beat of the payload, inclusive) in one `editBreakpoints` call. | Never sets a record mode; never widens a range. |
| `placeMidiClip` | Decodes `midBase64` and parses it with `MidiClipFile::importFromStream`, placing one clip per non-empty imported SMF track on the target MIDI track at `startBeat`. Clip length is `ceil` of its last note's end, floored at 1 beat, reusing `MidiClipFile::importIntoTrack`. | No paths, no plugin ids, no code — a `.mid` blob can only ever decode to notes, which is why this is the one op that accepts an opaque binary payload at all. |

## The local path

The local (Ollama) path can author this envelope too, behind
`AIIntegrationService::setTimelineToolsEnabled`, which `MainComponent` sets on unconditionally, and
additionally gated on a timeline context being installed (`hasTimelineContext()`). While active,
three things change and nothing else:

- the system prompt gains a `TIMELINE & AUTOMATION OPERATIONS` section teaching the five ops,
  swapped into the existing history **in place**, so a mid-conversation toggle never clears the chat
  and off means byte-identical to the pre-timeline prompt;
- the structured-output `format` becomes `AIStateMapper::getPatchSchemaWithTimelineOps()` —
  `getPatchSchema()` plus an OPTIONAL `timelineOps` array whose item schema is deliberately
  permissive (one object shape, only `"op"` required; it also offers `nodeId` and the modulation
  item's `destParam` for [edit plans](#one-edit-plan)). It is a grammar that lets the model express
  the ops, not a validator: `TimelineOps::validate` remains the gate, and the
  [reserved-fields rule](patch-format.md#reserved-keys-and-forward-compatibility) is untouched;
- the outgoing request grows an `## Automation targets` section
  (`buildAutomationTargetsSection()`): one line per uuid-bearing node listing its float parameter
  ids and RAW ranges, the addressing channel `writeLane` needs. Node uuids appear there **on
  purpose**, despite [arrangement context](arrangement-context.md)'s no-uuid rule: that rule keeps
  identifiers out of the human-readable summary, this section is what makes the grammar usable at
  all, uuids are random per-node identity and never a path or plugin id, and `validate()` only
  accepts pairs that resolve against the live graph anyway. Bounded to roughly 2000 characters,
  whole lines, with a truncation marker.

Extraction, validation, preview and Apply are unchanged and provider-agnostic either way: they act
on what a response actually carries, and the user's Apply click stays the write gate. Pinned by
`AIIntegrationServiceTest.TimelineToolsToggle*` and `AutomationTargetsSection*`.

## `addInstrumentTrack`

`TimelineOps` is static and only reads the graph, while the instrument-track build lives in the app
(macros, card placement). So the op goes through a **host seam**, `synth::TimelineOpsHost`
(`TimelineOps.h`), which `MainComponentTimelineOpsHost` implements and `MainComponentSetup.cpp`
installs on `AIIntegrationService::setTimelineOpsHost` beside the apply callback:

- `addInstrumentTrack(name, instrumentType, poly, inserts, envelopeParams)` — the app's own build
  (`MainComponent::buildInstrumentTrackBody`, shared with the menu), skipping the default track
  preset so the preview cannot lie, inside the batch's transaction. Returns an
  `InstrumentTrackBuildResult` — the uuids of the Track In, the instrument, each insert (in op
  order) and the envelope's ADSR (`envelopeUuid`, empty for a Sampler) — which an [edit
  plan](#one-edit-plan)'s in-response references (an insert's `id`, the op's `instrumentId`, the
  envelope's `id`) resolve against. `TimelineOps` on its own never reads them. Returns `nullopt`
  having removed anything it created.
- `editableTimelineDoc()` — the live doc an edit plan writes to inside `recordBatch` (default null:
  that host cannot apply a plan).
- `recordBatch(mutation)` — runs the whole batch as ONE undo step over graph, timeline and macros
  (`AppUndoManager::recordGraphTimelineAndMacroChange`), then reconciles the timeline.

`validate(envelope, doc, graph, host)` fails an `addInstrumentTrack` op outright when `host` is null
("This build cannot create instrument tracks from here."). With a host it checks every field and the
inserts (no host needed for those), then adds just the MIDI track to the scratch doc so later ops in
the batch (`placeClips` by name, …) see it. **The graph side cannot be dry-run**: validation never
calls the host, so a factory failure surfaces at apply, where the host's `nullopt` makes
`apply()` restore the doc and report it, with nothing pushed to undo.

`apply()` keeps the plain `recordTimelineChange` path for a batch with no `addInstrumentTrack` op.
Otherwise it runs the batch inside `host->recordBatch`, and the op calls `host->addInstrumentTrack`
(which creates the doc track and the nodes) instead of `doc.addTrack`.

Preview parts, pinned by `TimelineOpsInstrumentTrackTest.PreviewStringsArePinned`:
`adds instrument track "Bass" (Oscillator with envelope and channel strip)`, `(Sampler with channel
strip)` for a Sampler, a `poly ` prefix before the type when `poly` is on, `, inserts: Filter,
Distortion` appended when the op has inserts, and `, envelope: attack 0.005, decay 0.2, sustain 0,
release 0.15` after that listing only the envelope params the op set (attack, decay, sustain,
release in that order, then any other by name; no suffix when it set none).

`instrumentParams` is read by `readInstrumentTrackOpFields` like an insert's `params`
(`validateNodeParams` on a probe of the instrument type), applied by the real host with
`applyUntrustedParams` before the build wires anything, and by the preview's stand-in instrument. The
preview adds `, instrument: waveform Saw` after the type.

### The track's own envelope

An Oscillator or Wavetable track gets an ADSR and a VCA from the build; nothing else could reach that
ADSR, so a model could not make a pluck or pad on a new track. `envelope` is `{
    "id"
        : <int>
          ?
          , "params"
          : {<ADSR param id> : <raw value>}
          ? }`, closed to those two keys:

- `params` are checked by `validateNodeParams` against a real ADSR (the helper an insert's `params`
  use), then handed to the host, which applies them with `applyUntrustedParams` to the ADSR it built
  (`MainComponent::applyInstrumentEnvelopeParams`, the poly and the mono path alike). `poly` is
  rejected in them: the track's own `poly` decides whether the envelope is per-voice.
- A Sampler plays through its own one-shot envelope, so `envelope` on a Sampler is rejected with
  `Sampler tracks have no envelope; leave "envelope" out.`
- `id` is inert to `TimelineOps`; in an [edit plan](#one-edit-plan) it names the ADSR, so a
  modulation can use it as `source`, a `writeLane` as `nodeId`, and `remove` can delete it. That is
  how "plucky" (sustain 0, short decay) and "filter envelope" (the track's envelope onto a Filter
  insert's cutoff) are expressed without a free ADSR node that nothing triggers.
- Params go through the untrusted apply path, which rescales only a `[0,1]` value the parameter's
  range cannot hold ([patch-preview](patch-preview.md)): an ADSR decay of 0.2 s lands as 0.2 s.

The same field exists server-side in the hosted `project.generate` contract. The local prompt teaches
it with the sound-design words (pluck, pad, filter envelope, acid -> parameter values) and three
worked responses, only while the timeline tools are on; `Tools/AIEvalHarness --mode project` scores
whether an answer has the right shape ([harness](#measuring-sound-shape)).

Tests: `Tests/Timeline/TimelineOpsInstrumentTrackTests.cpp` (a fake host: field and insert checks,
previews, one undo step, host never called for an invalid batch) and
`Tests/Mixer/ChannelFlow/ChannelFlowTimelineOpsHostTests.cpp` (the real host end to end).

## One edit plan

A response root may carry patch keys (`nodes`, `connections`, `mode`, `remove`, `modulations`,
`removeModulations`) **and** a sibling `timelineOps` list that refer to each other: a modulation
onto an insert the same response builds, a `writeLane` on a node the patch creates.
`AIIntegrationService::previewProjectEdit` / `applyProjectEdit`
(`AIIntegrationServiceProjectEdit.cpp`, reading and running in
`AIIntegrationServiceProjectEditRun.cpp`) treat such a root as ONE plan. This is the client half of
the hosted `project.generate` capability (`ProjectEditEnvelopeSchema` server-side). Each half still
passes its own gate (`validatePatch`, `TimelineOps::validate`); the plan adds these rules on top:

- **One id namespace.** Patch node ids, every `instrumentId`, every `envelope.id` and every insert
  `id` must be distinct, and each must be a non-negative integer id; a repeat rejects the whole plan
  with a message naming the id and both uses. An `instrumentId`, `envelope.id` or insert `id` that is
  already the uid of a live node is rejected too (a merge patch addresses live nodes by uid, so it would name two nodes).
- **Mode.** With a track-creating op (`addTrack`, `addInstrumentTrack`) the patch runs as a merge
  (an absent `mode` counts as `merge`); `"replace"` rejects, since it would delete the instruments
  step 1 built. Without one, `merge`/`replace` mean what they say and an absent `mode` is a replace
  with `applyPatch`'s one-directional repair (a replace that is rejected but validates as a merge
  runs as a merge). The patch phase also gets `applyPatch`'s structural gate in the preview.
- **Fixed apply order**, whatever order the list is in:
  1. track-creating ops, in list order;
  2. the patch, through `validatePatch` / `applyJSONToGraph` with a `PatchIdScope`: each
     `instrumentId`, `envelope.id` and insert `id` is **bound** to the node its build returned
     (`InstrumentTrackBuildResult`, captured by a recording host), so connections, modulations (as
     `source` or `dest`) and `remove` may name it; an `envelope.id` on a build that returned no
     `envelopeUuid` fails the plan, and every node step 1 created is **hidden** from the raw-uid namespace
     (the model never saw those uids; letting `57` address one would make it an edit at apply but a
     new node in the preview). A patch node reusing a bound id is `DuplicateNodeId`;
  3. every other op (`placeClips`, `writeLane`, `placeMidiClip`), in list order.
- **`writeLane` addressing.** Exactly one of `nodeUuid` (a node that already exists) or `nodeId` (a
  patch node id, `instrumentId`, `envelope.id` or insert `id` from this response) - both or neither rejects. A
  `nodeId` is rewritten to the `nodeUuid` of the node that id denotes after step 2, on a fresh copy
  of the ops in every run, so **`TimelineOps` only ever sees uuids** (on its own it still refuses
  `nodeId` as an unknown field). `applyJSONToGraph` mints a uuid for every node it creates, which is
  what makes a patch node addressable right after step 2.
- **Preview mutates nothing.** The live graph is trusted-replayed into a scratch, the doc copied
  through `toVar`/`fromVar`, and the SAME `runPlan` the apply uses runs all three phases on the
  copies. Track builds go to a stand-in host: the doc track for real, and on the scratch graph only
  unwired nodes of the instrument and insert types (insert params applied) carrying uuids, plus an
  unwired ADSR carrying the envelope params for an Oscillator/Wavetable, so a `destParam` onto an
  insert, a modulation from an envelope id and a lane's range check resolve against real processors. **The graph
  side of a track build is still not dry-run** - the stand-ins have no Track In binding, envelope,
  channel strip or macro. The preview text is the patch phase as a sentence (what a merge changes,
  from `computeDiff`; what a replace contains, from `summarizePatch`) followed by `TimelineOps`'s
  own sentence for both op phases.
- **One undo step, all or nothing.** Apply previews first, then runs the three phases inside ONE
  `TimelineOpsHost::recordBatch` (`recordGraphTimelineAndMacroChange`). The op phases use
  `TimelineOps::applyInsideTransaction`, the batch runner `apply()` wraps, which assumes the caller
  validated (the plan validates each phase right before running it) and holds the transaction.
  `aiPatchAboutToApply` fires before the batch and `aiPatchApplied` after it; on undo and redo the
  transaction's own snapshot actions detach the module components before a processor is freed and
  refresh the canvas after, as for every graph-changing track flow
  ([engine](engine.md#undo-and-redo)).
- **Backstop.** A phase failing after the preview passed is unreachable for a patch or a doc-only
  op (the preview ran the same code on identical copies) and reachable only through a host build
  failing. Then the doc is restored from its pre-batch `toVar()` and the graph from a pre-batch
  trusted `graphToJSON()` replay (`applyJSONToGraph(..., clearExisting=true, trusted=true)`), and the
  failure is returned. The host's macro set is not restored here: the real host removes what a
  failed build made, but an earlier successful build in the same plan keeps its macro.

Asking for a plan: `sendProjectMessage` - see [engine](engine.md#request-flow). Tests:
`Tests/AI/AIIntegrationService/AIIntegrationServiceProjectEditTests.cpp` (the rules, the preview, the
backstop through a failing fake host, both request shapes),
`Tests/Mixer/ChannelFlow/ChannelFlowProjectEditTests.cpp` (the apply order and the one undo step
through the real host), `Tests/AI/AIStateMapper/AIStateMapperIdScopeTests.cpp` (the scope).

### Where things land

Positions are the app's, not the model's: a plan rarely sends `"position"`, and the canvas has no cards
while the batch runs (`aiPatchAboutToApply` detached them). Both rules below run inside the plan's one
undo step.

- **A new track goes below every existing one.** `addInstrumentTrack` places its Track In, and so the
  whole channel row and its macro, with the same `GraphEditor::findLeftEdgeSlotBelowModules` "+ Track"
  uses: the left edge, one gap below the lowest card, collapsed macro card or open macro hull. While
  the cards are detached, `MacroGroupController::placementBlockers` and the open-hull union read each
  node's model rect instead (stored `x`/`y`, estimated card size, `ModelCardBounds.h`), so existing
  track macros still count. On an empty canvas the track lands where it always did. "+ Track" itself is unchanged, since its cards are
  attached.
- **A new module goes beside what it connects to.** After the patch, `TimelineOpsHost::placeNewModules`
  gets every node the plan created; the app host (`synth::placeNewModulesBesideConnections`,
  `Source/UI/Graph/NewModulePlacement/`) places the ones with no stored position, one at a time:
  1. the source of a connection or modulation into an already-placed node goes **left** of its first
     such destination, on that node's row;
  2. otherwise the destination of a connection from an already-placed node goes **right** of it;
  3. otherwise below the last node it placed (the canvas' left edge below everything, for the first).

  The spot is walked down in grid steps (`LayoutUtil::findFreeSlotBelow`) until it clears every
  placement blocker, so it never covers a card; a "left of" spot that would cross the canvas origin
  is clamped there and walks down too. A placed node counts as placed for the rest, so a chain with no
  positions lays out left to right. A modulation's attenuverter is looked through (LFO -> Filter);
  MIDI cables and the output dock (Master, Rec Tap, Audio Output) never anchor, because the merge
  auto-wires MIDI from whichever source it finds first and the dock sits right of everything. A node
  anchored on a macro member joins that macro (`addSelectionToMacro`, no undo record of its own),
  lands inside its outline clear of every member, hidden ones included, and the macro makes room:
  the cards it pushes glide (`CardGlideAnimator`). Undo restores the whole plan in one step; that
  restore rebuilds the cards, so it lands at once.

Tests: `Tests/Mixer/ChannelFlow/ChannelFlowProjectEditPlacementTests.cpp`, and the walk-down in
`Tests/UI/Layout/LayoutUtilTests.cpp`.

## `placeMidiClip` and the `.mid` blob

Every other op in this grammar is closed field by field. `placeMidiClip` is the one exception that
accepts an opaque, base64-encoded blob, and it is safe to accept specifically *because*
`MidiClipFile::importFromStream` (`Source/Timeline/MidiClipFile.h`) is the safest surface available:
a Standard MIDI File can only ever decode to notes — pitch, velocity, channel and timing. There is
no way to encode a file path, a plugin identifier or code inside one, unlike almost any other blob a
model could hand back. `placeMidiClip` reuses that exact importer, the same strict parser a user's
own MIDI-file import goes through, rather than a looser variant for AI input.

Bounds, in the order they are checked:

- **`midBase64` size**, against `TimelineOps::kMaxMidBlobBytes` (262144), checked on the
  STILL-ENCODED string *before* any decode is attempted, so an oversized blob is rejected as cheaply
  as any other length check rather than by allocating a decode buffer for it first.
- **Decodability** — invalid base64 is rejected outright.
- **`MidiClipFile::importFromStream`'s own checks** — not a readable SMF, SMPTE time format (PPQ
  only), or any one imported track's note count over `TimelineDoc::kMaxNotesPerClip` all reject the
  op. An import failure never means "import what parsed and drop the rest".
- **An empty result** — a blob with no notes in it is refused; there is nothing to place.
- **The batch's own note and clip caps** — every note the blob contains counts toward
  `kMaxTotalNotesUntrusted` exactly like a `placeClips` note does, and the target track's clip count
  is checked against `TimelineDoc::kMaxClipsPerTrack` before anything is placed.

Any failure at any of those steps rejects the WHOLE batch, the same all-or-nothing contract every
other op has.

## Sibling, never nested

`timelineOps` sits **beside** a patch, never inside one. `validatePatch(trusted=false)` refuses a
`"timeline"` key in patch JSON and always will (see
[the two-door model](timeline-safety.md#the-two-door-model)). `"timelineOps"` is a different key, so
a single structured response may legitimately carry a patch and an ops envelope on the same object,
and each is validated and applied by its own gate with its own Apply button. Pinned by
`TimelineOpsTest.PatchGrammarStillClosed`: the document dialect smuggled in under `"timeline"` is
refused, while the same intent as a sibling `timelineOps` key is accepted by both halves.

## Trust posture

`validate()`, then preview, then the user clicks Apply, then `apply()`. Nothing is applied because a
model asked for it; a person agrees to it first, having read a summary of what it does. This is
identical to a patch's posture: both reach the user on the one edit-plan card.

- `validate()` mutates nothing and returns `previewText`, a deterministic sentence:
  `Adds midi track "Bass" (unbound - bind it in the timeline panel); places 1 clip (8 notes) at 0-4
  on "Bass"; writes 12 points to Filter cutoff over beats 0-11`. A bound module is named by its
  **display name**, never its uuid, on the write path as well as the read path.
- The per-op checks are [`validateTimeline`](timeline-safety.md)'s, **reused rather than
  re-stated**: the same caps (`TimelineDoc::kMax*`, `kMaxTotalNotesUntrusted`, `kMaxPpqUntrusted`),
  the same bounds, and the same rule that untrusted input is **rejected where a trusted path would
  clamp** — pitch 200 is refused, not rewritten to 127; a breakpoint outside the **live**
  parameter's range is refused, not pulled inside it. Two more caps bound the batch itself:
  `kMaxOps` (64) and `kMaxNameChars` (128); a third, `kMaxMidBlobBytes` (262144), bounds only
  `placeMidiClip`'s `midBase64`.
- **Capabilities are absent from the grammar, not refused field by field.** An op has no
  `assetRef`, no `recordMode`, no `bindingUuid`, and no kind beyond `midi` and `automation`
  (`addInstrumentTrack` binds only the `Track In` it creates). A
  `.mid` blob is not an exception to this, because it can only ever decode to notes. Unknown fields
  *inside* an op are **rejected**, so a future field cannot be smuggled past a gate that never
  inspected it — the same reasoning as `validateTimeline`'s unknown-top-level-key refusal. Unknown
  keys at the *envelope root* are ignored, because that is where the sibling patch's own `nodes`,
  `connections` and `mode` live.
- **All-or-nothing, and the preview cannot lie.** `validate()` runs the batch against a throwaway
  copy of the document (`fromVar(doc.toVar())`, replaying the app's own serialisation, which is
  trusted by definition) and `apply()` runs the *same code* against the real one, so every op sees
  the effect of the ones before it and no preview can describe an apply that then fails. A rejection
  means the live doc was never touched at all.
- **One undo step.** The whole batch runs inside a single `AppUndoManager::recordTimelineChange` (or,
  with an `addInstrumentTrack` op, the host's graph + timeline + macro transaction), so
  however many tracks, clips, notes and breakpoints it touches, one `Cmd+Z` reverts all of it — the
  contract `MidiRecorder::stopAndCommit` already relies on for a take's clip plus its notes.

## The chat seam

`AIIntegrationService`, once `setTimelineContext()` has wired the live timeline in, exposes:

- `extractTimelineOps()` — the same extraction `applyPatch` performs, returning the parsed root when
  it carries a `timelineOps` key. It keys on **presence**, not well-formedness, so a malformed
  envelope is surfaced as a visible rejection instead of being silently dropped.
- `previewTimelineOps()` — the validate step.
- `applyTimelineOps()` — routes through a `TimelineOpsApplyCallback` that
  `MainComponent::initialiseCommon` installs. The service holds the doc only as a `const` pointer
  and owns no undo manager for it, so the **host** supplies the write path, which is what puts an
  AI-applied batch on the same shared undo stack as the user's own edits.

The chat does not call these three. It treats every answer that changes the project as one
[edit plan](#one-edit-plan): `AIChatComponent` sends an edit request through `sendProjectMessage`
once a timeline is wired in, previews the answer with `previewProjectEdit` and renders ONE
`EditPlanCard` whose single **Apply** calls `applyProjectEdit`, whatever the answer holds: a patch,
`timelineOps`, or both. A refused plan gets the card with the reason and **no button**, because a
suggestion that cannot be applied must still say why but must not look clickable. The envelope
methods above remain the seam for the service tests, the eval harness and `TimelineOps`'s own
round trip. See [chat component](chat-component.md#one-answer-one-card).

Tests: `Tests/Timeline/TimelineOpsTests.cpp` — per-op apply, one-step undo, all-or-nothing with the
failing op named by index, caps and bounds, the ungrammatical capabilities, pinned preview strings,
the patch-grammar pin, and the service seam end to end.

## Measuring sound shape

Valid is not the same as right: a plan can preview fine and still leave the envelope at its default
(sustain 1, a drone) or add an ADSR nothing triggers. `Source/AI/SoundShapeChecks.h` holds three pure
checks over a parsed response root: `checkPluck` (some envelope with sustain <= 0.01, decay <= 0.5,
attack <= 0.02), `checkFilterEnvelope` (a modulation whose source is an envelope - an
`envelope.id` of the response, or an ADSR node of the response or the existing patch - and whose dest
is a Filter, by `destParam` `cutoff` or the Filter's cutoff port; a Filter insert of a new track must be
moved by that track's own `envelope.id`, since any other envelope follows another track's notes) and `checkAcid` (that, with the
`LPF24` filter type, resonance >= 60% of its range, envelope sustain <= 0.3 and a Saw or Square
oscillator: an `instrumentParams.waveform` of the new track, or an Oscillator node of the response or
the existing patch). A param the response
leaves out is judged at the module's own default. Tests: `Tests/AI/SoundShapeChecksTests.cpp`; the
prompt's three worked responses are previewed and scored in
`AIIntegrationServiceProjectEditTests.cpp`. `Tools/AIEvalHarness --mode project` sends five requests
through `sendProjectMessage` and prints per scenario whether the plan was valid and whether its shape
is right ([README](../../Tools/AIEvalHarness/README.md)).

## Measuring validity

`Tools/TimelineOpsHarness` is the timeline counterpart of `Tools/AIPatchHarness`, adapted to a seam
that has no live model to replay against: a `timelineOps` envelope's validity is a deterministic
function of `TimelineOps::validate` and a fixed graph, so what it measures is a fixed set of
**recorded fixtures** (`Tools/TimelineOpsHarness/Fixtures/*.json`) rather than prompts sent to
Ollama. The scenario set spans a valid three-op envelope; a valid `placeMidiClip` carrying a real
base64 `.mid`; notes over `TimelineDoc::kMaxNotesPerClip`; a `writeLane` value outside the live
parameter's range; an unknown op field; a SMPTE-format `.mid` blob; a `midBase64` over
`kMaxMidBlobBytes`; a valid `addInstrumentTrack` with one Filter insert followed by `placeClips` on
its new track (checked with a validation-only stub host, since the op refuses without one); and the
two-door pin — a timelineOps-shaped payload smuggled under a patch's
`"timeline"` key, checked through `AIStateMapper::validatePatch` instead and expected to come back
`TimelineNotAllowed`.

Each fixture pins its expected valid/invalid outcome plus a message — or, for the patch-smuggle
fixture, a `PatchValidationError` name — that the actual result must contain, and the harness prints
a per-fixture expected-versus-actual table and a summary match rate. It is gated behind
`-DENABLE_AI_HARNESS=ON` like its siblings, though, having no live model in the loop, it needs none
to build or run. `Tests/Timeline/TimelineOpsFixtureTests.cpp` asserts the identical fixture files as
fast gtest cases, which is what CI gates on.
