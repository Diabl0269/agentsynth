# Patch Safety

The trust boundary for model-authored patches, and the four generation-side layers that keep valid
patches coming out of it.

## The gate

Untrusted model output reaches the graph through one gate —
`AIStateMapper::validatePatch(..., trusted=false)` — which returns a typed `PatchValidationError`
plus a message. **That gate is a security boundary and is never relaxed to raise the pass rate.**
Everything below works on the *generation* side instead.

Each node's `params` keys are checked against that module's real `paramID`s
(`PatchValidationError::UnknownParameterKey`), not merely type- and range-checked when present. A
key that matches nothing is rejected rather than silently ignored: `applyParamsToProcessor` only
ever looks a key up *by name* among the real parameters, so an unmatched key — a corrupted or
typo'd key from a decoding artifact — would otherwise be dropped on the floor, leaving that
parameter at its default while the patch still reported success. Rejecting it surfaces the failure
so the retry and repair loop below engages.

A key that is real on some *other* module — `"waveform"` sent for a Filter node — is the same hard
rejection of the whole patch. **Why it cannot be narrower:** the schema's choice-parameter
`properties` are a union across all module types (`SchemaChoiceParamIdsAreUnambiguous`), so the
grammar cannot express "only this node type's params", and `params` itself stays
`additionalProperties: true` so numeric params remain emittable
(`SchemaStillAllowsNonChoiceParameters`). The validator is where per-module truth lives.

Validation reports only the **first** error. Fixing one class of failure does not simply shrink the
histogram, it *unmasks* whatever was next in the same patch, so an error count that rises after a
fix is expected, not a regression.

## The four layers, most upstream first

### Constrained decoding

`getPatchSchema()` is the strongest guarantee, because the backend can enforce it. How it is built
and where it comes from is in [structured output](structured-output.md).

The `node.type` enum is generated from `moduleFactory`, never hand-written, so the schema cannot
drift from what the app can actually create. Guarded by `SchemaModuleTypesMatchTheFactory`.

Because the enum is *derived*, **registering a module makes it model-authorable by default** — the
right default for an ordinary DSP module, the wrong one for anything that names an external
resource or carries privileged state. Such a module goes into `kNonAuthorableModuleTypes`
(`Source/AI/AIStateMapper/AIStateMapperInternal.h`) at the moment it is registered, with its reason
recorded against it. `AIStateMapperTest.AuthorableModuleTypesGolden` pins the exact resulting
allowlist, so either kind of addition fails the build until the choice is made deliberately.

The set covers: the attenuverter under both its names (`Attenuverter`, `Mod Slot`), an
implementation detail of the `modulations` array that `applyJSONToGraph` creates itself; the
timeline feeds `Track In` and `Track Audio`, whose only meaningful state lives outside the patch;
`Rec Tap`, which **names a file path on disk**, so a model that could author one could aim a
recording anywhere the app can write; `Hosted Plugin`, whose `state` is an opaque byte blob handed
verbatim to third-party code and whose identity selects which binary the host loads; a macro's
inlet and outlet jacks (`Macro In`, `Macro Out`, `Macro MIDI In`, `Macro MIDI Out`), whose macro
membership is keyed by a uuid a provider cannot supply; and the mixer's `Channel Strip` and
`Master`, which the app builds around rather than the model writing directly.

**The deny set is enforced by the validator, not just by the schema.**
`validatePatch(..., trusted=false)` rejects any node whose type is in `kNonAuthorableModuleTypes`
with `PatchValidationError::InternalModuleNotAllowed`. Leaving it to the schema enum alone would
make "non-authorable" mean *un-suggested* rather than *unreachable*: the schema is only a grammar
for backends that compile it, and a patch can also arrive from a local model or any future caller
that never saw it. The **trusted** path is untouched, because the app's own saves must round-trip an
Attenuverter or a Track In. Callers that gate app-authored data with `trusted=false` before applying
it trusted — session state, `.agsproj`, snippet files, the pairing described in
[`layout/snippets-clipboard.md`](../layout/snippets-clipboard.md) — pass `allowInternalModuleTypes=true`:
they are gating structure, ids, ranges and tampering, not authorship, and the app's own files
legitimately contain internal nodes. The parameter defaults to `false`, so a new model-facing caller
gets the restriction without knowing it exists.

