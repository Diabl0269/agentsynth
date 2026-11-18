#include "DepthPicker.h"

namespace synth::ui {

using synth::theme::DepthLevel;

namespace {
constexpr int kCaptionWidth = 56; // the knob picker's row-label width, so both captions line up
constexpr int kSegmentWidth = 64;
} // namespace

DepthPicker::DepthPicker() {
    setComponentID("depthPicker");
    caption_.setJustificationType(juce::Justification::centredLeft);
    caption_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(caption_);

    juce::StringArray labels;
    for (int i = 0; i < synth::theme::kDepthLevelCount; ++i)
        labels.add(synth::theme::depthLevelLabel((DepthLevel)i));
    switch_ = std::make_unique<CardSegmentedSwitch>("Depth", labels);
    switch_->setTooltip("Flat, Soft or Rich: how much light, shade and lift cards, icons and knobs get");
    switch_->setSelectedIndex((int)level_, juce::dontSendNotification);
    switch_->onChange = [this](int index) {
        level_ = (DepthLevel)index;
        if (onChanged)
            onChanged(level_);
    };
    addAndMakeVisible(*switch_);
}

DepthPicker::~DepthPicker() = default;

void DepthPicker::setDepthLevel(DepthLevel level) {
    level_ = level;
    switch_->setSelectedIndex((int)level, juce::dontSendNotification);
}

void DepthPicker::setTheme(const synth::theme::Theme& theme) {
    caption_.setColour(juce::Label::textColourId, theme.colors.textPrimary);
    repaint();
}

void DepthPicker::resized() {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromLeft(kCaptionWidth));
    switch_->setBounds(area.removeFromLeft(kSegmentWidth * synth::theme::kDepthLevelCount));
}

void DepthPicker::addTestModeState(juce::DynamicObject& into) const {
    into.setProperty("depth", juce::String(synth::theme::depthLevelId(level_)));
    into.setProperty("selected", (int)level_);
}

} // namespace synth::ui
