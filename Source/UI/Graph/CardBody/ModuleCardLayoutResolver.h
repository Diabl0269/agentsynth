#pragma once

#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/ModuleCardLayoutStore.h"
#include <optional>

namespace synth {

/** The layout a built-in module's card is drawn from, and where it came from. */
struct ResolvedModuleCardLayout {
    enum class Source { Instance, TypeDefault, CodeDefault, Automatic };

    Source source = Source::Automatic;
    std::optional<CardLayout> layout;  ///< Nullopt for Automatic: draw today's automatic layout.
    int defaultRevision = 0;           ///< The code default's revision; 0 unless source is CodeDefault.
    std::vector<CardDimRule> dimRules; ///< The type's code dim rules, whatever the source.
};

/**
 * First hit wins: the node's `cardLayout` property (`instanceOverride`, void when unset), the
 * type's stored default (`store` may be null), the code default in `defaults`, then automatic. A
 * source that does not parse is skipped, not fatal. A saved layout (either source) of a type that has a
 * code default is skipped too unless its `basedOn` ("<ModuleType>@<revision>") names the code default's
 * current revision or a newer one: a layout saved before a redesign, or with no `basedOn`, would otherwise
 * hide the redesign for ever. When `allParamIds` is given, a v1 (flat) layout from either user source
 * is upgraded with it (upgradeV1). Pure: no UI, no globals.
 */
ResolvedModuleCardLayout resolveModuleCardLayout(const juce::String& moduleType, const juce::var& instanceOverride,
                                                 const ModuleCardLayoutStore* store, const DefaultCardLayouts& defaults,
                                                 const juce::StringArray* allParamIds = nullptr);

/**
 * `layout` marked as based on `moduleType`'s current code default revision, for a layout about to be saved from what
 * the card shows: without the mark the resolver would skip it as older than the default. A layout that already has a
 * `basedOn`, or a type with no code default, is returned unchanged.
 */
CardLayout withBasedOnCurrentDefault(CardLayout layout, const juce::String& moduleType,
                                     const DefaultCardLayouts& defaults);

} // namespace synth
