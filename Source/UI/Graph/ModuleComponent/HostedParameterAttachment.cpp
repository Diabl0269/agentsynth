// HostedParameterAttachment.cpp -- the two-way binding between one hosted-plugin parameter and one card widget.
// Shaped like juce::ParameterAttachment + SliderParameterAttachment (juce_ParameterAttachments.cpp), which
// cannot be used here because a hosted parameter is not a RangedAudioParameter.
// See docs/control/plugin-card-layout.md#card-rendering-as-built-fro128.
#include "HostedParameterAttachment.h"
#include <cmath>

namespace synth::ui {

namespace {
// The most steps a parameter with no value strings of its own is still offered as a choice list.
constexpr int kMaxGeneratedChoices = 64;

// Not 0: a real format's getText (the VST3 wrapper's) truncates to maximumLength, so 0 would yield "".
constexpr int kTextLength = 1024;

int indexForValue(float normalized, int count) {
    if (count < 2)
        return 0;
    return juce::jlimit(0, count - 1, juce::roundToInt(normalized * static_cast<float>(count - 1)));
}

float valueForIndex(int index, int count) {
    return count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
}
} // namespace

juce::StringArray HostedParameterAttachment::choiceStrings(const juce::AudioProcessorParameter& param) {
    auto strings = param.getAllValueStrings();
    if (strings.size() > 0)
        return strings;

    // A slot forced to Choice on a parameter with no value strings: offer its steps when there are few enough.
    const int steps = param.getNumSteps();
    if (steps < 2 || steps > kMaxGeneratedChoices)
        return {};
    for (int i = 0; i < steps; ++i)
        strings.add(param.getText(valueForIndex(i, steps), kTextLength));
    return strings;
}

// ---- Construction / teardown ------------------------------------------------------------------------------

HostedParameterAttachment::HostedParameterAttachment(juce::AudioProcessorParameter& param, juce::Slider& slider)
    : kind_(Kind::Slider)
    , param_(&param)
    , slider_(&slider) {
    slider.setNormalisableRange(juce::NormalisableRange<double>(0.0, 1.0));
    slider.valueFromTextFunction = [this](const juce::String& text) {
        return param_ != nullptr ? static_cast<double>(param_->getValueForText(text)) : text.getDoubleValue();
    };
    slider.textFromValueFunction = [this](double value) {
        return param_ != nullptr ? param_->getText(static_cast<float>(value), kTextLength) : juce::String(value, 2);
    };
    slider.setDoubleClickReturnValue(true, static_cast<double>(param.getDefaultValue()));
    bind();
    slider.addListener(this);
}

HostedParameterAttachment::HostedParameterAttachment(juce::AudioProcessorParameter& param, juce::ToggleButton& toggle)
    : kind_(Kind::Toggle)
    , param_(&param)
    , toggle_(&toggle) {
    bind();
    toggle.addListener(this);
}

HostedParameterAttachment::HostedParameterAttachment(juce::AudioProcessorParameter& param, juce::ComboBox& combo)
    : kind_(Kind::Combo)
    , param_(&param)
    , combo_(&combo) {
    const auto strings = choiceStrings(param);
    comboCount_ = strings.size();
    combo.clear(juce::dontSendNotification);
    combo.addItemList(strings, 1);
    bind();
    combo.addListener(this);
}

// The widget always outlives the attachment (the card destroys attachments first), so it is safe to unhook
// from it here; the parameter side is only touched while param_ is still set.
HostedParameterAttachment::~HostedParameterAttachment() {
    detach();
    if (slider_ != nullptr) {
        slider_->removeListener(this);
        slider_->textFromValueFunction = nullptr; // the lambdas capture `this`
        slider_->valueFromTextFunction = nullptr;
    }
    if (toggle_ != nullptr)
        toggle_->removeListener(this);
    if (combo_ != nullptr)
        combo_->removeListener(this);
}

void HostedParameterAttachment::bind() {
    param_->addListener(this);
    showValue(param_->getValue());
}

// A gesture our own widget opened is closed first, on the still-valid parameter: a hosted parameter asserts
// in Debug if it is destroyed mid-gesture, and the card's undo bookkeeping needs the matching "end".
void HostedParameterAttachment::detach() {
    cancelPendingUpdate();
    if (param_ == nullptr)
        return;

    closeGesture();
    param_->removeListener(this);
    param_ = nullptr;
}

// For a parameter that may already be freed: nothing below may dereference it.
void HostedParameterAttachment::abandon() {
    cancelPendingUpdate();
    gestureOpen_ = false;
    param_ = nullptr;
}

// ---- Parameter -> widget ----------------------------------------------------------------------------------

// ANY thread, and the audio thread for automation: store and hop, nothing else -- exactly the
// HostedPluginModule::InstanceListener idiom. triggerAsyncUpdate() coalesces and does not allocate. The
// widget is only ever touched from handleAsyncUpdate(), even when this fires on the message thread, so a
// value change is never applied re-entrantly inside the caller's own setValueNotifyingHost.
void HostedParameterAttachment::parameterValueChanged(int, float newValue) {
    pendingValue_.store(newValue, std::memory_order_relaxed);
    triggerAsyncUpdate();
}

// ANY thread reports a gesture, but onGestureChanged is plain message-thread state (the card assigns it
// while it rebuilds), so a report from another thread is dropped here BEFORE the std::function is read. The
// only consumer is undo bookkeeping, which only the message thread can do anyway.
void HostedParameterAttachment::parameterGestureChanged(int, bool starting) {
    if (juce::MessageManager::existsAndIsCurrentThread() && onGestureChanged)
        onGestureChanged(starting);
}

void HostedParameterAttachment::handleAsyncUpdate() {
    if (param_ != nullptr)
        showValue(pendingValue_.load(std::memory_order_relaxed));
}

// Widget update, never a write back: the guard stops a widget that notifies regardless of the flag it is
// given from echoing the value into the parameter it just came from.
void HostedParameterAttachment::showValue(float normalized) {
    const juce::ScopedValueSetter<bool> guard(ignoreCallbacks_, true);
    switch (kind_) {
    case Kind::Slider:
        slider_->setValue(static_cast<double>(normalized), juce::dontSendNotification);
        break;
    case Kind::Toggle:
        toggle_->setToggleState(normalized >= 0.5f, juce::dontSendNotification);
        break;
    case Kind::Combo:
        combo_->setSelectedItemIndex(indexForValue(normalized, comboCount_), juce::dontSendNotification);
        break;
    }
}

// ---- Widget -> parameter ----------------------------------------------------------------------------------

void HostedParameterAttachment::openGesture() {
    if (param_ == nullptr || gestureOpen_)
        return;
    gestureOpen_ = true;
    param_->beginChangeGesture();
}

void HostedParameterAttachment::closeGesture() {
    if (param_ == nullptr || !gestureOpen_)
        return;
    gestureOpen_ = false;
    param_->endChangeGesture();
}

// A change that is not part of a drag (typed text, a click, a wheel notch) is its own begin/end pair, so
// one user action is always one gesture and, through the card's bookkeeping, one undo step.
void HostedParameterAttachment::writeComplete(float normalized) {
    if (param_ == nullptr || juce::approximatelyEqual(param_->getValue(), normalized))
        return;
    openGesture();
    param_->setValueNotifyingHost(normalized);
    closeGesture();
}

void HostedParameterAttachment::sliderDragStarted(juce::Slider*) { openGesture(); }
void HostedParameterAttachment::sliderDragEnded(juce::Slider*) { closeGesture(); }

void HostedParameterAttachment::sliderValueChanged(juce::Slider*) {
    if (ignoreCallbacks_ || param_ == nullptr)
        return;

    const auto value = static_cast<float>(slider_->getValue());
    if (gestureOpen_) {
        if (!juce::approximatelyEqual(param_->getValue(), value))
            param_->setValueNotifyingHost(value);
    } else {
        writeComplete(value);
    }
}

void HostedParameterAttachment::comboBoxChanged(juce::ComboBox*) {
    if (ignoreCallbacks_ || combo_->getSelectedItemIndex() < 0)
        return;
    writeComplete(valueForIndex(combo_->getSelectedItemIndex(), comboCount_));
}

void HostedParameterAttachment::buttonClicked(juce::Button*) {
    if (ignoreCallbacks_)
        return;
    writeComplete(toggle_->getToggleState() ? 1.0f : 0.0f);
}

} // namespace synth::ui