`node.params` enumerates every **choice** parameter's real options, so a model physically cannot
emit `"waveform": "White Noise"`. `additionalProperties` stays `true` so numeric parameters remain
expressible — a grammar restricted to the listed keys would make `cutoff` unreachable, a worse bug
than the one being fixed. Guarded by `SchemaConstrainsChoiceParametersToTheirOptions` and
`SchemaStillAllowsNonChoiceParameters`.

The schema is an **output** contract: every property in it is something the model is invited to
emit. Reference data does not belong there. The choice lists do real work as enums in `params`, and
stay human-readable in the system prompt via `getModuleSchema()`.

### Validate and retry

Cross-references — "a connection names an id that does not exist" — cannot be expressed in a JSON
schema at all, so `applyPatchWithRetry` handles them after the fact. On rejection the *specific*
validation message is fed back ("that patch was rejected because X; return a corrected patch").
Retries are bounded by `AIIntegrationService::kMaxPatchRetries` (2, so 3 attempts in total), each is
announced through the `onRetry` callback so the chat shows what is happening, and exhaustion
surfaces the error rather than looping.

Rejection messages name the offending id **and list the valid ones**. This is not cosmetic: a bare
"unknown node id" gives the model nothing to aim at, and the retry tends to repeat itself.

**Merge-mode id collisions are rejected, not resolved** (`NodeIdTypeMismatch`). A patch node whose
`id` matches a live node but whose `type` differs must not fall through the update-in-place branch
and create a *second* node, rebinding `idMap[id]` to it — every later connection and modulation in
the same patch that meant the original node would silently re-point at the new one. Ids the same
patch also `remove` are exempt: removals run first, so re-using such an id names a genuinely new
node. The trusted path is unaffected by design.

### Repair, one rule only

A patch that states no `"mode"` and is rejected as a *replace* is re-validated as a *merge*, and
applied that way if it passes. This costs no round-trip and alters nothing about the patch's
content; it only resolves which mode the caller guessed, and validation is the arbiter. It is
deliberately **one-directional**: reading a patch as a merge can only preserve nodes the user
already had, whereas quietly turning a merge into a replace would wipe their patch. It never fires
when the model stated a mode. Guarded by the `AIPatchRetryTest.Repair*` tests.

No other repair is applied. Dropping a connection that names a stale id would silently give the user
less than they asked for, which is exactly the silent-partial-apply failure the strict gate exists
to prevent.

### Prompt

The weakest guarantee, so it carries the least: the module and parameter tables, the
merge-versus-replace rules, and the worked examples below. Everything enforceable lives in the
schema.

## Worked examples in the system prompt

`initSystemPrompt()` (`Source/AI/AIIntegrationService/AIIntegrationServiceSystemPrompt.cpp`) embeds
5 hand-authored `(prompt, complete correctly-connected patch)` examples:

- **From scratch, bass** — Oscillator, Filter, VCA, Audio Output, with a Filter Env and an Amp Env
  each driving a real modulation target (`Cutoff` destPort 1, `CV` destPort 1).
- **From scratch, lead** — the same shape plus an LFO into the Oscillator's `Fine` input
  (destPort 4), a functional CV target unlike the Oscillator's `Pitch` target (destPort 0), which
  mono oscillators silently ignore. The prompt calls that trap out explicitly.
- **FX chain** — a source patch chained through Distortion, Chorus and Reverb, in line, into Audio
  Output. It also flags that Delay's and Reverb's advertised CV modulation targets are vestigial,
  because the audio engine never reads that CV, so `modulations` must not be routed into them.
