#pragma once

#include "Modules/CardLayout.h"
#include <vector>

namespace synth {

class HostedPluginModule;
class PluginCardLayoutStore;

/** A layout slot bound to the live instance's parameter. */
struct ResolvedCardSlot {
    CardSlot slot;
    juce::AudioProcessorParameter* param = nullptr; ///< Null when orphaned or when no instance is live.
    bool orphaned = false;                          ///< The instance is live and the slot no longer resolves.
};

/** The layout a hosted card shows, and where it came from. */
struct ResolvedCardLayout {
    enum class Source { Instance, PluginDefault, Automatic };

    Source source = Source::Automatic;
    CardLayout layout; ///< As stored, orphan slots included.
    std::vector<ResolvedCardSlot> slots;

    int orphanCount() const;
    /** `layout` minus its orphan slots: what the next save writes. */
    CardLayout layoutWithoutOrphans() const;
};

/** The first 8 automatable parameters, skipping the bypass parameter. Empty without a live instance. */
CardLayout automaticCardLayout(const HostedPluginModule& node);

/** Precedence: instance override, then the plugin's stored default (`store` may be null), then automatic. */
ResolvedCardLayout resolveHostedCardLayout(HostedPluginModule& node, const PluginCardLayoutStore* store);

} // namespace synth
