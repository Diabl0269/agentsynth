# AI Providers

How providers are registered, chosen and persisted, and the UI surfaces that depend on which one is
active. The two implementations are [OllamaProvider](ollama-provider.md) and
[RemoteProvider](remote-provider.md).

## The registry

Providers are registered once, in `synth::AIProviderRegistry::createDefault()`
(`Source/AI/AIProviderRegistry.cpp`). Adding a new hosted provider is a single `registerProvider(...)`
call there, not edits scattered across `MainComponent` and `SettingsWindow`.

Each `ProviderDescriptor` carries a stable `id` — persisted to the `"aiProvider"` setting — kept
separate from its `displayName`, which is what the UI shows, so renaming a UI label never breaks a
user's saved selection. `ProviderDescriptor::hidden` is a **runtime** flag, not a build-time one: a
hidden provider is still fully registered and constructible, which lets a provider ship wired up
before its UI entry point is turned on. Nothing is hidden.

`AIProviderRegistry::create()` falls back to the first registered provider when given an unknown or
empty id, which covers both a stale pre-registry saved value and a provider being removed later.
`"remote"` is registered **after** `"ollama"` deliberately, so that fallback stays `"ollama"`: an
unrecognised or corrupt persisted id must fail safe to the provider that sends no data anywhere,
never to the one that does.

`AISettingsTab` (`Source/UI/Settings/SettingsWindow.cpp`) populates its provider combo box from a
`visibleProviders` member rather than `providerRegistry.listAll()`, and `selectedDescriptor()` reads
the same filtered list. **Why both must use it:** indexing the combo's selected item against the
unfiltered list desyncs the moment a hidden provider sits between two visible ones.

## Which provider a launch gets

`"remote"` (hosted) is the default for a brand-new install; `"ollama"` (local) remains the default
for an install that has launched before, even if it has never opened AI settings. See
`MainComponent::resolveDefaultProviderId()`.

The persisted `"aiProvider"` key is only ever *written* by `AISettingsTab::updateSettings()`, so its
absence alone cannot distinguish "brand-new install" from "existing user who never touched AI
settings". `initialiseCommon()` instead checks whether the settings file already existed on disk
before this launch.

Each provider persists its own host under its own key, `"ollamaHost"` and `"remoteHost"`. **Why not
one key:** sharing one meant switching providers in Settings silently pointed the new one at
whatever host string the previous provider had left behind. An empty or unset `"remoteHost"` falls
back to `synth::branding::kApiBaseUrl` (`Source/Branding.h`); see that constant's own comment for
the current state of the deployed service.

Installing a provider after construction has an ordering contract of its own — see
[the model discovery ordering contract](chat-component.md#model-discovery-ordering-contract).

## Hosted mode disclosure

A visible line, not just a tooltip, appears next to the model picker in `AIChatComponent` whenever
the active provider is hosted: `"Hosted mode sends your prompt and current patch to Agent Synth's
servers."` **Why visible:** the disclosure must not be discoverable only by reading a policy page.

`AIProvider::isHosted()` (default `false`, overridden `true` in `RemoteProvider`) drives this via
`AIIntegrationService::isCurrentProviderHosted()`, and
`AIChatComponent::updateHostedModeNotice()` is called from
[`refreshModels()`](chat-component.md#model-discovery-ordering-contract), the same
post-`setProvider()` resync point model discovery uses, so the notice's visibility never lags a
provider switch. `AISettingsTab`'s provider combo box carries the same disclosure as a tooltip, for
the toggle itself.

## Model picker in hosted mode

`RemoteProvider::fetchAvailableModels()` always resolves `success=true` with an empty list, because
the service picks its own model server-side. `AIChatComponent::refreshModels()`'s callback treats
`success && models.isEmpty()` as "nothing to choose from", not a fetch failure: the picker shows a
single disabled `"Model chosen automatically"` entry rather than the misleading
`"Error fetching models"` that a plain `success` check would produce for every hosted-mode user.

## Eval harness parity

`Tools/AIEvalHarness` can replay its golden prompt set through `RemoteProvider` instead of
`OllamaProvider` via `--provider remote`, so a model can be scored through the exact stack a user
hits — client, service, inference backend — instead of an approximation of it. The service picks its
own model server-side, so the harness's `--model` is a report label only in that mode.
