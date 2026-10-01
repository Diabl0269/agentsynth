#pragma once

#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/ModuleCardLayoutStore.h"
#include <optional>

namespace synth {

/** The layout a built-in module's card is drawn from, and where it came from. */
struct ResolvedModuleCardLayout {
    enum class Source { Instance, TypeDefault, CodeDefault, Automatic };

    Source source = Source::Automatic;
    std::optional<CardLayout> layout; ///< Nullopt for Automatic: draw today's automatic layout.
    int defaultRevision = 0;          ///< The code default's revision; 0 unless source is CodeDefault.
};

/**
 * First hit wins: the node's `cardLayout` property (`instanceOverride`, void when unset), the
 * type's stored default (`store` may be null), the code default in `defaults`, then automatic. A
 * source that does not parse is skipped, not fatal. When `allParamIds` is given, a v1 (flat)
 * layout from either user source is upgraded with it (upgradeV1). Pure: no UI, no globals.
 */
ResolvedModuleCardLayout resolveModuleCardLayout(const juce::String& moduleType, const juce::var& instanceOverride,
                                                 const ModuleCardLayoutStore* store, const DefaultCardLayouts& defaults,
                                                 const juce::StringArray* allParamIds = nullptr);

} // namespace synth
