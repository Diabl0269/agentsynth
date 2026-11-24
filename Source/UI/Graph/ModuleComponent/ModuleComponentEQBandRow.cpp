// ModuleComponentEQBandRow.cpp -- see ModuleComponentEQBandRow.h. The card's Freq / Gain / Q sliders and its on/off
// toggle are bound to one band's parameters at a time, so every card feature that finds a knob through
// `sliders` / `sliderParams` (mod dots, rings, automation, MIDI Learn) sees the selected band's parameter.
#include "ModuleComponentEQBandRow.h"
#include "ModuleComponentInternal.h"
#include "UI/Graph/CardBody/LaneHairline.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/ModuleViews/EQBandKinds.h"
#include <algorithm>

namespace {
// The Main lane's three knobs are large and even across the lane; the dial is the slider's box less its value box.
constexpr int kKnobLabelHeight = 20;
constexpr int kKnobWidth = 84;
constexpr int kRowKnobHeight = 84;
constexpr int kTextBoxWidth = 64;
constexpr int kTextBoxHeight = 20;
constexpr int kRowGap = 6;
constexpr int kControlHeight = 26;
constexpr double kKnobGlideMs = 120.0;
constexpr int kPillWidth = 110;
constexpr int kOnWidth = 64;
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
        auto* knob = newKnob();
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

    solo_ = std::make_unique<synth::ui::EQSoloButton>(curve_);
    solo_->setComponentID("eqCardSolo");
    card_.addAndMakeVisible(solo_.get());

    title_ = card_.adoptBespokeWidget(new juce::Label("eqLaneTitle", "Band"));
    synth::cardbody::styleLaneTitle(*title_);
    outTitle_ = card_.adoptBespokeWidget(new juce::Label("eqOutTitle", "Out"));
    synth::cardbody::styleLaneTitle(*outTitle_);
    titleRule_ = card_.adoptBespokeWidget(new synth::cardbody::LaneHairline());
    outTitleRule_ = card_.adoptBespokeWidget(new synth::cardbody::LaneHairline());
    laneDivider_ = card_.adoptBespokeWidget(new synth::cardbody::LaneHairline());
    for (auto* part : {(juce::Component*)title_, (juce::Component*)titleRule_, (juce::Component*)outTitle_,
                       (juce::Component*)outTitleRule_, (juce::Component*)laneDivider_})
        card_.addAndMakeVisible(part);

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
        wireKnobGestures(*slider, param);
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

// The lane is titled after the band it shows (the type is the pill's glyph, never spelt out); the toggle's own word is
// "On".
void ModuleComponent::EqBandRow::refreshText() {
    toggle_->setButtonText("On");
    title_->setText("Band " + juce::String(boundBand_ + 1), juce::dontSendNotification);
}

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

// One Out knob: caption over a small dial and its value box, the cell's top-left at (x, y).
void ModuleComponent::EqBandRow::layoutOutKnob(const juce::String& name, int x, int y) {
    namespace cb = synth::cardbody;
    for (int i = 0; i < card_.sliders.size(); ++i) {
        if (!card_.sliders[i]->getComponentID().equalsIgnoreCase(name))
            continue;
        card_.sliderLabels[i]->setBounds(x, y, cb::kSmallKnobCellWidth, cb::kSmallKnobCaptionHeight);
        card_.sliders[i]->setBounds(x, y + cb::kSmallKnobCaptionHeight, cb::kSmallKnobCellWidth,
                                    cb::kSmallKnobWidgetHeight);
        return;
    }
}

void ModuleComponent::EqBandRow::layoutLanes(int x, int y, int width) {
    namespace cb = synth::cardbody;
    origin_ = {x, y};
    const int outSlotX = x + width - kOutLaneWidth;
    const int mainRight = outSlotX - cb::kLaneRuleWidth;
    const int bandTop = y + cb::kLaneTitleBand;
    const int bandBottom = bandTop + cb::kLaneBandHeight;

    // Main: the title and its rule over the usable width (kLaneClear off the hairline), the on / solo / type row at
    // the top of the band, the three knobs spread evenly and sitting on its bottom.
    const int mainX = x;
    const int mainW = mainRight - cb::kLaneClear - mainX;
    title_->setBounds(mainX, y, mainW, cb::kLaneTitleHeight);
    titleRule_->setBounds(mainX, y + cb::kLaneTitleHeight, mainW, cb::kLaneRuleWidth);
    const int rowY = bandTop;
    toggle_->setBounds(mainX, rowY, kOnWidth, kControlHeight);
    solo_->setBounds(mainX + kOnWidth + kRowGap, rowY, kControlHeight, kControlHeight);
    pill_->setBounds(mainX + kOnWidth + kControlHeight + 2 * kRowGap, rowY, kPillWidth, kControlHeight);
    const int colW = mainW / kFields;
    const int knobsTop = bandBottom - (kKnobLabelHeight + kRowKnobHeight);
    for (int f = 0; f < kFields; ++f) {
        const int colX = mainX + f * colW;
        labels_[(size_t)f]->setBounds(colX, knobsTop, colW, kKnobLabelHeight);
        knobs_[(size_t)f]->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kTextBoxWidth, kTextBoxHeight);
        knobs_[(size_t)f]->setBounds(colX + (colW - kKnobWidth) / 2, knobsTop + kKnobLabelHeight, kKnobWidth,
                                     kRowKnobHeight);
    }

    // The hairline between the lanes, then Out: its title and rule, Mix on top, Pan, Output on the bottom.
    laneDivider_->setBounds(mainRight, y, cb::kLaneRuleWidth, cb::kLaneTitleBand + cb::kLaneBandHeight);
    const int outX = outSlotX + cb::kLaneClear;
    const int outW = kOutLaneWidth - cb::kLaneClear;
    outTitle_->setBounds(outX, y, outW, cb::kLaneTitleHeight);
    outTitleRule_->setBounds(outX, y + cb::kLaneTitleHeight, outW, cb::kLaneRuleWidth);
    const int cellX = outX + (outW - cb::kSmallKnobCellWidth) / 2;
    int cellY = bandTop;
    for (const char* name : {"Mix", "Pan", "Output"}) {
        layoutOutKnob(name, cellX, cellY);
        cellY += cb::kSmallKnobCellHeight + cb::kLaneCellGap;
    }
}

int ModuleComponent::EqBandRow::laneRankOf(const juce::Component* stop) const {
    for (int i = 0; i < card_.sliders.size(); ++i)
        if (card_.sliders[i] == stop) {
            const auto id = card_.sliders[i]->getComponentID();
            if (id == "Mix")
                return 100;
            if (id == "Pan")
                return 101;
            if (id == "Output")
                return 102;
        }
    // Main's controls in reading order, column first: the row, then Freq, Gain, Q.
    if (stop == toggle_)
        return 0;
    if (stop == static_cast<const juce::Component*>(solo_.get()))
        return 1;
    if (stop == static_cast<const juce::Component*>(pill_.get()))
        return 2;
    for (int f = 0; f < kFields; ++f)
        if (stop == knobs_[(size_t)f])
            return 3 + f;
    return -1;
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
