# RemoteProvider

`Source/AI/RemoteProvider.{h,cpp}` talks to the hosted inference service over libcurl (WinHTTP on Windows) instead of
`juce::WebInputStream`. Registration and selection are in [providers](providers.md).

It reuses [OllamaProvider](ollama-provider.md)'s worker-thread, queue and cancellation architecture
almost exactly — `queueLock`-guarded `pendingRequests`, an `inFlight` map, the
`idle`/`starting`/`running` worker state, and `claimDelivery()`/`deliverResult()`/`deliverError()`
with the same exactly-once and `forceSynchronous`-during-shutdown contract — so the worker-thread
rules stated there apply here unchanged.

## Wire contract

- `POST {host}/v1/capability/patch.generate`
- Request: `{"productName": string, "userPrompt": string, "promptVersion": 3}`. `currentPatch` is
  always omitted, never sent as `null`. `promptVersion` 3 (`kPatchGeneratePromptVersion`) is the prompt
  that writes a sequencer's pattern as the node's `state` (2) and keeps new Sequencers on Follow Song Key (3); a request without it gets prompt 1.
- Success: HTTP 200, `{"data": <Patch JSON object>}`. `data` is re-serialized with
  `juce::JSON::toString()` and delivered as `AIResponse::content`, the same raw-JSON-text shape
  `AIIntegrationService::applyPatch()` expects from any provider.
- Error: non-2xx, `{"error": {"code": string, "message": string}}`.

