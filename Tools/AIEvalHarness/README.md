# AIEvalHarness

Scores how often AI-generated patches that actually apply are *usable* synthesizer patches, not
just schema-valid JSON. This is what lets you safely move to a cheaper or local model later —
without it, every cost optimization is a guess about quality.

It is a measurement instrument, not a test. It needs a live Ollama instance, so it is excluded
from `Tests` and from CI, and must be opted into at configure time.

## Running

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIEvalHarness
./build/Tools/AIEvalHarness/AIEvalHarness \
    --model artifish/llama3.2-uncensored:latest --runs 2 --json out.json
```

| Flag | Default | Meaning |
| :--- | :--- | :--- |
| `--provider` | `ollama` | `ollama` talks to Ollama's `/api/chat` directly. `remote` talks to a local `synth-platform` inference service instead (see below). |
| `--model` | `gemma4:12b-it-qat` | With `--provider ollama`: the Ollama model tag, must be one `ollama list` reports. With `--provider remote`: a label for the report only — the service picks its own model server-side (see below). |
| `--runs` | `1` | How many times to replay the whole scenario set. |
| `--host` | `http://localhost:11434` (`ollama`) / `http://localhost:8787` (`remote`) | Base URL of the provider being measured. |
| `--json` | *(none)* | Write per-attempt records to this file for later analysis. |

## Scoring a model through the service (`--provider remote`)

`--provider ollama` measures a model reached directly, bypassing the `synth-platform` service
entirely — the numbers describe the model, not the stack a real user hits. `--provider remote`
routes every request through `RemoteProvider` (`Source/AI/RemoteProvider.h`) at
`POST {host}/v1/capability/patch.generate` instead, the same code path `AgentSynth`'s Apply/Merge
button uses once the `remote` provider is enabled. The service picks its own model server-side —
`--model` only labels the report — so which model actually answers is controlled entirely by the
service's own `INFERENCE_PROVIDER`/`INFERENCE_MODEL_ID`/`OLLAMA_BASE_URL` env vars
(`synth-platform/apps/api/.env.example`), not by anything passed to this tool.

```bash
# In synth-platform/apps/api/.env: INFERENCE_PROVIDER=ollama, INFERENCE_MODEL_ID=gpt-oss:20b
# (ollama pull gpt-oss:20b first — ~14GB)
pnpm --filter api dev   # starts the service on :8787

./build/Tools/AIEvalHarness/AIEvalHarness \
    --provider remote --model gpt-oss:20b --runs 2 --json out.json
```

`GROQ_API_KEY` is not required for this — `INFERENCE_PROVIDER=ollama` keeps the service on the
free local backend. Comparing a hosted model (`INFERENCE_PROVIDER=groq`) is out of scope here;
that needs a Groq key nobody has supplied yet.

## How this differs from AIPatchHarness

`Tools/AIPatchHarness` asks "did the model's answer pass `validatePatch` and survive the retry
path" — a syntactic question about the JSON. This tool asks a different one: of the patches that
*did* make it into the graph, are they patches a user would actually want? A patch can be
perfectly schema-valid and still be silent (nothing wired to Audio Output) or dangling (an
oscillator nobody connected to anything) — `validatePatch` has no opinion on either, because
both are structurally legal graphs.

## What it replays

40 realistic prompts, split between building a patch from scratch (replace mode) and modifying
an existing one (merge mode), spanning every module family in the app: oscillators, filters,
envelopes, every FX module, poly/sequencing, external MIDI, and parameter-only tweaks ("make it
brighter", "increase the resonance"). Merge scenarios first apply a fixed seed patch — MIDI ->
Oscillator -> Filter -> VCA -> Output — as *trusted* JSON, exactly like `AIPatchHarness`.

Each attempt follows the exact production path an Apply/Merge click takes, then scores the
result:

```
sendMessage(useStructuredOutput=true)
  -> applyPatchWithRetry(...)      # what the user actually ends up with
  -> evaluatePatch(graph)          # Source/AI/PatchEval.h
```

`evaluatePatch` runs three structural checks against the resulting `juce::AudioProcessorGraph`,
unit-tested independently of any model in `Tests/PatchEvalTests.cpp`:

- **has an output** — an `Audio Output` node exists in the graph.
- **connects** — that `Audio Output` is reachable, following audio (not MIDI) connections
  backward, from some `Oscillator` — i.e. there is an actual sound path from a source to the
  speaker, not just isolated nodes sharing a canvas.
- **params in range** — every parameter's current value lies within its own declared range (and,
  for choice parameters, its index is a legal choice). This is guaranteed today by
  `NormalisableRange::convertFrom0to1` clamping on every write path, so it is defense-in-depth
  against a future regression rather than something expected to fail — but it exercises the
  values *real model output* actually produces, not just the ones a unit test thinks to try.

## Reading the output

Quality is only meaningful for patches that actually applied — a provider error or a rejected
patch can't be scored against `PatchEval`, so **applied** (not the number of scenarios) is the
denominator for every percentage. A model that validates well in `AIPatchHarness` but scores
poorly here is producing schema-legal patches that don't actually do anything — worth knowing
before switching the default model to it.

## Caveats

Same as `Tools/AIPatchHarness`: not every backend enforces the JSON schema as a grammar (MLX
models fall back to prompt compliance), both providers give up after 240s per request
(`kChatRequestTimeoutMs` in `OllamaProvider.cpp`, `kRequestTimeoutMs` in `RemoteProvider.cpp` —
neither backend streams, so this doubles as the model's real time-to-finish budget, not just a
connect timeout), and results are model- and machine-dependent — quote the model tag alongside any
number. A model that times out scores as `NOT-APPLIED`, indistinguishable from the provider being
unreachable — the first six-model sweep run against this harness caught exactly this with
`gemma4:12b-it-qat` (~180s/request average, close enough to the old 120s bound that most of its
attempts failed on timing, not quality).

With `--provider remote`, a `NOT-APPLIED`/`PROVIDER-ERROR` result can also mean the service itself
is misconfigured (wrong `INFERENCE_MODEL_ID`, Ollama not running, port `8787` not listening) —
check the service's own logs before concluding the model is at fault.
