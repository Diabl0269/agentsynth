#include "AIProviderRegistry.h"
#include "OllamaProvider.h"

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
    return registry;
}

} // namespace synth
