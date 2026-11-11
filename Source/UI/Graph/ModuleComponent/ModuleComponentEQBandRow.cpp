// ModuleComponentEQBandRow.cpp -- see ModuleComponentEQBandRow.h. The card's Freq / Gain / Q sliders and its on/off
// toggle are bound to one band's parameters at a time, so every card feature that finds a knob through
// `sliders` / `sliderParams` (mod dots, rings, automation, MIDI Learn) sees the selected band's parameter.
#include "ModuleComponentEQBandRow.h"
#include "ModuleComponentInternal.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>

namespace {
constexpr int kKnobColumn = 90;
constexpr int kKnobLabelHeight = 20;
constexpr int kKnobWidth = 70;
constexpr int kRowKnobHeight = 60;
constexpr int kTextBoxWidth = 50;
constexpr int kTextBoxHeight = 20;
constexpr int kRowGap = 6;
constexpr int kControlHeight = 26;
constexpr double kKnobGlideMs = 120.0;
constexpr int kPillWidth = 110;
constexpr const char* kFieldSuffix[3] = {"Freq", "Gain", "Q"};

juce::RangedAudioParameter* fieldParam(ParametricEQModule& eq, int band, int field) {
    const auto id = "band" + juce::String(band + 1) + kFieldSuffix[field];
    for (auto* p : eq.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}

juce::RangedAudioParameter* onParam(ParametricEQModule& eq, int band) {
    const auto id = "band" + juce::String(band + 1) + "On";
    for (auto* p : eq.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}
} // namespace

ModuleComponent::EqBandRow::EqBandRow(ModuleComponent& card, ParametricEQModule& eq, EQCurveComponent& curve)
    : card_(card)
    , eq_(eq)
    , curve_(curve) {
    // The selection starts on the lowest band that is on, so the knobs show a band that sounds.
    const auto& bands = curve_.getBands();
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        if (bands[(size_t)b].enabled) {
            curve_.setSelectedBand(b);
            break;
        }
    boundBand_ = std::max(0, curve_.getSelectedBand());

    strip_ = std::make_unique<synth::ui::EQBandStrip>(curve_);
    strip_->setComponentID("eqCardStrip");
    strip_->onPreferredHeightChanged = [safe = juce::Component::SafePointer<ModuleComponent>(&card_)] {
        if (safe == nullptr)
            return;
        safe->updateLayout();
        safe->owner.handleModuleResized(safe.getComponent());
        safe->repaint();
    };
    card_.addAndMakeVisible(strip_.get());

    for (int f = 0; f < kFields; ++f) {
        auto* knob = card_.adoptBespokeWidget(detail::newCardKnobSlider());
        knobs_[(size_t)f] = knob;
        card_.sliders.add(knob);
        card_.sliderParams.add(nullptr);
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kTextBoxWidth, kTextBoxHeight);
        card_.addAndMakeVisible(knob);
        knob->addMouseListener(&card_, false);

        auto* label = card_.adoptBespokeWidget(new juce::Label(kFieldSuffix[f], kFieldSuffix[f]));
        labels_[(size_t)f] = label;
        card_.sliderLabels.add(label);
        label->setJustificationType(juce::Justification::centred);
        card_.addAndMakeVisible(label);
    }

    toggle_ = card_.adoptBespokeWidget(new detail::MidiLearnableToggleButton());
    card_.toggles.add(toggle_);
    card_.addAndMakeVisible(toggle_);
    toggle_->addMouseListener(&card_, false);

    pill_ = std::make_unique<synth::ui::EQBandTypePill>(curve_);
    card_.addAndMakeVisible(pill_.get());

    glide_ = std::make_unique<synth::ui::KeyedPointTween>(*knobs_[0]);
    glide_->onDirty = [this](juce::Rectangle<int>) { applyGlideFrame(); };

    bind(boundBand_);
    curve_.addListener(this);
}

ModuleComponent::EqBandRow::~EqBandRow() {
    if (alive_)
        curve_.removeListener(this);
}

void ModuleComponent::EqBandRow::shutdown(bool processorAlive) {
    if (!alive_)
        return;
    alive_ = false;
    curve_.removeListener(this);
    glide_.reset();
    for (auto& attachment : attachments_) {
        if (!processorAlive)
            (void)attachment.release(); // its parameter may already be freed
        attachment.reset();
    }
    if (!processorAlive)
        (void)onAttachment_.release();
    onAttachment_.reset();
}

// The selection follows the curve both ways; the knobs only move for a band, never for "no selection".
void ModuleComponent::EqBandRow::eqCurveChanged() {
    const int selected = curve_.getSelectedBand();
    if (selected >= 0 && selected != boundBand_)
        bind(selected);
    else
        refreshText();
}

// Moves the three knobs and the toggle onto `band`. Each slider keeps its place in the card's parallel arrays: only
// the parameter its slot names changes, so mod dots, rings, MIDI Learn and automation answer for the new band. The
// knobs ease from the value they showed to the new band's, with nothing written to a parameter.
void ModuleComponent::EqBandRow::bind(int band) {
    if (!alive_)
        return;
    const int previous = boundBand_;
    boundBand_ = band;
    for (int f = 0; f < kFields; ++f) {
        auto* param = fieldParam(eq_, band, f);
        auto* slider = knobs_[(size_t)f];
        if (param == nullptr)
            continue;
        const auto* old = fieldParam(eq_, previous, f);
        const float shown = old != nullptr ? glide_->drawnAt(f, {old->getValue(), 0.0f}).x : param->getValue();
        const int index = card_.sliders.indexOf(slider);
        if (index >= 0)
            card_.sliderParams.set(index, param);
        // The old attachment goes first: while it lives, the slider taking the new band's value would write it to the
        // band the knob just left.
        attachments_[(size_t)f].reset();
        attachments_[(size_t)f] = std::make_unique<juce::SliderParameterAttachment>(*param, *slider);
        slider->setComponentID(param->getName(100));
        slider->setTitle(param->getName(100));
        slider->setTooltip(param->getName(100));
        card_.rebindMidiLearnable(*slider, param);
        if (auto* gestures = dynamic_cast<synth::ui::CardControlGestures*>(slider))
            card_.wireCardControlGestures(*slider, *gestures, param);
        if (previous != band) {
            glide_->retarget(f, {shown, 0.0f}, {param->getValue(), 0.0f}, false, kKnobGlideMs);
            applyGlideFrame();
        }
    }
    if (auto* on = onParam(eq_, band)) {
        onAttachment_.reset();
        onAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*on, *toggle_);
        toggle_->setComponentID(on->getName(100));
        toggle_->setTooltip("Switch band " + juce::String(band + 1) + " on or off");
        toggle_->setTitle("Band " + juce::String(band + 1) + " on");
        card_.rebindMidiLearnable(*toggle_, on);
    }
    refreshText();
    card_.syncModDotButtons();
    card_.repaint();
}

