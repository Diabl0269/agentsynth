# Gravisynth AI Engine Documentation

This document provides a comprehensive overview of the AI Engine integrated into Gravisynth, detailing its architecture, components, communication patterns, and key functionalities.

## 1. Overview
The Gravisynth AI Engine serves as an intelligent sound design assistant, enabling users to generate and modify synthesizer patches using natural language commands. Its primary goal is to bridge the gap between intuitive textual instructions and the complex, modular architecture of the Gravisynth synthesizer. This allows for a more accessible and creative sound design workflow.

## 2. Architecture

The AI Engine's architecture is designed for modularity and extensibility, primarily centered around the `AIIntegrationService` which orchestrates interactions between AI models and the core synthesizer.

### Core Components:

-   **`AIProvider`**: An abstract interface defining the contract for any AI backend integration. This allows Gravisynth to support various large language models (LLMs) or AI services (e.g., Ollama, OpenAI) by implementing this interface. It specifies methods for sending prompts, retrieving responses, and managing available models.

-   **`OllamaProvider`**: A concrete implementation of the `AIProvider` interface specifically designed to interact with local Ollama instances. It handles the HTTP communication with the Ollama API, including fetching available models and managing chat completions.

-   **`AIIntegrationService`**: The central orchestrator of the AI Engine. This service manages the overall AI interaction flow. Its responsibilities include:
    *   Maintaining the conversation history with the AI.
    *   Sending user prompts (potentially augmented with current synth context) to the configured `AIProvider`.
    *   Interpreting responses from the `AIProvider`.
    *   Applying AI-generated patch data to the `juce::AudioProcessorGraph`.
    *   Managing the selection and fetching of available AI models.
    *   Notifying listeners of AI-driven changes to the synthesizer state.

-   **`AIStateMapper`**: A utility component responsible for translating between the AI-friendly JSON representation of a synthesizer patch and Gravisynth's internal `juce::AudioProcessorGraph` structure. It handles both `graphToJSON` (for providing context to the AI) and `applyJSONToGraph` (for applying AI suggestions).

### Interaction Flow:

1.  **User Input**: The user provides a natural language prompt via the UI (e.g., "create a warm pad sound with a slow attack").
2.  **Prompt Processing**: The `AIIntegrationService` receives the prompt, adds it to the chat history, and may augment it with the current synthesizer's state (obtained via `AIStateMapper`).
3.  **AI Communication**: The `AIIntegrationService` forwards the processed prompt to the currently selected `AIProvider` (e.g., `OllamaProvider`).
4.  **AI Response**: The `AIProvider` communicates with the external AI model, receives a response, and returns it to the `AIIntegrationService`.
5.  **Response Interpretation**: The `AIIntegrationService` parses the AI's response. If the response contains a JSON patch (identified by a specific format like ````json`), it extracts this data.
6.  **Patch Application**: The extracted JSON patch is then passed to the `AIStateMapper`, which translates it into commands to modify the `juce::AudioProcessorGraph`, effectively updating the synthesizer's patch.
7.  **UI Update**: The UI is updated to reflect the new chat history and the applied synthesizer changes.

## 3. Communication Pattern

The AI communicates with Gravisynth using a simplified JSON schema to describe synthesizer patches. This schema defines nodes (representing modules) and connections between them.

### Example JSON Patch Format:

```json
{
  "nodes": [
    { "id": 1, "type": "Oscillator", "params": { "Frequency": 0.2, "Waveform": "Saw" } },
    { "id": 2, "type": "Filter", "params": { "Cutoff": 0.5, "Resonance": 0.7 } },
    { "id": 3, "type": "Audio Output" }
  ],
  "connections": [
    { "src": 1, "srcPort": 0, "dst": 2, "dstPort": 0 },
    { "src": 2, "srcPort": 0, "dst": 3, "dstPort": 0 }
  ]
}
```

-   **`nodes`**: An array of synthesizer modules.
    -   `id`: A unique integer identifier for the module.
    -   `type`: The string name of the module (e.g., "Oscillator", "Filter", "ADSR").
    -   `params`: An optional object containing key-value pairs for module parameters (e.g., "Frequency", "Cutoff").

