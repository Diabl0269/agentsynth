# Chat Component

`AIChatComponent` (`Source/UI/Assistant/AIChatComponent/`) is the chat UI for AI-assisted patching.
It wires user prompts to `AIIntegrationService` and displays the conversation, where an answer that
changes the project carries one edit-plan card.

The class is split by concern across that directory:

| Unit | Concern |
|------|---------|
| `AIChatComponent.cpp` | Construction and destruction, the cancel/timeout watchdog, painting |
| `AIChatComponentMessageList.cpp` | Message bubbles, the `resized()` layout loop |
| `AIChatComponentEditPlanCard.h/.cpp` | The edit-plan card and what its Apply does |
| `AIChatComponentSending.cpp` | Routing a request, reading its answer, previewing the plan once |
| `AIChatComponentProvider.cpp` | Model and provider selection |
| `AIChatComponentHistory.cpp` | Local and cloud conversation history, the upsell and downgrade strips |

## One answer, one card

There is one input and no mode selector. `sendButtonClicked()` routes by what the message asks for:

- With a timeline wired in (`AIIntegrationService::hasTimelineContext()`), an edit request goes
  through `sendProjectMessage()`: hosted `project.generate`, or the local model with the combined
  schema (`getPatchSchemaWithTimelineOps()`). An edit request is what `shouldUseStructuredOutput()`
  classifies as one (a module name, or an edit or arrangement word such as add, track, automate,
  modulate, notes) and, on a hosted provider, every message, since the hosted service has no
  free-text chat.
- A local conversational question goes through `sendMessage(text, cb, false)` and gets text back.
- With no timeline (tests, a host without one) the request is the patch request it always was:
  `sendMessage(text, cb, useStructuredOutput)`.

Every route shares the waiting state: the spinner, Cancel, the "AI is thinking..." line and the
timeout watchdog start before the request goes out. A hosted plan takes 20 to 30 seconds (27 s
measured on gpt-oss-20b), well inside the 4-minute default.

