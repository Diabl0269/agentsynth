// DefaultCardLayouts.cpp -- the registry of code-defined card layouts, keyed by module type.
#include "DefaultCardLayouts.h"

namespace synth {

void DefaultCardLayouts::add(const juce::String& moduleType, CardLayout layout, int defaultRevision) {
    entries_[moduleType] = Entry{std::move(layout), defaultRevision};
}

const DefaultCardLayouts::Entry* DefaultCardLayouts::find(const juce::String& moduleType) const {
    const auto it = entries_.find(moduleType);
    return it != entries_.end() ? &it->second : nullptr;
}

const DefaultCardLayouts& DefaultCardLayouts::builtIn() {
    static const DefaultCardLayouts registry;
    return registry;
}

} // namespace synth