-   **`connections`**: An array detailing the signal flow between modules.
    -   `src`: The `id` of the source module.
    -   `srcPort`: The output port index of the source module.
    -   `dst`: The `id` of the destination module.
    -   `dstPort`: The input port index of the destination module.

## 4. Key Functionality

-   **Natural Language Interaction**: Users can describe desired sounds or modifications in plain English.
-   **Patch Generation & Modification**: The AI can create entirely new patches or intelligently adjust existing ones based on prompts.
-   **Context-Aware Responses**: By providing the AI with the current synthesizer state, it can generate more relevant and informed suggestions.
-   **Model Management**: Users can select from various available AI models (e.g., different Ollama models) via the application's UI.
-   **Extensible Provider System**: The `AIProvider` interface allows for easy integration of new AI backends in the future.
-   **Undoable Patches**: Apply/Merge on a patch card is recorded on the app undo stack, so `Cmd+Z` restores the user's previous patch (see below).

### AI Patch Undo/Redo Contract

`AIIntegrationService::applyPatch()` routes through `AppUndoManager::recordAIPatch()` whenever an
undo manager has been injected (`MainComponent::initialiseCommon()` calls
`aiService.setUndoManager(&undoManager)`). The service holds a **non-owning, nullable** pointer —
constructing an `AIIntegrationService` without one keeps the old direct-apply behaviour, which is
what the standalone service tests do.

The recorded action is the existing snapshot-based `SnapshotAction`, not a fine-grained diff:
`graphToJSON(graph)` is captured before the patch and after it, undo restores the before-snapshot
and redo re-applies the after-snapshot. Merge mode needs no special handling — the after-snapshot
is the whole merged graph. Transactions are named `"AI patch"` / `"AI merge"`.

Two invariants:

1.  **Listener notifications must fire on undo and redo, not just the initial apply.**
    `aiPatchAboutToApply` / `aiPatchApplied` are passed to the action as its `preRestore` /
    `postRestore` hooks. Undo and redo rebuild the graph exactly the way the original apply did,
    so skipping them would leave `GraphEditor` holding stale `ModuleComponent`s pointing at
    `VisualBuffer`s that `applyJSONToGraph` has already freed — a use-after-free, not just a
    stale render. Guarded by `AIUndoTest.ListenersFireOnUndo`.
2.  **A rejected patch pushes nothing.** `applyPatch()` runs `validatePatch()` before touching any
    listener or the undo stack, and `recordAIPatch()` returns without pushing if the mutation
    reports failure, so an invalid patch leaves no no-op entry for `Cmd+Z` to consume. Guarded by
    `AIUndoTest.FailedPatchPushesNothing`.

Because the notifications are dispatched from the undoable action, they are wrapped in a
`juce::WeakReference<AIIntegrationService>` — the action can outlive nothing in practice, but the
weak ref keeps the ordering safe if the service is ever destroyed before the undo manager.

Tests live in `Tests/AIUndoTests.cpp`.

## 5a. Patch Validity: Constrained Decoding, Retry, and Repair

Untrusted model output reaches the graph through one gate — `AIStateMapper::validatePatch(...,
trusted=false)` — which returns a typed `PatchValidationError` plus a message. That gate is a
security boundary and is never relaxed to raise the pass rate; everything below works on the
*generation* side instead.

### Measuring first

`Tools/AIPatchHarness` replays a fixed set of realistic prompts through the exact production path
and tallies rejections by `PatchValidationError`. It needs a live Ollama, so it is opt-in
(`-DENABLE_AI_HARNESS=ON`) and excluded from CI. See its README for flags and caveats.

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

Tests: `Tests/AIPatchValidationTests.cpp` (table-driven, one malformed patch per
`PatchValidationError`, plus the schema contract) and `Tests/AIPatchRetryTests.cpp` (retry bound,
error feedback, repair scope).

## 5. AIChatComponent and Logging

`AIChatComponent` (`Source/UI/AIChatComponent.cpp`) is the chat UI for AI-assisted patching. It wires user prompts to `AIIntegrationService` and displays the conversation history with optional JSON patch previews.

### Debug Logger Registration (Debug builds only)

