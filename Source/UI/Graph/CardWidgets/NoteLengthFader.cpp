// NoteLengthFader.cpp -- the note-division fader: its travel is the eight lengths of the parameter's current
// feel (the slider's value is still the parameter's own index), and the S | T | D picker under it writes the feel.
#include "NoteLengthFader.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"

namespace synth::ui {

namespace {

constexpr int kLengths = 8;
constexpr int kKinds = 3;

} // namespace

NoteLengthFader::NoteLengthFader()
    : CardFader(Orientation::Vertical)
    , picker_("Feel", juce::StringArray{"S", "T", "D"}) {
    addAndMakeVisible(picker_);
    picker_.setTooltip("Feel: S straight, T triplet, D dotted");
    reserveBelowValueText(kPickerHeight);
}

NoteLengthFader::~NoteLengthFader() = default;

bool NoteLengthFader::suits(const juce::AudioParameterChoice& param) {
    return param.choices == synth::envelopeNoteDivisions() && param.choices.size() == kLengths * kKinds;
}

// The feel is read from the parameter at the moment of every conversion, so the one source of truth stays the
// parameter: a drag moves within the feel it has, a snap never leaves it.
std::unique_ptr<juce::ParameterAttachment> NoteLengthFader::bindTo(juce::AudioParameterChoice& param,
                                                                   std::shared_ptr<bool> alive) {
    param_ = &param;
    alive_ = alive;
    const auto feelStart = [p = &param] { return (p->getIndex() / kLengths) * kLengths; };
    juce::NormalisableRange<double> steps(
        0.0, double(kLengths * kKinds - 1),
        [feelStart](double, double, double proportion) {
            return double(feelStart() + juce::roundToInt(juce::jlimit(0.0, 1.0, proportion) * (kLengths - 1)));
        },
        [](double, double, double value) {
            return double(juce::jlimit(0, kLengths - 1, juce::roundToInt(value) % kLengths)) / (kLengths - 1);
        },
        [feelStart](double, double, double value) {
            const int start = feelStart();
            return double(juce::jlimit(start, start + kLengths - 1, juce::roundToInt(value)));
        });
    steps.interval = 1.0;
    setNormalisableRange(steps);
    setValue(double(param.getIndex()), juce::dontSendNotification);

    picker_.onChange = [this](int feel) {
        if (alive_ != nullptr && *alive_ && attachment_ != nullptr && param_ != nullptr)
            attachment_->setValueAsCompleteGesture(float(feel * kLengths + param_->getIndex() % kLengths));
    };
    auto attachment = std::make_unique<juce::ParameterAttachment>(param, [this, alive](float index) {
        if (*alive)
            picker_.setSelectedIndex(juce::roundToInt(index) / kLengths, juce::dontSendNotification);
    });
    attachment_ = attachment.get();
    attachment_->sendInitialUpdate();
    return attachment;
}

void NoteLengthFader::resized() {
    CardFader::resized();
    picker_.setBounds(0, getHeight() - kPickerHeight, getWidth(), kPickerHeight);
    picker_.toFront(false); // the slider's value box is a child it rebuilds, and it must not cover the picker
}

void NoteLengthFader::valueBoxStyled() { picker_.toFront(false); }

} // namespace synth::ui
