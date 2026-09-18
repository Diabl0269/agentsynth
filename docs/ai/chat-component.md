# Chat Component

`AIChatComponent` (`Source/UI/Assistant/AIChatComponent/`) is the chat UI for AI-assisted patching.
It wires user prompts to `AIIntegrationService` and displays the conversation history with optional
JSON patch previews.

The class is split by concern across that directory:

| Unit | Concern |
|------|---------|
| `AIChatComponent.cpp` | Construction and destruction, the cancel/timeout watchdog, painting |
| `AIChatComponentMessageList.cpp` | Message bubbles, patch and timeline cards, the `resized()` layout loop |
| `AIChatComponentSending.cpp` | Sending and streaming a request, attaching its patch-diff preview |
| `AIChatComponentProvider.cpp` | Model and provider selection |
| `AIChatComponentHistory.cpp` | Local and cloud conversation history, the upsell and downgrade strips |

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
height, which is what lets a long wrapped line — a "Preview unavailable" status, a long
`hostedModeNotice` or `downgradeStripLabel` — get clipped.
`PatchCard::getRequiredHeight(width)` and `TimelineCard::getRequiredHeight(width)` take the render
width as a parameter for the same reason: the height calculation and the actual render width must
always agree.

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
[the hosted-mode privacy notice](providers.md#hosted-mode-disclosure) and for the Patch/Arrange
selector's gate.

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