In **Debug builds only**, `AIChatComponent` registers itself as the global `juce::Logger` by calling `juce::Logger::setCurrentLogger(this)` inside the `#else` branch of an `#ifdef NDEBUG` guard in the constructor. The debug console (`TextEditor`) and the "Debug" toggle button are also created and wired there. The destructor unregisters under `#ifndef NDEBUG`:

```cpp
// Constructor
#ifdef NDEBUG
    juce::Logger::writeToLog("AIChatComponent initialized (Release)");
#else
    // debug console setup + addChildComponent(debugConsole) ...
    juce::Logger::setCurrentLogger(this);
#endif

// Destructor
#ifndef NDEBUG
    juce::Logger::setCurrentLogger(nullptr);
#endif
```

This means `juce::Logger::writeToLog(...)` output is piped into the in-UI `TextEditor` debug console **only in Debug builds**. In Release builds no logger registration occurs.

### Logging Rules

Appends to the debug console are coalesced and the console is length-bounded. However:

> **Do NOT add high-frequency `writeToLog` calls** (per-parameter, per-sample, per-frame, per-connection). They run on the UI thread. A per-parameter log on preset load once caused a multi-second UI freeze; this is guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`. Keep logging to errors and rare events only.

### Panel Visibility Persistence

The AI panel visibility persists via the `ApplicationProperties` key `"aiPanelVisible"` (default `false`). It is read in `MainComponent::initialiseCommon()`:

```cpp
isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);
```

Changes are written back to the same key when the panel is toggled.

### Model Discovery Ordering Contract

`AIChatComponent`'s constructor calls `refreshModels()` at construction time, which calls
`AIIntegrationService::fetchAvailableModels()`. **If the service has no provider installed
yet at that point, discovery short-circuits** (`AIIntegrationService::fetchAvailableModels`
immediately invokes the callback with `({}, false)` when `provider == nullptr`) and no model
is ever selected — `AIProvider::currentModel` (e.g. `OllamaProvider::currentModel`) stays
empty for the rest of the session.

This matters because `MainComponent` declares `aiChatComponent` as a member that is
constructed in the member-initialiser list — i.e. **before** the constructor body runs — while
`aiService.setProvider(...)` only happens later, inside `MainComponent::initialiseCommon()`.
So the chat component's own ctor-time `refreshModels()` call is guaranteed to run with no
provider installed and is therefore a no-op (it issues no `/api/tags` request at all).

**Any owner that constructs `AIChatComponent` before installing a provider on the
`AIIntegrationService` it was given MUST call `chatComponent.refreshModels()` again AFTER
`aiService.setProvider(...)`.** `MainComponent::initialiseCommon()` does this immediately
after constructing the provider (either the one injected by a caller/test, or the one built via `synth::AIProviderRegistry` from the persisted provider id — see `Source/AI/AIProviderRegistry.h`). Skipping this step means `currentModel` stays
empty and every subsequent `/api/chat` request is sent with `"model": ""`, which Ollama
rejects with HTTP 400 `"model is required"`.

Regression: this call was mistakenly deleted in commit `f7cba4a` (issue #96) and replaced
with a comment incorrectly claiming the ctor-time call already covered discovery. Locked by
`MainComponentTest.AiProviderGetsModelSelectedOnStartup` and
`AIChatComponentTest.RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction`.

### AI Provider Registry

Providers are registered once, in `synth::AIProviderRegistry::createDefault()`
(`Source/AI/AIProviderRegistry.cpp`) — adding a new hosted provider is a single
`registerProvider(...)` call there, not edits scattered across `MainComponent` and
`SettingsWindow`. Each `ProviderDescriptor` carries a stable `id` (persisted to the
`"aiProvider"` setting) separately from its `displayName` (shown in the UI), so renaming
the UI label never breaks a user's saved selection. `AIProviderRegistry::create()` falls
back to the first registered provider when given an unknown or empty id — this covers
both a stale pre-registry saved value and any future case of a provider being removed.

### OllamaProvider: Fail-Fast on Empty Model + HTTP-Status-Aware Errors

`OllamaProvider::processRequest` (used by `sendPrompt`) now guards against the empty-model
case directly: if `currentModel.isEmpty()`, it returns
`"Error: No Ollama model selected. Check that Ollama is running and that a model is available
(ollama list)."` with `success = false` **without touching the network** — this closes the
window where the bug above could still silently POST `{"model": ""}`.