The answer is read once (`handleResponse()` -> `extractEditPlan()`): a fenced ```json block, or the
whole response when the request was structured. A JSON object is a plan only when it carries a patch
key (`nodes`, `connections`, `remove`, `modulations`, `removeModulations`) or `timelineOps`; anything
else, including JSON that is not a plan, is shown as text. A plan is kept as `MessageData::planJson`
and previewed ONCE by `attachPlanPreview()` through `previewProjectEdit()`, which caches the card's
lines (`ProjectEditResult::previewLines`: the patch phase, then the timeline ops) and the per-change
details. History restore reads saved turns the same way, so an old patch-only answer comes back as
the same card (the engine treats it as a plan with no ops).

`EditPlanCard` (`AIChatComponentEditPlanCard.h/.cpp`), from the top:

- the title **Edit plan** (`accent2`; `warning` when refused);
- the preview, one line per phase, measured with `computeWrappedTextHeight()` so it never clips;
- a row with **Show details** on the left and **Apply** on the right. Apply calls
  `applyEditPlan()` -> `applyProjectEdit()` once: the whole plan as one undo step. A failure is
  reported as an assistant bubble, never swallowed, and there is no retry loop (the plan was checked
  against the live project when it arrived). A refused plan's card reads "This plan was rejected and
  was not applied: <reason>" and has no Apply;
- the thumbs rating, with the comment row once a rating is picked (recorded with the plan's JSON in
  `PatchFeedbackStore`, as the patch card's was);
- the details panel when open: the per-change list (`computeDiff` grouped for a merge,
  `summarizePatch` for a replace) and the plan's JSON.

The card has no entrance animation, like the patch and timeline cards it replaces:
`updateChatDisplay()` rebuilds every bubble on each redraw, so a tween would replay on every
rating or apply.

Tests: `Tests/UI/Assistant/AIChatComponent/AIChatComponentEditPlanTests.cpp`.

## Response timing marker

Assistant bubbles that end an in-flight wait — a successful reply, a provider error, a cancel, or
the request timeout — show a compact elapsed-time label right-aligned on the same role row as
`"AI"` (`340ms`, `1.2s`, `1m 5s`). The value is wall-clock milliseconds from send until the wait
ends, stored on `MessageData::responseMs`. History-restored turns and patch-retry or apply-failure
bubbles leave `responseMs` at `-1` and omit the marker. Format helper:
`AIChatComponent::formatResponseTime`.

While a request is in flight, the `"AI is thinking..."` status line shows the same formatted elapsed
time and refreshes on a 500 ms `juce::Timer` tick — label text only, not a full chat redraw. That
timer also enforces the request timeout.

## Request timeout

The default request timeout is **4 minutes** (240000 ms), user-configurable via Settings → AI →
Request Timeout, with presets of 2, 4 (default), 6 and 10 minutes. The persisted key is
`aiRequestTimeoutMs` in milliseconds (`juce::ApplicationProperties`).

**ONE value drives both halves of the timeout**: `AIChatComponent`'s in-flight-request watchdog (the
500 ms timer above, comparing elapsed time against `AIChatComponent::requestTimeoutMs`) and the
active provider's own HTTP connection timeout (`AIProvider::setRequestTimeoutMs()`).
`AIIntegrationService` holds the last-configured value and re-applies it to any newly installed
provider inside `setProvider()` — the same "must survive a provider swap" contract as
`refreshModels()` below — so switching providers can never silently reset the timeout to that
provider's own hardcoded default.

**Why one value.** Two independent constants drift: a UI watchdog shorter than the provider timeout
cancels every request before a local model on modest hardware can legitimately finish, and always
reports it as a timeout, which is misleading about where the limit came from.

## Bubble sizing and wrapped-height measurement

Each `MessageBubble` caps at `kBubbleWidthFraction` (0.8) of the message list width, with the
remaining gutter left on the side opposite the sender: user bubbles hug the right edge, assistant
bubbles the left (`MessageBubble::isUserRole()`, read by the layout loop). Bubble fill, border, role
and timestamp colours resolve through theme tokens (`accent`, `surfaceHi`, `border`, `textMuted`)
via `dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())`, never raw `juce::Colours`.

`AIChatComponent::computeWrappedTextHeight(font, text, width)` is the **required pattern** for any
chat-panel element whose text length varies at runtime. It measures the actual wrapped height via
`juce::GlyphArrangement` rather than estimating from a fixed line count or a fixed single-line
height, which is what lets a long wrapped line — a refused plan's reason, a long
`hostedModeNotice` or `downgradeStripLabel` — get clipped.
`EditPlanCard::getRequiredHeight(width)` takes the render width as a parameter for the same reason:
the height calculation and the actual render width must always agree.

## Debug logger registration

In **Debug builds only**, `AIChatComponent` registers itself as the global `juce::Logger` by calling
`juce::Logger::setCurrentLogger(this)` inside the `#else` branch of an `#ifdef NDEBUG` guard in the
constructor. The debug console (`TextEditor`) and the "Debug" toggle button are created and wired
there too. The destructor unregisters under `#ifndef NDEBUG`:

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

So `juce::Logger::writeToLog(...)` output is piped into the in-UI `TextEditor` debug console **only
in Debug builds**. In Release builds no logger registration occurs.

## Logging rules

Appends to the debug console are coalesced and the console is length-bounded. However:

> **Do NOT add high-frequency `writeToLog` calls** — per-parameter, per-sample, per-frame,
> per-connection. They run on the UI thread. A per-parameter log on preset load causes a
> multi-second UI freeze; this is guarded by `AIStateMapperTest.PresetLoadDoesNotSpamLogger`. Keep
> logging to errors and rare events only.

## Screen reader

The message list is a `ChatMessageViewport` (a `juce::Viewport`): a Tab stop with the accent focus
ring that scrolls with the arrow, Page and Home/End keys, exposed as a **list** titled "Chat messages".
Each `MessageBubble` is a **list item** whose title is the whole message, "You: ..." or "Assistant: ..."
with a note when it carries an edit plan that can be applied (`ChatMessageAccessibilityText.h`);
the label inside it is hidden from the screen reader so the text is not read twice, while a bubble's
card and buttons stay separate controls. The edit-plan card is a **group** titled "Edit plan"; its
preview label carries the text as its description. Its Apply button is a Tab stop with the accent
focus ring, named "Apply edit plan" with the tooltip "Apply this answer to the project (one undo
step)"; Show details, the thumbs and the comment field are named and tipped too.

