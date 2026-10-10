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
| `--mode` | `patch` | `patch` replays the 40 golden prompts against `getPatchSchema()`. `timeline` replays a separate 8-prompt set against `getPatchSchemaWithTimelineOps()` instead — same request path, the extended schema. See Structured-output corruption below. `project` sends five sound-design requests through the edit-plan path and scores their SHAPE - see Scoring sound shape below. `track` sends ten whole-track requests through the same path - see Whole tracks below. |
| `--timeout-ms` | *(unset: 240000)* | The provider's request timeout in ms. The harness's own waits for an answer become this + 30000, so the provider's message is the one reported. Raise it for a slow local model. |
| `--project-prompt-version` | *(unset: the app's pin, 5)* | `--mode project`/`track` only: the project.generate prompt version the request asks the service for. Use it to measure a new server prompt before the app pins it. |
| `--save-projects` | *(none)* | `--mode project`/`track`: apply every plan that previews valid and save it as `<dir>/<model>-<scenario>-run<N>.agsproj` (see Listening to results). With `--replay`: where the projects go. |
| `--replay` | *(none)* | A `--json` file from an earlier `--mode project`/`track` run. Makes no model calls; applies and saves each of its records (needs `--save-projects`). |
| `--check-project` | *(none)* | Print the tracks, clips, notes, lanes and Track In bindings in a saved `.agsproj` and exit (1 when it does not load or a track is unbound). |
| `--keep-audio` | *(none)* | With `--render-peaks`: also save each bundle's master render as `DIR/<bundle name>.wav` (16-bit, same sample rate), to listen to without opening the app. The measured float render is unchanged. |
| `--render-peaks` | *(none)* | A `.agsproj` bundle, or a directory of them: render each offline (master mix and one stem per track, beat 0 to the last clip plus a 2 s tail, 32-bit float so overs are measured) and print the peak in dBFS, flagging `CLIPS` at 0.0 or above. With `--json FILE`, also writes `{bundle, masterPeakDb, tracks, clips}` records. Exit 1 when a bundle does not open or render. |
| `--think` | *(unset)* | `--provider ollama` only. `true`/`false` — sets Ollama's `think` request field. Unset sends today's exact request body (no `think` key at all). |
| `--temperature` | *(unset)* | `--provider ollama` only. Nests under the request's `options.temperature`. |
| `--seed` | *(unset)* | `--provider ollama` only. Nests under the request's `options.seed` — pin this alongside `--temperature 0` for a reproducible before/after comparison. |

### Structured-output corruption

`--think`/`--temperature`/`--seed`/`--mode timeline` exist to reproduce and compare fixes for a
corruption bug where local Ollama structured-output decoding leaked model reasoning text into a
constrained JSON string value (see `docs/ai/structured-output.md` for the confirmed
`{}`-open-schema defect, and `docs/ai/ollama-provider.md#sampling-options` for why `think: false`
is not a fix for it). `--mode timeline`'s
summary reports `timelineOps present in response` / `timelineOps corrupted/rejected` — validated
via `AIIntegrationService::previewTimelineOps()` (checked, never applied), the timeline
counterpart of the `applyError` corruption proxy the plain patch mode already surfaces (a
corrupted choice value shows up there as `"Invalid value for choice parameter …"`).

```bash
# baseline: today's exact request shape, unpinned
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --json eval-results/baseline.json

# candidate fix, same prompts, pinned sampling for a fair comparison
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --think false --seed 42 --temperature 0 \
  --json eval-results/think-false.json

# same comparison against the timelineOps-extended schema
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --mode timeline --json eval-results/baseline-timeline.json
```

## Scoring sound shape (`--mode project`)

`validatePatch` and the plan validator say a response is *legal*; they cannot say it is a pluck.
`--mode project` sends five requests through `AIIntegrationService::sendProjectMessage` (the path the
chat takes: the local model with the timeline prompt and the widened schema, or the hosted
`project.generate` capability with `--provider remote`), previews each answer with
`previewProjectEdit`, and scores it with `Source/AI/SoundShapeChecks.h`:

| Scenario | Prompt | Starting project | Shape check |
| :--- | :--- | :--- | :--- |
| `plucky` | make a plucky lead track | empty | some envelope with sustain 0 (<= 0.01), decay <= 0.5 s, attack <= 0.02 s |
| `filter-env-new` | add a bass track with a filter envelope | empty | a modulation from an envelope (the new track's `envelope.id`) onto a Filter's cutoff |
| `filter-env-existing` | add a filter envelope | Poly MIDI, Oscillator, Filter, VCA, an ADSR modulating the VCA, Audio Output | the same, from the patch's existing ADSR |
| `filter-env-beside` | add a bass track with a filter envelope | the `filter-env-existing` patch | the new track's Filter is moved by the new track's own envelope, not the existing ADSR (which follows another track's notes) |
| `acid` | make an acid bassline track | empty | that, plus the 24 dB low-pass (`LPF24`), resonance >= 60% of its range, envelope sustain <= 0.3 |

```bash
./build/Tools/AIEvalHarness/AIEvalHarness --mode project --model gpt-oss:20b --runs 3 --json out.json
./build/Tools/AIEvalHarness/AIEvalHarness --mode project --provider remote --model gpt-oss:20b
```

Each line reports `valid`/`INVALID` (the plan previews) and `shape-ok`/`shape-wrong` with the reason
for the shape verdict; the summary counts both over the responses received. The two are independent:
a plan that previews fine but leaves the envelope at its default (a drone) is `valid` and
`shape-wrong`, which is exactly the failure this mode is for. The scenario table and the runner are in
`Tools/AIEvalHarness/ProjectMode.h`; the checks are unit-tested without a model in
`Tests/AI/SoundShapeChecksTests.cpp`. Local models need Ollama 0.34.4 or newer.

Each `--json` record carries the model's raw `response`, so a failed scenario can be read back.

## Whole tracks (`--mode track`)

`--mode track` sends ten whole-track requests from an empty project ("make an upbeat dark techno
track with sparkling sounds", lo-fi, progressive house, ambient, drum and bass, synthwave, acid
techno, chillwave, trap, trance) through the same edit-plan path as `--mode project`. Its check only
asks whether a track came back: at least 3 new tracks and notes on at least two of them. The reason
column counts tracks, clips with notes, notes and automation lanes. Whether the track is any good is
judged by listening, so keep `--json` and load the responses.

```bash
./build/Tools/AIEvalHarness/AIEvalHarness --mode track --provider remote --model claude-haiku-5-5 --runs 3 --json track-haiku.json
```

Every note is written out today, so expect answers to hit the service's output cap
(`MAX_OUTPUT_TOKENS`, 4,096 by default); raise it on the service to see what the model does
unconstrained.

## Listening to results

`--save-projects` turns the answers into projects you can open and play. A plan is applied with the
app's own code: a real `MainComponent` is built headless, loaded with the scenario's starting patch,
and `applyProjectEdit` runs on its service with its own timeline host, so tracks get their Track In,
instrument, envelope, inserts, wiring and channel exactly as the chat's Apply gives them. The result
is written by the same call as File > Save. Each bundle is then read back and compared with the plan
(tracks, clips, notes, lanes, every track bound to a Track In); the line says `verified` or `MISMATCH`.
The run never touches your real app settings.

```bash
# during a run
./build/Tools/AIEvalHarness/AIEvalHarness --mode track --provider remote --model claude-haiku-5-5 \
    --json track-haiku.json --save-projects projects/

# from an earlier run: no model calls, one line per record
./build/Tools/AIEvalHarness/AIEvalHarness --replay track-haiku.json --save-projects projects/

# open one in the app
"/Applications/Agent Synth.app/Contents/MacOS/Agent Synth" projects/claude-haiku-5-5-dark-techno-run1.agsproj
```

A record that did not respond, or whose plan the app refuses, prints why and writes nothing. Replay
needs the file's `mode` to be `project` or `track` and knows each scenario's starting patch by name.
`--check-project projects/<name>.agsproj` prints what a bundle holds.

`--render-peaks projects/` renders every bundle in the folder offline with the app's own bounce and stem export and prints each one's master and per-track peak in dBFS (`CLIPS` when the master reaches 0.0), so a mix that is far too loud shows up without opening the app.

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
unit-tested independently of any model in `Tests/AI/PatchEvalTests.cpp`:

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
