#pragma once

#include "AIProvider.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace synth {

/** Config passed to a ProviderDescriptor's factory when constructing a provider instance. */
struct ProviderConfig {
    juce::String host;
    juce::String authToken;
};

/** Describes one registrable AI provider: how to build it and what the UI needs to show.
    `id` is persisted to user settings and must never change once shipped — `displayName`
    is free to change at any time without breaking a user's saved selection. */
struct ProviderDescriptor {
    juce::String id;
    juce::String displayName;
    bool needsHost = false;
    bool needsAuth = false;
    std::function<std::unique_ptr<AIProvider>(const ProviderConfig&)> create;

    // Appended LAST: existing call sites and tests use positional aggregate initialization
    // (e.g. {"ollama", "Ollama (local)", true, false, [](...){...}}), so inserting a field
    // earlier than `create` would silently break every one of them. Hidden from the Settings
    // UI (AISettingsTab's provider combo) while still fully registered and constructible — a
    // runtime feature flag, not a build-time one, for a provider that ships wired up before its
    // UI entry point is turned on.
    bool hidden = false;
};

/** Registry of available AI providers, keyed by stable id.
    Populated once via createDefault() and shared by MainComponent (construction) and
    SettingsWindow (UI population) so that adding a new hosted provider later is a single
    registration rather than edits scattered across both. */
class AIProviderRegistry {
public:
    void registerProvider(ProviderDescriptor descriptor);

    /** Returns nullptr if no provider with this id is registered. */
    const ProviderDescriptor* find(const juce::String& id) const;

    const std::vector<ProviderDescriptor>& listAll() const { return descriptors; }

    /** Constructs a provider by id. Falls back to the first registered provider if `id`
        is unknown or empty, and returns nullptr only if the registry itself is empty. */
    std::unique_ptr<AIProvider> create(const juce::String& id, const ProviderConfig& config) const;

    /** Registry pre-populated with all built-in providers (currently: Ollama). This is the
        ONE place a new provider gets registered. */
    static AIProviderRegistry createDefault();

private:
    std::vector<ProviderDescriptor> descriptors;
};

} // namespace synth
