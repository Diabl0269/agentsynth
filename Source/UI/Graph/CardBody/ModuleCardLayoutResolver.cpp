// ModuleCardLayoutResolver.cpp -- which layout a built-in module's card is drawn from.
// See docs/layout/module-card-layout.md#where-a-layout-comes-from.
#include "ModuleCardLayoutResolver.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"

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

// The revision in "<ModuleType>@<revision>", or nullopt for a missing, malformed or other-type basis.
std::optional<int> basedOnRevision(const CardLayout& layout, const juce::String& moduleType) {
    if (!layout.basedOn.has_value())
        return std::nullopt;
    const auto at = layout.basedOn->lastIndexOfChar('@');
    if (at < 0 || layout.basedOn->substring(0, at) != moduleType)
        return std::nullopt;
    const auto digits = layout.basedOn->substring(at + 1);
    if (digits.isEmpty() || !digits.containsOnly("0123456789") || digits.length() > 6)
        return std::nullopt;
    return digits.getIntValue();
}

// A saved layout is kept only while it is based on the code default's current revision or a newer one. Older (or
// unlabelled) layouts predate a redesign of the default and would hide it; a type with no code default has nothing
// to be older than.
std::optional<CardLayout> keepIfCurrent(std::optional<CardLayout> layout, const juce::String& moduleType,
                                        const DefaultCardLayouts::Entry* entry) {
    if (!layout.has_value() || entry == nullptr)
        return layout;
    const auto revision = basedOnRevision(*layout, moduleType);
    if (revision.has_value() && *revision >= entry->defaultRevision)
        return layout;
    return std::nullopt;
}

} // namespace

CardLayout withBasedOnCurrentDefault(CardLayout layout, const juce::String& moduleType,
                                     const DefaultCardLayouts& defaults) {
    if (layout.basedOn.has_value())
        return layout;
    if (const auto* entry = defaults.find(moduleType))
        layout.basedOn = moduleType + "@" + juce::String(entry->defaultRevision);
    return layout;
}

ResolvedModuleCardLayout resolveModuleCardLayout(const juce::String& moduleType, const juce::var& instanceOverride,
                                                 const ModuleCardLayoutStore* store, const DefaultCardLayouts& defaults,
                                                 const juce::StringArray* allParamIds) {
    ResolvedModuleCardLayout resolved;
    const auto* codeEntry = defaults.find(moduleType);
    if (auto layout = keepIfCurrent(layoutFromVar(instanceOverride, allParamIds), moduleType, codeEntry)) {
        resolved.source = ResolvedModuleCardLayout::Source::Instance;
        resolved.layout = std::move(layout);
    } else if (auto stored = keepIfCurrent(storedDefault(moduleType, store, allParamIds), moduleType, codeEntry)) {
        resolved.source = ResolvedModuleCardLayout::Source::TypeDefault;
        resolved.layout = std::move(stored);
    } else if (codeEntry != nullptr) {
        resolved.source = ResolvedModuleCardLayout::Source::CodeDefault;
        resolved.layout = codeEntry->layout;
        resolved.defaultRevision = codeEntry->defaultRevision;
    }
    // A saved ADSR layout in the first Separate form (both groups always shown) is read in the current one.
    if (resolved.layout.has_value() && resolved.source != ResolvedModuleCardLayout::Source::CodeDefault &&
        hasAdsrTimeTempo(moduleType))
        resolved.layout = withAdsrSeparateLooks(std::move(*resolved.layout));
    // A dim rule states what the module does (Detune does nothing at one voice), not where a control
    // sits, so it applies to whichever layout the card draws; the card binds only the rules whose
    // parameter it shows.
    if (codeEntry != nullptr)
        resolved.dimRules = codeEntry->dimRules;
    return resolved;
}

} // namespace synth
