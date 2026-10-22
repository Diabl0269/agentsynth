#pragma once

#include "AudioEngine/ModulationRoutingTypes.h"
#include "UI/Layout/FadeAmount.h"
#include <map>
#include <memory>
#include <vector>

// docs/layout/mod-ring-fade.md

namespace synth::ui {

/** The fade of a card's modulation rings: a knob's ring fades in when it gets its first modulation and out when its
 *  last is removed, and a second modulation on a ringed knob changes nothing. Message thread only. */
class ModRingFades {
public:
    /** One ring to paint: `info` as last seen, drawn at `alpha` (0..1). */
    struct Ring {
        ModulationDisplayInfo info;
        float alpha = 1.0f;
    };

    /** `owner` is the card; it is repainted on every frame of a fade. */
    explicit ModRingFades(juce::Component& owner)
        : owner_(owner) {}

    /** Reads the routings now on `node` and starts the fade of every channel that gained or lost its modulation.
     *  A card that has never synced shows what it finds at once. */
    void sync(const std::vector<ModulationDisplayInfo>& infos, juce::AudioProcessorGraph::NodeID node);

    /** Every ring to draw now: the live routings of `node`, and the last-seen routings of a channel that is fading out.
     *  A routing `sync` has not met yet is not drawn (it is about to fade in). */
    std::vector<Ring> rings(const std::vector<ModulationDisplayInfo>& infos,
                            juce::AudioProcessorGraph::NodeID node) const;

    /** The channel's ring alpha, 0 when it has none. */
    float alphaFor(int destChannel) const;

private:
    struct Entry {
        std::unique_ptr<FadeAmount> fade;
        std::vector<ModulationDisplayInfo> last;
    };

    juce::Component& owner_;
    std::map<int, Entry> entries_;
    bool synced_ = false;
};

} // namespace synth::ui
