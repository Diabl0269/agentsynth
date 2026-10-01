// DefaultCardLayouts.cpp -- the registry of code-defined card layouts, keyed by module type. Each module
// family registers its own layouts from its own unit (DefaultCardLayouts<Family>.cpp), so work on one
// family never edits another family's file.
#include "DefaultCardLayouts.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

void DefaultCardLayouts::add(const juce::String& moduleType, CardLayout layout, int defaultRevision,
                             std::vector<CardDimRule> dimRules) {
    entries_[moduleType] = Entry{std::move(layout), defaultRevision, std::move(dimRules)};
}

const DefaultCardLayouts::Entry* DefaultCardLayouts::find(const juce::String& moduleType) const {
    const auto it = entries_.find(moduleType);
    return it != entries_.end() ? &it->second : nullptr;
}

const DefaultCardLayouts& DefaultCardLayouts::builtIn() {
    static const DefaultCardLayouts registry = [] {
        DefaultCardLayouts defaults;
        registerSourceCardLayouts(defaults);
        registerEnvelopeCardLayouts(defaults);
        registerFilterDynamicsCardLayouts(defaults);
        registerEffectCardLayouts(defaults);
        return defaults;
    }();
    return registry;
}

} // namespace synth