Tab order is the input, Send (or Cancel, in the same slot), the message list, then the cards inside
it in message order (`setExplicitFocusOrder` 1 to 4 on the input row and the list; the rest of the
chrome follows by position). The input box, Send, Cancel, New Chat, History and the model picker all
carry a screen-reader name and a tooltip, checked by the `AIChat` surface of the accessibility
coverage test ([`docs/development/accessibility.md`](../development/accessibility.md)).

## Panel visibility persistence

The AI panel's visibility persists via the `ApplicationProperties` key `"aiPanelVisible"`, default
`false`. It is read in `MainComponent::initialiseCommon()`:

```cpp
isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);
```

Changes are written back to the same key when the panel is toggled.

## Model discovery ordering contract

`AIChatComponent`'s constructor calls `refreshModels()`, which calls
`AIIntegrationService::fetchAvailableModels()`. **If the service has no provider installed at that
point, discovery short-circuits** — `fetchAvailableModels` immediately invokes the callback with
`({}, false)` when `provider == nullptr` — and no model is ever selected;
`AIProvider::currentModel` stays empty for the rest of the session.

This matters because `MainComponent` declares `aiChatComponent` as a member constructed in the
member-initialiser list, i.e. **before** the constructor body runs, while `aiService.setProvider(...)`
only happens later inside `MainComponent::initialiseCommon()`. The chat component's own ctor-time
`refreshModels()` call is therefore guaranteed to run with no provider installed and is a no-op: it
issues no `/api/tags` request at all.

**Any owner that constructs `AIChatComponent` before installing a provider on the
`AIIntegrationService` it was given MUST call `chatComponent.refreshModels()` again AFTER
`aiService.setProvider(...)`.** `MainComponent::initialiseCommon()` does this immediately after
constructing the provider — either the one injected by a caller or test, or the one built via
`synth::AIProviderRegistry` from the persisted provider id. Skipping this step means `currentModel`
stays empty and every subsequent `/api/chat` request is sent with `"model": ""`, which Ollama
rejects with HTTP 400 `"model is required"`. Locked by
`MainComponentTest.AiProviderGetsModelSelectedOnStartup` and
`AIChatComponentTest.RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction`.

Because `refreshModels()` runs more than once by design — constructor, post-`setProvider()`, and
again whenever `SettingsWindow` triggers a re-fetch after a host or provider change — it must call
`modelPicker.clear()` before re-adding the `"Loading models..."` placeholder at item ID 1. Skipping
the clear lets that `addItem(..., 1)` collide with an ID already in the box (a model from a prior
successful fetch, or the placeholder itself): `juce::ComboBox::addItem()` asserts on duplicate IDs,
and the picker is left showing stale and fresh entries together for the duration of the new fetch.
Locked by `AIChatComponentTest.RefreshModelsClearsStaleItemsBeforeSecondFetchResolves`.

`refreshModels()` is also the resync point for
[the hosted-mode privacy notice](providers.md#hosted-mode-disclosure).

## Auth token re-push contract

`AIIntegrationService::setAuthToken()` is a second instance of the same ordering hazard, for the
same underlying reason: `AccountService` — and the `AIChatComponent`/`AccountRow` wiring that
observes it — can exist and fire callbacks before `MainComponent::initialiseCommon()` has installed
a real `AIProvider` on the service.

`setAuthToken(token)` stores the value in `currentAuthToken` **regardless of whether a provider is
currently installed**, and forwards it to `provider->setAuthToken(...)` only if one exists.
`setProvider(...)` then re-pushes `currentAuthToken`, when non-empty, onto whatever provider it just
installed, so a token set first is never lost. `AIProvider::setAuthToken()` defaults to a no-op, so
calling it on any provider — including `OllamaProvider`, which has no notion of auth — is always
safe.

Locked by `AIIntegrationServiceTest.SetAuthTokenForwardsToInstalledProvider` and
`AIIntegrationServiceTest.SetAuthTokenBeforeProviderInstalledIsRePushedBySetProvider` in
`Tests/AI/AIIntegrationService/AIIntegrationServiceProviderConfigTests.cpp`.
