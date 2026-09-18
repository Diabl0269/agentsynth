# Feedback and Prompt Collection

Three separate things that all involve the user telling the team something, deliberately kept apart:
thumbs on one AI patch, free-text feedback about the app, and an opt-in to have hosted-mode prompts
reviewed.

## Patch feedback

`AIChatComponent::PatchCard` carries a "Good"/"Bad" pair next to the
[diff preview](patch-preview.md), plus an optional single-line comment revealed once a rating is
picked. Clicking either commits immediately — thumbs are meant to be zero-friction, not a form — and
reveals the comment field for anyone who wants to say why. Submitting a comment later, with Enter or
"Save", writes a second record rather than mutating the first, because the underlying store is an
append-only log, not a keyed table.

`Source/AI/PatchFeedbackStore` appends one JSON object per line to
`<user app data>/Agent Synth/patch_feedback.jsonl`:
`{timestamp, rating, comment?, conversationId?, messageId?, patch}`. `patch` is the parsed patch
JSON, falling back to a `patchRaw` string if it does not parse; `conversationId` and `messageId` are
present only when known.

**This local log is written unconditionally on every rating**, regardless of plan and regardless of
whether a server sync happens. Cloud-less conversations, offline sessions and free-tier accounts
stay local-only.

The rating lives on `MessageData::ratingState` and `ratingComment` for the session; the durable copy
is the JSONL log, not the in-memory chat history.

### Sync to the server

A rating additionally reaches
`POST /v1/conversations/:conversationId/messages/:messageId/feedback`
(`{"rating": "up"|"down", "comment"?: string}`, Bearer auth) for signed-in Pro users only, and only
when a server-side message id is available for the rated turn.

**Where the message id comes from.** A hosted response that a Pro account persisted server-side
returns an `x-message-id` header alongside `x-conversation-id`. It lands on
`AIProvider::AIResponse::messageId`, populated in `RemoteProvider::processRequest()` the same way
`conversationId` is — read via `result.headers`, empty when absent. Unlike `conversationId` it is
**not** re-pushed or threaded through `AIIntegrationService` state: it is per-turn, so it flows
through the response object to `AIChatComponent`'s callback unchanged, which stashes it onto that
turn's `MessageData::serverMessageId` at the same point `jsonPatch` is set. It is only ever
populated for a live, same-session assistant message and is **not** reconstructed by the
history-replay loop, the same session-scoped precedent as `ratingState` and `showUpgradeAction`, so
**rating a message from a restored conversation stays local-only**.

**The conversation id used for the sync is the SERVER one**,
`AIIntegrationService::getConversationId()`, not `AIChatComponent::currentLocalConversationId`,
which is the unrelated key `LocalHistoryStore` uses. Sending the wrong one fails the server's
ownership check indistinguishably from a nonexistent id, as a 404.

The rating callback, after writing the local record:

1. Reads `aiService.getConversationId()`.
2. Fires the sync if the rated message's `serverMessageId` is non-empty AND that conversation id is
   non-empty AND the attached `AccountService` reports signed-in AND Pro AND a non-empty access
   token. Otherwise it is a silent no-op; the local log already has the rating either way.
3. The sync itself is **fire-and-forget**: a detached background thread — the same shape as
   `CloudHistorySource`'s calls, copying a small stateless `AuthClient`, the token and plain
   strings, never `this` or any UI state — calls `AuthClient::submitMessageFeedback(...)`. No retry,
   no queueing, no UI error surface. A failed sync just means that one rating never reached the
   server; nothing blocks or spins on it.

`AuthClient::submitMessageFeedback(accessToken, conversationId, messageId, rating, comment,
cancelled)` mirrors `listConversations()`'s `{ok, transportError}` result-type convention. The JSON
body omits `comment` entirely when empty, the same convention `PatchFeedbackStore::record()` uses
for its own local `comment` field. Server responses this client interprets: 200 ok; 404 (wrong or
unowned conversation or message id, indistinguishable from nonexistent) and 400 (bad `rating`)
surface as `!ok` with a `transportError`; 403 (non-Pro) is unreachable from this client, since it
gates on Pro before ever calling, but the server re-checks it independently regardless — the same
"the client only decides what to show, never what to allow" boundary as every other Pro-gated
endpoint.

Test injection: `AIChatComponent::setFeedbackHttpPerformerForTesting(HttpPerformer)` installs a fake
transport for the rating callback's locally-constructed `AuthClient`, mirroring
`setHistorySourcesForTesting()`'s fake-backend idiom but at the `HttpPerformer` layer, because this
call does not go through `ConversationHistorySource` at all.

Locked by `Tests/Account/AuthClient/AuthClientFeedbackTests.cpp` (`SubmitMessageFeedback*` — request
shape, comment omission, 404/403/400/transport-failure mapping);
`Tests/AI/RemoteProvider/RemoteProviderResponseHandlingTests.cpp`
(`MessageIdHeaderCapturedFromResponseIntoAIResponse`, and
`MissingConversationIdHeaderLeavesAIResponseFieldEmpty` extended to assert `messageId` too);
`Tests/AI/PatchFeedbackStoreTests.cpp` (`IncludesConversationAndMessageIdWhenProvided`,
`OmitsConversationAndMessageIdWhenNotProvided`); and `Tests/AIChatComponentTests.cpp`
(`RatingWithServerMessageIdAndProAccountFiresExactlyOneFeedbackPost`,
`RatingOnFreePlanAccountDoesNotFireFeedbackPost`,
`RatingWithNoServerMessageIdDoesNotFireFeedbackPostEvenWhenPro`).

