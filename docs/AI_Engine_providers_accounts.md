# AI Engine: Providers & Accounts

This document covers the AI Engine's provider layer, account/sign-in surface, conversation
history, patch feedback sync, and quota/entitlement mechanics for Agent Synth. Core architecture,
the communication/patch-validation pipeline, and the agentic timeline security model live in the
companion doc, [`AI_Engine.md`](AI_Engine.md).

- [1. Conversation History](#1-conversation-history)
- [2. Local History](#2-local-history)
- [3. Patch Feedback Sync to Server](#3-patch-feedback-sync-to-server)
- [4. Opt-In Prompt Collection for Product Learning](#4-opt-in-prompt-collection-for-product-learning)
- [5. Account Sign-In Surface (AccountRow / SignInDialog)](#5-account-sign-in-surface-accountrow--signindialog)
- [6. AI Provider Registry](#6-ai-provider-registry)
- [7. OllamaProvider: Fail-Fast on Empty Model + HTTP-Status-Aware Errors](#7-ollamaprovider-fail-fast-on-empty-model--http-status-aware-errors)
- [8. Structured-Output Corruption, the Envelope Codegen, and the `params-openness` Tradeoff](#8-structured-output-corruption-the-envelope-codegen-and-the-params-openness-tradeoff)
- [9. OllamaProvider: Worker-Thread Contract — Never Silence](#9-ollamaprovider-worker-thread-contract--never-silence)
- [10. Request Cancellation](#10-request-cancellation)
- [11. RemoteProvider: The Hosted Inference Backend (libcurl)](#11-remoteprovider-the-hosted-inference-backend-libcurl)
- [12. Device Id and Anonymous Trial](#12-device-id-and-anonymous-trial)
- [13. Quota UI and the Upgrade Path](#13-quota-ui-and-the-upgrade-path)
- [14. Future Considerations](#14-future-considerations)

## 1. Conversation History

Server-side conversation history (the backend's conversation-history store) is Pro-plan only:
`RemoteProvider`'s `patch.generate` calls carry an `x-conversation-id` request header when the
client has one from a prior response in the same session, and the server responds with one (new
or same) only when it actually persisted the exchange — a free-plan response carries **no**
header at all, not an empty one.

The re-push shape mirrors [`AI_Engine.md`](AI_Engine.md#auth-token-re-push-contract)'s Auth Token contract almost exactly:
`AIIntegrationService::setConversationId(id)` stores the value in `currentConversationId`
regardless of whether a provider is installed, and `setProvider(...)` re-pushes it to whatever
provider it installs next. `AIProvider::setConversationId()` defaults to a no-op, so `OllamaProvider`
and any local/test provider are unaffected automatically.

The one-way difference from the auth token: nothing external ever calls `setConversationId()` with
a *real* id under normal operation. `AIIntegrationService::sendMessage()`'s success callback (the
same branch that appends the assistant turn to `chatHistory`) captures a non-empty
`AIResponse::conversationId` and calls `setConversationId()` itself, so the **next** call in the
session continues the same server-side thread automatically. `RemoteProvider::processRequest()`
reads the response's `x-conversation-id` header (same `result.headers` lookup used for
`Retry-After`) onto the `AIResponse` it delivers, and sends the stored id as a request header only
when non-empty — it has no notion of plans and never decides on its own whether to send one.

`AIChatComponent` is the one plan-aware call site (`Source/AI/AccountService.h`'s free function
`isProPlan(const AccountSnapshot&)`, also used by `PlanBadge`): right before every
`aiService.sendMessage(...)` call in `sendButtonClicked()`, it clears the conversation id
(`aiService.setConversationId({})`) whenever the attached `AccountService` is absent or its
snapshot isn't Pro. This is defense-in-depth, not the real gate (the server enforces Pro-only
persistence on its own) — its only job is covering a Pro→Free downgrade mid-session, where a
conversation id captured earlier in the session would otherwise still be sitting in
`AIIntegrationService` and get resent to a now-free account for no reason. A brand-new free-plan
session never has an id to clear in the first place, since the server never sent one.

Locked by `RemoteProviderTest.ConversationIdHeaderSentWhenSet` /
`ConversationIdHeaderOnlySentWhenSet` / `ConversationIdHeaderCapturedFromResponseIntoAIResponse` /
`MissingConversationIdHeaderLeavesAIResponseFieldEmpty` in `Tests/RemoteProviderTests.cpp`, and
`AIIntegrationServiceTest.ConversationIdCapturedFromResponseAndRePushedToProvider` /
`EmptyConversationIdOnResponseDoesNotCallSetConversationId` /
`SetConversationIdBeforeProviderInstalledIsRePushedBySetProvider` in
`Tests/AIIntegrationServiceTests.cpp`.

`Source/AI/AuthClient.h/.cpp` additionally exposes cloud-only conversation methods alongside
`fetchEntitlement()` — `listConversations()`, `getConversation(id)`, `deleteConversation(id)`,
`deleteAllConversations()`. Note `listConversations()` is not a side-effect-free read: the server's
`GET /v1/conversations` lazily sets/clears a grace-period deletion date on every call
(`ListConversationsResult::deletionScheduledAt`, empty when null).

## 2. Local History

Local-first, cloud-as-sync: **every** session writes its conversation to a local file regardless of
plan; a Pro session *additionally* syncs to the cloud via the conversation-id threading above. This
gives Pro users offline resilience for free and keeps `AIChatComponent` on one code path instead of
branching storage logic throughout it.

**`Source/AI/LocalHistoryStore.h/.cpp`** — one JSON file per conversation, following
`SnippetManager`'s exact convention (static methods over an explicit directory, a
`getDefaultHistoryDirectory()` for production callers, pure/filesystem-free JSON transforms):

- Location: `<userApplicationDataDirectory>/<kSettingsFolderName>/History/<id>.json`.
- Shape: `{id, title, createdAt, updatedAt, messages: [{role, content, createdAt}]}` — the same
  field names as `AuthClient::ConversationDetailResult`/`ConversationMessage`, so a UI reading
  either backend never has to translate field names. `content` still carries any fenced ` ```json `
  block **unsplit**, exactly like a stored `AIProvider::Message` — `AIChatComponent`'s existing
  extract/clean logic (originally only run once, on `aiService.getHistory()` in the constructor) is
  now `AIChatComponent::replayMessagesFrom()`, shared by that constructor path and by restoring a
  history-panel entry, so the two can never drift.
- No file-locking — same as `SnippetManager`/`ThemeManager`/`DeviceIdStore`. The multi-instance
  concurrent-write hazard is avoided by construction: each app/plugin-instance *session* mints its
  own conversation id lazily, on its first successful exchange (`AIChatComponent::
  saveCurrentConversationLocally()`), so no two writers ever target the same file. A history-panel
  list scanning all files can very rarely race a torn write from another live instance — accepted,
  same as the classes above.
- Retention is **user-configurable, local only**: a day count (30/90/180/365) or "keep forever"
  (`LocalHistoryStore::kRetainForever`), read from `juce::ApplicationProperties`'s
  `"historyRetentionDays"` key (Settings → AI tab, added after the provider/host controls —
  `SettingsWindow.cpp`'s `AISettingsTab`). `save()` prunes by `updatedAt` on every write; an
  out-of-range persisted value (e.g. a hand-edited `0`) falls back to the 180-day default rather
  than pruning everything. Independent of that setting, a hard cap
  (`LocalHistoryStore::kHardCapFiles`, 2000 files) always applies as an engineering backstop.
  **Cloud retention is explicitly NOT per-tier/configurable in this PR** — it stays the server's
  single global default; only the local copy has a user-facing retention control.

**`Source/AI/ConversationHistorySource.h`** — the backend-agnostic interface behind the history
panel: `list()`/`get(id)`/`deleteAll()`, all callback-based (never blocking the message thread).
`LocalHistorySource` wraps `LocalHistoryStore` and answers synchronously (file I/O is fast enough
not to need offloading). `CloudHistorySource` wraps the `AuthClient` conversation methods; each call
launches a **detached** worker thread that copies everything it needs (a copy of the small,
stateless `AuthClient`, the access token, a heap-allocated cancellation flag) before returning, so
the `ConversationHistorySource` object itself never needs to outlive the call — mirrors
`AIProvider::sendPrompt()`'s existing contract that a completion callback may arrive on a background
thread, leaving the caller responsible for hopping back to the message thread
(`Component::SafePointer` + `MessageManager::callAsync`, exactly like
`AIChatComponent::sendButtonClicked()` already does for `aiService.sendMessage()`).

**Backend selection** (`AIChatComponent::historyButtonClicked()`) is plan-driven, but the cloud call
itself is **signed-in**-driven, not plan-driven — that asymmetry is deliberate: `listConversations()`
is the *only* source of a pending grace-period deletion date, and that date only ever matters for a
signed-in account that has lapsed off Pro. So: whenever signed in, a cloud `listConversations()` call
always fires (learning `deletionScheduledAt` either way); when the plan is Pro its result is *also*
the list rendered; when the plan is Free/lapsed, the list rendered instead comes from
`LocalHistoryStore`. Not signed in (or no `AccountService` attached) skips the cloud call entirely.
This call happens **only** on an explicit History-button click — never speculatively, never on a
timer — because of the read-writes-on-read caveat above.

**Restoring** an entry (`AIChatComponent::restoreConversation()`) replays its messages via
`replayMessagesFrom()` and clears `aiService`'s own `chatHistory` (`aiService.clearHistory()`) —
deliberately does **not** attempt to re-seed `AIIntegrationService`'s history with the restored
turns, since no API exists for that (out of scope for this PR; adding one is a candidate for a
follow-up). The model therefore has no memory of the restored conversation until new turns
accumulate in the current session. The restored id is adopted as this session's
`currentLocalConversationId`, so subsequent local (and, for a Pro/cloud restore,
`aiService.setConversationId(id)`-continued cloud) saves keep appending to that same conversation
rather than starting a new one.

**UI chrome** (`Source/UI/AIChatComponent.h/.cpp`):
- A **History** button (top toolbar, next to New Chat) opens a `juce::PopupMenu` — a "Clear my
  history" item, then one row per conversation (title + readable date). No custom list component
  needed; `PopupMenu` was already this codebase's pattern for a "pick one item" affordance
  elsewhere (`GraphEditor`, `ModuleLibraryComponent`, `ModMatrixComponent`).
- **Upsell strip** (`upsellButton`): a single "Upgrade to Pro" button (same `urlOpener`/`kUpgradeUrl`
  mechanism as the Quota-error bubble's button, but a persistent strip, not a per-message one).
  Shown whenever `!isProPlan(snapshot)` — **including with no `AccountService` attached at all**,
  which deliberately diverges from `accountRow`/`planBadge`/`hostedModeNotice`'s "invisible until a
  service says otherwise" convention: those default to invisible because they have nothing true to
  say yet, but "not Pro" is already true before any `AccountService` exists (every caller starts on
  the free tier, signed out). The explanatory copy ("Your history is saved locally only —
  subscribers get automatic cloud backup across devices.") lives on `historyButton`'s tooltip
  instead of its own label, so it doesn't compete for space in the bottom-chrome stack — see
  `updateUpsellStrip()`.
- **Downgrade notice** (`downgradeStripLabel`): "Your subscription has lapsed — your saved history
  will be deleted on {date}." Shown only once signed in, `!isProPlan(snapshot)`, and a
  `deletionScheduledAt` is known from the last History click. Never polled.
- Both strips follow `hostedModeNotice`'s exact construction/visibility pattern
  (`addChildComponent` + `set*Visible()` + `resized()` reserving height only when visible).

Tests: `Tests/LocalHistoryStoreTests.cpp` (save/list/get/delete round trips; pure JSON transforms;
age-based pruning at each offered retention value plus "forever"; the hard cap; out-of-range
retention falling back to the default; an unparseable `updatedAt` being kept, not treated as
infinitely old). `Tests/AIChatComponentTests.cpp` (upsell/downgrade strip visibility across
signed-out/free/pro/lapsed-with-date snapshots; history panel backend selection per plan via
`setHistorySourcesForTesting()`; "Clear my history" wired to the plan-appropriate backend; restoring
a conversation replaying its messages; every successful exchange saved locally regardless of plan).
`Tests/SettingsWindowTests.cpp` (the retention control's default, persisted-value load, round trip,
and out-of-range fallback). `Tests/BrandingTests.cpp` (`resolveApiBaseUrl()`'s Debug-only
`AGENTSYNTH_LOCAL_API_URL` env var override, used to point a local build's auth/entitlement/
cloud-history traffic at a locally-run instance of the private backend — see `docs/testing.md` "Testing
Cloud-Gated Features Locally").

## 3. Patch Feedback Sync to Server

Wires the local patch-feedback thumbs log to a new endpoint,
`POST /v1/conversations/:conversationId/messages/:messageId/feedback` (`{"rating": "up"|"down",
"comment"?: string}`, Bearer auth), for signed-in Pro users only, and only when a server-side
message id is available for the rated turn.

**Where the message id comes from.** A hosted (`RemoteProvider`) response that a Pro account
persisted server-side now also returns an `x-message-id` header alongside the existing
`x-conversation-id` one — `AIProvider::AIResponse::messageId`, populated in
`RemoteProvider::processRequest()` the same way `conversationId` is (read via `result.headers`,
empty when absent). Unlike `conversationId`, this is **not** re-pushed/threaded through
`AIIntegrationService` state — it's per-turn, so it just flows through the response object to
`AIChatComponent`'s callback unchanged, which stashes it onto that turn's
`MessageData::serverMessageId` at the same point `jsonPatch` is set. Only ever populated for a
live, same-session assistant message; **not** reconstructed by the history-replay loop (same
"session-scoped" precedent as `ratingState`/`showUpgradeAction`), so rating a message from a
restored conversation stays local-only — deliberate scope limit, not a gap to fix later.

**The conversation id used for the sync is the SERVER one**, `AIIntegrationService::
getConversationId()` (a new public getter over the existing `currentConversationId` member) — not
`AIChatComponent::currentLocalConversationId`, the unrelated key `LocalHistoryStore` uses. Sending
the wrong one would fail the server's ownership check indistinguishably from a nonexistent id
(404).

**The rating callback** (`AIChatComponent`'s thumbs handler, same lambda that has always called
`patchFeedbackStore.record()`) now also, after the local record:
1. Reads `aiService.getConversationId()`.
2. If the rated message's `serverMessageId` is non-empty AND that conversation id is non-empty AND
   the attached `AccountService` reports signed-in AND Pro AND a non-empty access token — fires the
   sync. Otherwise it's a silent no-op; the local log already has the rating either way.
3. The sync itself is **fire-and-forget**: a detached background thread (same shape as
   `CloudHistorySource`'s calls — copies of a small stateless `AuthClient`, the token, and plain
   strings, never `this` or any UI state) calls the new `AuthClient::submitMessageFeedback(...)`.
   No retry, no queueing, no UI error surface — a failed sync just means that one rating never
   reached the server; nothing blocks or spins on it.

`AuthClient::submitMessageFeedback(accessToken, conversationId, messageId, rating, comment,
cancelled)` mirrors `listConversations()`'s `{ok, transportError}` result-type convention. The
JSON body omits `comment` entirely when empty, the same convention `PatchFeedbackStore::record()`
already used for its own local `comment` field. Server responses this client interprets: 200 ok;
404 (wrong/unowned conversation or message id, indistinguishable from nonexistent) and 400 (bad
`rating`) surface as `!ok` with a `transportError`; 403 (non-Pro) is unreachable from this client
since it gates on Pro before ever calling, but the server independently re-checks it regardless —
same "client only decides what to show, never what to allow" boundary as every other Pro-gated
endpoint in this file.

Test injection: `AIChatComponent::setFeedbackHttpPerformerForTesting(HttpPerformer)` installs a
fake transport for the rating callback's locally-constructed `AuthClient`, mirroring
`setHistorySourcesForTesting()`'s fake-backend idiom but at the `HttpPerformer` layer (this call
doesn't go through `ConversationHistorySource` at all).

Locked by: `Tests/AuthClientTests.cpp` (`SubmitMessageFeedback*` — request shape, comment
omission, 404/403/400/transport-failure mapping). `Tests/RemoteProviderTests.cpp`
(`MessageIdHeaderCapturedFromResponseIntoAIResponse`,
`MissingConversationIdHeaderLeavesAIResponseFieldEmpty` extended to also assert `messageId`).
`Tests/PatchFeedbackStoreTests.cpp` (`IncludesConversationAndMessageIdWhenProvided`,
`OmitsConversationAndMessageIdWhenNotProvided` — old call sites keep the original line shape
exactly). `Tests/AIChatComponentTests.cpp`
(`RatingWithServerMessageIdAndProAccountFiresExactlyOneFeedbackPost`,
`RatingOnFreePlanAccountDoesNotFireFeedbackPost`,
`RatingWithNoServerMessageIdDoesNotFireFeedbackPostEvenWhenPro`).

## 4. Opt-In Prompt Collection for Product Learning

A single settings checkbox — "Help improve AgentSynth — share my hosted-mode prompts for product
learning" (`Source/UI/SettingsWindow.cpp`'s `AISettingsTab`, next to the provider picker's hosted-
mode disclosure) — lets a signed-in user opt in to the team reviewing their hosted-mode prompt +
resulting patch for **human review** (improving prompts/UX/features), off by default. This is
explicitly **not** used to train or fine-tune AI models — the privacy policy's existing "we do not
use your prompts or patches to train AI models" promise stays intact and untouched by this feature.
Toggling off purges any already-collected samples for that user server-side immediately; the client
has nothing further to do on revoke.

The client half is a thin state-sync layer, mirroring the entitlement fetch almost exactly:
`AccountSnapshot::promptLearningOptIn`/`promptLearningOptInAt` are populated the same way
`plan`/`monthlyRequestLimit` are — `AccountService::refreshPromptLearningOptIn()` (fire-and-forget,
GET `/v1/prompt-learning`) and `setPromptLearningOptIn(bool)` (fire-and-forget, PUT
`/v1/prompt-learning` with `{"opted_in": ...}`) both go through `AuthClient`'s existing
Bearer-token layer, both no-op when signed out, and both merge their result onto the currently
published snapshot rather than replacing it (dropping the result if a sign-out raced the network
call, same guard as `refreshEntitlement()`'s). `AccountService::PendingJob` gained two `Kind`
values for this and a `bool boolArg` field (used only by `setPromptLearningOptIn`, to carry the new
value alongside the access token already occupying `arg`) — the minimal extension rather than a
second `Kind` pair or a second queue slot.

`AISettingsTab` reflects the checkbox's enabled/checked state from `AccountService`'s published
snapshot — disabled with a "Sign in required" tooltip when signed out (same gating precedent as
`AccountRow`/`PlanBadge` reading `AccountService::getSnapshot().state`), and kept live while the
Settings dialog is open by chaining onto `AccountService::onStateChanged`. That callback is a
**single `std::function` slot**, not a multicast delegate — `AIChatComponent` installs it once, for
the app's lifetime, in its `setAccountService()` — so `AISettingsTab` captures whatever was already
installed, wraps it with its own refresh, and restores the original callback verbatim in its own
destructor rather than overwriting the slot outright (which would silently stop
`AIChatComponent`'s `accountRow`/`planBadge` from refreshing for as long as the Settings dialog
stayed open). This is safe only because nothing else touches `onStateChanged` while a
`SettingsWindow` is open in practice; a future second long-lived subscriber to this slot should
make it an actual multicast rather than adding a third link to this chain.

**Distinct from two other things that also touch "prompts," easy to conflate:**
- **Failure-debug retention** is not an app-owned table at all — it's GCP Cloud Logging's
  unconditional 30-day bucket retention on the backend's error logs, which only ever contain
  `{err, capability, code}`, never a prompt body. It applies to every request regardless of this
  opt-in and has no client-side surface at all.
- **Conversation history** (§1 above; `/v1/conversations/*`) is a **Pro-only, user-facing
  convenience** — letting a subscriber list/resume/delete their *own* past exchanges, stored so
  *they* can come back to them. This feature is a **different purpose and different storage**: the
  product team reviewing opted-in samples to improve the product, available regardless of plan
  (consent is the gate here, not plan tier), stored separately from conversation rows. Toggling
  this off purges this feature's samples; it has no effect on the conversation history, and vice
  versa.

Tests: `Tests/AuthClientTests.cpp` (`fetchPromptLearningPreference`/`setPromptLearningPreference` —
method/URL/headers/body, the off-by-default null-timestamp shape, unauthorized, transport failure).
`Tests/AccountServiceTests.cpp` (`setPromptLearningOptIn`/`refreshPromptLearningOptIn` go through
the authenticated job/token path and update the snapshot; both are a no-op when signed out, asserted
by a zero-calls check on the fake server — mirroring `RefreshEntitlementIsNoOpWhenSignedOut`).
`Tests/SettingsWindowTests.cpp` (checkbox disabled/unchecked with no `AccountService` or when signed
out with the "Sign in required" tooltip; enabled and reflecting the server's opted-in value once
signed in; toggling it calls into `AccountService::setPromptLearningOptIn()`).

## 5. Account Sign-In Surface (AccountRow / SignInDialog)

The AI panel's account UI is `Source/UI/AccountRow.h/.cpp` (a slim status row: "Sign in" /
"Signing in…" / email + "Sign out") and `Source/UI/SignInDialog.h/.cpp` (the modal device-code
flow, launched the same way `SettingsWindow` is — content handed to
`juce::DialogWindow::LaunchOptions::content.setOwned(...)`, not a `DialogWindow` subclass).
`AIChatComponent` owns an `AccountRow` member unconditionally; with no `AccountService` attached
(`setAccountService()` never called) the row is invisible and contributes zero height to
`resized()`, so every existing caller/test sees byte-identical layout.

**Single-owner-per-callback-slot contract.** `AccountService::onStateChanged` and
`onAccessTokenChanged` are single `std::function` members, not a multicast listener list.
`AIChatComponent::setAccountService(AccountService*)` is the **sole** setter of both — it installs
`onStateChanged` to call `accountRow.refresh()` and `onAccessTokenChanged` to call
`aiService.setAuthToken(token)`. Neither `AccountRow` nor `SignInDialog` ever assigns those
callbacks itself:
- `AccountRow::refresh()` re-reads the snapshot for its own UI, then forwards to a currently-open
  `SignInDialog` (tracked via a non-owning `juce::Component::SafePointer<SignInDialog>`, so a
  dialog closed by any path — its own Cancel button, auto-close on success, or the native title
  bar — never leaves a dangling pointer) by calling `dialog->refresh()`.
- `SignInDialog::refresh()` re-reads the snapshot for its own UI (code, status text, the one-time
  auto-open of the verification URL).

If a second call site ever needs `AccountService`'s callbacks, it must go through
`AIChatComponent::setAccountService()`'s fan-out (extend `refresh()`/its callers) rather than
assigning `onStateChanged`/`onAccessTokenChanged` directly — a second direct assignment anywhere
would silently steal the slot from `AIChatComponent`.

Both callback lambdas installed by `setAccountService()` capture a
`juce::Component::SafePointer<AIChatComponent>`, not `this` — required because
`AccountService::publishSnapshot()`/`setAccessTokenFromWorker()` copy the `std::function` out of
the member *before* dispatching it via `MessageManager::callAsync()`, so a callback already queued
at the moment `AIChatComponent` is destroyed still runs; the `SafePointer` is what makes that safe.
`~AIChatComponent()` also clears both slots on its stored `AccountService*` as a second layer of
defense. This is why `MainComponent.h` declares `accountService` **before** `aiChatComponent`:
members are destroyed in reverse declaration order, so `aiChatComponent` (which owns those two
callback slots) is torn down first, while `accountService` is still alive to have them cleared.

`MainComponent::initialiseCommon()` calls `aiChatComponent.setAccountService(&accountService)`
**before** `accountService.attemptSilentSignIn()`, so the wiring is live for any state changes the
silent sign-in attempt produces. `accountService` is default-constructed (production host,
`KeychainTokenStore`) — on a machine with no stored refresh token this is a fast, silent no-op
(see `AccountService::attemptSilentSignIn()`'s own doc comment), which is also why every existing
`MainComponent`-constructing test continues to run at its normal speed. A machine that *does* have
a token from a real sign-in could see different behavior when a different (e.g. unsigned/ad-hoc)
build reads that Keychain item — no test seam for this exists yet; out of scope here.

## 6. AI Provider Registry

Providers are registered once, in `synth::AIProviderRegistry::createDefault()`
(`Source/AI/AIProviderRegistry.cpp`) — adding a new hosted provider is a single
`registerProvider(...)` call there, not edits scattered across `MainComponent` and
`SettingsWindow`. Each `ProviderDescriptor` carries a stable `id` (persisted to the
`"aiProvider"` setting) separately from its `displayName` (shown in the UI), so renaming
the UI label never breaks a user's saved selection. `AIProviderRegistry::create()` falls
back to the first registered provider when given an unknown or empty id — this covers
both a stale pre-registry saved value and any future case of a provider being removed.

## 7. OllamaProvider: Fail-Fast on Empty Model + HTTP-Status-Aware Errors

`OllamaProvider::processRequest` (used by `sendPrompt`) now guards against the empty-model
case directly: if `currentModel.isEmpty()`, it returns
`"Error: No Ollama model selected. Check that Ollama is running and that a model is available
(ollama list)."` with `success = false` **without touching the network** — this closes the
window where the bug described in
[`AI_Engine.md`](AI_Engine.md#model-discovery-ordering-contract) could still silently POST
`{"model": ""}`.

When the HTTP request itself fails, `OllamaProvider` now distinguishes a reachable-but-rejecting
server from an unreachable one using `juce::URL::InputStreamOptions::withStatusCode(&httpStatus)`:
- Non-zero `httpStatus` (server responded, but `createInputStream` returned `nullptr` because the
  status wasn't 2xx): `"Error: Ollama at <host> rejected the request (HTTP <status>)."`
- `httpStatus == 0` (no response at all — connection refused/timeout): the original
  `"Error: Could not connect to Ollama at <host>"`.

Locked by `OllamaProviderTest.SendPromptWithNoModelFailsWithoutHittingNetwork` and
`OllamaProviderTest.SendPromptIncludesSelectedModelInRequestBody` in
`Tests/OllamaProviderTests.cpp`.

## 8. Structured-Output Corruption, the Envelope Codegen, and the `params-openness` Tradeoff

Two corrupted local Ollama patch-generation samples with gpt-oss-20b showed a JSON key (meant to
be `"waveform": "Saw"`) replaced with garbage containing leaked model reasoning text.
**Reproduced live** (`eval-results/p6-13-baseline-patch-2026-08-21.json`, `poly-pad` scenario:
`"filterType": "LPF24", "poly": true } },?? Wait. The earlier error shows stray quotes. We must
correct JSON.` — leaked self-correction text inside a JSON key string) at a 17.5% rejection rate
(7/40) against gpt-oss:20b, unpinned sampling, the hand-written schema as it stood before this fix. **This exact
corruption is still open** — neither angle investigated below explains or fixes it; both are real,
separately-useful findings. See `Tools/AIEvalHarness/README.md` for how to reproduce.

**Confirmed: the `{}` open-schema bug.** The backend's inference service
documents a proven Ollama grammar-compiler defect — an "anything goes" subschema spelled as `{}`
(the empty-object JSON Schema, as opposed to the boolean `true`) gets mangled into a garbage
wrapped object instead of passing the value through unconstrained, confirmed on gpt-oss:20b and
gemma4:12b-it-qat. `AIStateMapper::getPatchSchemaWithTimelineOps()`'s `"track"` field
(`Source/AI/AIStateMapper.cpp`) had exactly that shape; it is now `"type": "string"` (narrowed, not
`oneOf`/`anyOf` — see the doc comment there for why: this header's own note that llama.cpp's
grammar compiler "handles anyOf poorly" rules that out too). `getPatchSchema()`'s own `params`
field never hit this — its `additionalProperties: true` was already the JSON Schema boolean, not
`{}`.

**Reproducibility knobs.** `OllamaProvider::setSamplingOptions()` adds optional `think` /
`temperature` / `seed` fields to the request body (all omitted when unset — no production caller
sets these, so this is opt-in only). `Tools/AIEvalHarness` exposes them as `--think`/
`--temperature`/`--seed`, plus a `--mode timeline` that replays a separate scenario set through
`getPatchSchemaWithTimelineOps()` instead of `getPatchSchema()` — same request path
(`OllamaProvider::processRequest` → `format` field), just the extended schema, so a corruption fix
gets verified against both local structured-output schemas the client actually sends, not just the
plain patch one.

**Tested and refuted: `think: false`.** The leading hypothesis going in — that Ollama routes
reasoning tokens into a `message.content`-adjacent `thinking` field only when `think` is
explicitly set, and that leaving it unset was the leak — was wrong, and expensively so.
`--think false --seed 42 --temperature 0` against the exact same 40 prompts: **0/40 applied,
every single response failed with `"Root is not an object"`** (`eval-results/p6-13-think-false-
2026-08-21.json`), down from the baseline's 33/40. `think: false` does not stop gpt-oss-20b's
"harmony" format from reasoning — it removes the channel a reasoning model needs to route that
reasoning into, and the model appears to emit that reasoning as (or in place of) `content`
instead, unparseable as JSON at all rather than merely corrupted in one key. **Do not set `think:
false` for this model** — this finding exists specifically so a future session doesn't re-attempt
the same fix.

A direct `curl` against `/api/chat` with a small hand-written schema (`{"waveform": {"enum":
["Sine","Saw","Square"]}}`) confirmed `format` genuinely constrains decoding on this setup — a
clean `{"waveform":"Sine"}` back, not prompt-compliance fallback. So the corruption is not "the
grammar isn't binding at all"; it is specific to the larger, real patch schema under longer
generation (many optional properties, real conversation context) in a way a trivial schema doesn't
trigger — genuinely still open, and the next angle worth trying (context length, prompt structure,
or a non-reasoning instruct model as a candidate angle) is a new investigation, not
something this document resolves.

**Confirmed clean: the new envelope + the `track` fix, live.** `--mode timeline` (default,
unpinned sampling, current post-rewrite schema) — the only path that exercises both fixes
together against a real model — passed 8/8 scenarios, `timelineOps` present in all 8 responses,
**0/8 corrupted/rejected** (`eval-results/p6-13-baseline-timeline-2026-08-21.json`). This isolates
two things at once: the new, stricter generated envelope (`additionalProperties: false` on every
nested object, where the hand-written schema had none) does not itself break generation under
normal sampling — which is what makes the `think:false` run's 0/40 attributable to `think:false`
and not to the schema rewrite — and the `track` field fix holds under real model output, not just
the unit test asserting its shape.

**Envelope codegen.** `AIStateMapper::getPatchSchema()` no longer hand-builds the schema field by
field. It parses `synth::generated::kPatchEnvelopeSchemaJson`
(`Source/AI/generated/PatchEnvelopeSchema.g.h`) — a header **vendored from the private backend
repo**, generated from the backend's patch schema definition (the same Zod source its own codegen
already uses for the client's typed C++ structs) — then layers on the two things
that source can't know: the `"type"` enum and the per-choice-parameter `enum`s inside `params`,
both read from *this build's* live module registry (`moduleFactory`, built by instantiating every
registered `AudioProcessor` and reading `AudioParameterChoice::choices`). This is a deliberate
split, not an oversight: the backend's own schema source carries a comment explaining that node
`type` stays `z.string()` because "per-module constraints are layered on by the client, not this
envelope" — the server has no module registry to enumerate against.

Regenerate after editing the backend's patch schema source: run its envelope-schema codegen in the
private backend repo, then copy the generated header into this
repo's `Source/AI/generated/PatchEnvelopeSchema.g.h` verbatim and commit both — there is no
CMake→codegen build dependency (this repo's cache/build invariants rule that out), so this is a
manual copy-and-commit step, same discipline as `Patch.g.h`'s cross-repo flow. The generated schema
is rendered **flat/inlined** (`$refStrategy: "none"`, no `$ref`/`definitions`) rather than reusing
`buildPatchJsonSchema()`'s named-definitions rendering used for the hosted inference backend path —
`$ref` indirection is untested territory for llama.cpp's grammar compiler, and the hand-written
schema it replaces has never needed anything but a flat shape.

**The `params`-openness tradeoff, stated plainly.** The hosted schema's `params` must stay open
(`additionalProperties: true`) because numeric parameter values can't be fully enumerated — that
requirement is unconditional, not something this fix can trade away, and `true` (not `{}`) already
satisfies it safely. Where a real conflict *would* exist — a field that legitimately needs to be
open-shaped for Ollama specifically but can't be — the resolution is a per-provider variant (open
for hosted providers, a narrower client-only form for Ollama), exactly what happened to the
timeline-ops `"track"` field above. `getPatchSchemaWithTimelineOps()` itself stays a client-side
C++ extension on top of the generated envelope — the private backend repo has its own hosted
`buildTimelineOpsJsonSchema()` for a *different*
capability (`POST /v1/capability/timeline.generate`), but unifying the client's local schema
against it is out of scope for now and tracked as a separate follow-up.

Locked by `AIStateMapperTest.TimelineOpsTrackFieldIsNotOpenSchema`,
`OllamaProviderTest.SendPromptOmitsSamplingOptionsWhenUnset` and
`OllamaProviderTest.SendPromptIncludesSamplingOptionsWhenSet`.

## 9. OllamaProvider: Worker-Thread Contract — Never Silence

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

## 10. Request Cancellation

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

### Publish before connect — the part that makes it work

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

## 11. RemoteProvider: The Hosted Inference Backend (libcurl)

`Source/AI/RemoteProvider.{h,cpp}` is the first non-Ollama `AIProvider`: it talks to a local
instance of the private backend's inference service over libcurl instead of `juce::WebInputStream`.
It reuses `OllamaProvider`'s worker-thread/queue/cancellation architecture almost exactly
(`queueLock`-guarded `pendingRequests`, `inFlight` map, `idle`/`starting`/`running` worker state,
`claimDelivery()`/`deliverResult()`/`deliverError()` with the same exactly-once and
`forceSynchronous`-during-shutdown contract) — see "OllamaProvider: Worker-Thread Contract" above
for the rules this inherits unchanged.

**Wire contract:**

- `POST {host}/v1/capability/patch.generate`
- Request: `{"productName": string, "userPrompt": string}` — `currentPatch` and `promptVersion`
  are always omitted (not sent as `null`).
- Success: HTTP 200, `{"data": <Patch JSON object>}`. `data` is re-serialized with
  `juce::JSON::toString()` and delivered as `AIResponse::content` — the same raw-JSON-text shape
  `AIIntegrationService::applyPatch()` already expects from any provider.
- Error: non-2xx, `{"error": {"code": string, "message": string}}`.

**Why `userPrompt` can carry the client's already-wrapped text unmodified.**
`AIIntegrationService::buildPatchAugmentedContent()` renders the current graph into the *last*
conversation message as `"Current patch state:\n\`\`\`json\n<JSON>\n\`\`\`\n\nUser request:
<text>"` whenever the graph is non-empty, and as `"Current patch is empty.\n\nUser request:
<text>"` when it has zero nodes — the model always gets an explicit signal about whether a patch
already exists, rather than silently falling back to the bare request text (which left "is there
already a patch?" unanswered for a fresh-session prompt like "create a bass patch"). The service's own
`buildUserMessage()` (the backend's patch-generate capability)
performs the *exact same* wrapping server-side, from a separate `currentPatch` + `userPrompt`
pair, and only wraps when `currentPatch` is present. Sending the client's already-wrapped text as
`userPrompt` alone, with `currentPatch` omitted, therefore produces byte-identical model input to
the "proper" structured split — without RemoteProvider ever having to parse the wrapper back
apart. **Do not "fix" this into a parser** that splits `userPrompt` into `currentPatch` +
`userPrompt` — it would just reimplement, and risk diverging from, wrapping the service already
does.

**Conversational mode is out of scope.** The service exposes no plain-chat capability (its
capabilities are `patch.generate` and `timeline.generate` — see "Remote capabilities beyond
patch.generate" below). `AIIntegrationService::sendMessage()` calls `sendPrompt()` with a void
`responseSchema` for ordinary chat turns, so `RemoteProvider::sendPrompt()` fails fast — no
network call — with `AIErrorKind::Schema` when `responseSchema.isVoid()`, mirroring
`OllamaProvider`'s "no model selected" fail-fast precedent. It also fails fast the same way for an
empty `conversation` or a blank/whitespace-only last message (mirrors the service's own
`userPrompt: z.string().min(1)`).

**Error-kind mapping** (checked in this order — `cancelled` is checked before any of the below,
same as `OllamaProvider` checks `wasCancelled()` right after the network call returns):

| Condition                                    | `AIErrorKind`                    |
|-----------------------------------------------|-----------------------------------|
| transport failure that wasn't a cancellation  | `Network`                        |
| `timedOut`                                    | `Timeout`                        |
| `cancelled`                                   | `Cancelled`                      |
| HTTP 401 / 403                                | `Auth`                            |
| HTTP 429, `error.code == "QUOTA_EXCEEDED"`    | `Quota` (the monthly quota — see "Quota UI and the Upgrade Path" below) |
| HTTP 429, any other/no code                   | `RateLimit` (reads `Retry-After`) |
| HTTP 402, `error.code == "TRIAL_EXHAUSTED"`   | `TrialExhausted` (see "Device Id and Anonymous Trial" below) |
| HTTP 402, any other/no code                   | `Quota`                           |
| HTTP 503, `error.code == "SERVICE_CAPACITY_EXCEEDED"` | `ServiceCapacityExceeded` (see below) |
| HTTP 400 / 404                                | `Schema` (client/request-shape problem, not worth retrying as-is) |
| HTTP 500 / 502 / 503 (any other code) / any other unexpected non-2xx | `Server` |
| 2xx with no parseable `data`                  | `Schema`                          |

Whenever the response body parses as JSON with a string `error.message`, it is appended to the
delivered error message (mirrors the "name the specific failure" ethos on
`AIIntegrationService::buildCorrectionPrompt`); otherwise the message just names the HTTP status.
`TrialExhausted`, `ServiceCapacityExceeded`, and `Quota` via the `QUOTA_EXCEEDED` 429 path are the
exceptions to "appended": the server's `error.message` there is a complete, user-facing sentence on
its own (see below and "Quota UI and the Upgrade Path"), so it is delivered verbatim as
`AIError::message` rather than tacked onto a generic prefix. The pre-existing generic-402 `Quota`
mapping (no recognised code) keeps the appended-prefix behaviour — only the `QUOTA_EXCEEDED` 429
path is verbatim.

**Cancellation, simplified vs `OllamaProvider`.** libcurl doesn't need the
`StreamPublisher`/`activeStream`/`streamLock` machinery `OllamaProvider` uses to abort a
`WebInputStream` mid-connect: `CURLOPT_XFERINFOFUNCTION` is invoked periodically by libcurl
*during* the transfer, on the same thread running `curl_easy_perform()`, and returning non-zero
aborts it with `CURLE_ABORTED_BY_CALLBACK`. `RequestState` therefore only needs `cancelled` and
`delivered` atomics. `cancel()` on a still-queued request pulls it out of `pendingRequests` and
delivers `Cancelled` immediately (identical to `OllamaProvider::cancel()`'s queued branch);
`cancel()` on an in-flight request just sets the flag — the worker's own progress callback notices
it inside `curl_easy_perform()` and unwinds on its own. A `CURL*` handle is never touched from any
thread other than the one running `curl_easy_perform()` for it.

### Remote capabilities beyond patch.generate: timeline.generate (arrange mode)

The service exposes more than one capability, and they differ in **input shape**: `patch.generate`
takes a prompt (the `sendPrompt()` path above), while `timeline.generate`
(the backend's timeline-generate capability) takes structured
fields that have no home in a conversation. `AIProvider::sendCapabilityRequest(capability, body,
callback)` is the non-conversational sibling: the **caller** authors the body field by field, the
provider adds what *it* owns (`productName`, the `Authorization`/`X-Device-Id`/`x-conversation-id`
headers) and posts to `POST {host}/v1/capability/<capability>`. The default implementation on
`AIProvider` fails synchronously with a typed `Schema` error, so providers with no capability
endpoint (local Ollama, test doubles) never need to know the method exists. Inside
`RemoteProvider` a capability request rides the SAME queue/cancel/delivery machinery and — the
part that matters — the same one status→`AIErrorKind` mapping above, which is what keeps
quota/trial/capacity enforcement byte-identical across capabilities (locked by
`RemoteProviderTest.CapabilityQuotaExceededMapsToQuotaWithServerMessageIntact` /
`CapabilityTrialExhaustedMapsToDistinctKindWithServerMessageIntact`). The body is serialized to
its final JSON string on the *enqueuing* thread (`Request::capabilityBodyJson`) so no ref-counted
`juce::var` ever crosses to the worker.

**The arrange path, end to end — one intent, two transports.** `AIChatComponent` shows a
Patch/Arrange selector in the model row, visible while its gate is satisfied
(`areTimelineToolsEnabled()`, unconditionally on now that the timeline is GA, + a live
`setTimelineContext()`). The gate is deliberately **provider-agnostic** — the local/remote parity
rule: arrange mode works on both transports, so the provider never gates the UI. Routing is the
selector's call **alone — never a keyword heuristic**; `shouldUseStructuredOutput()` stays a
patch-path concern. The selector's gate re-syncs at `refreshModels()` (a convenient known resync
point) and at `AIChatComponent::refreshModeControls()` called by `MainComponent::initialiseCommon`
once the timeline context is installed (the service has no listener mechanism for that, so the
owner that installs it re-syncs the selector). Hiding the selector resets it to Patch — an
invisible control must not keep steering requests.

An Arrange send goes through `AIIntegrationService::sendArrangeMessage()`, which absorbs the
transport difference so it is never a behaviour difference:

- **Hosted provider** → `sendCapabilityRequest("timeline.generate", …)` with the structured
  input body below.
- **Local provider (Ollama)** → `sendPrompt()` with the SAME fields composed into the outgoing
  message (`buildArrangeAugmentedContent()`, section-for-section the way the server's own
  `buildTimelineUserMessage()` composes them: arrangement context when non-empty, then tracks,
  then targets, then the prompt — plus one trailing steering line, standing in for the dedicated
  arrange system prompt the server swaps in and a mid-conversation local request cannot), and
  `AIStateMapper::getTimelineOpsEnvelopeSchema()` as the response contract: an envelope-ONLY
  grammar sharing the ops item schema with `getPatchSchemaWithTimelineOps` (one source, cannot
  drift), with `timelineOps` **required** — an arrange answer with no ops is not an answer. The
  history splice matches `sendMessage()`: chatHistory keeps the raw user text, the composed
  context exists only on the wire.

Either way the answer is the same `{"timelineOps": [...]}` envelope, and everything downstream is
shared. The structured input (`buildArrangeRequestBody()` — public, so tests reproduce the real
request):

- `userPrompt` — the **raw** user text. Deliberately NOT pre-wrapped the way
  `buildPatchAugmentedContent()` wraps the patch path's last message: `timeline.generate`'s own
  `buildTimelineUserMessage()` composes the context sections server-side from the structured
  fields below **unconditionally**, so pre-wrapping would put every section in the model input
  twice. (Contrast with the patch path's wrap-once equivalence argument above — same goal,
  opposite conclusion, because the two capabilities wrap differently server-side.)
- `arrangementContext` — `ArrangementContext::summarize()` (§8 in
  [`AI_Engine.md`](AI_Engine.md#8-arrangement-context-arrangementcontextsummarize)); `""` for an empty doc (the
  schema requires the key but allows it empty).
- `paramTargets` — `{nodeUuid, nodeName, paramId, min, max, default}` per automatable parameter,
  from `enumerateAutomationTargets()`: the SAME enumeration `buildAutomationTargetsSection()`
  renders as text for the local model, so the two surfaces cannot disagree about what is
  automatable. Capped at `kMaxRemoteParamTargets` (64, mirroring the server's
  `MAX_PARAM_TARGETS` — a longer list is a 400 before any model sees it).
- `availableTracks` — `{name, kind, index}` per live `TimelineDoc` track, in doc order.
  `TimelineDoc::kMaxTracks` equals the server's `TIMELINE_OPS_MAX_TRACKS` (256), so no cap is
  needed.

History and conversation-id bookkeeping are shared with `sendMessage()` via
`wrapCompletionForHistory()` — one wrapper, so the two send paths cannot drift.

**The response re-enters the existing seam unchanged.** `timeline.generate` answers
`{"data": {"timelineOps": [...]}}`; `RemoteProvider` re-serializes `data` as `AIResponse::content`
exactly as for a patch, and the §9 flow in
[`AI_Engine.md`](AI_Engine.md#9-timeline-operations) — `extractTimelineOps()` → `TimelineOps::validate` →
`TimelineCard` preview → the user's Apply — consumes it with **no remote-specific branch**. Arrange
mode adds a second way to *ask*, never a second way to *apply*; both doors' validators are
untouched (the two-door model of §7 in
[`AI_Engine.md`](AI_Engine.md#7-untrusted-timeline-data-validatetimeline) stands). A response that fails `TimelineOps::validate` shows
the rejection in the card with no Apply button, and there is **no client retry loop**: the server
already runs its own bounded repair-retry inside the capability (`generateStructured`), so a
rejection here is information for the user, not a trigger for another round trip.

**Why the client never calls `automation.generate`.** It is a strict subset of
`timeline.generate` in both directions: its input schema is what `TimelineGenerateInputSchema`
`.extend()`s (minus `availableTracks`, with `paramTargets` required non-empty), and its output
envelope is `writeLane`-only — anything it can say, `timeline.generate` can say, and the client's
single gate (`TimelineOps::validate`) accepts both. A second client path would mean a second body
builder, a second routing branch and a second test surface for zero user-visible gain; the
narrower capability exists server-side for clients that only automate. If a dedicated
automation-only surface ever becomes worth it client-side, the seam is ready:
`sendCapabilityRequest("automation.generate", …)` with the same body minus `availableTracks`.

Tests: `Tests/RemoteProviderTests.cpp` (capability URL/body/headers, fail-fast validation, the
entitlement-error pass-through, envelope re-serialization), `Tests/AIIntegrationServiceTests.cpp`
(`Arrange*` — request-body shape, the 64-target cap, empty-timeline explicitness, the shared
history/conversation-id contract, the local transport's composed message + envelope-only schema +
raw-text history, typed no-provider and hosted-without-capability failures), and
`Tests/AIChatComponentTests.cpp` (`Arrange*` — provider-agnostic selector gating, explicit
routing on both transports, and the card flow for a validated and a rejected canned envelope).

**HTTP transport seam.** `RemoteProvider::HttpPerformer` (`RemoteProvider.h`) parallels
`OllamaProvider::InputStreamFactory`: a constructor taking just a host installs the real
libcurl-backed performer, and a second constructor injects a fake one for tests (no real sockets —
see `Tests/RemoteProviderTests.cpp`).

**Windows is not supported yet.** `RemoteProvider.cpp`'s libcurl implementation is compiled only
under `#ifndef _WIN32`; the `#else` branch is a stub returning `transportFailed=true` with a clear
message, touching no curl API. The `windows-latest` CI job has no libcurl setup step, and Windows/WinINet support
was already flagged as an unverified follow-up risk rather than a blocker.
Root `CMakeLists.txt` requires `CURL` (`find_package(CURL REQUIRED)`) under `if(NOT WIN32)` —
covering both Linux and macOS (whose SDK ships a `curl.tbd` stub, so no Homebrew dependency is
needed there) — and deliberately does not require it on `WIN32`.

**Now visible.** `ProviderDescriptor::hidden` (`Source/AI/AIProviderRegistry.h`) is a
runtime flag, not a build-time one: `RemoteProvider` is fully registered and constructible via
`AIProviderRegistry::createDefault()` (id `"remote"`, registered after `"ollama"` so the
unknown-id fallback to `descriptors.front()` is unaffected — and stays that way deliberately: an
unrecognised/corrupt persisted id fails safe to the provider that sends no data anywhere, never to
the one that does), and today `hidden=false`, so `AISettingsTab`
(`Source/UI/SettingsWindow.cpp`) offers it in the provider combo box alongside `"ollama"`. The
`visibleProviders` member (still present even with nothing currently hidden) is used consistently
by both the population loop and `selectedDescriptor()` — indexing the combo's selected item
against the *unfiltered* `providerRegistry.listAll()` would desync the moment a future hidden
provider sits between two visible ones.

**Default provider.** `"remote"` (hosted) is the default for a brand new install;
`"ollama"` (local) remains the default for an install that has already launched before, even if it
has never opened AI settings — see `MainComponent::resolveDefaultProviderId()` and the migration
comment in `MainComponent::initialiseCommon()`. The persisted `"aiProvider"` key is only ever
*written* by `AISettingsTab::updateSettings()`, so its absence alone can't distinguish "brand new
install" from "existing user who never touched AI settings"; `initialiseCommon()` instead checks
whether the settings file already existed on disk before this launch. Each provider persists its
own host under its own key (`"ollamaHost"` / `"remoteHost"`) — sharing one key, the earlier
behaviour, meant switching providers in Settings silently pointed the new one at whatever host
string the previous provider had left behind. An empty/unset `"remoteHost"` falls back to
`synth::branding::kApiBaseUrl` (`Source/Branding.h`), the production Cloud Run URL — see that
constant's comment for the caveat that the service doesn't accept public traffic yet.

**Privacy disclosure.** A visible line — not just a tooltip, since the
acceptance criterion is explicit that this "should not be discoverable only by reading a policy
page" — appears next to the model picker in `AIChatComponent` whenever the active provider is
hosted: `"Hosted mode sends your prompt and current patch to Agent Synth's servers."`
`AIProvider::isHosted()` (default `false`, overridden `true` in `RemoteProvider`) drives this via
`AIIntegrationService::isCurrentProviderHosted()`; `AIChatComponent::updateHostedModeNotice()` is
called from `refreshModels()`, the same post-`setProvider()` resync point documented in
[`AI_Engine.md`](AI_Engine.md#model-discovery-ordering-contract) for model discovery, so the notice's visibility never lags a provider switch. `AISettingsTab`'s
provider combo box also carries the same disclosure as a tooltip, for the toggle itself.

**Model picker in hosted mode.** `RemoteProvider::fetchAvailableModels()` always resolves
`success=true` with an empty list (the service picks its own model server-side — see that method's
doc comment). `AIChatComponent::refreshModels()`'s callback treats `success && models.isEmpty()`
as "nothing to choose from," not a fetch failure: the picker shows a single disabled
`"Model chosen automatically"` entry rather than the misleading `"Error fetching models"` a plain
`success` check would have produced for every hosted-mode user.

**Eval harness parity.** `Tools/AIEvalHarness` (see its README) can replay its golden prompt set
through `RemoteProvider` instead of `OllamaProvider` via `--provider remote`, so a model can be
scored through the exact stack a user hits — client -> service -> Ollama/the hosted inference backend — instead of an
approximation of it. The service picks its own model server-side; the harness's `--model` is a
report label only in this mode.

## 12. Device Id and Anonymous Trial

`Source/Auth/DeviceIdStore.h/.cpp` generates a stable per-install identifier (`juce::Uuid`,
dashed-string form) the first time it runs and persists it under the app's standard settings
folder (`userApplicationDataDirectory/<kSettingsFolderName>/device_id`, the same
`getSpecialLocation`/`kSettingsFolderName` convention `ThemeManager`/`SnippetManager` use), then
reuses it for the lifetime of the install. If the file is missing, empty, or its contents don't
look like a plausible id, a fresh one is generated and written rather than crashing or leaving the
id blank — a lost/corrupted id just makes the backend see this install as new, which is harmless.

**Not a secret.** Unlike the refresh token (`KeychainTokenStore`, Keychain-backed), the device id
grants no account access by itself — it is only a "this install" signal — so it is deliberately
stored in a plain file, not the Keychain. Both `AccountService` (for `AuthClient`) and
`RemoteProvider` construct their own `DeviceIdStore` in their production constructors and read the
same persisted value; their test constructors take an explicit `deviceId` string instead (default
empty, field/header omitted) so unit tests never touch the real per-install file.

**Where it's sent:**

- `AuthClient::requestDeviceCode()` / `pollDeviceToken()` / `refreshToken()` — `device_id` form
  field alongside the existing `client_id`, whenever non-empty.
- `RemoteProvider` — `X-Device-Id` header on every `/v1/capability/*` request, sent whether or not
  `Authorization` is also set: an anonymous free-request-tier signal when there's no bearer token,
  anti-abuse signal once there is one.

**Trial-exhausted UX path.** When the free trial is used up, the capability endpoint answers
`402 {"error":{"code":"TRIAL_EXHAUSTED","message":"..."}}`, mapped by `RemoteProvider` to
`AIErrorKind::TrialExhausted` with the server's `message` carried through unchanged (see the
error-kind table above). That response reaches `AIChatComponent` through the same path every other
provider error already does — appended to the conversation as an assistant bubble
(`"Error: " + error.message`) — so no new UI surface is needed. This "no new UI surface" reasoning
is specific to `TrialExhausted`: the caller isn't signed in yet, so "upgrade" isn't the right verb —
the server's message text specifically invites signing in with Google to continue, and the existing
`AccountRow` "Sign in" affordance (see "Account Sign-In Surface" above) is already visible
immediately above the chat whenever an `AccountService` is attached, so the next step (opening
`SignInDialog`) is always one click away without this feature needing to auto-launch it. A
service-wide `503 {"error":{"code":"SERVICE_CAPACITY_EXCEEDED","message":"..."}}` is handled the
same way but mapped to the distinct `AIErrorKind::ServiceCapacityExceeded` — it's a daily cap on
the service as a whole, unrelated to the caller's own trial/quota, and gets its own message rather
than being confused with either. `AIErrorKind::Quota` (a signed-in account over its *paid* plan's
allowance) is the one case that does get a new UI surface — see below.

## 13. Quota UI and the Upgrade Path

Once a caller is signed in, the backend's quota enforcement for the monthly request quota
answers `429 {"error":{"code":"QUOTA_EXCEEDED","message":"..."}}` when it's exhausted, mapped by
`RemoteProvider` to `AIErrorKind::Quota` with the server's message carried through unchanged (see
the error-kind table above). Unlike `TrialExhausted`, this *is* the "you're signed in, you're over
your plan, upgrading raises it" moment, so `AIChatComponent` gives it a distinct treatment instead
of the flat error bubble every other kind gets:

- The assistant bubble carries the server's message verbatim (no `"Error: "` prefix, same as
  `TrialExhausted`/`ServiceCapacityExceeded`) plus an **"Upgrade to Pro"** button, opening
  `synth::branding::kUpgradeUrl` (`Source/Branding.h` — a static Polar checkout link; this feature does not
  create checkout sessions dynamically, per `docs/billing.md`'s "what this deliberately does not
  do") via the injected `urlOpener` (`AIChatComponent::setUrlOpenerForTesting()` swaps this for a
  non-browser-launching fake in tests).
- This button is carried on `MessageData::showUpgradeAction`, which is deliberately **not**
  reconstructed by the history-replay loop in `AIChatComponent`'s constructor — a `New Chat` or app
  restart drops it along with the rest of that turn's transient UI state (the same way
  Cancel-button/spinner state never survives a reload).
- A `Quota` error also triggers `AccountService::refreshEntitlement()` — a fire-and-forget re-fetch
  of `GET /v1/entitlement` that updates the account's cached plan/limit/usage without touching
  sign-in state, so a user who upgrades mid-session and immediately retries sees their new plan
  reflected (in `PlanBadge`, below) without restarting the app.

**`PlanBadge`** (`Source/UI/PlanBadge.h/.cpp`) is a small usage indicator placed in the same
bottom-chrome stack as `AccountRow` and the model picker, showing `"Free · 240 / 1000 this month"`
or `"Pro · 1,203 / 10,000 this month"`. It follows `AccountRow`'s exact zero-height-when-absent
contract (`setAccountService()`/`refresh()`/`getPreferredHeight()`) — invisible and contributing
nothing to layout until an `AccountService` is attached *and* has a known entitlement
(`AccountSnapshot::entitlementKnown`), which `AccountService::completeSignIn()` populates alongside
`fetchMe()` (same non-fatal contract: an entitlement-fetch failure never blocks sign-in) and
`refreshEntitlement()` updates on demand. `usage.requests_used` is a recent addition to the
`GET /v1/entitlement` response (`docs/billing.md`) — `AuthClient::fetchEntitlement()` degrades to
`requestsUsed = 0` rather than failing the whole parse if an older server doesn't send it.

**Reachable client-side, still blocked at the infra layer.** `RemoteProvider` is no
longer `hidden` (see "Now visible" above) and is the default provider for a fresh install,
so this feature — the 429→`Quota` mapping, `fetchEntitlement()`, `AccountService`'s entitlement
fields, `PlanBadge`, and the upgrade bubble — is reachable end to end from the client for the first
time. It still can't complete against production today: the deployed Cloud Run service has no
`allUsers` invoker binding, so every request 403s at the IAM layer before the app's own
`AUTH_REQUIRED` check ever runs — a deliberate follow-up, not a defect in this client (see
`synth::branding::kApiBaseUrl`'s comment in `Source/Branding.h`).

## 14. Future Considerations

-   **Direct Saving of AI Suggested Patches**: Implement functionality for users to directly save AI-generated patches as presets.
-   **Adding AI Suggested Patches Instead of Overriding**: Provide options to merge or add AI-suggested patch components without completely replacing the existing patch.
-   **Enhanced State Mapping**: More granular control and mapping of complex module interactions and parameters.
-   **Performance Optimization**: Improving the latency of AI responses and patch application.
-   **Advanced AI Prompting**: Techniques for more sophisticated prompt construction, including few-shot examples or more detailed system instructions.
-   **User Feedback Integration**: Allowing users to explicitly rate AI-generated patches to refine future suggestions.

---
