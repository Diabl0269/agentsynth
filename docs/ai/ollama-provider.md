# OllamaProvider

`Source/AI/OllamaProvider.{h,cpp}` talks to a local Ollama server over `juce::WebInputStream`.
Registration and selection are in [providers](providers.md).

## Fail fast on an empty model

`OllamaProvider::processRequest`, used by `sendPrompt`, guards the empty-model case directly: if
`currentModel.isEmpty()` it returns `"Error: No Ollama model selected. Check that Ollama is running
and that a model is available (ollama list)."` with `success = false` **without touching the
network**. This closes the window in which
[the model discovery ordering hazard](chat-component.md#model-discovery-ordering-contract) could
still silently POST `{"model": ""}`.

When the HTTP request itself fails, a reachable-but-rejecting server is distinguished from an
unreachable one using `juce::URL::InputStreamOptions::withStatusCode(&httpStatus)`:

- Non-zero `httpStatus` — the server responded, but `createInputStream` returned `nullptr` because
  the status was not 2xx: `"Error: Ollama at <host> rejected the request (HTTP <status>)."`
- `httpStatus == 0` — no response at all, connection refused or timed out:
  `"Error: Could not connect to Ollama at <host>"`.

Locked by `OllamaProviderTest.SendPromptWithNoModelFailsWithoutHittingNetwork` and
`OllamaProviderTest.SendPromptIncludesSelectedModelInRequestBody` in
`Tests/AI/OllamaProvider/OllamaProviderRequestTests.cpp`.

## Worker-thread contract: never silence

**Every request accepted by `sendPrompt()` eventually gets its callback invoked**, with the model's
answer or with an error string. Nothing is ever dropped quietly, because a dropped request leaves
the chat UI waiting forever with no error to show.

Three rules make that hold:

1. **The worker parks; it does not exit on drain.** `run()` loops until `threadShouldExit()`,
   waiting on the thread's own `juce::WaitableEvent` (`Thread::wait()` / `notify()`) when the queue
   is empty. Breaking out as soon as the queue empties is wrong: `juce::Thread` only clears its
   handle *after* `run()` has returned, so `isThreadRunning()` still says `true` in that window,
   `sendPrompt()`'s `if (!isThreadRunning()) startThread()` skips the restart, and the request sits
   in the queue with nothing to pick it up.
2. **Queue ownership is explicit, not inferred from `isThreadRunning()`.** A `workerState` (`idle`,
   `starting`, `running`) guarded by `queueLock` decides whether a worker will drain the queue. It
   is released in the *same* locked section that empties the queue, so a concurrent `sendPrompt()`
   either hands its request to the retiring worker — which fails it with a callback — or sees `idle`
   and starts a fresh one. `sendPrompt()` also self-heals a `running` state whose thread has
   vanished.

   **`stopThread(0)` must never be called here.** JUCE never waits when given a zero timeout, so it
   force-kills the worker and `run()` never gets to hand the queue back. That recovery branch is
   deliberately untested: forcing it requires `pthread_cancel`, whose forced unwind aborts under
   glibc when it reaches the `catch (...)` in JUCE's `threadEntryPoint`
   (`FATAL: exception not rethrown`).
3. **Shutdown fails the queue instead of discarding it.** `~OllamaProvider()` sets `isShuttingDown`,
   so late `sendPrompt()` calls fail immediately rather than resurrecting a thread on a dying
   object, then calls `stopThread(2000)`. Requests still queued are failed with `"Error: Request
   cancelled - the Ollama provider is shutting down."` **inline**, not via
   `MessageManager::callAsync`, because the message loop cannot be trusted to run a deferred
   callback before the provider is gone. Every shutdown callback has therefore already fired by the
   time the destructor returns; none can fire after it.

Locked by `OllamaProviderTest.QueuedRequestDuringThreadShutdownStillCompletes` and
`OllamaProviderTest.PendingRequestsAreFailedOnDestruction`.

## Request cancellation

`sendPrompt()` returns an `AIProvider::RequestId`; `cancel(id)` abandons that request. This is a
real abort, not a UI dismissal. A Cancel button that only hides the spinner while the HTTP request
runs to completion bills a metered backend for work the user has abandoned and, because the queue
drains serially, makes the *next* message wait behind it.

The contract, which every provider must honour:

1. **A cancelled request still gets exactly one callback**, with `AIErrorKind::Cancelled`. A caller
   is never left hanging, and never called twice.
2. **It must not block requests queued behind it.** If it is still in the queue, `cancel()` removes
   it under `queueLock`, the same lock the worker pops with, so exactly one of them ends up owning
   it. If it is already in flight, the read is aborted so the worker is genuinely free rather than
   waiting out the response.
3. **An unknown, completed or already-cancelled id is a safe no-op.** The UI holds stale handles
   routinely — a response and a Cancel click cross — so this is the common case. The delivery path
   deregisters the request, which is what makes a later `cancel()` inert.

`AIIntegrationService` must **not** append an assistant turn for a cancelled request: it produced no
answer, and `chatHistory` is replayed as context, so an invented turn would be fed back on every
later message. The user's own turn stays.

### Publish before connect

**The provider does not use `juce::URL::createInputStream()`, and must not start.** A chat request
(every kind except an edit plan, see below) sets `"stream": false`, so Ollama sends *nothing at
all* — not even response headers — until the entire generation is finished. That means `connect()`, not `read()`, is where a request spends
essentially all of its time. `createInputStream()` connects internally and only hands the stream
back afterwards, so a factory built on it cannot expose the stream until the generation it was
supposed to cancel has already completed.

`InputStreamFactory` therefore takes a third argument, a `StreamPublisher`. The factory constructs
the `WebInputStream`, calls `publish(stream.get())`, and only then calls `connect()`. A factory must
also call `publish(nullptr)` before destroying a stream it is not returning, so a concurrent
`cancel()` can never reach a dead object. Every store and load of the pointer is under
`RequestState::streamLock`.

**Why, measured:** against a live Ollama with a long generation in flight, publishing after connect
leaves `cancel()` unable to stop the request at all; publishing before it, the callback fires within
a few milliseconds and the next message is answered promptly.

How the read is aborted, in order of preference:

- **`juce::WebInputStream::cancel()`** — the real mechanism, documented to cancel a blocking read
  and prevent subsequent connection attempts, and safe to call from another thread.
- **`synth::CancellableStream`** — an opt-in mix-in for injected test factories, which are not
  `WebInputStream`s, so tests can unblock a stream by the same route a real socket uses.
- **The `cancelled` flag alone** — the fallback for any other stream type. The worker finishes the
  read and discards the response. Still correct, just not prompt.

Model discovery (`fetchAvailableModels`) passes a no-op publisher: it is not cancellable and is
bounded by its own 4 s connection timeout.

Locked by `OllamaProviderTest.CancelledRequestInvokesCallbackWithCancelledKind`,
`CancelDoesNotBlockSubsequentRequest`, `CancelOfUnknownRequestIdIsSafeNoOp`,
`CallbackFiresExactlyOnceEvenIfCancelRacesCompletion` (run with `--gtest_repeat=100`),
`CancelAndCompletionEachDeliverExactlyOnceInEitherOrder`,
`AIIntegrationServiceTest.CancelledRequestDoesNotAppendToHistory`, and the `AICancelTest` UI locks.

> The race test sweeps the cancel across the completion by a growing offset rather than just
> spawning two threads. Without the offset, "completion wins" is never exercised: `cancel()` sets
> the cancelled flag before releasing the read, so the worker otherwise always observes the cancel.

> **Re-verifying by hand.** The unit tests use a mock stream and therefore cannot cover
> `WebInputStream::cancel()`. With `ollama serve` running, send a prompt that generates for a while,
> hit Cancel, and confirm the input frees immediately and the next message is answered without
> delay. A regression here is invisible to the suite, because the mock path still passes.

### Edit plans stream, so a blank runaway is cut off

An edit-plan request (`isEditPlanSchema`) sends `"stream": true` and `readChatAnswer` reads the reply
as newline-delimited chunks: the `message.content` pieces add up to the answer, and the last chunk
(`done: true`) carries the token counts and timings. Ollama's JSON-schema grammar allows free
whitespace between tokens, so a model that closes its last string early can write blanks until the
4096-token cap (about 48 s of nothing on gemma4:e4b-mlx, roughly 1 whole track in 10). When the
answer's trailing run of whitespace passes 200 characters the provider aborts the stream (the same
`abortStream` path as Cancel) and delivers a `Schema` error within seconds. Every other request keeps
`"stream": false` and reads one object, which the same reader accepts. Locked by
`OllamaProviderStreaming.*`.

