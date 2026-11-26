# Conversation History

Local-first, cloud-as-sync: **every** session writes its conversation to a local file regardless of
plan, and a Pro session *additionally* syncs to the cloud. This gives Pro users offline resilience
for free and keeps `AIChatComponent` on one code path instead of branching storage logic throughout
it.

## Server-side conversation history

Server-side conversation history is Pro-plan only, and **that is resolved server-side from the
account's entitlement, never trusted from a client header.** `RemoteProvider`'s `patch.generate`
calls carry an `x-conversation-id` request header when the client has one from a prior response in
the same session, and the server responds with one — new or the same — only when it actually
persisted the exchange. A free-plan response carries **no** header at all, not an empty one.

The re-push shape mirrors
[the auth token re-push contract](chat-component.md#auth-token-re-push-contract) almost exactly:
`AIIntegrationService::setConversationId(id)` stores the value in `currentConversationId` regardless
of whether a provider is installed, and `setProvider(...)` re-pushes it to whatever provider it
installs next. `AIProvider::setConversationId()` defaults to a no-op, so `OllamaProvider` and any
local or test provider are unaffected automatically.

The one-way difference from the auth token: nothing external calls `setConversationId()` with a
*real* id under normal operation. `AIIntegrationService::sendMessage()`'s success callback — the
same branch that appends the assistant turn to `chatHistory` — captures a non-empty
`AIResponse::conversationId` and calls `setConversationId()` itself, so the **next** call in the
session continues the same server-side thread automatically. `RemoteProvider::processRequest()`
reads the response's `x-conversation-id` header (the same `result.headers` lookup used for
`Retry-After`) onto the `AIResponse` it delivers, and sends the stored id as a request header only
when non-empty. It has no notion of plans and never decides on its own whether to send one.

`AIChatComponent` is the one plan-aware call site (`Source/AI/AccountService.h`'s free function
`isProPlan(const AccountSnapshot&)`, also used by `PlanBadge`): right before every
`aiService.sendMessage(...)` call in `sendButtonClicked()` it clears the conversation id
(`aiService.setConversationId({})`) whenever the attached `AccountService` is absent or its snapshot
is not Pro. **Why, given the server already enforces it:** this is defence in depth covering a
Pro-to-Free downgrade mid-session, where an id captured earlier would otherwise still be sitting in
`AIIntegrationService` and get resent for a now-free account for no reason. A brand-new free-plan
session never has an id to clear, since the server never sent one.

Locked by `RemoteProviderTest.ConversationIdHeaderSentWhenSet` and
`ConversationIdHeaderOnlySentWhenSet` in
`Tests/AI/RemoteProvider/RemoteProviderRequestShapeTests.cpp`;
`ConversationIdHeaderCapturedFromResponseIntoAIResponse` and
`MissingConversationIdHeaderLeavesAIResponseFieldEmpty` in
`Tests/AI/RemoteProvider/RemoteProviderResponseHandlingTests.cpp`; and
`AIIntegrationServiceTest.ConversationIdCapturedFromResponseAndRePushedToProvider`,
`EmptyConversationIdOnResponseDoesNotCallSetConversationId` and
`SetConversationIdBeforeProviderInstalledIsRePushedBySetProvider` in
`Tests/AI/AIIntegrationService/AIIntegrationServiceProviderConfigTests.cpp`.

`Source/AI/AuthClient.h/.cpp` exposes the cloud-only conversation methods alongside
`fetchEntitlement()`: `listConversations()`, `getConversation(id)`, `deleteConversation(id)` and
`deleteAllConversations()`.

**`listConversations()` is not a side-effect-free read.** The server's `GET /v1/conversations`
lazily sets or clears a grace-period deletion date on every call
(`ListConversationsResult::deletionScheduledAt`, empty when null), so it must never be polled or
called speculatively — only in response to an explicit user action.

## Local history

`Source/AI/LocalHistoryStore.h/.cpp` writes one JSON file per conversation, following
`SnippetManager`'s exact convention: static methods over an explicit directory, a
`getDefaultHistoryDirectory()` for production callers, and pure filesystem-free JSON transforms.

