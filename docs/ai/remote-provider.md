# RemoteProvider

`Source/AI/RemoteProvider.{h,cpp}` talks to the hosted inference service over libcurl instead of
`juce::WebInputStream`. Registration and selection are in [providers](providers.md).

It reuses [OllamaProvider](ollama-provider.md)'s worker-thread, queue and cancellation architecture
almost exactly — `queueLock`-guarded `pendingRequests`, an `inFlight` map, the
`idle`/`starting`/`running` worker state, and `claimDelivery()`/`deliverResult()`/`deliverError()`
with the same exactly-once and `forceSynchronous`-during-shutdown contract — so the worker-thread
rules stated there apply here unchanged.

## Wire contract

- `POST {host}/v1/capability/patch.generate`
- Request: `{"productName": string, "userPrompt": string}`. `currentPatch` and `promptVersion` are
  always omitted, never sent as `null`.
- Success: HTTP 200, `{"data": <Patch JSON object>}`. `data` is re-serialized with
  `juce::JSON::toString()` and delivered as `AIResponse::content`, the same raw-JSON-text shape
  `AIIntegrationService::applyPatch()` expects from any provider.
- Error: non-2xx, `{"error": {"code": string, "message": string}}`.

Why the client's already-wrapped prompt text can travel as `userPrompt` alone is explained in
[the engine's request flow](engine.md#request-flow).

**There is no conversational mode.** The service exposes no plain-chat capability; its
capabilities are `patch.generate` and `timeline.generate`. `AIIntegrationService::sendMessage()`
calls `sendPrompt()` with a void `responseSchema` for ordinary chat turns, so
`RemoteProvider::sendPrompt()` fails fast — no network call — with `AIErrorKind::Schema` when
`responseSchema.isVoid()`, mirroring `OllamaProvider`'s no-model-selected precedent. It fails fast
the same way for an empty `conversation` or a blank or whitespace-only last message.

## Error-kind mapping

Checked in this order; `cancelled` is checked before any of the below, the same way `OllamaProvider`
checks `wasCancelled()` right after the network call returns.

| Condition | `AIErrorKind` |
|---|---|
| transport failure that was not a cancellation | `Network` |
| `timedOut` | `Timeout` |
| `cancelled` | `Cancelled` |
| HTTP 401 / 403 | `Auth` |
| HTTP 429, `error.code == "QUOTA_EXCEEDED"` | `Quota` (see [quota and the upgrade path](accounts.md#quota-and-the-upgrade-path)) |
| HTTP 429, any other or no code | `RateLimit` (reads `Retry-After`) |
| HTTP 402, `error.code == "TRIAL_EXHAUSTED"` | `TrialExhausted` (see [the anonymous trial](accounts.md#device-id-and-anonymous-trial)) |
| HTTP 402, any other or no code | `Quota` |
| HTTP 503, `error.code == "SERVICE_CAPACITY_EXCEEDED"` | `ServiceCapacityExceeded` |
| HTTP 400 / 404 | `Schema` — a client or request-shape problem, not worth retrying as-is |
| HTTP 500 / 502 / 503 (any other code) / any other unexpected non-2xx | `Server` |
| 2xx with no parseable `data` | `Schema` |

Whenever the response body parses as JSON with a string `error.message`, it is appended to the
delivered error message, mirroring the name-the-specific-failure ethos of
`AIIntegrationService::buildCorrectionPrompt`; otherwise the message just names the HTTP status.
`TrialExhausted`, `ServiceCapacityExceeded` and `Quota` via the `QUOTA_EXCEEDED` 429 path are the
exceptions: the server's `error.message` there is a complete, user-facing sentence on its own, so it
is delivered verbatim as `AIError::message` rather than tacked onto a generic prefix. The generic
402 `Quota` mapping, with no recognised code, keeps the appended-prefix behaviour.

## Cancellation

libcurl does not need the `StreamPublisher`/`activeStream`/`streamLock` machinery `OllamaProvider`
uses to abort a `WebInputStream` mid-connect. `CURLOPT_XFERINFOFUNCTION` is invoked periodically by
libcurl *during* the transfer, on the same thread running `curl_easy_perform()`, and returning
non-zero aborts it with `CURLE_ABORTED_BY_CALLBACK`. `RequestState` therefore only needs `cancelled`
and `delivered` atomics.

`cancel()` on a still-queued request pulls it out of `pendingRequests` and delivers `Cancelled`
immediately, identical to `OllamaProvider::cancel()`'s queued branch; `cancel()` on an in-flight
request just sets the flag, and the worker's own progress callback notices it inside
`curl_easy_perform()` and unwinds on its own. **A `CURL*` handle is never touched from any thread
other than the one running `curl_easy_perform()` for it.**

## Capability requests

The service exposes more than one capability, and they differ in **input shape**: `patch.generate`
takes a prompt, while `timeline.generate` takes structured fields that have no home in a
conversation.

`AIProvider::sendCapabilityRequest(capability, body, callback)` is the non-conversational sibling of
`sendPrompt`: the **caller** authors the body field by field, the provider adds what *it* owns
(`productName`, and the `Authorization`, `X-Device-Id` and `x-conversation-id` headers) and posts to
`POST {host}/v1/capability/<capability>`. The default implementation on `AIProvider` fails
synchronously with a typed `Schema` error, so providers with no capability endpoint — local Ollama,
test doubles — never need to know the method exists.

Inside `RemoteProvider` a capability request rides the SAME queue, cancel and delivery machinery
and, the part that matters, the same one status-to-`AIErrorKind` mapping above, which is what keeps
quota, trial and capacity enforcement byte-identical across capabilities. Locked by
`RemoteProviderTest.CapabilityQuotaExceededMapsToQuotaWithServerMessageIntact` and
`CapabilityTrialExhaustedMapsToDistinctKindWithServerMessageIntact`. The body is serialized to its
final JSON string on the *enqueuing* thread (`Request::capabilityBodyJson`), so no ref-counted
`juce::var` ever crosses to the worker.

### Arrange mode: one intent, two transports

`AIChatComponent` shows a Patch/Arrange selector in the model row while its gate is satisfied:
`areTimelineToolsEnabled()` plus a live `setTimelineContext()`. The gate is deliberately
**provider-agnostic** — arrange mode works on both transports, so the provider never gates the UI.
Routing is the selector's call **alone, never a keyword heuristic**; `shouldUseStructuredOutput()`
stays a patch-path concern.

The selector's gate re-syncs at
[`refreshModels()`](chat-component.md#model-discovery-ordering-contract) and at
`AIChatComponent::refreshModeControls()`, called by `MainComponent::initialiseCommon` once the
timeline context is installed — the service has no listener mechanism for that, so the owner that
installs it re-syncs the selector. Hiding the selector resets it to Patch: an invisible control must
not keep steering requests.

An Arrange send goes through `AIIntegrationService::sendArrangeMessage()`, which absorbs the
transport difference so it is never a behaviour difference:

- **Hosted provider** — `sendCapabilityRequest("timeline.generate", ...)` with the structured input
  body below.
- **Local provider** — `sendPrompt()` with the SAME fields composed into the outgoing message
  (`buildArrangeAugmentedContent()`, section for section the way the server composes them:
  arrangement context when non-empty, then tracks, then targets, then the prompt, plus one trailing
  steering line standing in for the dedicated arrange system prompt the server swaps in and a
  mid-conversation local request cannot), and `AIStateMapper::getTimelineOpsEnvelopeSchema()` as the
  response contract: an envelope-ONLY grammar sharing the ops item schema with
  `getPatchSchemaWithTimelineOps`, so the two cannot drift, with `timelineOps` **required** — an
  arrange answer with no ops is not an answer. The history splice matches `sendMessage()`:
  `chatHistory` keeps the raw user text, and the composed context exists only on the wire.

The structured input body is `buildArrangeRequestBody()`, public so tests reproduce the real request:

- `userPrompt` — the **raw** user text. Deliberately NOT pre-wrapped the way the patch path's last
  message is: `timeline.generate` composes the context sections server-side from the structured
  fields below **unconditionally**, so pre-wrapping would put every section in the model input
  twice. Same goal as the patch path's wrap-once equivalence, opposite conclusion, because the two
  capabilities wrap differently server-side.
- `arrangementContext` — [`ArrangementContext::summarize()`](arrangement-context.md); `""` for an
  empty doc, since the schema requires the key but allows it empty.
- `paramTargets` — `{nodeUuid, nodeName, paramId, min, max, default}` per automatable parameter,
  from `enumerateAutomationTargets()`: the SAME enumeration `buildAutomationTargetsSection()`
  renders as text for the local model, so the two surfaces cannot disagree about what is automatable.
  Capped at `AIIntegrationService::kMaxRemoteParamTargets` (64), mirroring the server's own limit — a
  longer list is a 400 before any model sees it.
- `availableTracks` — `{name, kind, index}` per live `TimelineDoc` track, in doc order.
  `TimelineDoc::kMaxTracks` (256) equals the server's limit, so no cap is needed.

History and conversation-id bookkeeping are shared with `sendMessage()` via
`wrapCompletionForHistory()`, one wrapper, so the two send paths cannot drift.

**The response re-enters the existing seam unchanged.** `timeline.generate` answers
`{"data": {"timelineOps": [...]}}`; `RemoteProvider` re-serializes `data` as `AIResponse::content`
exactly as for a patch, and the [timeline ops](timeline-ops.md) flow —
`extractTimelineOps()`, `TimelineOps::validate`, the `TimelineCard` preview, the user's Apply —
consumes it with **no remote-specific branch**. Arrange mode adds a second way to *ask*, never a
second way to *apply*; both doors' validators are untouched and
[the two-door model](timeline-safety.md#the-two-door-model) stands.

A response that fails `TimelineOps::validate` shows the rejection in the card with no Apply button,
and there is **no client retry loop**: the server runs its own bounded repair-retry inside the
capability, so a rejection here is information for the user, not a trigger for another round trip.

**Why the client never calls `automation.generate`.** It is a strict subset of `timeline.generate`
in both directions: its input schema is what the timeline-generate input schema extends (minus
`availableTracks`, with `paramTargets` required non-empty), and its output envelope is
`writeLane`-only. Anything it can say, `timeline.generate` can say, and the client's single gate
accepts both. A second client path would mean a second body builder, a second routing branch and a
second test surface for zero user-visible gain; the narrower capability exists server-side for
clients that only automate. The seam is ready if a dedicated automation-only surface ever becomes
worth it: `sendCapabilityRequest("automation.generate", ...)` with the same body minus
`availableTracks`.

Tests: `Tests/AI/RemoteProvider/RemoteProviderCapabilityTests.cpp` (capability URL, body, headers,
fail-fast validation, entitlement-error pass-through, envelope re-serialization),
`Tests/AI/AIIntegrationService/AIIntegrationServiceArrangeModeTests.cpp` (request-body shape, the
64-target cap, empty-timeline explicitness, the shared history and conversation-id contract, the
local transport's composed message plus envelope-only schema plus raw-text history, typed
no-provider and hosted-without-capability failures), and `Tests/AIChatComponentTests.cpp`
(provider-agnostic selector gating, explicit routing on both transports, and the card flow for a
validated and a rejected canned envelope).

## Transport seam and platform support

`RemoteProvider::HttpPerformer` (`RemoteProvider.h`) parallels `OllamaProvider::InputStreamFactory`:
a constructor taking just a host installs the real libcurl-backed performer, and a second
constructor injects a fake one for tests, with no real sockets — see
`Tests/AI/RemoteProvider/RemoteProviderTestFixture.h`.

**Windows is not supported.** `RemoteProvider.cpp`'s libcurl implementation is compiled only under
`#ifndef _WIN32`; the `#else` branch is a stub returning `transportFailed=true` with a clear
message, touching no curl API. The `windows-latest` CI job has no libcurl setup step. Root
`CMakeLists.txt` requires `CURL` (`find_package(CURL REQUIRED)`) under `if(NOT WIN32)`, covering
Linux and macOS — whose SDK ships a `curl.tbd` stub, so no Homebrew dependency is needed there — and
deliberately does not require it on `WIN32`.
