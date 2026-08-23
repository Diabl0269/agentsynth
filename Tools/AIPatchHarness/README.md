# AIPatchHarness

Measures how often AI-generated patches actually pass `AIStateMapper::validatePatch`, and which
`PatchValidationError` values dominate when they don't.

It is a measurement instrument, not a test. It needs a live Ollama instance, so it is excluded
from `Tests` and from CI, and must be opted into at configure time.

This answers a syntactic question — did the JSON pass validation. For whether patches that *do*
apply are actually usable (has an output, connects to a source, params in range), see
`Tools/AIEvalHarness` instead.

## Running

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness
./build/Tools/AIPatchHarness/AIPatchHarness \
    --model artifish/llama3.2-uncensored:latest --runs 2 --json out.json
```

| Flag | Default | Meaning |
| :--- | :--- | :--- |
| `--provider` | `ollama` | `ollama` talks directly to a local Ollama instance (existing behavior). `remote` talks through a running synth-platform API server instead (`synth::RemoteProvider`) — that server's own `INFERENCE_PROVIDER` env var decides whether it forwards to local Ollama or AWS Bedrock, so this is how to measure the real production model. |
| `--model` | `gemma4:12b-it-qat` | Ollama model tag. Must be one `ollama list` reports. Ignored for `--provider remote` — the server picks its own model. |
| `--runs` | `1` | How many times to replay the whole scenario set. |
| `--host` | `http://localhost:11434` | Ollama base URL. Defaults to `http://localhost:8787` instead when `--provider remote` (the synth-platform API server's default port). |
| `--limit` | `0` (no limit) | Run only the first N scenarios instead of all 20 — use this for a cheap/fast validation pass before a full `remote` sweep, since remote calls cost real (tiny) money via AWS Bedrock. |
| `--json` | *(none)* | Write per-attempt records to this file for later analysis. |

### `--json` record format

The output file is one JSON object:

```jsonc
{
  "model": "gpt-oss:20b",
  "capturedAt": "2026-08-23T14:03:11.000Z",  // ISO 8601 UTC, when this run was captured
  "runs": 2,
  "attempted": 40,        // records where the provider actually responded (excludes provider errors)
  "valid": 34,            // of `attempted`, how many validated on the model's first answer
  "unparseable": 1,
  "providerErrors": 0,
  "records": [ /* one entry per (scenario, run) — see below */ ]
}
```

Each entry in `records`:

| Field | Meaning |
| :--- | :--- |
| `scenario` | Name from the fixed prompt table (`Scenarios.h`, shared with the fixture-replay test — see below). |
| `run` | 1-based index when `--runs` > 1. |
| `merge` | Whether this scenario replays as a merge (`true`) or replace (`false`) patch. |
| `responded` | Whether the provider returned a successful completion at all. Everything below is meaningless (and empty/false) when this is `false` — a provider error (timeout, unreachable Ollama) leaves no raw model text to record. |
| `parsed` | Whether `extractJsonFromResponse` + `JSON::parse` produced a JSON object. |
| `valid` | Whether `validatePatch` accepted the model's FIRST answer. |
| `error` | `PatchValidationError` name (`"None"` when `valid`). |
| `message` | Human-readable validation message. |
| `appliedAfterRetry` | Whether `applyPatchWithRetry` ultimately got a patch onto the graph — what the user experiences. |
| `retriesUsed` | How many correction round-trips that took. |
| `modeRepaired` | Whether the mode-less-patch repair fired (see `AIIntegrationService::applyPatch`). |
| `finalError` | The error `applyPatchWithRetry` gave up with, if `appliedAfterRetry` is `false`. |
| `rawResponse` | **The raw model text for the first answer**, before any extraction/parsing. This is what fed `parsed`/`valid`/`error` above. Empty when `responded` is `false`. |
| `retryResponses` | The raw model text for each correction round-trip `applyPatchWithRetry` made, in order (length equals `retriesUsed`, barring a provider error mid-retry). Empty array when no retry happened. |

`rawResponse` + `retryResponses` are what make a recorded run replayable offline with no model in
the loop: `rawResponse` goes through `extractJsonFromResponse -> JSON::parse -> validatePatch`
directly, and `applyPatchWithRetry` is replayed against a provider double that hands back
`retryResponses` in order instead of calling a real model. See
`Tests/AIPatchFixtureReplayTests.cpp`.

**Format stability:** this schema only grows new fields; existing ones keep their meaning and
type. A committed fixture under `Tests/fixtures/ai-patches/` is read by that name-keyed schema,
not by field position, so an old fixture stays valid after a future field is added.