## General feedback

The Settings dialog's "Feedback" tab (`Source/UI/FeedbackSettingsTab`, the last tab, after
Appearance) is the general-purpose sibling of the patch-specific thumbs: free-text bug reports,
feature requests, or comments not tied to any one AI-generated patch.

`Source/GeneralFeedbackStore` appends one JSON object per line — `{timestamp, category, text}` — to
`<user app data>/Agent Synth/general_feedback.jsonl`, the same append-only JSON-Lines shape and
rationale as `PatchFeedbackStore`, but in its own file since the two logs track unrelated things.

Each submission is also synced to the server, fire-and-forget, with one deliberate difference from
patch feedback: **this sync is not Pro-gated.** `POST /v1/feedback` has no plan check server-side
either, so any account may submit general feedback. It fires whenever `FeedbackSettingsTab` has an
`accountService` attached at all, whether or not the user is signed in:
`AuthClient::submitGeneralFeedback` sets `Authorization: Bearer <accessToken>` only when
`accessToken` is non-empty, and always sets `X-Device-Id` from the `AuthClient`'s own stable
[per-install device id](accounts.md#device-id-and-anonymous-trial) when non-empty, so a signed-out
submission is still attributable server-side via that anonymous id rather than being dropped
client-side. The local log is written unconditionally regardless of sign-in state or sync outcome.

## Opt-in prompt collection

A single settings checkbox — "Help improve AgentSynth — share my hosted-mode prompts for product
learning", in `AISettingsTab` next to the provider picker's hosted-mode disclosure — lets a
signed-in user opt in to the team reviewing their hosted-mode prompt and the resulting patch. Off by
default.

**This is human review only, never model training or fine-tuning.** The privacy policy's "we do not
use your prompts or patches to train AI models" promise stays intact. Toggling off purges any
already-collected samples for that user server-side immediately; the client has nothing further to
do on revoke.

The client half is a thin state-sync layer mirroring the entitlement fetch:
`AccountSnapshot::promptLearningOptIn` and `promptLearningOptInAt` are populated the same way `plan`
and `monthlyRequestLimit` are, by `AccountService::refreshPromptLearningOptIn()` (fire-and-forget,
`GET /v1/prompt-learning`) and `setPromptLearningOptIn(bool)` (fire-and-forget,
`PUT /v1/prompt-learning` with `{"opted_in": ...}`). Both go through `AuthClient`'s existing
Bearer-token layer, both no-op when signed out, and both merge their result onto the currently
published snapshot rather than replacing it, dropping the result if a sign-out raced the network
call — the same guard `refreshEntitlement()` has. `AccountService::PendingJob` carries a `boolArg`
field used only by `setPromptLearningOptIn`, to carry the new value alongside the access token
already occupying `arg`.

`promptLearningOptIn` defaults to `false`, matching the server's default, so a default-constructed
or not-yet-fetched snapshot is indistinguishable from "known opted out" — safe here because a
checkbox reading it before the first fetch shows unchecked, never a stale opted-in.

`AISettingsTab` reflects the checkbox's enabled and checked state from `AccountService`'s published
snapshot: disabled with a "Sign in required" tooltip when signed out, the same gating precedent as
`AccountRow` and `PlanBadge` reading `AccountService::getSnapshot().state`, and kept live while the
Settings dialog is open by chaining onto `AccountService::onStateChanged`.

That callback is a **single `std::function` slot**, not a multicast delegate, and `AIChatComponent`
installs it once for the app's lifetime (see
[single owner per callback slot](accounts.md#single-owner-per-callback-slot)). `AISettingsTab`
therefore captures whatever was already installed, wraps it with its own refresh, and **restores the
original callback verbatim in its own destructor** rather than overwriting the slot outright, which
would silently stop `AIChatComponent`'s `accountRow` and `planBadge` from refreshing for as long as
the Settings dialog stayed open. This is safe only because nothing else touches `onStateChanged`
while a `SettingsWindow` is open.

**Three things that all touch "prompts", easy to conflate:**

- **This opt-in** — the product team reviewing opted-in hosted-mode samples to improve the product.
  Available regardless of plan: consent is the gate, not plan tier. Stored separately from
  conversation rows. Toggling it off purges this feature's samples only.
- **[Conversation history](history.md)** — a Pro-only, user-facing convenience letting a subscriber
  list, resume and delete their *own* past exchanges, stored so *they* can come back to them.
  Unaffected by this opt-in, in either direction.
- **Failure-debug retention** — not an app-owned table at all, but the hosting platform's
  unconditional bucket retention on the backend's error logs, which only ever contain
  `{err, capability, code}`, never a prompt body. It applies to every request regardless of this
  opt-in and has no client-side surface.

Tests: `Tests/Account/AuthClient/AuthClientAccountTests.cpp`
(`fetchPromptLearningPreference`/`setPromptLearningPreference` — method, URL, headers, body, the
off-by-default null-timestamp shape, unauthorized, transport failure).
`Tests/Account/AccountServiceTests.cpp` (`setPromptLearningOptIn`/`refreshPromptLearningOptIn` go
through the authenticated job and token path and update the snapshot; both are a no-op when signed
out, asserted by a zero-calls check on the fake server, mirroring
`RefreshEntitlementIsNoOpWhenSignedOut`). `Tests/UI/Settings/SettingsWindowTests.cpp` (checkbox
disabled and unchecked with no `AccountService` or when signed out, with the "Sign in required"
tooltip; enabled and reflecting the server's opted-in value once signed in; toggling it calls into
`AccountService::setPromptLearningOptIn()`).
