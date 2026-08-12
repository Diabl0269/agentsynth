#include "AIProviderRegistry.h"
#include "../Branding.h"
#include "OllamaProvider.h"
#include "RemoteProvider.h"

namespace synth {

void AIProviderRegistry::registerProvider(ProviderDescriptor descriptor) {
    descriptors.push_back(std::move(descriptor));
}

const ProviderDescriptor* AIProviderRegistry::find(const juce::String& id) const {
    for (auto& d : descriptors)
        if (d.id == id)
            return &d;
    return nullptr;
}

std::unique_ptr<AIProvider> AIProviderRegistry::create(const juce::String& id, const ProviderConfig& config) const {
    if (descriptors.empty())
        return nullptr;

    const ProviderDescriptor* descriptor = find(id);
    if (descriptor == nullptr)
        descriptor = &descriptors.front();

    return descriptor->create(config);
}

AIProviderRegistry AIProviderRegistry::createDefault() {
    AIProviderRegistry registry;
    registry.registerProvider(
        {"ollama", "Ollama (local)", true, false, [](const ProviderConfig& config) -> std::unique_ptr<AIProvider> {
             return std::make_unique<OllamaProvider>(config.host);
         }});

    // Registered AFTER "ollama" — order matters: AIProviderRegistry::create() falls back to
    // descriptors.front() for an unknown/empty id, and that fallback must stay "ollama". This is
    // deliberate even now that "remote" is the default (see MainComponent::resolveDefaultProviderId()):
    // an unrecognised/corrupt persisted id fails safe to the provider that sends no data anywhere,
    // never to the one that does.
    // hidden=false as of P4-6: offered in AISettingsTab's provider combo alongside "ollama".
    registry.registerProvider({"remote", "Remote (hosted)", true, true,
                               [](const ProviderConfig& config) -> std::unique_ptr<AIProvider> {
                                   auto provider = std::make_unique<RemoteProvider>(
                                       config.host.isNotEmpty() ? config.host : synth::branding::kApiBaseUrl);
                                   if (config.authToken.isNotEmpty())
                                       provider->setAuthToken(config.authToken);
                                   return provider;
                               },
                               /*hidden=*/false});

    return registry;
}

} // namespace synth
