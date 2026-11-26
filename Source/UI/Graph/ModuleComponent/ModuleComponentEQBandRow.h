#pragma once

// Private to the ModuleComponent units (ModuleComponent.cpp, ModuleComponentEQCard.cpp, ModuleComponentEQBandRow.cpp):
// the Parametric EQ card's band strip and selected-band row. Nothing outside those units should include this header.

#include "ModuleComponent.h"
#include "UI/Graph/CardBody/CardBodyLanes.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/UIAnimation.h"
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

    /** Tests: advances a running lane slide to `ms` after it began. */
    void stepLaneSlideForTest(double ms) { slide_.stepForTest(ms); }
    /** Tests: how far the lanes are drawn from where they are laid out, in px (0 at rest). */
    float laneOffsetForTest() const noexcept { return laneOffset_; }

    /** Drops the attachments. `processorAlive` false leaks them (the parameters are already freed). Idempotent. */
    void shutdown(bool processorAlive);

    /** The band the knobs, toggle and pill show now. */
    int boundBand() const noexcept { return boundBand_; }

    /** Height the strip needs at `width`. */
    int stripHeight(int width) const { return strip_->heightForWidth(width); }
    /** Places the strip across [x, x + width) at `y`; returns its height. */
    int layoutStrip(int x, int y, int width);
    /** Places the two lanes in a kLaneZoneHeight band at `y` across [x, x + width): the Main lane (titled after the
     *  selected band) on the left, a hairline, then the Out lane (title, rule and its three small knobs, Mix on top)
     *  at the right end. */
    void layoutLanes(int x, int y, int width);
    /** The Main lane: its title and rule, the toggle, solo and type pill on a row, then Freq, Gain and Q. */
    static constexpr int kLaneZoneHeight = synth::cardbody::kLaneTitleBand + synth::cardbody::kLaneBandHeight;
    /** The Out lane's width: its dial cells and the clear space at its left. */
    static constexpr int kOutLaneWidth = synth::cardbody::kLaneClear + synth::cardbody::kSmallKnobCellWidth + 6;
    /** The Main lane's title for the selected band, "Band 2 · Bell". */
    juce::String laneTitle() const { return title_->getText(); }
    /** Tab rank of a stop in the two lanes: Main's controls column first, then Out's (Mix, Pan, Output); -1 for a
     *  component that is in neither. */
    int laneRankOf(const juce::Component* stop) const;
    /** The lanes' top-left, for reading-order sorting. */
    juce::Point<int> lanesOrigin() const noexcept { return origin_; }

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

    // The Main and Out lanes' titles and rules, owned by the card as bespoke widgets.
    juce::Label* title_ = nullptr;
    juce::Label* outTitle_ = nullptr;
    juce::Component* titleRule_ = nullptr;
    juce::Component* outTitleRule_ = nullptr;
    juce::Component* laneDivider_ = nullptr;
    juce::Point<int> origin_;
    // When the strip gains or loses a row the lanes' laid-out place is final at once and only their drawing trails it:
    // laneOffset_ is the drawn minus the final y, eased to 0 over the card's height glide.
    void startLaneSlide(float from);
    juce::VBlankAnimatorUpdater slideUpdater_;
    synth::ui::AnimationDriver slide_;
    float laneOffset_ = 0.0f;
    int lastStripHeight_ = 0;
    int lanesWidth_ = 0;
    void layoutOutKnob(const juce::String& name, int x, int y);
};
