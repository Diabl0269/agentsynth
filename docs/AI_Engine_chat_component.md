# AI Engine: Chat Component

`AIChatComponent` and its logging rules (§11 below, numbering kept as in the parent doc). The AI
Engine's architecture, communication pattern, and patch-diff/feedback UI live in
[`AI_Engine.md`](AI_Engine.md) (§1-4); patch validity, few-shot examples, the untrusted-timeline
data model, arrangement context, timeline operations, and the agentic timeline security model
live in [`AI_Engine_patch_safety.md`](AI_Engine_patch_safety.md) (§5-10). Providers, accounts,
conversation history, and quota mechanics live in
[`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md).

---

## 11. AIChatComponent and Logging

`AIChatComponent` (`Source/UI/Assistant/AIChatComponent/AIChatComponent.cpp`) is the chat UI for AI-assisted patching. It wires user prompts to `AIIntegrationService` and displays the conversation history with optional JSON patch previews.

### Response timing marker

Assistant bubbles that end an in-flight wait (successful reply, provider error, cancel, or the
request timeout) show a compact elapsed-time label right-aligned on the same role row as `"AI"`
(e.g. `340ms`, `1.2s`, `1m 5s`). The value is wall-clock ms from send until the wait ends, stored
on `MessageData::responseMs`. History-restored turns and patch-retry / apply-failure bubbles leave
`responseMs` at `-1` and omit the marker. Format helper: `AIChatComponent::formatResponseTime`.

While a request is in flight, the `"AI is thinking..."` status line shows the same formatted elapsed
time and refreshes on a 500 ms `juce::Timer` tick (label text only — not a full chat redraw). That
timer also enforces the request timeout, described next.

### Request timeout

The default request timeout is **4 minutes** (240000 ms), user-configurable via Settings → AI →
Request Timeout, with presets of 2, 4 (default), 6, and 10 minutes. The persisted key is
`aiRequestTimeoutMs` (milliseconds, `juce::ApplicationProperties`). Crucially, ONE value now drives
both halves of the timeout: `AIChatComponent`'s in-flight-request watchdog (the 500 ms timer above,
comparing elapsed time against `AIChatComponent::requestTimeoutMs`) and the active `AIProvider`'s
own HTTP connection timeout (`OllamaProvider`/`RemoteProvider::requestTimeoutMs`, pushed via
`AIProvider::setRequestTimeoutMs()`). `AIIntegrationService` holds the last-configured value and
re-applies it to any newly installed provider inside `setProvider()` — the same "must survive a
provider swap" contract as `refreshModels()` (see the Model Discovery Ordering Contract below) —
so switching providers can never silently reset the timeout to that provider's own hardcoded
default. Previously these were two independent, hardcoded constants (a 120 s UI watchdog and a
240 s provider timeout) that had drifted apart: the UI cancelled every request at 120 s, well before
a local Ollama model on modest hardware could legitimately finish, always producing a misleading
"timed out" error. They are now the same configurable value everywhere.

### Bubble sizing and wrapped-height measurement

Each `MessageBubble` caps at `kBubbleWidthFraction` (0.8) of the message list width, with the
remaining gutter left on the side opposite the sender — user bubbles hug the right edge, assistant
bubbles the left (`MessageBubble::isUserRole()`, read by `AIChatComponent`'s layout loop). Bubble
fill/border/role/timestamp colours resolve through theme tokens (`accent`/`surfaceHi`/`border`/
`textMuted`) via `dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())`, never raw `juce::Colours`.

`AIChatComponent::computeWrappedTextHeight(font, text, width)` is the **required pattern** for any
chat-panel element whose text length varies at runtime — it measures the actual wrapped height via
`juce::GlyphArrangement` rather than estimating from a fixed line count or a fixed single-line
height, which is what let a long wrapped line (e.g. a "Preview unavailable…" status, or a long
`hostedModeNotice`/`downgradeStripLabel`) get clipped. `PatchCard`/`TimelineCard::getRequiredHeight(width)`
take the render width as a parameter for the same reason: the height calculation and the actual
render width must always agree.

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

Because `refreshModels()` runs more than once by design (ctor, post-`setProvider()`, and
again whenever `SettingsWindow` triggers a re-fetch after a host/provider change), it must
`modelPicker.clear()` before re-adding the `"Loading models..."` placeholder at item ID 1.
Skipping the clear lets that `addItem(..., 1)` collide with an ID already in the box (a
model from a prior successful fetch, or the placeholder itself) — `juce::ComboBox::addItem()`
jasserts on duplicate IDs, and the picker is left showing stale and fresh entries together
for the duration of the new fetch. Locked by
`AIChatComponentTest.RefreshModelsClearsStaleItemsBeforeSecondFetchResolves`.

### Auth Token Re-Push Contract

`AIIntegrationService::setAuthToken()` is a second instance of the same ordering hazard as the
Model Discovery Ordering Contract above, for the same underlying reason: `AccountService` (and
the `AIChatComponent`/`AccountRow` wiring that observes it) can exist and fire callbacks before
`MainComponent::initialiseCommon()` has installed a real `AIProvider` on the service.

`setAuthToken(token)` stores the value in `currentAuthToken` **regardless of whether a provider
is currently installed**, and forwards it to `provider->setAuthToken(...)` only if one exists.
`setProvider(...)` then re-pushes `currentAuthToken` (when non-empty) onto whatever provider it
just installed, so a token set first is never lost. `AIProvider::setAuthToken()` defaults to a
no-op, so calling it on any provider — including `OllamaProvider`, which has no notion of auth —
is always safe.

Locked by `AIIntegrationServiceTest.SetAuthTokenForwardsToInstalledProvider` and
`AIIntegrationServiceTest.SetAuthTokenBeforeProviderInstalledIsRePushedBySetProvider` in
`Tests/AI/AIIntegrationServiceTests.cpp`.

