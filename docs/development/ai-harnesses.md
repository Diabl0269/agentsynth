# AI Measurement Harnesses

Two opt-in tools that measure model behaviour rather than assert on it. Neither is a test and
neither is ever *run* by CI: both need a live Ollama, so they sit behind `ENABLE_AI_HARNESS`. Only
the Linux CI job builds them, purely to catch one that stops compiling; the macOS and Windows jobs
leave the flag off because the harness targets add 183 translation units to those builds. The
offline, always-run counterpart is the fixture-replay suite in
[`test-layers.md`](test-layers.md#ai-patch-fixture-replay-corpus-driven-offline), which is built
from this patch harness's recorded output.

## AIPatchHarness — how often model output passes validation

`Tools/AIPatchHarness` replays a fixed prompt set through the production path and tallies rejections
by `PatchValidationError` value.

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness
./build/Tools/AIPatchHarness/AIPatchHarness --model <tag> --runs 2
```

See `Tools/AIPatchHarness/README.md` — in particular that `format` enforcement is backend-dependent,
and that `OllamaProvider` times out at 240 s (`kChatRequestTimeoutMs`), which shows up as a provider
error rather than a rejection. `--json` records include the raw model text (`rawResponse`,
`retryResponses`) behind each outcome; that is the offline corpus the fixture-replay suite is built
from.

**Determinism and cost ceilings.** This harness pins `--temperature 0` and a fixed `--seed` by
default, unlike the eval harness below.

**Why pinned.** Unpinned sampling swings the pass rate by around 8 points run to run, which is more
than most real changes move it — repeat runs have to be comparable enough to ratchet against.
`--runs` is hard-capped at 10, and `--max-requests` bounds total outbound requests for the whole
invocation; it is required, with no default, for `--provider remote`, since that path can reach a
paid vendor. `Tools/AIPatchHarness/RequestBudget.h`'s `RequestBudget` class is covered directly by
`Tests/AI/RequestBudgetTests.cpp`, independent of any live model.

**Nightly schedule.** `.github/workflows/ai-eval-nightly.yml` runs this harness on a schedule, off
by default (gated on the `AI_EVAL_ENABLED` repository variable) — see "Nightly scheduled eval" in
`Tools/AIPatchHarness/README.md` for the switch, runner and ratchet. It never blocks a merge: it is
not a `pull_request`/`push` workflow, and `scripts/ai-eval-ratchet.sh` (unit-tested by
`scripts/tests/ai-eval-ratchet.test.sh`, run by the Lint job) only fails *that job*. The ratchet
compares against a committed baseline; until one is committed the script is inert — it reports and
exits 0.

## AIEvalHarness — whether a valid patch is actually usable

`Tools/AIEvalHarness` scores a different thing: of the patches that pass validation and apply, do
they have an output, is that output reachable from a real sound source, is every parameter in range?
It replays 40 golden prompts and runs `Source/AI/PatchEval.h`'s checks against the resulting graph.

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIEvalHarness
./build/Tools/AIEvalHarness/AIEvalHarness --model <tag> --runs 2
```

The structural checks themselves (`evaluatePatch()`) have no model dependency and are covered by
`Tests/AI/PatchEvalTests.cpp` in the regular suite — only the golden-prompt replay needs Ollama.

**Reproducibility and investigation flags**, `--provider ollama` only, all unset by default. These
are for comparing corruption/rejection rates before and after a candidate fix, not production knobs:

```bash
# pin sampling so a before/after comparison isn't confounded by run-to-run variance
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --think false --seed 42 --temperature 0 \
  --json eval-results/my-run.json

# exercise getPatchSchemaWithTimelineOps() instead of getPatchSchema() -- same request path
# (OllamaProvider::processRequest -> `format` field), the extended schema. Works on every build.
./build/Tools/AIEvalHarness/AIEvalHarness --model gpt-oss:20b --mode timeline --json eval-results/my-timeline-run.json
```

`--mode timeline`'s summary reports `timelineOps present in response` and `timelineOps
corrupted/rejected`, the latter via `AIIntegrationService::extractTimelineOps()` plus
`previewTimelineOps()` (validated, never applied) — the timeline-side equivalent of the `applyError`
corruption proxy plain patch mode already surfaces, where a corrupted choice value shows up as
`"Invalid value for choice parameter …"`.

## `evaluatePatch` is a contract, not only a score

`AIIntegrationService::applyPatch` gates on `sourceReachesOutput` and surfaces `detail` to the user
via `getLastPatchError()`, so those strings are a contract two `AIIntegrationServiceTest` cases
assert verbatim. `SamplerAloneDoesNotCountAsAReachableSource` /
`SamplerAlongsideAnOscillatorStillPasses` cover the one module that is a sound source in the library
but deliberately not one here: a Sampler is silent until a file is loaded, and a model-authored
patch cannot load one.
