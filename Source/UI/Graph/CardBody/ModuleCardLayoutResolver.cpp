// ModuleCardLayoutResolver.cpp -- which layout a built-in module's card is drawn from.
// See docs/layout/module-card-layout.md#where-a-layout-comes-from.
#include "ModuleCardLayoutResolver.h"

namespace synth {

namespace {

// An override or file that does not parse (corrupt, or written by a newer version) is skipped: the
// next source in the precedence chain is used and the unreadable one is left untouched.
std::optional<CardLayout> layoutFromVar(const juce::var& json, const juce::StringArray* allParamIds) {
    if (json.isVoid())
        return std::nullopt;
    auto parsed = CardLayout::fromVar(json);
    if (parsed.status != CardLayout::ParseStatus::Ok)
        return std::nullopt;
    if (allParamIds != nullptr)
        return upgradeV1(parsed.layout, *allParamIds);
    return std::move(parsed.layout);
}

std::optional<CardLayout> storedDefault(const juce::String& moduleType, const ModuleCardLayoutStore* store,
                                        const juce::StringArray* allParamIds) {
    if (store == nullptr)
        return std::nullopt;
    auto loaded = store->loadDefault(moduleType);
    if (loaded.status != ModuleCardLayoutStore::LoadStatus::Ok)
        return std::nullopt;
    if (allParamIds != nullptr)
        return upgradeV1(loaded.layout, *allParamIds);
    return std::move(loaded.layout);
}

} // namespace

ResolvedModuleCardLayout resolveModuleCardLayout(const juce::String& moduleType, const juce::var& instanceOverride,
                                                 const ModuleCardLayoutStore* store, const DefaultCardLayouts& defaults,
                                                 const juce::StringArray* allParamIds) {
    ResolvedModuleCardLayout resolved;
    if (auto layout = layoutFromVar(instanceOverride, allParamIds)) {
        resolved.source = ResolvedModuleCardLayout::Source::Instance;
        resolved.layout = std::move(layout);
    } else if (auto stored = storedDefault(moduleType, store, allParamIds)) {
        resolved.source = ResolvedModuleCardLayout::Source::TypeDefault;
        resolved.layout = std::move(stored);
    } else if (const auto* entry = defaults.find(moduleType)) {
        resolved.source = ResolvedModuleCardLayout::Source::CodeDefault;
        resolved.layout = entry->layout;
        resolved.defaultRevision = entry->defaultRevision;
    }
    // A dim rule states what the module does (Detune does nothing at one voice), not where a control
    // sits, so it applies to whichever layout the card draws; the card binds only the rules whose
    // parameter it shows.
    if (const auto* entry = defaults.find(moduleType))
        resolved.dimRules = entry->dimRules;
    return resolved;
}

} // namespace synth
