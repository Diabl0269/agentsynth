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
sets `"stream": false`, so Ollama sends *nothing at all* — not even response headers — until the
entire generation is finished. That means `connect()`, not `read()`, is where a request spends
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

## Sampling options

`OllamaProvider::setSamplingOptions()` adds optional `think`, `temperature` and `seed` fields to the
request body, all omitted when unset. No production caller sets them; they exist for
`Tools/AIEvalHarness`, which exposes them as `--think`, `--temperature` and `--seed`.

**Do not set `think: false` for a reasoning model.** It does not stop the model reasoning; it
removes the channel the reasoning is routed into, and the model emits that reasoning as, or in place
of, `content` — unparseable as JSON at all rather than merely corrupted in one key. Measured against
gpt-oss-20b, where it takes a working prompt set to zero applied patches. See
[structured output](structured-output.md) for the corruption this was an attempted fix for.

Locked by `OllamaProviderTest.SendPromptOmitsSamplingOptionsWhenUnset` and
`OllamaProviderTest.SendPromptIncludesSamplingOptionsWhenSet`.
