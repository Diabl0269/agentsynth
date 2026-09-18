# Structured Output

How the JSON schemas the client sends as a model's `format` are built, the shapes that are known to
break a grammar compiler, and the openness tradeoff that constrains them.

The schemas themselves are generated in `Source/AI/AIStateMapper/AIStateMapperSchema.cpp`:
`getPatchSchema()`, `getPatchSchemaWithTimelineOps()` and `getTimelineOpsEnvelopeSchema()`. What
each one is used for is in [patch safety](patch-safety.md) and [timeline ops](timeline-ops.md).

## Envelope codegen

`getPatchSchema()` does not hand-build the schema field by field. It parses
`synth::generated::kPatchEnvelopeSchemaJson` (`Source/AI/generated/PatchEnvelopeSchema.g.h`), a
header **vendored from the private backend repo** and generated from that repo's patch schema
definition — the same source its own codegen uses for the client's typed C++ structs — then layers
on the two things that source cannot know: the `"type"` enum and the per-choice-parameter `enum`s
inside `params`, both read from *this build's* live module registry (`moduleFactory`, built by
instantiating every registered `AudioProcessor` and reading `AudioParameterChoice::choices`).

**Why the split.** The backend has no module registry to enumerate against, so node `type` stays a
plain string in its envelope and per-module constraints are layered on by the client. The backend's
own schema source carries a comment saying exactly that.

**Regenerating.** After editing the backend's patch schema source, run its envelope-schema codegen
in the private backend repo, copy the generated header into `Source/AI/generated/PatchEnvelopeSchema.g.h`
verbatim, and commit both. There is no CMake-to-codegen build dependency — this repo's cache and
build invariants rule that out — so this is a manual copy-and-commit step, the same discipline as
`Patch.g.h`'s cross-repo flow.

The generated schema is rendered **flat and inlined** (`$refStrategy: "none"`, no `$ref` or
`definitions`) rather than reusing the named-definitions rendering used for the hosted inference
path. `$ref` indirection is untested territory for llama.cpp's grammar compiler, and a flat shape is
all this schema has ever needed.

`getPatchSchemaWithTimelineOps()` stays a client-side C++ extension on top of the generated
envelope.

## Shapes a grammar compiler mishandles

**Never spell an "anything goes" subschema as `{}`.** Ollama's grammar compiler mangles the empty
*object* JSON Schema into a garbage wrapped object instead of passing the value through
unconstrained — confirmed on more than one local model. The boolean `true` is the safe spelling of
the same intent. `getPatchSchemaWithTimelineOps()`'s `"track"` field is `"type": "string"` for this
reason: narrowed, not `oneOf` or `anyOf`, because llama.cpp's grammar compiler handles `anyOf`
poorly as well. `getPatchSchema()`'s own `params` field was never affected — its
`additionalProperties: true` is already the JSON Schema boolean, not `{}`.

Pinned by `AIStateMapperTest.TimelineOpsTrackFieldIsNotOpenSchema`.

See also [the sampling options warning](ollama-provider.md#sampling-options): `think: false` is not
a fix for corrupted structured output and makes it worse on a reasoning model.

## The `params`-openness tradeoff

The patch schema's `params` must stay open (`additionalProperties: true`) because numeric parameter
values cannot be fully enumerated. That requirement is unconditional, and `true` — not `{}` —
already satisfies it safely.

Where a real conflict would exist — a field that legitimately needs to be open-shaped for one
backend but cannot be — the resolution is a per-provider variant: open for hosted providers, a
narrower client-only form for Ollama. That is exactly what the timeline-ops `"track"` field above
is.

## Measuring a schema change

`Tools/AIEvalHarness`'s `--mode timeline` replays a separate scenario set through
`getPatchSchemaWithTimelineOps()` instead of `getPatchSchema()` — the same request path, just the
extended schema — so a change here is verified against both local structured-output schemas the
client actually sends, not only the plain patch one. See its README, and
[measuring](patch-safety.md#measuring) for the rules on quoting a rate.

**Known defect: structured-output corruption under the full schema.** A local model can replace a
JSON key with garbage containing leaked reasoning text. It is reproducible with the real patch
schema under longer generation, and not with a trivial hand-written schema, which confirms `format`
genuinely constrains decoding on the same setup — so the cause is specific to the larger schema
with many optional properties and real conversation context, and is not "the grammar is not binding
at all".
