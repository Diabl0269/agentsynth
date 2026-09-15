// Concern: FRO11 (P9-5) -- MixerFader's slider wiring, undo bracket and dB readout.
#include "MixerFader.h"

#include "AppUndoManager.h"

namespace synth::ui {

MixerFader::MixerFader() {
    slider_.setSliderStyle(juce::Slider::LinearVertical);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(slider_);

    readout_.setJustificationType(juce::Justification::centred);
    readout_.setFont(juce::Font(juce::FontOptions(10.0f)));
    addAndMakeVisible(readout_);
}

MixerFader::~MixerFader() { unbind(); }

void MixerFader::bind(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, juce::AudioParameterFloat& param) {
    unbind();
    graph_ = &graph;
    undoManager_ = &undoManager;
    param_ = &param;

    // Slider position <-> dB is exactly the param's own mapping -- no second formula to drift out
    // of sync with it (plan (c)). juce::Slider wants NormalisableRange<double>; the param's own is
    // <float> -- converted field-for-field, not re-derived.
    const auto floatRange = param.getNormalisableRange();
    slider_.setNormalisableRange(juce::NormalisableRange<double>((double)floatRange.start, (double)floatRange.end,
                                                                 (double)floatRange.interval, (double)floatRange.skew,
                                                                 floatRange.symmetricSkew));
    attachment_ = std::make_unique<juce::SliderParameterAttachment>(param, slider_);
    param_->addListener(this);
    readout_.setText(juce::String(param_->get(), 1) + " dB", juce::dontSendNotification);
}

void MixerFader::unbind() {
    if (param_ != nullptr)
        param_->removeListener(this);
    attachment_.reset();
    param_ = nullptr;
    graph_ = nullptr;
    undoManager_ = nullptr;
    gestureActive_ = false;
}

void MixerFader::parameterValueChanged(int, float) {
    if (param_ == nullptr)
        return;
    // AudioProcessorParameter::Listener callbacks can land on any thread that writes the
    // parameter (a fader drag is message-thread, but stay safe generically); the label update is
    // message-thread-only UI work. SafePointer, not a raw `this` -- unbind()/destruction does not
    // cancel an already-queued callback (same pattern as every other callAsync call site in this
    // codebase, e.g. ModuleComponentInteraction.cpp), and this fader can be torn down (a graph
    // rebuild from undo/redo, or MixerPanelComponent::rebuild()) before the queued message runs.
    juce::Component::SafePointer<MixerFader> safeThis(this);
    juce::MessageManager::callAsync([safeThis, db = param_->get()] {
        if (safeThis != nullptr)
            safeThis->readout_.setText(juce::String(db, 1) + " dB", juce::dontSendNotification);
    });
}

void MixerFader::parameterGestureChanged(int, bool gestureIsStarting) {
    if (graph_ == nullptr || undoManager_ == nullptr)
        return;
    if (gestureIsStarting) {
        gestureActive_ = true;
        undoManager_->captureBeforeState(*graph_);
    } else if (gestureActive_) {
        gestureActive_ = false;
        undoManager_->pushSnapshotFromCapture(*graph_);
    }
}

void MixerFader::resized() {
    auto bounds = getLocalBounds();
    readout_.setBounds(bounds.removeFromBottom(16));
    slider_.setBounds(bounds);
}

} // namespace synth::ui