When the HTTP request itself fails, `OllamaProvider` now distinguishes a reachable-but-rejecting
server from an unreachable one using `juce::URL::InputStreamOptions::withStatusCode(&httpStatus)`:
- Non-zero `httpStatus` (server responded, but `createInputStream` returned `nullptr` because the
  status wasn't 2xx): `"Error: Ollama at <host> rejected the request (HTTP <status>)."`
- `httpStatus == 0` (no response at all — connection refused/timeout): the original
  `"Error: Could not connect to Ollama at <host>"`.

Locked by `OllamaProviderTest.SendPromptWithNoModelFailsWithoutHittingNetwork` and
`OllamaProviderTest.SendPromptIncludesSelectedModelInRequestBody` in
`Tests/OllamaProviderTests.cpp`.

### OllamaProvider: Worker-Thread Contract — Never Silence

**Every request accepted by `sendPrompt()` eventually gets its callback invoked**, with the
model's answer or with an error string. Nothing is ever dropped quietly, because a dropped
request leaves the chat UI waiting forever with no error to show.

Three rules make that hold:

1. **The worker parks; it does not exit on drain.** `run()` loops until `threadShouldExit()`,
   waiting on the thread's own `juce::WaitableEvent` (`Thread::wait()` / `notify()`) when the
   queue is empty. It previously `break`ed out as soon as the queue emptied — and because
   `juce::Thread` only clears its handle *after* `run()` has returned, `isThreadRunning()`
   still said `true` in that window, so `sendPrompt()`'s `if (!isThreadRunning()) startThread()`
   skipped the restart and the request sat in the queue with nothing to pick it up.
2. **Queue ownership is explicit, not inferred from `isThreadRunning()`.** A `workerState`
   (`idle` / `starting` / `running`) guarded by `queueLock` decides whether a worker will drain
   the queue. It is released in the *same* locked section that empties the queue, so a
   concurrent `sendPrompt()` either hands its request to the retiring worker (which fails it
   with a callback) or sees `idle` and starts a fresh one. `sendPrompt()` also self-heals a
   `running` state whose thread has vanished — `stopThread(0)` force-kills the worker, since
   juce never waits when given a zero timeout, so `run()` never gets to hand the queue back.
   That recovery branch is deliberately untested: forcing it requires `pthread_cancel`, whose
   forced unwind aborts under glibc when it reaches the `catch (...)` in juce's
   `threadEntryPoint` (`FATAL: exception not rethrown`). **Do not call `stopThread(0)`.**
3. **Shutdown fails the queue instead of discarding it.** `~OllamaProvider()` sets
   `isShuttingDown` (late `sendPrompt()` calls then fail immediately rather than resurrecting a
   thread on a dying object), then `stopThread(2000)`. Requests still queued are failed with
   `"Error: Request cancelled - the Ollama provider is shutting down."` **inline**, not via
   `MessageManager::callAsync` — the message loop cannot be trusted to run a deferred callback
   before the provider is gone. So every shutdown callback has already fired by the time the
   destructor returns; none can fire after it.

Locked by `OllamaProviderTest.QueuedRequestDuringThreadShutdownStillCompletes` and
`OllamaProviderTest.PendingRequestsAreFailedOnDestruction`.

### Request Cancellation

`sendPrompt()` returns an `AIProvider::RequestId`; `cancel(id)` abandons that request. This is a
real abort, not a UI dismissal — the Cancel button used to only hide the spinner while the HTTP
request ran to completion, which billed a metered backend for work the user had abandoned and,
because the queue is drained serially, made the *next* message wait behind it.

The contract, which every provider must honour:

1.  **A cancelled request still gets exactly one callback**, with `AIErrorKind::Cancelled`. A caller
    is never left hanging, and never called twice.
2.  **It must not block requests queued behind it.** If it is still in the queue, `cancel()` removes
    it under `queueLock` — the same lock the worker pops with, so exactly one of them ends up owning
    it. If it is already in flight, the read is aborted so the worker is genuinely free rather than
    waiting out the response.
3.  **An unknown, completed or already-cancelled id is a safe no-op.** The UI holds stale handles
    routinely (a response and a Cancel click cross), so this is the common case. The delivery path
    deregisters the request, which is what makes a later `cancel()` inert.

`AIIntegrationService` must **not** append an assistant turn for a cancelled request: it produced no
answer, and `chatHistory` is replayed as context, so an invented turn would be fed back on every
later message. The user's own turn stays.

#### Publish before connect — the part that makes it work

**The provider does not use `juce::URL::createInputStream()`, and must not start.** A chat request
sets `"stream": false`, so Ollama sends *nothing at all* — not even response headers — until the
entire generation is finished. That means `connect()`, not `read()`, is where a request spends
essentially all of its time. `createInputStream()` connects internally and only hands the stream
back afterwards, so a factory built on it cannot expose the stream until the generation it was
supposed to cancel has already completed.

So `InputStreamFactory` takes a third argument, a `StreamPublisher`. The factory constructs the
`WebInputStream`, calls `publish(stream.get())`, and only then calls `connect()`. A factory must
also call `publish(nullptr)` before destroying a stream it is not returning, so a concurrent
`cancel()` can never reach a dead object; every store and load of the pointer is under
`RequestState::streamLock`.

This was measured, not assumed. Against a live Ollama with a long generation in flight: publishing
after connect, `cancel()` could not stop the request at all (still running after 10 s); publishing
before it, the callback fires in **1–8 ms** and the next message is answered in ~100 ms.

How the read is aborted, in order of preference:

-   **`juce::WebInputStream::cancel()`** — the real mechanism, documented to cancel a blocking read
    and prevent subsequent connection attempts, and safe to call from another thread.
-   **`synth::CancellableStream`** — an opt-in mix-in for injected test factories, which are not
    `WebInputStream`s, so tests can unblock a stream by the same route a real socket uses.
-   **The `cancelled` flag alone** — the fallback for any other stream type. The worker finishes the
    read and discards the response. Still correct, just not prompt.

Model discovery (`fetchAvailableModels`) passes a no-op publisher: it is not cancellable and is
bounded by its own 4 s connection timeout.

Locked by `OllamaProviderTest.CancelledRequestInvokesCallbackWithCancelledKind`,
`CancelDoesNotBlockSubsequentRequest`, `CancelOfUnknownRequestIdIsSafeNoOp`,
`CallbackFiresExactlyOnceEvenIfCancelRacesCompletion` (run with `--gtest_repeat=100`),
`CancelAndCompletionEachDeliverExactlyOnceInEitherOrder`,
`AIIntegrationServiceTest.CancelledRequestDoesNotAppendToHistory`, and the `AICancelTest` UI locks.

> The race test sweeps the cancel across the completion by a growing offset rather than just
> spawning two threads. Without the offset "completion wins" is never exercised — `cancel()` sets the
> cancelled flag before releasing the read, so the worker always observes the cancel (measured: 0
> completion-wins in 1000 iterations; with the sweep, ~860 in 1000).

> **Re-verifying by hand** (the unit tests use a mock stream and therefore cannot cover
> `WebInputStream::cancel()`): with `ollama serve` running, send a prompt that generates for a while,
> hit Cancel, and confirm the input frees immediately and the next message is answered without delay.
> A regression here is invisible to the suite — the mock path would still pass.

## 6. Future Considerations

-   **Direct Saving of AI Suggested Patches**: Implement functionality for users to directly save AI-generated patches as presets.
-   **Adding AI Suggested Patches Instead of Overriding**: Provide options to merge or add AI-suggested patch components without completely replacing the existing patch.
-   **Enhanced State Mapping**: More granular control and mapping of complex module interactions and parameters.
-   **Performance Optimization**: Improving the latency of AI responses and patch application.
-   **Advanced AI Prompting**: Techniques for more sophisticated prompt construction, including few-shot examples or more detailed system instructions.
-   **User Feedback Integration**: Allowing users to explicitly rate AI-generated patches to refine future suggestions.

---
