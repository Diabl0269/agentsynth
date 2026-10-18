#pragma once

// The Sampler card's picture of the loaded instrument: a keyboard across its key span, one band above it per
// recording (the loud layer on the upper half, the soft layer on the lower), and the sounding notes lit on the keys.

#include "Modules/SamplerModule.h"
#include "UI/Theme/Theme.h"
#include <bitset>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

class SamplerKeyStrip
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    /** The keys one recording covers and the layer it plays on. `single` = the recording covers the whole velocity
     *  range, so it is drawn at full height. */
    struct Band {
        int lowKey = 0, highKey = 0;
        bool loud = false;
        bool single = false;
    };

    explicit SamplerKeyStrip(SamplerModule& sampler);

    /** Re-reads the instrument when it changed, and the sounding notes. */
    void refresh();
    /** The gated tick: re-reads the sounding notes only while one is lit or the module is playing. */
    void poll();

    int getRecordingCount() const noexcept { return recordings_; }
    bool hasSoftAndLoud() const noexcept { return softAndLoud_; }
    const std::vector<Band>& getBands() const noexcept { return bands_; }
    const std::bitset<128>& getLitNotes() const noexcept { return lit_; }
    /** The first and last key drawn. */
    juce::Range<int> getKeySpan() const noexcept { return span_; }
    /** "<N> recordings · soft and loud" (or "· one layer"); empty with no instrument. */
    juce::String getCaptionText() const;

    void paint(juce::Graphics& g) override;

private:
    void readInstrument();
    void setLit(const std::bitset<128>& notes);
    void paintKeys(juce::Graphics& g, juce::Rectangle<float> area, const synth::theme::Theme& theme) const;
    float keyX(int note, juce::Rectangle<float> area) const;

    SamplerModule& sampler_;
    int generation_ = -1;
    std::vector<Band> bands_;
    juce::Range<int> span_{36, 96};
    int recordings_ = 0;
    bool softAndLoud_ = false;
    std::bitset<128> lit_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerKeyStrip)
};

} // namespace synth::ui