Why the client's already-wrapped prompt text can travel as `userPrompt` alone is explained in
[the engine's request flow](engine.md#request-flow).

**There is no conversational mode.** The service exposes no plain-chat capability; its
calls are the capabilities `patch.generate` and `project.generate` and the routes `/v1/song/plan` and `/v1/song/build`. `AIIntegrationService::sendMessage()`
calls `sendPrompt()` with a void `responseSchema` for ordinary chat turns, so
`RemoteProvider::sendPrompt()` fails fast — no network call — with `AIErrorKind::Schema` when
`responseSchema.isVoid()`, mirroring `OllamaProvider`'s no-model-selected precedent. It fails fast
the same way for an empty `conversation` or a blank or whitespace-only last message.

## Which server gets the token

The account token (`Authorization: Bearer`) and `X-Device-Id` go **only to the official server** (the host of
`synth::branding::kApiBaseUrl`; in Debug builds also the `AGENTSYNTH_LOCAL_API_URL` host). Everything is decided by
`synth::serverurl` (`Source/AI/ServerUrlPolicy.{h,cpp}`):

- A custom server address must be `https://`, or `http://` only for `localhost`, `127.0.0.1` or `::1`. Any other address
  fails the request with a `Network` error and no network call (a saved bad address is refused, never swapped for the
  official one).
- An accepted custom address still gets no `Authorization` and no `X-Device-Id`.
- Settings > AI refuses a bad Remote address on Return or on leaving the field (inline message, read out to screen
  readers, not saved). The Ollama host is only warned about when it is not on this machine.
- The sign-in page from the device-code response opens only when it is `https` on the API host or on `agentsynth.app`
  (`isTrustedVerificationUri`); otherwise sign-in fails with an error and nothing is opened.
- `CredentialSentToUnverifiedHostGuard` (`Tests/AI/ServerUrlPolicyTests.cpp`) lists every file that sets an
  `Authorization` header; a new one must gate on `isOfficialServer`.

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

## Prompt-version skew

The app pins a system-prompt version per capability (`kProjectGeneratePromptVersion`, `kPatchGeneratePromptVersion`,
`kTimelineGeneratePromptVersion`), and a build can reach users before the service deploy that serves it. The service
then answers (HTTP 502, `error.message`) `Unknown <capability> system prompt version: N. Known versions: 1, 2, ... M`.
`RemoteProvider::processRequest` handles it for every capability request and for `sendPrompt`: it retries **once** with
`promptVersion` rewritten to M (`parseHighestKnownPromptVersion`, `withPromptVersion`; the "A to B" spelling parses
too), only when M is below the pin. A retry that still hits the skew, or a message with no readable list, fails with
`promptVersionSkewUserMessage()` ("The AI service is being updated...") instead of the server text; any other retry
failure maps as usual. Tests: `Tests/AI/RemoteProvider/RemoteProviderCapabilityTests.cpp`.

The release workflow also runs `scripts/check-prompt-pins.sh` before it creates a tag: it reads the three pins from
source, probes each capability with version 9999 (the service has no version-list endpoint, so the error text is the
list; the probe sends the anonymous `X-Device-Id: ci-prompt-pin-probe`) and fails when a pin is ahead of the live
service. An unreachable or unreadable service only warns. Self-test: `scripts/tests/check-prompt-pins.test.sh` (Lint).
A new pin must be added to the table at the top of the script.

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

### One plan, two transports

The chat has one input and no mode selector. With a timeline wired in, every edit request goes
through `AIIntegrationService::sendProjectMessage()`, which asks for one
[edit plan](timeline-ops.md#one-edit-plan) (a patch plus a sibling `timelineOps` list, either side
optional) and absorbs the transport difference so it is never a behaviour difference:

- **Hosted provider** — `sendCapabilityRequest("project.generate", buildProjectRequestBody(text))`:
  the `timeline.generate` fields below plus `currentPatch`, the same stripped graph JSON the patch
  path sends, as an object and omitted when the graph is empty, and `songKey` (e.g. `"D Dorian (D E F G A B C)"`) when the project has a song key. The hosted service has no free-text
  chat, so on a hosted provider every message is an edit request.
- **Local provider** — `sendPrompt()` with the SAME fields composed into the outgoing message
  (`buildProjectAugmentedContent()`, in `project.generate`'s own section order: the patch or
  "Current patch is empty.", the arrangement when non-empty, the tracks, the targets, the prompt,
  plus one trailing steering line standing in for the dedicated system prompt the server swaps in),
  and `AIStateMapper::getPatchSchemaWithTimelineOps()` as the response contract. A local message the
  chat classifies as conversational (`shouldUseStructuredOutput()` false) goes through
  `sendMessage()` instead and gets text back.

The history splice matches `sendMessage()`: `chatHistory` keeps the raw user text, and the composed
context exists only on the wire. Without a timeline (tests, a host without one) the chat keeps the
plain patch request (`sendMessage()`), and its answer is read the same way.

`RemoteProvider` still accepts any capability name, `timeline.generate` included (the hosted server
offers it), but the app no longer sends it: the arrange-only request path was removed, and
`buildArrangeRequestBody()` is the base of the project body.

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

**The response re-enters the existing seam unchanged.** `project.generate` answers
`{"data": {...plan...}}`; `RemoteProvider` re-serializes `data` as `AIResponse::content` exactly as
for a patch, and the chat reads it the way it reads a local answer: the plan goes to
`previewProjectEdit()`, ONE card shows the preview, and the user's single Apply runs
`applyProjectEdit()` ([chat component](chat-component.md#one-answer-one-card)), with **no
remote-specific branch**. A new way to *ask*, never a second way to *apply*; both doors' validators
are untouched and [the two-door model](timeline-safety.md#the-two-door-model) stands.

A plan the engine refuses shows the reason in the card with no Apply button, and there is **no
client retry loop**: the server runs its own bounded repair-retry inside the capability, so a
rejection here is information for the user, not a trigger for another round trip.

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
`Tests/AI/AIIntegrationService/AIIntegrationServiceProjectEditTests.cpp` (both `sendProjectMessage`
request shapes, the structured body, the 64-target cap, empty-timeline explicitness), and
`Tests/UI/Assistant/AIChatComponent/AIChatComponentEditPlanTests.cpp` (the chat's routing on both
transports and the one-card flow).

## Service routes and streamed responses

`sendRouteRequest` posts to a route outside `/v1/capability/` (the song plan) and answers with the response's `data`;
`streamRouteRequest` reads the response as lines (NDJSON) on the worker thread, posts each to the message thread in order
and finishes once. The body adds `productName` like a capability's. Failures go through the same status mapping
(`classifyFailure`) and the typed error also carries the HTTP status and the server's `error.code`. A stream has its own
11 minute ceiling instead of the request timeout and holds the single worker for its whole length. cancel() closes the
connection (libcurl's progress callback, or the WinHTTP watchdog); lines still queued for the message thread are dropped.
The transport seam is `StreamPerformer`; a provider built with only an `HttpPerformer` streams through it as one chunk.
Details: [song steps](song-steps.md).

## Transport seam and platform support

`RemoteProvider::HttpPerformer` (`RemoteProvider.h`) parallels `OllamaProvider::InputStreamFactory`:
a constructor taking just a host installs the real platform performer (libcurl, or WinHTTP on Windows), and a second
constructor injects a fake one for tests, with no real sockets — see
`Tests/AI/RemoteProvider/RemoteProviderTestFixture.h`.

**Per-platform transport.** macOS and Linux use libcurl (`RemoteProvider.cpp`, `AuthClient.cpp`;
root `CMakeLists.txt` requires `CURL` there, and the macOS SDK's `curl.tbd` stub means no Homebrew
dependency). Windows uses WinHTTP through `Source/AI/WinHttpTransport.cpp`, shared by both clients,
with no third-party library: a watchdog thread closes the request handle when the cancel flag is set
or the total timeout elapses, so a request blocked on a slow generation still aborts promptly.
`RawHeaderParser.cpp` turns WinHTTP's raw header block into the same header map curl produces.