## Edit plans on Ollama

`AIIntegrationService::sendProjectMessage` on a local provider sends the app's own small prompt
(`Source/AI/ProjectGenerate/LocalProjectPrompt.cpp`, about 6 KB: the output shape, a dozen plain rules, three short
worked examples) plus a module table cut to the modules the request can use as the system message, the same user
message the hosted server builds (`buildProjectAugmentedContent`), and `AIStateMapper::getProjectEditSchema()` as the
format. The prompt is not a copy of the hosted one: a small model re-reads every byte before each answer, and the
shape is enforced by the grammar and the plan validator rather than by prose.

**The module table.** `moduleTypesForRequest` picks the core set (Oscillator, Filter, LFO, Distortion), every module
the project already uses, and any module the request names or hints at ("echo" -> Delay). Modules a track builds
itself (filter envelope, amp envelope, Pluck, the I/O modules) are never offered, and the table has no "Modulation
Targets" list: a small model that sees them writes them as nodes or writes `destParam` "cutoff_1".

**The grammar's key order is part of the prompt.** The decoder writes keys in schema order, so the schema puts
`timelineOps` before `modulations` (a modulation can then name the ids a new track's operation just created), and
the examples follow that order and the operation's own order (`inserts`, `instrumentParams`, `envelope`). An example
that breaks the order leads a small model to drop fields; with the order matched the same model went from 2 to 12
valid plans of 15.

**Songs.** A request the chat reads as a song, track or beat (`AIChatComponent::shouldPlanSong`, passed on with
`setLocalSongRequest`) adds a "Whole song" section and one worked house song (about 3 KB) to the rules, and the Drum Kit
to the module table. The model then answers with the `"song"` layout (named patterns of `start:length:pitch:velocity`
notes, then sections that play them) and `SongLayout` expands it into clips before the preview, the way the hosted
service does. Any other request gets the shorter prompt without it.

A song request also gets its own grammar, `AIStateMapper::getLocalSongSchema()`: `"song"` is required and the
note-object clip ops (`placeClips`, `placeMidiClip`, `makeSection`, `placeSection`) are left out of the operation list.
Offered both, gemma4:e4b-mlx wrote 7 of 10 whole tracks as `placeClips` with one JSON object per note (about three
times the tokens per note, one or two bars instead of a song) and 4 of 10 were valid. With the song grammar all 10
were songs, 9 of 10 valid, 7 of 10 shape-ok (6 before), median answer 22 s against 27 s on the same quiet machine
(10 requests, seed 42, temperature 0; the suite's `analytics/ai-evals/2026-11-26-fro2317-song-grammar/`). Locked by
`AIIntegrationServiceProjectEditTest.LocalSongSchemaRequiresTheSongLayoutAndHasNoNoteObjectClipOps` and
`ALocalSongRequestOffersTheSongSchemaAndOtherRequestsTheEditSchema`.

**Judging whole-track speed when the Mac is busy.** Read the answer tokens, not the wall time: the time is answer
tokens divided by decode speed, plus 3 to 11 s to read the 5,800-token prompt once per module table (later requests
reuse Ollama's cached prefix). A whole track is about 1,400 answer tokens (1,050 to 1,800), so it stays under a minute
while decode speed stays above about 25 tokens/s; gemma4:e4b-mlx decodes at 57 to 83 tokens/s. A run that comes in
slower with the same token counts is machine load, not the request. The confirmation run of the shipped path (main
f486f6e, 10 requests, seed 42, temperature 0, load average 4 to 7; the suite's
`analytics/ai-evals/2026-11-26-fro2363-track-confirm/`): 9 of 10 answered, 8 valid, 7 shape-ok, median 16.5 s, median
1,389 answer tokens, 83 tokens/s, slowest 28 s. The tenth wrote blank space and was stopped after 14 s (see
"Edit plans stream" above) instead of running to the 4,096-token cap.

**Repairs on the way in** (`projectedit`, before validation, each listed as a repaired note on the plan): a parameter
written under an invented spelling ("filterType_id", "cutoff_1", "attackDiv_placeholder") is read as the real one,
also in a modulation's "destParam", and a key that matches no parameter of its module is left out with a note instead of
rejecting the plan (`ParamNameRepair::repairInventedParamNames`); "poly" inside the instrument parameters moves to the track and a second
new track with a name already used in the answer is numbered (`repairTrackOpSlips`); a cutoff aimed at a missing id or
at the track's own instrument goes to the track's Filter. The validator itself is never relaxed.

Measured on gemma4:e4b, 5 sound-design requests x 3 runs, temperature 0 (`--local-prompt minimal|full` on the eval
harness; run folders in the suite's `analytics/ai-evals/2026-11-20-fro2115-minimal-local-prompt/`): valid plans 10 and
9 of 15 (two seeds) with the median reply 19 and 27 s, against 7 of 15 and 40 s for the whole vendored prompt. Whole
tracks (10 requests) valid 5 vs 0, median 31 s vs 64 s. Locked by `LocalProjectPromptTest` and
`AIIntegrationServiceProjectEditTest`.

## Context length (`num_ctx`)

Ollama's server default context depends on the machine (4096 tokens on a 16 GB Mac, up to 256k on a large one)
and a prompt longer than it is cut from the front without an error, so the production edit-plan prompt (about
15k tokens) would lose its opening rules on a small machine. `processRequest` therefore sends `options.num_ctx`
from `OllamaProvider::contextLengthFor`: the messages and schema at 3 characters a token plus a 2048-token
answer, rounded up to a power of two from 8192 (capped at 131072), so Ollama reloads the model only when a
request crosses a size. A short chat that fits 4096 tokens sends none and its body is unchanged. Locked by
`OllamaProviderContextLength.SizesContextToThePrompt` and
`OllamaProviderTest.SendPromptSendsContextLengthForALongPrompt`.

## Keep-alive and timings

Every request body carries `"keep_alive": "30m"` (`OllamaProvider::keepAliveValue`). Ollama's default unloads the
model 5 minutes after the last answer, and reloading a multi-GB model costs far more than the answer itself. The
system prompt stays the first message, unchanged between requests, so Ollama's cached prompt prefix is reused;
keep anything that varies per request (the user message, the project context) after it. The debug log prints
`prompt_eval` against `eval` seconds from each response (`OllamaProvider::describeTimings`), which tells a slow
prompt from a slow generation. Each successful answer also carries Ollama's own counters to the caller as
`AIResponse::usage` (prompt and answer tokens, and the seconds each took; `OllamaProvider::usageFrom`). The eval
harness records them per answer and prints the median answer tokens and the decode speed: wall time on a busy Mac
swings two to three times for the same answer, the token counts do not. Locked by
`OllamaProviderTest.SendPromptAsksOllamaToKeepTheModelLoaded`,
`OllamaProviderTimings.SeparatesPromptProcessingFromGeneration`,
`OllamaProviderUsage.ReadsTokenCountsAndDurationsAndLeavesMissingOnesUnknown` and
`OllamaProviderTest.ASuccessfulAnswerCarriesTheServersTokenCounts`.

## Runaway answers on the edit-plan path

A small local model sometimes falls into a runaway inside the grammar: `"nodeId": 1000000000000000000` with digits
until the request timeout (measured on gemma4:e4b-mlx, a longer timeout never turned one into an answer). Two
guards end it early, and both apply only to the edit-plan request (the one whose schema carries `"song"`,
`OllamaProvider::isEditPlanSchema`):

- `options.num_predict` is capped at 4096 tokens (`editPlanTokenLimit`). A plan that long is not a plan, and the
  cut-off JSON fails to read as a plain error in seconds instead of at the timeout.
- `AIStateMapper::getProjectEditSchema()` bounds every integer to +-100000 and every free string to 256 characters
  (`boundRunawayValues`), so the decoder cannot emit unbounded digits or text. The shipped patch schema
  (`getPatchSchema()`) is not changed.

Locked by `OllamaProviderTest.EditPlanRequestCapsTheAnswerLengthAndOtherRequestsDoNot` and
`AIIntegrationServiceProjectEditTest.ProjectEditSchemaBoundsEveryIntegerAndFreeString`.

## Sampling options

`OllamaProvider::setSamplingOptions()` adds optional `think`, `temperature` and `seed` fields to the
request body, all omitted when unset (`temperature`/`seed` share `options` with `num_ctx`). No production caller sets them; they exist for
`Tools/AIEvalHarness`, which exposes them as `--think`, `--temperature` and `--seed`.

**Do not set `think: false` for a reasoning model.** It does not stop the model reasoning; it
removes the channel the reasoning is routed into, and the model emits that reasoning as, or in place
of, `content` — unparseable as JSON at all rather than merely corrupted in one key. Measured against
gpt-oss-20b, where it takes a working prompt set to zero applied patches. See
[structured output](structured-output.md) for the corruption this was an attempted fix for.

Locked by `OllamaProviderTest.SendPromptOmitsSamplingOptionsWhenUnset` and
`OllamaProviderTest.SendPromptIncludesSamplingOptionsWhenSet`.

## Corruption investigation log (structured-output leak)

Attempt of 2026-11-19, inconclusive; skip what it already covered. Harness `--mode patch --seed 42 --temperature 0`,
no `--think`, built from this tree, Ollama 0.40.12.

- **Models available:** `gemma4:12b-it-qat`, `gemma4:12b-it-qat-ctx32k`, `gemma4:e4b-mlx`, `qwen3.8:27b-mlx`. `gpt-oss:20b`, the model the
  corruption was first seen on, was not installed, so the original model was not retested.
- **`gemma4:12b-it-qat`, patch mode, 40 prompts, stopped after about 45 minutes with 14 finished.** Six of the first
  seven prompts ended as `Could not connect to Ollama` after running to the request timeout (the model was loaded and the
  server was generating the whole time, so this reads as a generation that does not stop, not a connection
  failure). One prompt (`stab`) produced a corrupted key, `"attack,"` for `attack`, rejected by the
  parameter check on both attempts. That is a one-key corruption of the kind in the bug report, without leaked prose.
- **Not measured:** the other three models, `--mode timeline`, and every untried angle (larger `num_ctx`, schema
  trimming or ordering, prompt structure, a non-reasoning model). A pinned 40-prompt run on a 12B model here takes over
  an hour, so a before/after over 20+ prompts per angle does not fit one sitting.
- **Next attempt:** pass `--timeout-ms 60000` so a runaway generation fails fast, run models one at a time, and cap a run
  with `--runs 1` on a prompt subset. Count runaway generations (timeout) separately from corrupted keys; they
  may be the same defect.

No code change came out of this attempt.

### Second attempt, 2026-11-21: invalid, the server was busy with another job

Harness `--mode patch --seed 42 --temperature 0 --runs 1 --timeout-ms 60000`, no `--think`, Ollama 0.40.12, one model at
a time. The harness has no flag to pick a prompt subset (it always replays the 40), so the run was stopped by hand after
10 prompts.

- **`gemma4:12b-it-qat-ctx32k`, first 10 prompts (`basic-bass` to `poly-keys`): 10 of 10 timeouts**, 0 ok, 0 corrupted
  keys, 0 leaked prose. This is not a measurement of the model. A plain request with no schema ("say hi in 3 words",
  same seed and temperature) got no answer in 100 s either.
- **Cause: the server had one slot and another job was holding it.** A song-steps eval run against the same model
  through `/v1/chat/completions` was generating the whole time (server log: requests of 50 to 70 minutes each, 8 t/s,
  ending at 21k to 24k tokens of a 32k context, so those runs are themselves very long or runaway generations). Every
  harness request queued behind it and hit the 60 s timeout. The earlier `/api/chat` entries in the server log are all
  `500` after exactly 1 m.
- **This weakens the 2026-11-19 result too.** Its 6 of 7 timeouts on `gemma4:12b-it-qat` may have been queueing
  behind a long job rather than runaway generation. Treat "runaway generation" there as unproven.
- **Not measured:** `qwen3.8:27b-mlx` and `gemma4:e4b-mlx` (not started, the server was still busy; a second
  model would also have queued behind the same job). `gpt-oss:20b` is still not installed.
- **Ruled out:** nothing. **Not ruled out:** every angle, including a larger `num_ctx`.
- **Next step:** run only when the server is idle (`curl localhost:11434/api/ps` shows no active generation and
  `tail ~/.ollama/logs/server.log` shows no running `/v1/chat/completions`), and first send one plain request to
  confirm it answers in a few seconds. Add a harness flag to pick a prompt subset so a 10-prompt run is one command.
  Then run the three remaining models as listed above.

No code change came out of this attempt.

### Third attempt, 2026-11-22: idle server, first real measurement

Harness `--mode patch --seed 42 --temperature 0 --runs 1`, no `--think`, Ollama 0.40.12, server idle (`/api/ps` empty,
no other job). The harness now takes `--only a,b` and `--limit N` to replay a prompt subset.

- **`gemma4:12b-it-qat` (GGUF) never answers here.** 10 of 10 patch prompts timed out at 120 s, and so did a plain
  request with no schema ("say hi in 3 words", no reply in 150 s) on the idle server. So the 2026-11-19 and
  2026-11-21 timeouts were not queueing or runaway structured output: this model hangs on any request on this
  machine. Do not use it for the investigation until that is fixed (a stuck GGUF runner, not the schema).
- **`gemma4:e4b-mlx` works (plain request in 3 s), first 20 prompts: 14 applied, 5 never applied, 1 provider
  error.** All 14 applied patches passed the shape checks. The 5 rejections are wrong parameter names, not leaked
  reasoning: `"rate"` (4 prompts, the same LFO name the hosted catalog repair fixes, FRO1469) and `"steps"` (1).
  No prompt produced leaked prose or a garbled key like `"attack,"`.
- **`qwen3.5:4b-mlx`:** a plain request took 85 s because it thinks first; not run through the harness.
- **Not reproduced.** Neither model shows the leak, and `gpt-oss:20b` is still not installed. Ruled out: queueing as the
  cause of the earlier timeouts. Not ruled out: the leak on the original model, a larger `num_ctx`, `--mode timeline`.
- **Next step:** install `gpt-oss:20b` (or ask Tal which model the leak was seen on), then run `--mode timeline` and
  the full 40 prompts on it with `--timeout-ms 120000`.

### Fourth attempt, 2026-11-23: the original model, small Gemma and Llama 4

Harness `--mode patch --limit 20` and `--mode timeline`, `--runs 1 --seed 42 --temperature 0`, no `--think`, Ollama 0.40.12,
idle server. Patch mode applied 13 of 20 (`gemma4:e4b-mlx`), 15 of 20 (`gemma4:e2b-mlx`) and 1 of 20 (`gpt-oss:20b`).

- **The corruption reproduces on `gpt-oss:20b`, as garbled keys, not leaked prose.** Answers came back with escaped quotes
  fused into keys (`"resonance\": 0.4"`, `"sustain\": 0.2, "`), so the patch is rejected as `Root is not an object`. 17 of 20
  patch requests and all 8 timeline requests hit the 180 s timeout; the answers that did come back took 4 to 7 minutes,
  and two re-run with a 15-minute timeout still failed to apply. Most of that time is hidden reasoning, since the answers are about 1.4 k characters.
- **Both small Gemma models corrupt keys too:** `scaleRoot_`, `scaleRootB`, `scale_Choice`, `rate_choice`. Median answer
  79 s (`e2b`) and 106 s (`e4b`). Timeline ops were rejected in 4 of 8 (`e2b`) and 3 of 7 (`e4b`) answers.
- **Llama 4:** the smallest build is Scout (109 B, 67 GB, about 90 s cold load). On 3 prompts: 2 applied, 1 timed out at 600 s,
  171 to 195 s per answer. Too slow and too large to be a small-model candidate; not run further.
- **Next step:** make a local answer fast (well under a minute) before running more comparisons.
