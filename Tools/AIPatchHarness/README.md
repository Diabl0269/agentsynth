# AIPatchHarness

Measures how often AI-generated patches actually pass `AIStateMapper::validatePatch`, and which
`PatchValidationError` values dominate when they don't.

It is a measurement instrument, not a test. It needs a live Ollama instance, so it is excluded
from `GravisynthTests` and from CI, and must be opted into at configure time.

## Running

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness
./build/Tools/AIPatchHarness/AIPatchHarness_artefacts/AIPatchHarness \
    --model artifish/llama3.2-uncensored:latest --runs 2 --json out.json
```

| Flag | Default | Meaning |
| :--- | :--- | :--- |
| `--model` | `gemma4:12b-it-qat` | Ollama model tag. Must be one `ollama list` reports. |
| `--runs` | `1` | How many times to replay the whole scenario set. |
| `--host` | `http://localhost:11434` | Ollama base URL. |
| `--json` | *(none)* | Write per-attempt records to this file for later analysis. |

## What it replays

Twenty realistic prompts, split between building a patch from scratch (replace mode) and
modifying an existing one (merge mode). Merge scenarios first apply a fixed seed patch — MIDI →
Oscillator → Filter → VCA → Output — as *trusted* JSON, so the model faces real pre-existing node
ids. Seeding is trusted deliberately: it is a fixture, not model output, and must not be judged by
the gate being measured.

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
- **`OllamaProvider` gives up after 120 s.** A model slower than that per request (on this machine,
  `gemma4:12b-it-qat` averaged ~180 s) reports as a provider error rather than a rejection. Pick a
  model that answers within the timeout, or the run measures the timeout instead of the model.
- Results are model- and machine-dependent. Quote the model tag alongside any number.
