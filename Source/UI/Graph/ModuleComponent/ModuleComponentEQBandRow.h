#pragma once

// Private to the ModuleComponent units (ModuleComponent.cpp, ModuleComponentEQCard.cpp, ModuleComponentEQBandRow.cpp):
// the Parametric EQ card's band strip and selected-band row. Nothing outside those units should include this header.

#include "ModuleComponent.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQBandTypePill.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQSoloButton.h"
#include <array>
#include <memory>
#include <optional>

/** The band strip under the card's curve, and the three knobs, on/off toggle, solo button and type pill of the selected
 * band.
 *
 *  The curve, the strip and this row share one selection (the curve's). Freq, Gain and Q are three real sliders whose
 *  attachment, parameter, MIDI Learn entry, tooltip and gestures follow the selected band, so the card carries a
 *  handful of widgets however many of the 24 bands are on. A change of selection eases the knobs from the band they
 *  showed to the band they show now. Owned by the card; message thread only. */
class ModuleComponent::EqBandRow final : private EQCurveComponent::Listener {
public:
    static constexpr int kFields = 3; // Freq, Gain, Q

    EqBandRow(ModuleComponent& card, ParametricEQModule& eq, EQCurveComponent& curve);
    ~EqBandRow() override;

    /** Drops the attachments. `processorAlive` false leaks them (the parameters are already freed). Idempotent. */
    void shutdown(bool processorAlive);

    /** The band the knobs, toggle and pill show now. */
    int boundBand() const noexcept { return boundBand_; }

    /** Height the strip needs at `width`. */
    int stripHeight(int width) const { return strip_->heightForWidth(width); }
    /** Places the strip across [x, x + width) at `y`; returns its height. */
    int layoutStrip(int x, int y, int width);
    /** Places the three knob columns, the toggle, the solo button and the pill in a kRowHeight band at `y`. */
    void layoutSelected(int x, int y, int width);
    static constexpr int kRowHeight = 80;

    /** Where a cable to band-parameter channel `channel` lands while its knob is not shown: the band's chip (the
     * strip's start when the band has none), in card coordinates. Null for a channel that is no band's. */
    std::optional<juce::Point<float>> anchorFor(int channel) const;

    synth::ui::EQBandStrip& strip() noexcept { return *strip_; }
    synth::ui::EQBandTypePill& pill() noexcept { return *pill_; }
    synth::ui::EQSoloButton& soloButton() noexcept { return *solo_; }
    juce::ToggleButton& toggle() noexcept { return *toggle_; }
    juce::Slider& knob(int field) noexcept { return *knobs_[(size_t)field]; }
    synth::ui::KeyedPointTween& glideForTest() noexcept { return *glide_; }

private:
    void eqCurveChanged() override;
    void bind(int band);
    juce::Slider* newKnob();
    void wireKnobGestures(juce::Slider& knob, juce::RangedAudioParameter* param);
    void refreshText();
    void applyGlideFrame();

    ModuleComponent& card_;
    ParametricEQModule& eq_;
    EQCurveComponent& curve_;
    int boundBand_ = 0;
    bool alive_ = true;

    // Owned here, not adopted by the card: both listen to the curve, so they must go before it does.
    std::unique_ptr<synth::ui::EQBandStrip> strip_;
    std::unique_ptr<synth::ui::EQBandTypePill> pill_;
    std::unique_ptr<synth::ui::EQSoloButton> solo_;
    juce::ToggleButton* toggle_ = nullptr;
    std::array<juce::Slider*, kFields> knobs_{};
    std::array<juce::Label*, kFields> labels_{};
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kFields> attachments_;
    std::unique_ptr<juce::ButtonParameterAttachment> onAttachment_;
    std::unique_ptr<synth::ui::KeyedPointTween> glide_;
};