- **Merge mode, adding a node** — stacking a second, detuned Oscillator into an existing filter,
  emitting only the new node and the one new connection, per the delta convention.
- **Merge mode, removing a node** — removing a Distortion node and rewiring around the gap.

**Why examples rather than more prompt rules.** Model failures here are not syntactic —
`validatePatch` already forces well-formed JSON — but structural: a patch with no `Audio Output`
node, or with nodes never wired to one. Rules tell a model what is legal; only a complete example
shows it a working patch end to end. The technique helps most on backends that ignore `format`
entirely and fall back on prompt compliance, which is exactly where grammar enforcement cannot
help. It is not free: a longer prompt costs some models basic JSON-production reliability, and the
system prompt's literal text roughly doubles to carry these five, which is a real cost on any
backend billed or latency-bound per token. Widen the set only against a measured per-model
before/after.

**Non-overlap with the eval harness.** Reusing one of `Tools/AIEvalHarness`'s 40 eval prompts as a
few-shot example would be train/test contamination and would invalidate the measurement. Each
example's prompt text is checked by hand against `Tools/AIEvalHarness/Main.cpp`'s `scenarios()` for
verbatim or near-paraphrase overlap, and
`AIIntegrationServiceTest.WorkedExamplePromptsDoNotOverlapEvalScenarios`
(`Tests/AI/AIIntegrationService/AIIntegrationServiceSystemPromptTests.cpp`) enforces it in CI against
a manually-synced copy of those prompts — a guard against drift, not a substitute for the manual
check when new examples are added.

`AIIntegrationServiceTest.WorkedExamplePatchesAreStructurallyValid` runs every example through
`AIStateMapper::applyJSONToGraph` and `synth::evaluatePatch`, so a hand-authoring mistake (a
dangling node, an out-of-range parameter) fails CI. That proves the examples are valid, not that
they help a model; only a harness pass-rate delta does that.

## Measuring

`Tools/AIPatchHarness` replays a fixed set of realistic prompts through the exact production path
and tallies rejections by `PatchValidationError`. `Tools/AIEvalHarness` answers a different
question: of the patches that *do* pass validation and apply, are they usable — an output wired to a
source, not just schema-legal JSON — scoring 40 golden prompts against `Source/AI/PatchEval.h`'s
structural checks, which are unit-tested model-independently in `Tests/AI/PatchEvalTests.cpp`. Both
need a live Ollama, so both are opt-in (`-DENABLE_AI_HARNESS=ON`) and excluded from CI; see their
READMEs for flags and caveats. `.github/workflows/ai-eval-nightly.yml` runs the measurement on a
schedule, off by default, ratcheted against a committed baseline.

**A quoted rate means nothing without the model and the sampling settings alongside it.**
`AIPatchHarness` pins `--temperature 0` and a fixed `--seed` by default, unlike `AIEvalHarness`,
which leaves them unset for its own before/after comparisons; an unpinned run swings by several
points on the same model and prompts, so a bare percentage is not comparable across runs unless both
sides fixed sampling the same way. `--provider remote` ignores these entirely, because
`RemoteProvider` has no sampling knobs — which is why the harness's `--json` output records
`temperature` and `seed` only for `--provider ollama`. Never claim pinned sampling that was not
applied.

**`format` enforcement is backend-dependent.** Ollama compiles the JSON schema to a GBNF grammar for
llama.cpp models, so anything the schema *encodes* becomes impossible to emit. MLX-backed models
ignore `format` entirely and fall back to prompt compliance. Schema work therefore helps some
backends and not others; retries cover the rest.

## Logging

One line per rejection and one per retry. Retries happen at user-click frequency, so this stays
within the no-high-frequency-logging rule — never log per candidate token or per validation pass.

Tests: `Tests/AI/AIPatchValidationTests.cpp` (table-driven, one malformed patch per
`PatchValidationError`, plus the schema contract) and `Tests/AI/AIPatchRetryTests.cpp` (retry bound,
error feedback, repair scope).