## Recording the fixture corpus

`Tests/fixtures/ai-patches/*.json` are `--json` outputs from this harness, committed verbatim and
replayed offline by `Tests/AIPatchFixtureReplayTests.cpp` — see `docs/testing.md` for what that
test asserts. To re-record (e.g. after a `Scenarios.h` prompt change, a schema change, or to widen
model coverage):

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness

# The confirmed production model (P1-12) — always include this one.
./build/Tools/AIPatchHarness/AIPatchHarness --model gpt-oss:20b --runs 2 \
    --json Tests/fixtures/ai-patches/gpt-oss-20b.json

# At least one other local model for contrast (llama3.1/gemma4 family).
./build/Tools/AIPatchHarness/AIPatchHarness --model llama3.1:8b --runs 2 \
    --json Tests/fixtures/ai-patches/llama3.1-8b.json
```

Then rebuild and run `Tests` — `AIPatchFixtureReplayTest.EveryRecordedResponseReplaysItsRecordedOutcome`
walks every record in every `*.json` file under `Tests/fixtures/ai-patches/` through the
production path and diffs the replayed outcome against what's recorded. A model producing
different output than last time is expected to shift some outcomes — inspect the diff, decide
whether it's a real regression in the retry/validation code (fix the code) or just a legitimately
different corpus (commit the new fixture), and re-record only for a **deliberate** change, never
to make a failing assertion pass.

Aim for 60-100 total recorded responses across all committed files, including some rejections —
`--runs 2` over the 20-scenario table gives 40 per model, so two models comfortably clears that
bar. `AIPatchFixtureReplayTest.CorpusIsPopulatedAndContainsARejection` enforces both.

### Testing against the API server (local or remote/Bedrock)

`--provider remote` requires a synth-platform API server actually running: `cd apps/api && pnpm dev`
in the synth-platform repo, which listens on port 8787 by default — matching `--host`'s default for
this mode. To hit real Bedrock instead of local Ollama, start that server with
`INFERENCE_PROVIDER=bedrock pnpm dev` and valid AWS credentials in the environment. Since Bedrock
calls cost real (tiny) money, start with `--limit 3` to validate the plumbing before running the
full 20-scenario set.

## What it replays

Twenty realistic prompts (`Scenarios.h`), split between building a patch from scratch (replace
mode) and modifying an existing one (merge mode). Merge scenarios first apply a fixed seed patch —
MIDI → Oscillator → Filter → VCA → Output — as *trusted* JSON, so the model faces real
pre-existing node ids. Seeding is trusted deliberately: it is a fixture, not model output, and
must not be judged by the gate being measured. `Scenarios.h` is shared verbatim with
`Tests/AIPatchFixtureReplayTests.cpp`, which looks a recorded fixture's `scenario` name up there to
rebuild the same seed graph and mode offline — one source of truth for what a fixture's name
means, so the prompt table and the corpus it produced can't silently drift apart.

Each attempt follows the exact production path an Apply/Merge click takes:

```
sendMessage(useStructuredOutput=true)      # schema handed to Ollama as `format`
  -> extractJsonFromResponse
  -> juce::JSON::parse
  -> validatePatch(..., trusted=false)     # first-answer validity
  -> applyPatchWithRetry(...)              # what the user actually ends up with
```

## Reading the output

Two rates are reported, and they answer different questions:

- **valid on first answer** — how good the model's unaided output is. This is the number the
  schema (constrained decoding) moves.
- **APPLIED after retry/repair** — what the user experiences, once a bounded correction round-trip
  and the mode repair have had their turn.

`provider errors` are excluded from the denominator so an unreachable or timed-out Ollama cannot
flatter the result.

## Caveats

- **Not every backend enforces `format`.** Ollama applies the JSON schema as a GBNF grammar for
  llama.cpp models, but MLX-backed models (e.g. `gemma4:e4b-mlx`) ignore it and fall back to
  prompt compliance — they will happily emit fenced Markdown. Schema-side fixes therefore show up
  on llama.cpp models and not on MLX ones; retries cover both.
- **`OllamaProvider` gives up after 240 s** (`kChatRequestTimeoutMs` in `OllamaProvider.cpp`; was
  120 s until this was measured with `gemma4:12b-it-qat` averaging ~180 s per request on real
  hardware — most of its requests were failing on the timeout alone, not on quality). A model
  slower than the current bound still reports as a provider error rather than a rejection. Pick a
  model that answers within the timeout, or the run measures the timeout instead of the model.
- Results are model- and machine-dependent. Quote the model tag alongside any number.
