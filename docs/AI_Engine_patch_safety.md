# AI Engine: Patch & Timeline Safety

This document covers the trust boundary for AI-authored content: patch validity (constrained
decoding, retry and repair), few-shot patch examples, the untrusted-timeline data model
(`validateTimeline`), arrangement context, timeline operations, and the agentic timeline security
model (§5-10 below, numbering kept as in the parent doc). The AI Engine's architecture,
communication pattern, and patch-diff/feedback UI live in the companion doc,
[`AI_Engine.md`](AI_Engine.md) (§1-4). `AIChatComponent` and its logging rules live in
[`AI_Engine_chat_component.md`](AI_Engine_chat_component.md) (§11). Providers, accounts,
conversation history, and quota mechanics live in
[`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md).

- [5. Patch Validity: Constrained Decoding, Retry, and Repair](#5-patch-validity-constrained-decoding-retry-and-repair)
- [6. Few-Shot Patch Examples](#6-few-shot-patch-examples)
- [7. Untrusted Timeline Data: `validateTimeline`](#7-untrusted-timeline-data-validatetimeline)
- [8. Arrangement Context: `ArrangementContext::summarize`](#8-arrangement-context-arrangementcontextsummarize)
- [9. Timeline Operations](#9-timeline-operations)
- [10. The Agentic Timeline Security Model](#10-the-agentic-timeline-security-model)

---

## 5. Patch Validity: Constrained Decoding, Retry, and Repair

Untrusted model output reaches the graph through one gate — `AIStateMapper::validatePatch(...,
trusted=false)` — which returns a typed `PatchValidationError` plus a message. That gate is a
security boundary and is never relaxed to raise the pass rate; everything below works on the
*generation* side instead.

Each node's `params` keys are checked against that module's real `paramID`s
(`PatchValidationError::UnknownParameterKey`), not merely type/range-checked when present. A key
that matches nothing is rejected rather than silently ignored — `applyParamsToProcessor` only ever
looks a key up *by name* among the real parameters, so an unmatched key (e.g. a corrupted or
typo'd key from a decoding artifact) was previously dropped on the floor, leaving that parameter at
its default while the patch still reported success. Rejecting it surfaces the failure so the
bounded retry/repair loop below actually engages.

One consequence worth calling out: the schema's choice-parameter `properties` are a union across
*all* module types (see `SchemaChoiceParamIdsAreUnambiguous`), so the grammar cannot express "only
this node type's params" and `params` itself stays `additionalProperties: true` so numeric params
remain emittable (see `SchemaStillAllowsNonChoiceParameters`). A key that is real on some other
module — `"waveform"` sent for a Filter node, say — used to be silently ignored and is now a hard
`UnknownParameterKey` rejection of the whole patch. This is an intentional tightening, not measured
against live traffic (`Tools/AIPatchHarness` needs a live Ollama and is excluded from CI), so watch
for `UnknownParameterKey` becoming a new significant rejection class in a harness run.

### Measuring first

`Tools/AIPatchHarness` replays a fixed set of realistic prompts through the exact production path
and tallies rejections by `PatchValidationError`. It needs a live Ollama, so it is opt-in
(`-DENABLE_AI_HARNESS=ON`) and excluded from CI. See its README for flags and caveats.

`Tools/AIEvalHarness` answers a different question: of the patches that *do* pass validation and
apply, are they usable — an output wired to a source, not just schema-legal JSON? It scores 40
golden prompts against `Source/AI/PatchEval.h`'s structural checks (unit-tested in
`Tests/AI/PatchEvalTests.cpp`, model-independently) and is what makes switching to a cheaper or local
model a measured decision instead of a guess. Same opt-in flag, same exclusion from CI — see its
README.

**A quoted rate means nothing without the model and the sampling settings alongside it.**
`AIPatchHarness` pins `--temperature 0` and a fixed `--seed` by default (unlike `AIEvalHarness`,
which leaves them unset for its own before/after comparisons) — an unpinned run swings ~8
points on the same model and prompts, so a bare percentage is not comparable across runs unless
both sides fixed sampling the same way. `--provider remote` ignores these entirely (`RemoteProvider`
has no sampling knobs), which is why the harness's `--json` output only ever records
`temperature`/`seed` for `--provider ollama` — never claim pinned sampling that wasn't actually
applied. `.github/workflows/ai-eval-nightly.yml` runs this measurement on a schedule, OFF by
default; see `Tools/AIPatchHarness/README.md`'s "Nightly scheduled eval" section for the switch,
the runner, and the ratchet against a committed baseline.

Two facts that measurement established, and that any future change here should be re-checked
against:

- **`format` enforcement is backend-dependent.** Ollama compiles the JSON schema to a GBNF grammar
  for llama.cpp models, so anything the schema *encodes* becomes impossible to emit. MLX-backed
  models (e.g. `gemma4:e4b-mlx`) ignore `format` entirely and fall back to prompt compliance.
  Schema work therefore helps some backends and not others; retries cover the rest.
- **Validation reports only the first error.** Fixing one class of failure does not simply shrink
  the histogram — it *unmasks* whatever was next in the same patch. An error count that rises after
  a fix is expected, not a regression.

### The four layers, most upstream first

1.  **Constrained decoding (`getPatchSchema()`).** The strongest guarantee, because the backend
    can enforce it. Two things matter here:
    - The `node.type` enum is generated from `moduleFactory`, not hand-written. The old literal had
      drifted — `Voice Mixer` was creatable but missing from the schema, so a constrained decoder
      could never produce one. Guarded by `SchemaModuleTypesMatchTheFactory`.

      Because the enum is *derived*, **registering a module makes it model-authorable by default** —
      the right default for an ordinary DSP module, the wrong one for anything that names an
      external resource or carries privileged state (a hosted plugin, a timeline feed). Such a
      module goes into `kNonAuthorableModuleTypes` (AIStateMapper/AIStateMapperInternal.h) when it is registered.
      `AIStateMapperTest.AuthorableModuleTypesGolden` pins the exact resulting list, so either kind
      of addition fails the build until the choice is made deliberately.

      The set today: `Attenuverter` / `Mod Slot` (an implementation detail of the `modulations`
      array), `Track In` (a timeline feed, whose only meaningful state lives outside the patch), and
      **`Rec Tap`**. Rec Tap's reason is the sharpest of the four: it **names a file path on
      disk**, so a model that could author one could aim a recording anywhere the app can write —
      the write-side twin of the `"state"` file-path restriction below.

      **The deny set is enforced by the validator, not just by the schema.**
      `validatePatch(..., trusted=false)` rejects any node
      whose type is in `kNonAuthorableModuleTypes` with `PatchValidationError::InternalModuleNotAllowed`.
      Leaving it to the schema enum alone would have made "non-authorable" mean *un-suggested*
      rather than *unreachable*: the schema is only a grammar for backends that compile it, and a
      patch can also arrive from a local model or any future caller that never saw it. The **trusted**
      path is untouched — our own saves must round-trip an Attenuverter or a Track In. Callers that
      gate app-authored data with `trusted=false` before applying it trusted (session state,
      `.agsproj`, snippet files — the docs/layout.md §12.5 pairing) pass
      `allowInternalModuleTypes=true`: they are gating structure, ids, ranges and tampering, not
      authorship, and our own files legitimately contain internal nodes. The parameter defaults to
      `false`, so a new model-facing caller gets the restriction without knowing it exists.
    - `node.params` enumerates every **choice** parameter's real options, so a model physically
      cannot emit `"waveform": "White Noise"`. `additionalProperties` stays `true` so numeric
      parameters remain expressible — a grammar restricted to the listed keys would make `cutoff`
      unreachable, a worse bug than the one being fixed. Guarded by
      `SchemaConstrainsChoiceParametersToTheirOptions` and `SchemaStillAllowsNonChoiceParameters`.

    The schema is an **output** contract: every property in it is something the model is invited to
    emit. Reference data does not belong there, which is why the old `parameterChoices` block was
    removed — the choice lists do real work as enums in `params`, and stay human-readable in the
    system prompt via `getModuleSchema()`.

2.  **Validate-and-retry (`applyPatchWithRetry`).** Cross-references — "connection names an id that
    does not exist" — cannot be expressed in a JSON schema at all, so they are handled after the
    fact. On rejection the *specific* validation message is fed back ("that patch was rejected
    because X; return a corrected patch"). Retries are bounded by `kMaxPatchRetries` (2, i.e. 3
    attempts total), each is announced through the `onRetry` callback so the chat shows what is
    happening, and exhaustion surfaces the error rather than looping.

    Rejection messages name the offending id **and list the valid ones**. This is not cosmetic: a
    bare "unknown node id" gives the model nothing to aim at and the retry tends to repeat itself.

    **Merge-mode id collisions are rejected, not resolved** (`NodeIdTypeMismatch`). A patch node
    whose `id` matches a live node but whose `type` differs used to fall through the
    update-in-place branch and create a *second* node, rebinding `idMap[id]` to it — so every later
    connection/modulation in the same patch that meant the original node silently re-pointed at the
    new one. Ids the same patch also `remove` are exempt: removals run first, so re-using such an id
    names a genuinely new node. The trusted path is unaffected by design.

3.  **Repair — one rule only.** A patch that states no `"mode"` and is rejected as a *replace* is
    re-validated as a *merge*, and applied that way if it passes. This costs no round-trip and
    alters nothing about the patch's content; it only resolves which mode the caller guessed, and
    validation is the arbiter. It is deliberately **one-directional**: reading a patch as a merge
    can only preserve nodes the user already had, whereas quietly turning a merge into a replace
    would wipe their patch. Never fires when the model stated a mode. Guarded by the
    `AIPatchRetryTest.Repair*` tests.

    No other repair is applied. Dropping a connection that names a stale id would silently give the
    user less than they asked for, which is exactly the silent-partial-apply failure the strict
    gate exists to prevent.

4.  **Prompt.** Weakest guarantee, so it carries the least. It documents the module/parameter
    tables and the merge-vs-replace rules; the enforceable parts live in the schema.

### Logging

One line per rejection and one per retry. Retries happen at user-click frequency, so this stays
within the no-high-frequency-logging rule — never log per candidate token or per validation pass.

Tests: `Tests/AI/AIPatchValidationTests.cpp` (table-driven, one malformed patch per
`PatchValidationError`, plus the schema contract) and `Tests/AI/AIPatchRetryTests.cpp` (retry bound,
error feedback, repair scope).

## 6. Few-Shot Patch Examples

The first `AIEvalHarness` sweep (6 local models, 40 prompts each, single run) found nothing
shippable — pass rates ranged from the high 50s to low 70s.
The failures were not syntactic (`validatePatch` already forces well-formed JSON) but structural: a
model produces a patch with no `Audio Output` node, or with nodes that are never actually wired to
one. The prompt's module/parameter tables and syntax snippets tell a model the *rules*; they never show
it a complete, working patch end to end.

`initSystemPrompt()` (`Source/AI/AIIntegrationService.cpp`, ~line 430) now embeds 5 hand-authored
`(prompt → complete, correctly-connected patch)` worked examples, covering the categories the task
called for:

- **From scratch, bass** — Oscillator → Filter → VCA → Audio Output, with a Filter Env and an Amp Env
  each driving a real modulation target (`Cutoff` destPort 1, `CV` destPort 1).
- **From scratch, lead** — the same shape plus an LFO into the Oscillator's `Fine` input (destPort 4) —
  a functional CV target, unlike the Oscillator's `Pitch` target (destPort 0), which mono oscillators
  silently ignore. The prompt calls this trap out explicitly.
- **FX chain** — a source patch chained through Distortion → Chorus → Reverb, in line, into Audio
  Output. Also flags that Delay's and Reverb's advertised CV modulation targets are vestigial (the
  audio engine never reads that CV) — don't route `modulations` into them.
- **Merge mode, adding a node** — stacking a second, detuned Oscillator into an existing filter, only
  emitting the new node and the one new connection per the delta convention.
- **Merge mode, removing a node** — removing a Distortion node and rewiring around the gap, the exact
  failure category (a node removed but the chain left dangling) the task was written to fix.

**Non-overlap with `AIEvalHarness`.** Reusing one of the harness's 40 eval prompts as a few-shot example
would be train/test contamination and invalidate the measurement. Each example's prompt text was
checked by hand against `Tools/AIEvalHarness/Main.cpp`'s `scenarios()` for verbatim or near-paraphrase
overlap, and `AIIntegrationServiceTest.WorkedExamplePromptsDoNotOverlapEvalScenarios`
(`Tests/AI/AIIntegrationServiceTests.cpp`) enforces it in CI against a manually-synced copy of the 40
prompts — a guard against future drift, not a substitute for the manual check when new examples are
added.

**Proof.** `AIIntegrationServiceTest.WorkedExamplePatchesAreStructurallyValid` catches a hand-authoring
mistake (a dangling node, an out-of-range parameter) at CI time by running every example through
`AIStateMapper::applyJSONToGraph` and `synth::evaluatePatch`. That is necessary but not sufficient — it
proves the examples themselves are valid, not that they help a model.

The actual evidence is a before/after `Tools/AIEvalHarness` pass-rate delta, measured on the same
machine immediately before and after this change. The technique's biggest win was on the backend that
ignores `format` entirely and falls back to prompt compliance (see "Measuring first" above) — exactly
the case where schema/grammar enforcement can't help and prompt content is the only lever available.
Results were more mixed on other models: a longer prompt costs some models basic JSON-production
reliability, so a bigger example set is not a strictly free improvement and should be measured per
model before being widened further — the system prompt's literal text roughly doubled in size to add
these five examples, a real cost on any backend billed or latency-bound per token.

## 7. Untrusted Timeline Data: `validateTimeline`

`synth::validateTimeline` (`Source/Timeline/TimelineValidator.h/.cpp`) is the untrusted gate for
AI/tool-supplied **timeline** JSON — tracks, clips, notes and automation lanes in the dialect
`TimelineDoc::toVar` writes. It is a separate function from `validatePatch`, with its own error
enum (`TimelineValidationError`) and its own name table (`timelineValidationErrorName`, same idiom
as `patchValidationErrorName`).

### The two-door model

There are two ways timeline data could reach the app, and exactly one of them is open:

| Door | Status | Rule |
| --- | --- | --- |
| The **patch grammar** — a `"timeline"` key inside a patch suggestion | **Closed, permanently** | `validatePatch(trusted=false)` refuses it (`TimelineNotAllowed`, §"Patch format forward-compatibility"). A patch is applied to the graph; a timeline is not part of a graph. |
| The **tools** — the discrete app-side timeline tools (add-track, place-clips, write-lane), §9 below | **Open, guarded** | Each payload goes through `validateTimeline` before it touches `TimelineDoc`. |

This is the deliberate commit that opens the door the patch format closed — through its own guarded
entrance, *not* by relaxing the patch path. Pinned by
`TimelineValidatorTest.PatchGrammarStillRefusesTimelineData`, which takes a document this validator
accepts, smuggles it into a patch, and asserts `validatePatch` still refuses it.

### Contract

-   **Validates strictly, mutates nothing** — not the doc, not the graph, not the input `var`.
-   The caller applies via `TimelineDoc::fromVar` (all-or-nothing) **only** after this passes.
-   **A pass means the apply cannot fail.** The last thing the function does is load the document
    into a throwaway `TimelineDoc` to prove it. A var that satisfies every named check and is still
    refused by the loader yields `InternalError` — the validator and `fromVar` have drifted, and the
    caller applies nothing either way. (The one reachable case today is a malformed
    `nextTrackId`/`nextClipId`/`nextLaneId`/`nextNoteId`/`nextMarkerId` counter — the document's own
    bookkeeping, which a tool payload has no reason to carry.)
-   **Rejects where the trusted paths repair.** `fromVar` clamps a breakpoint's tension into
    `[-1, 1]` and its value into the lane's range snapshot, and repairs a broken sort order. None of
    that happens for untrusted data: a value we would have to correct is a value the sender did not
    mean, so it is refused with a message that says which one and why.
-   Only the **first** problem is reported, like `validatePatch` — fixing one class can unmask the
    next. Every message names the offending track/clip/note/lane so it can be handed back as a
    correction rather than a complaint.

### The checks

1.  **Structural** — root is an object of the `TimelineDoc` dialect; `version` present, integer, and
    no newer than `TimelineDoc::kFormatVersion`; `tracks` and `markers` (if present) arrays; ids
    present, positive and unique per kind; one lane per `(nodeUuid, paramId)` doc-wide.
    **Unknown top-level keys are refused**, where `PatchDocument` deliberately *preserves* the ones
    it does not understand. That asymmetry is the point: forward-compatibility is a property a
    document format needs and an untrusted payload does not, and an ignored key is exactly how a
    later build starts honouring a field today's gate never inspected. The allowlist is `version`,
    `tracks`, `markers` and the five next-id counters — and a key earns its place there **only** by
    having a per-item check written for it (see 9 below). Adding one without that check defeats this
    whole paragraph.
2.  **Caps** — the per-container limits are `TimelineDoc`'s own constants, referenced and never
    duplicated: `kMaxTracks` (256), `kMaxClipsPerTrack` (4096), `kMaxNotesPerClip` (16384),
    `kMaxLanesPerTrack` (512), `kMaxBreakpointsPerLane` (16384), `kMaxMarkers` (1024),
    `kMaxMarkerTextLength` (128). Two more exist only on this path,
    because they bound the whole payload rather than one container:
    `kMaxTotalNotesUntrusted = 65536` (notes summed across every clip) and
    `kMaxPpqUntrusted = 100000.0` (the largest beat position or length in beats — ~14 hours at
    120 BPM).
3.  **Beats** — every `startBeat`, `lengthBeats`, fade and breakpoint beat must be finite, `>= 0`
    and `<= kMaxPpqUntrusted`; lengths must be `> 0` (`BeatOutOfBounds`).
4.  **Notes** — pitch `0..127`, velocity `1..127`, channel `1..16`, **rejected** rather than
    clamped (`NoteOutOfRange`).
5.  **Lanes** — every `(nodeUuid, paramId)` must resolve against the **live graph**: a node carrying
    that `uuid` property, holding a `RangedAudioParameter` with that `paramID`. Unresolvable is
    `UnresolvableBinding` — an orphaned binding is a state the app *recovers from* when a node
    disappears under an existing lane, not one untrusted input may author from nothing. The
    same rule applies to a track's non-empty `bindingUuid` (empty is legal: an unbound track).
    Every breakpoint value must sit inside the **resolved parameter's real range** — *not* the
    lane's own `RangeSnapshot`, which is data the sender wrote and therefore cannot be the authority
    on what the parameter accepts. `tension` must be in `[-1, 1]`, `curve` in `0..2`.
6.  **Assets** — any clip with a non-empty `assetRef` is refused (`AssetNotAllowed`). Stricter than
    `TimelineDoc::isValidAssetRef`, which only stops a path escaping the bundle: untrusted input may
    not name an asset **at all**, however well-formed.
7.  **Record modes** — a lane may only ask for `Read` (1) or `Off` (0). `Touch`/`Latch`/`Write` arm
    the lane to capture the user's own gestures, which is the user's decision
    (`RecordModeNotAllowed`).
8.  **Track kinds** — `Midi` (0), `Audio` (1) and `Automation` (2); reserved kinds 3..15 are refused
    (`ReservedKindNotAllowed`). An audio *track* is a legal shape — what makes it unauthorable in
    practice is check 6, since an audio track with no asset-bearing clip is just an empty row.
9.  **Markers** — the `"markers"` array (`TimelineDoc::Marker`: `id`, `beat`, `text`, `colourArgb`)
    is allow-listed at the top level *because* it is checked here, field by field. A marker carries
    no binding, no asset and nothing executable — it is a beat, a label and a colour — so the rules
    are entirely about bounds and shape:
    - `id` required, positive, unique across markers (`MalformedRoot`), like a note's;
    - `beat` finite, `>= 0` and `<= kMaxPpqUntrusted` (`BeatOutOfBounds`) — the same bound every
      other beat in the document gets;
    - `text` a string of at most `kMaxMarkerTextLength` (128) characters, **rejected not truncated**
      (`MarkerTextTooLong`) — the same rule note pitch 128 gets: a label we would have to shorten is
      not the label the sender meant. An EMPTY label is legal (an unlabelled flag);
    - `colourArgb` an integer in the full 32-bit range `0 .. 4294967295` — inert display data, so
      every value is legal and only the *type* and the range are checked;
    - array size at most `kMaxMarkers` (`TooManyMarkers`);
    - **unknown keys inside a marker object are refused.** Markers are the one container checked with
      a *closed key set* — tracks, clips and notes only reject unknown keys at the top level. The
      reasoning is check 1's, one level down, and markers got the stricter rule from the start
      because that reasoning was already known when they were added; retro-fitting it onto the older
      containers is a separate change with its own compatibility question.

### Trusted-only forever

Audio assets and the clips that reference them, plugin state blobs, a node's `"state"` object
(`ModuleBase::setExtraState`), and lane record arming. Each is a capability that reaches outside the
document — the filesystem, opaque third-party state, or the user's own playing — and none of them
has an untrusted form. Widening `validateTimeline` to admit one is the same class of mistake as
relaxing `validatePatch` to raise the AI pass rate.

Tests: `Tests/Timeline/TimelineValidatorTests.cpp` (table-driven, one deliberate defect per case, every
`TimelineValidationError` value covered).

## 8. Arrangement Context: `ArrangementContext::summarize`

`synth::ArrangementContext::summarize` (`Source/Timeline/ArrangementContext.h/.cpp`) is the
timeline sibling of the patch-context injection: a compact, token-bounded, read-only text summary
of the arrangement — tracks, clip windows, note counts and automation lanes — folded into the same
outgoing AI request the current patch JSON already rides on.

### Where it's built in

`AIIntegrationService::buildPatchAugmentedContent` (the function `sendMessage` calls to build the
structured-output request) is the one seam patch context reaches the model through today ("Current
patch state:\n\`\`\`json...\`\`\`"). This adds a second, independent section right beside it,
under its own "## Arrangement" delimiter, included only when
`ArrangementContext::summarize` returns non-empty text (an empty/absent timeline adds nothing, the
same "say nothing rather than say empty" rule the patch section already follows). `AIIntegrationService`
does not own a `TimelineDoc`/`TransportService` itself — `MainComponent::initialiseCommon` installs
non-owning pointers to its own (app-lifetime) instances via `setTimelineContext()`, mirroring
`setProvider()`/`setUndoManager()`.

### Security model — read path only

This is a **read-only summary that never round-trips**: nothing it emits can be replayed back into
the timeline, and it inherits the same two boundaries `validateTimeline` (§7) enforces on the
*write* path, applied here to a text summary instead of a JSON payload:

-   **Never a file path.** An audio clip's `assetRef` is bundle-relative (`Clip::assetRef`); the
    summary emits only the bare file name (everything after the last `/`) and drops the directory
    component outright — never a stored-then-redacted path, a name that was never anything but the
    bare file name to begin with.
-   **Never a plugin/implementation identifier.** A bound track or lane is named by the bound
    node's display name (`juce::AudioProcessor::getName()`, the same string its title bar shows) —
    never a node id, factory type key, or raw uuid. `summarize()` resolves every binding against
    the **live graph** passed in (not the doc's own cached `orphaned` flags, which may be stale);
    an unresolvable binding reports `"MISSING"` rather than leaking the uuid it failed to resolve.

### Format and budget

One line per item, in `TimelineDoc`'s own stable order — a header (`"Arrangement: N tracks, bpm B,
T/S, loop [a, b)"` or `"loop off"`), then per track: kind, name, `armed`/`muted`/`soloed` flags
(shown only when set), and its binding; a compressed clip line for MIDI tracks (`"3 clips @ 0-8,
8-12, 16-20 beats; 42 notes total"`), one line per clip for audio tracks (name + beat window + bare
file name); then one line per automation lane (`"cutoff lane on Filter: 12 points, Read"`).
`maxChars` (default 2000) is enforced at **track granularity only** — a track is included whole or
not at all, so the result is never cut mid-line — and a dropped tail is marked deterministically
with `"… [+K more tracks]"`.

Tests: `Tests/Timeline/ArrangementContextTests.cpp` (tracks/clips/lanes rendering across bound/unbound/
orphaned states, track-granularity truncation, the empty-doc case, and the file-path-leak pin;
plus a seam-level test that the injected request gains an "## Arrangement" section exactly when
`buildPatchAugmentedContent` should add one).

## 9. Timeline Operations

`synth::TimelineOps` (`Source/Timeline/TimelineOps.h/.cpp`) is the **write** half of the timeline
seam: discrete, validated, previewable operations a model may ask for, applied to `TimelineDoc` as
one undo step. §7 is the gate they go through; §8 is the read-only context that lets a model know
what to ask for in the first place.

### The envelope

```json
{ "timelineOps": [
  { "op": "addTrack",   "kind": "midi", "name": "Bass" },
  { "op": "placeClips", "track": "Bass",
    "clips": [ { "startBeat": 0, "lengthBeats": 4, "name": "A",
                 "notes": [ { "startBeat": 0, "lengthBeats": 1,
                              "pitch": 36, "velocity": 100, "channel": 1 } ] } ] },
  { "op": "writeLane",  "nodeUuid": "…", "paramId": "cutoff",
    "points": [ { "beat": 0, "value": 800, "tension": 0, "curve": 1 } ] },
  { "op": "placeMidiClip", "track": "Bass", "startBeat": 0,
    "midBase64": "<base64-encoded Standard MIDI File>" }
] }
```

This is the **client** half of the capability. The private backend repo owns the **server** half — the
capability schema the model actually emits against, the counterpart of `getPatchSchema` for
patches. Nothing here trusts that schema: an envelope is re-validated locally whatever produced it.

**The LOCAL (Ollama) path can author this envelope too**, behind
`AIIntegrationService::setTimelineToolsEnabled` — set unconditionally on by `MainComponent` now
that the timeline is GA, and additionally gated on a timeline context being installed
(`hasTimelineContext()`). While active, three things change and nothing else:

- the system prompt gains a "TIMELINE & AUTOMATION OPERATIONS" section teaching the four ops
  (swapped into the existing history **in place** — a mid-conversation toggle never clears the
  chat, and off means byte-identical to the pre-timeline prompt);
- the structured-output `format` becomes `AIStateMapper::getPatchSchemaWithTimelineOps()` —
  `getPatchSchema()` plus an OPTIONAL `timelineOps` array whose item schema is deliberately
  permissive (one object shape, only `"op"` required): it is a grammar that lets the model express
  the ops, not a validator — `TimelineOps::validate` remains the gate, and the reserved-fields
  rule (`"timeline"`, `"schemaVersion"`, node `"uuid"` absent) is untouched;
- the outgoing request grows an `## Automation targets` section
  (`buildAutomationTargetsSection()`): one line per uuid-bearing node listing its float parameter
  ids and RAW ranges — the addressing channel `writeLane` needs. Node uuids appear there **on
  purpose**, despite `ArrangementContext`'s no-uuid rule: that rule keeps identifiers out of the
  human-readable summary; this section is what makes the grammar usable at all, uuids are random
  per-node identity (never a path or plugin id), and `validate()` only accepts pairs that resolve
  against the live graph anyway. Bounded to ~2000 chars, whole lines, with a truncation marker.

Extraction, validation, preview and Apply are unchanged and provider-agnostic either way — they
act on what a response actually carries, and the user's Apply click stays the write gate.
Pinned by `AIIntegrationServiceTest.TimelineToolsToggle*` / `AutomationTargetsSection*`.

| Op | What it does | What it deliberately does not do |
| --- | --- | --- |
| `addTrack` | Creates the **doc** track. `kind` is `"midi"` or `"automation"`. | No graph node, no Track In wiring — binding a track to a module is a routing decision about the user's own patch, so it stays a user/host gesture. The new track is unbound and the preview says so. `"audio"` is not offered: an audio track needs an asset, and assets are trusted-only. |
| `placeClips` | Places clips (and their clip-relative notes) on a MIDI track, targeted by exact name or `{"index": N}`. | A name matching no track — or more than one — rejects the whole batch rather than guessing. |
| `writeLane` | Find-or-creates the lane for `(nodeUuid, paramId)` on the document's Automation track (creating that track if there is none, exactly as `MainComponent::automateParameter` does it), then REPLACES every point in the written span (min..max beat of the payload, inclusive) in one `editBreakpoints` call. | Never sets a record mode; never widens a range. |
| `placeMidiClip` | Decodes `midBase64` and parses it with `MidiClipFile::importFromStream`, placing one clip per non-empty imported SMF track on the target MIDI track at `startBeat` (clip length is `ceil` of its last note's end, floored at 1 beat — reusing `MidiClipFile::importIntoTrack`). | No paths, no plugin ids, no code — a `.mid` blob can only ever decode to notes, which is why this is the one op that accepts an opaque binary payload at all. |

### `placeMidiClip` — the `.mid` blob is the safest AI note surface

Every other op in this grammar is closed field-by-field (§"Capabilities are absent from the
grammar" below). `placeMidiClip` is the one exception that accepts an opaque, base64-encoded blob —
and it is safe to accept specifically *because* `MidiClipFile::importFromStream`
(`Source/Timeline/MidiClipFile.h`) was designed as "the safest future AI patching surface" from the
start: a Standard MIDI File can only ever decode to notes (`pitch`/`velocity`/`channel`/timing).
There is no way to encode a file path, a plugin identifier, or code inside one, unlike almost any
other blob a model could hand back. `placeMidiClip` reuses that exact importer — the same strict
parser a user's own MIDI-file import goes through — rather than a looser variant for AI input.

Bounds, in the order they're checked:

-   **`midBase64` size**, against `TimelineOps::kMaxMidBlobBytes` (262144) — checked on the
    STILL-ENCODED string, *before* any decode is attempted, so an oversized blob is rejected as
    cheaply as any other length check rather than by allocating a decode buffer for it first.
-   **Decodability** — invalid base64 is rejected outright.
-   **`MidiClipFile::importFromStream`'s own checks** — not a readable SMF, SMPTE time format (PPQ
    only), or any one imported track's note count over `TimelineDoc::kMaxNotesPerClip` all reject
    the op (an import failure never means "import what parsed and drop the rest").
-   **An empty result** — a blob with no notes in it is refused; there is nothing to place.
-   **The batch's own note/clip caps** — every note the blob contains still counts toward
    `kMaxTotalNotesUntrusted` exactly like a `placeClips` note does, and the target track's clip
    count is still checked against `TimelineDoc::kMaxClipsPerTrack` before anything is placed.

Any failure at any of those steps rejects the WHOLE batch, the same all-or-nothing contract every
other op has.

### Sibling, never nested

`timelineOps` sits **beside** a patch, never inside one. `validatePatch(trusted=false)` still refuses
a `"timeline"` key in patch JSON and always will (§7's two-door model) — `"timelineOps"` is a
different key, so a single structured response may legitimately carry a patch and an ops envelope
on the same object, and each is validated and applied by its own gate with its own Apply button.
Pinned by `TimelineOpsTest.PatchGrammarStillClosed`: the document dialect smuggled in under
`"timeline"` is refused, while the same intent as a sibling `timelineOps` key is accepted by both
halves.

### Trust posture — identical to the patch card

`validate()` → preview → the user clicks Apply → `apply()`. Nothing is applied because a model asked
for it; a person agrees to it first, having read a summary of what it does.

-   `validate()` mutates nothing and returns `previewText`, a deterministic sentence —
    `Adds midi track "Bass" (unbound - bind it in the timeline panel); places 1 clip (8 notes) at
    0-4 on "Bass"; writes 12 points to Filter cutoff over beats 0-11`. A bound module is named by
    its **display name**, never its uuid (§8's rule, on the write path).
-   The per-op checks are `validateTimeline`'s, **reused rather than re-stated**: the same caps
    (`TimelineDoc::kMax*`, `kMaxTotalNotesUntrusted`, `kMaxPpqUntrusted`), the same bounds, and the
    same rule that untrusted input is **rejected where a trusted path would clamp** — pitch 200 is
    refused, not rewritten to 127; a breakpoint outside the **live** parameter's range is refused,
    not pulled inside it. Two more caps bound the batch itself: `kMaxOps` (64) and `kMaxNameChars`
    (128); a third, `kMaxMidBlobBytes` (262144), bounds only `placeMidiClip`'s `midBase64`.
-   **Capabilities are absent from the grammar, not refused field by field.** An op has no
    `assetRef`, no `recordMode`, no `bindingUuid`, and no kind beyond `midi`/`automation`. A `.mid`
    blob is not an exception to this — it can only ever decode to notes, never a path or an id.
    Unknown fields *inside* an op are **rejected**, so a future field cannot be smuggled past a gate
    that never inspected it — the same reasoning as `validateTimeline`'s unknown-top-level-key
    refusal. Unknown keys at the *envelope root* are ignored, because that is where the sibling
    patch's own `nodes`/`connections`/`mode` live.
-   **All-or-nothing, and the preview cannot lie.** `validate()` runs the batch against a throwaway
    copy of the document (`fromVar(doc.toVar())` — replaying our own serialisation is trusted by
    definition) and `apply()` runs the *same code* against the real one, so every op sees the effect
    of the ones before it and no preview can describe an apply that then fails. A rejection means the
    live doc was never touched at all.
-   **One undo step.** The whole batch runs inside a single `AppUndoManager::recordTimelineChange`,
    so however many tracks, clips, notes and breakpoints it touches, one Cmd+Z reverts all of it —
    the contract `MidiRecorder::stopAndCommit` already relies on for a take's clip plus its notes.

### The chat seam

`AIIntegrationService` (once
`setTimelineContext()` has wired the live timeline in) gains:
`extractTimelineOps()` — the same extraction `applyPatch` performs, returning the parsed root when
it carries a `timelineOps` key (**presence**, not well-formedness, so a malformed envelope is
surfaced as a visible rejection instead of being silently dropped); `previewTimelineOps()` — the
validate step; and `applyTimelineOps()`, which routes through a `TimelineOpsApplyCallback` that
`MainComponent::initialiseCommon` installs. The service holds the doc only as a `const` pointer and
owns no undo manager for it, so the **host** supplies the write path — which is what puts an
AI-applied batch on the same shared undo stack as the user's own edits.

`AIChatComponent` renders `TimelineCard` beside `PatchCard`, to the same conventions, with an
"Apply timeline changes" button. A response carrying both gets both cards; a rejected envelope gets
the card with the reason and **no button**, because a suggestion that cannot be applied must still
say why but must not look clickable.

Tests: `Tests/Timeline/TimelineOpsTests.cpp` (per-op apply, one-step undo, all-or-nothing with the failing
op named by index, caps/bounds, the ungrammatical capabilities, pinned preview strings, the
patch-grammar pin, and the service seam end to end).

### Measuring validity: the timeline-ops eval scenarios

`Tools/TimelineOpsHarness` is the timeline counterpart of `Tools/AIPatchHarness`, adapted to a
seam that has no live model to replay against: a `timelineOps` envelope's validity is a
deterministic function of `TimelineOps::validate` and a fixed graph, so what it measures is a fixed
set of **recorded fixtures** (`Tools/TimelineOpsHarness/Fixtures/*.json`) rather than prompts sent
to Ollama. The scenario set spans: a valid three-op envelope; a valid `placeMidiClip` carrying a
real base64 `.mid` (generated once with `MidiClipFile::exportClip`); notes over
`TimelineDoc::kMaxNotesPerClip`; a `writeLane` value outside the live parameter's range; an unknown
op field; a SMPTE-format `.mid` blob; a `midBase64` over `kMaxMidBlobBytes`; and the two-door pin —
a timelineOps-shaped payload smuggled under a patch's `"timeline"` key, checked through
`AIStateMapper::validatePatch` instead and expected to come back `TimelineNotAllowed`. Each fixture
pins its expected valid/invalid outcome plus a message (or, for the patch-smuggle fixture, a
`PatchValidationError` name) the actual result must contain, and the harness prints a
per-fixture expected-vs-actual table and a summary match rate — gated behind
`-DENABLE_AI_HARNESS=ON` like its siblings, though (having no live model in the loop) it needs none
to build or run. `Tests/Timeline/TimelineOpsFixtureTests.cpp` asserts the identical fixture files as fast
gtest cases, which is what CI actually gates on.

## 10. The Agentic Timeline Security Model

The single statement the per-feature sections above implement. When extending the AI's reach into
the timeline, this table is the contract to preserve — every row exists because the mechanism next
to it enforces it, not because a prompt asks nicely.

**What AI output may author:**

| Surface | Mechanism | Bound |
|---|---|---|
| MIDI notes | `placeClips` note lists, or `.mid` blobs via `placeMidiClip` (§9) | note caps, pitch/velocity/channel ranges REJECTED not clamped; blob ≤ 256 KiB, PPQ-only SMF parsed by `MidiClipFile` (a format that structurally cannot carry a path, a plugin id, or code) |
| Automation lanes | `writeLane` (§9) | values validated against the **live** parameter's range intersected with the lane snapshot; (nodeUuid, paramId) must resolve against the live graph — untrusted input can never author an orphan |
| Doc-only tracks | `addTrack` (kinds `midi`/`automation` only) | unbound — wiring a Track In/Track Audio node stays a user gesture |

**What AI output may never touch, and why:**

| Never | Why | Enforced by |
|---|---|---|
| Asset references / file paths | an assetRef is a file **read**; honoring one from a model turns a chat reply into arbitrary file access | `AssetNotAllowed` in `validateTimeline` (§7); unknown-field rejection makes `assetRef` unreachable by grammar in ops (§9) |
| Plugin identifiers / state blobs | a plugin blob is a code-execution surface, not a parameter | node `state` is trusted-path-only (`applyExtraStateToProcessor`); internal-only module types rejected untrusted (`InternalModuleNotAllowed`); hosted-plugin types join that list |
| Record arming / record modes | untrusted input must not start capturing the user's audio | `RecordModeNotAllowed` (§7); ops carry no such field by grammar |
| The patch grammar's `timeline` key | timeline data rides its own validated door, never the patch schema — every property in `getPatchSchema` invites the model to emit it on every request | `TimelineNotAllowed` (permanent, §7); pinned in both directions by tests and a harness fixture |

Read-path symmetry: what the model *sees* (§8) follows the same rule — arrangement summaries carry
bare file names only, never paths or directories.