void ModuleComponent::EqBandRow::refreshText() { toggle_->setButtonText("Band " + juce::String(boundBand_ + 1)); }

// One frame of the glide: each knob shows the new band's value plus whatever distance is left.
void ModuleComponent::EqBandRow::applyGlideFrame() {
    if (!alive_ || glide_ == nullptr)
        return;
    for (int f = 0; f < kFields; ++f) {
        auto* param = fieldParam(eq_, boundBand_, f);
        if (param == nullptr)
            continue;
        const float norm = juce::jlimit(0.0f, 1.0f, glide_->drawnAt(f, {param->getValue(), 0.0f}).x);
        knobs_[(size_t)f]->setValue((double)param->convertFrom0to1(norm), juce::dontSendNotification);
    }
}

int ModuleComponent::EqBandRow::layoutStrip(int x, int y, int width) {
    const int h = strip_->heightForWidth(width);
    strip_->setBounds(x, y, width, h);
    return h;
}

void ModuleComponent::EqBandRow::layoutSelected(int x, int y, int width) {
    for (int f = 0; f < kFields; ++f) {
        const int colX = x + f * kKnobColumn;
        labels_[(size_t)f]->setBounds(colX, y, kKnobColumn - 4, kKnobLabelHeight);
        knobs_[(size_t)f]->setBounds(colX + (kKnobColumn - 4 - kKnobWidth) / 2, y + kKnobLabelHeight, kKnobWidth,
                                     kRowKnobHeight);
    }
    const int sideX = x + kFields * kKnobColumn + 10;
    const int sideW = std::max(0, x + width - sideX);
    toggle_->setBounds(sideX, y + 8, std::min(sideW, kPillWidth), kControlHeight);
    pill_->setBounds(sideX, y + 8 + kControlHeight + kRowGap, std::min(sideW, kPillWidth), kControlHeight - 4);
}

std::optional<juce::Point<float>> ModuleComponent::EqBandRow::anchorFor(int channel) const {
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        for (int f = 0; f < kFields; ++f) {
            if (ParametricEQModule::bandCvChannel(b, f) != channel)
                continue;
            const auto* chip = strip_->getChip(b);
            const auto local =
                chip != nullptr ? chip->getBounds().getCentre() : juce::Point<int>(0, strip_->getHeight() / 2);
            return (strip_->getPosition() + local).toFloat();
        }
    return std::nullopt;
}
