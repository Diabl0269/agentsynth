#pragma once

#include "AudioEngine/ModulationRoutingTypes.h"
#include "UI/Layout/FadeAmount.h"
#include <map>
#include <memory>
#include <optional>
#include <vector>

// docs/layout/mod-ring-fade.md

namespace synth::ui {

class ModChipFade;

/** A ringed knob's picture: the card's cached picture leaves the knob out and the card's live layer draws it over its
 *  ring with paint(). `card` is the knob's card. Installed by ModRingFades::holdKnob while the knob shows a ring. */
class RingedKnobPicture final : public juce::CachedComponentImage {
public:
    RingedKnobPicture(juce::Component& knob, juce::Component& card);
    void paint(juce::Graphics& g) override;
    bool invalidateAll() override;
    bool invalidate(const juce::Rectangle<int>& area) override;
    void releaseResources() override;

private:
    juce::Component& knob_;
    juce::Component::SafePointer<juce::Component> card_;
    juce::Image image_;
    float scale_ = 1.0f;
    bool valid_ = false;
};

/** The fades of a card's modulation overlays. The rings: a knob's ring fades in when it gets its first modulation and
 *  out when its last is removed, and a second modulation on a ringed knob changes nothing. The chip: see chip().
 *  Message thread only. */
class ModRingFades {
public:
    /** One ring to paint: `info` as last seen, drawn at `alpha` (0..1). */
    struct Ring {
        ModulationDisplayInfo info;
        float alpha = 1.0f;
    };

    /** `owner` is the card; it is repainted on every frame of a fade. */
    explicit ModRingFades(juce::Component& owner);
    ~ModRingFades();

    /** The fade of the mod hover chip the card paints under a knob. */
    ModChipFade& chip() noexcept { return *chip_; }
    const ModChipFade& chip() const noexcept { return *chip_; }

    /** Reads the routings now on `node` and starts the fade of every channel that gained or lost its modulation.
     *  A card that has never synced shows what it finds at once. */
    void sync(const std::vector<ModulationDisplayInfo>& infos, juce::AudioProcessorGraph::NodeID node);

    /** Every ring to draw now: the live routings of `node`, and the last-seen routings of a channel that is fading out.
     *  A routing `sync` has not met yet is not drawn (it is about to fade in). */
    std::vector<Ring> rings(const std::vector<ModulationDisplayInfo>& infos,
                            juce::AudioProcessorGraph::NodeID node) const;

    /** One live or fading channel: `repaint` when its rings would draw differently from the last time this was asked
     *  (a routing's value moved by 1/2000 or more, or its amount, polarity, the hover or the fade changed). */
    struct Step {
        int channel = 0;
        bool repaint = false;
    };
    /** The step of every channel `sync` knows, valid until the next call. `hoveredChannel` is the hovered one, or -1.
     */
    const std::vector<Step>& steps(const std::vector<ModulationDisplayInfo>& infos,
                                   juce::AudioProcessorGraph::NodeID node, int hoveredChannel);

    /** True once synced with no channel showing or fading a ring; before the first sync a live routing still draws. */
    bool drawsNothing() const noexcept { return synced_ && entries_.empty(); }

    /** `knob` draws `destChannel`'s ring: it is held as a RingedKnobPicture until the ring has gone. */
    void holdKnob(int destChannel, juce::Component& knob);

    /** The channel's ring alpha, 0 when it has none. */
    float alphaFor(int destChannel) const;

private:
    struct DrawnRouting {
        float value = 0.0f, amount = 1.0f;
        bool bipolar = true;
    };
    struct Drawn {
        std::vector<DrawnRouting> routings;
        float alpha = 0.0f;
        bool hovered = false;
    };
    struct Entry {
        std::unique_ptr<FadeAmount> fade;
        std::vector<ModulationDisplayInfo> last;
        std::optional<Drawn> drawn;
        size_t cursor = 0; // scratch of steps()
        bool differs = false;
        juce::Component::SafePointer<juce::Component> knob;
        void releaseKnob();
    };
    static constexpr float kRingValueStep = 1.0f / 2000.0f;

    juce::Component& owner_;
    std::unique_ptr<ModChipFade> chip_;
    std::map<int, Entry> entries_;
    std::vector<Step> steps_;
    bool synced_ = false;
};

} // namespace synth::ui