- Location: `<userApplicationDataDirectory>/<kSettingsFolderName>/History/<id>.json`.
- Shape: `{id, title, createdAt, updatedAt, messages: [{role, content, createdAt}]}` — the same
  field names as `AuthClient::ConversationDetailResult`/`ConversationMessage`, so a UI reading either
  backend never has to translate field names. `content` carries any fenced ` ```json ` block
  **unsplit**, exactly like a stored `AIProvider::Message`.
  `AIChatComponent::replayMessagesFrom()` holds the extract-and-clean logic, shared by the
  constructor path and by restoring a history-panel entry, so the two can never drift.
- **No file locking**, the same as `SnippetManager`, `ThemeManager` and `DeviceIdStore`. The
  multi-instance concurrent-write hazard is avoided by construction: each app or plugin-instance
  *session* mints its own conversation id lazily, on its first successful exchange
  (`AIChatComponent::saveCurrentConversationLocally()`), so no two writers ever target the same
  file. A history-panel list scanning all files can very rarely race a torn write from another live
  instance — accepted, the same as for the classes above.

### Retention

Retention is **user-configurable and local only**: a day count (30, 90, 180 or 365) or "keep
forever" (`LocalHistoryStore::kRetainForever`), read from `juce::ApplicationProperties`'s
`"historyRetentionDays"` key and exposed in Settings → AI. `save()` prunes by `updatedAt` on every
write, and an out-of-range persisted value — a hand-edited `0` — falls back to
`kDefaultRetentionDays` (180) rather than pruning everything. Independent of that setting, a hard
cap of `kHardCapFiles` (2000 files) always applies as an engineering backstop.

**Cloud retention is not user-configurable and is not per-tier**: it is the server's single global
default. Only the local copy has a user-facing retention control.

## The history panel

`Source/AI/ConversationHistorySource.h` is the backend-agnostic interface behind the panel:
`list()`, `get(id)` and `deleteAll()`, all callback-based, never blocking the message thread.

- `LocalHistorySource` wraps `LocalHistoryStore` and answers synchronously, because file I/O is fast
  enough not to need offloading.
- `CloudHistorySource` wraps the `AuthClient` conversation methods. Each call launches a
  **detached** worker thread that copies everything it needs — a copy of the small, stateless
  `AuthClient`, the access token, and a heap-allocated cancellation flag — before returning, so the
  `ConversationHistorySource` object itself never needs to outlive the call. This mirrors
  `AIProvider::sendPrompt()`'s contract that a completion callback may arrive on a background
  thread, leaving the caller responsible for hopping back to the message thread with
  `Component::SafePointer` plus `MessageManager::callAsync`, exactly as
  `AIChatComponent::sendButtonClicked()` already does.

**Backend selection** (`AIChatComponent::historyButtonClicked()`) is plan-driven, but the cloud call
itself is **signed-in**-driven, not plan-driven. That asymmetry is deliberate: `listConversations()`
is the *only* source of a pending grace-period deletion date, and that date only ever matters for a
signed-in account that has lapsed off Pro. So whenever signed in, a cloud `listConversations()` call
always fires, learning `deletionScheduledAt` either way; when the plan is Pro its result is *also*
the list rendered; when the plan is free or lapsed, the list rendered comes from `LocalHistoryStore`
instead. Not signed in, or no `AccountService` attached, skips the cloud call entirely. This call
happens **only** on an explicit History-button click, never speculatively and never on a timer,
because of the writes-on-read caveat above.

### Restoring

`AIChatComponent::restoreConversation()` replays an entry's messages via `replayMessagesFrom()` and
re-seeds `aiService`'s own `chatHistory` with the same role/content pairs
(`AIIntegrationService::setHistory()`), so a follow-up such as "make that softer" is answered in
context. Stored content goes back verbatim (an assistant answer keeps its fenced json block
unsplit). `setHistory()` keeps the system prompt, ignores stored system turns and trims to the same
`kMaxHistoryTurns` cap as live turns, so only the most recent pairs of a long conversation reach the
model.

The restored id is adopted as this session's `currentLocalConversationId`, so subsequent local
saves — and, for a Pro cloud restore, `aiService.setConversationId(id)`-continued cloud saves — keep
appending to that same conversation rather than starting a new one.

### Chrome

`Source/UI/Assistant/AIChatComponent/AIChatComponent.h` and `AIChatComponentHistory.cpp`:

- A **History** button in the top toolbar, next to New Chat, opens one compact list popover
  (`ChatHistoryPicker.h/.cpp`, launched through `launchPopover` with the button as its trigger, so it
  rises out of the button, Esc and a second press close it). The list is the shared `ModMatrixPicker`:
  a search field, one row per conversation (title, with the readable date as its second line), capped
  at about 360 px and scrolling, and a "Clear my history" strip under it that asks to confirm. **Only
  the rows in view exist as components**, so a history of a thousand chats opens as fast as one of
  three. Up/Down move, Return or a click opens that one chat and closes the list, and an empty history
  shows a "No saved conversations" line. A test reads the list through the helpers in
  `ChatHistoryPicker.h` (`AIChatComponent::historyListForTesting()`).
- **Listing is cheap.** `LocalHistoryStore::list()` reads only the first 8 KB of each file, where the
  header fields sit ahead of the messages (`summaryFromHead()`), and parses the whole file only when
  that cannot answer (a very long title, an unusual key order). A file cut off after its header lists
  a row that `get()` then reports as unreadable.
- **A restored chat builds only its newest bubbles.** `replayMessagesFrom()` keeps every turn in
  `messages`, in the export and in the model's history, but `updateChatDisplay()` builds bubbles from
  `firstBuiltMessage_` on: the newest `kTailBubbles` (40) after a restore. Scrolling the message list
  to the top builds the next `kOlderChunk` (40) older bubbles above and keeps the view on the message
  it was on (`loadOlderMessages()`). Code that needs a bubble goes through `bubbleForMessage(i)`,
  never `messageList.getChildComponent(i)`, because child 0 is no longer message 0.
- **Upsell strip** (`upsellButton`): a single "Upgrade to Pro" button, using the same `urlOpener` and
  `kUpgradeUrl` mechanism as the quota-error bubble's button but as a persistent strip rather than a
  per-message one. Shown whenever `!isProPlan(snapshot)`, **including with no `AccountService`
  attached at all**. **Why that diverges** from `accountRow`/`planBadge`/`hostedModeNotice`'s
  invisible-until-a-service-says-otherwise convention: those default to invisible because they have
  nothing true to say yet, whereas "not Pro" is already true before any `AccountService` exists —
  every caller starts on the free tier, signed out. The explanatory copy ("Your history is saved
  locally only — subscribers get automatic cloud backup across devices.") lives on `historyButton`'s
  tooltip rather than its own label, so it does not compete for space in the bottom-chrome stack.
  See `updateUpsellStrip()`.
- **Downgrade notice** (`downgradeStripLabel`): "Your subscription has lapsed — your saved history
  will be deleted on {date}." Shown only once signed in, `!isProPlan(snapshot)`, and a
  `deletionScheduledAt` is known from the last History click. Never polled.
- Both strips follow `hostedModeNotice`'s exact construction and visibility pattern:
  `addChildComponent` plus `set*Visible()` plus `resized()` reserving height only when visible.

Tests: `Tests/AI/LocalHistoryStoreTests.cpp` (save/list/get/delete round trips; pure JSON transforms;
age-based pruning at each offered retention value plus "forever"; the hard cap; out-of-range
retention falling back to the default; an unparseable `updatedAt` being kept rather than treated as
infinitely old). `Tests/UI/Assistant/AIChatComponent/AIChatComponentLargeHistoryTests.cpp` (a thousand chats build only the rows in view, Return and a click open one chat, a 500-message restore builds only the newest bubbles and scrolling up builds older ones). `Tests/AIChatComponentTests.cpp` (upsell and downgrade strip visibility across
signed-out, free, pro and lapsed-with-date snapshots; history panel backend selection per plan via
`setHistorySourcesForTesting()`; "Clear my history" wired to the plan-appropriate backend; restoring
a conversation replaying its messages; every successful exchange saved locally regardless of plan).
`Tests/UI/Settings/SettingsWindowTests.cpp` (the retention control's default, persisted-value load,
round trip and out-of-range fallback). `Tests/App/BrandingTests.cpp` (`resolveApiBaseUrl()`'s
Debug-only `AGENTSYNTH_LOCAL_API_URL` override, which points a local build's auth, entitlement and
cloud-history traffic at a locally run backend instance — see
[testing cloud-gated features locally](../development/local-cloud-dev.md)).
