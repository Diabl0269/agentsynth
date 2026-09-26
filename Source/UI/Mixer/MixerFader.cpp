// Concern: FRO11 (P9-5) -- MixerFader's slider wiring, undo bracket and dB readout. FRO150 added
// the taper-based NormalisableRange and the onDragStart/onDragEnd gesture wiring in bind() below.
#include "MixerFader.h"

#include "AppUndoManager.h"
#include "MixerDbAccessibilityText.h"
#include "MixerFaderTaper.h"
#include <cmath>

namespace synth::ui {

int MixerFader::liveUnbindCallCountForTest_ = 0;

MixerFader::MixerFader() {
    slider_.setSliderStyle(juce::Slider::LinearVertical);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    // FRO18: the panel is the single focusable leaf (MixerPanelComponent::keyPressed) -- a
    // focused Slider would otherwise eat Up/Down before the panel ever saw them (the T160 trap
    // docs/control/shortcuts.md documents).
    slider_.setWantsKeyboardFocus(false);
    applyDbAccessibilityText(slider_);
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

    // FRO150: juce::SliderParameterAttachment's OWN constructor calls slider.setNormalisableRange()
    // (built from the param's own linear mapping) and overwrites slider.textFromValueFunction --
    // so attachment_ must be constructed FIRST, and our own taper range/accessibility text
    // installed AFTER, or the attachment silently undoes both. setNormalisableRange() only ever
    // changes how a value maps to a 0..1 proportion, never the slider's CURRENT value itself
    // (juce::Slider::Pimpl::setNormalisableRange is exactly `normRange = newRange`), so swapping
    // the range here is safe even though attachment_'s constructor already pushed the param's
    // initial value into the slider under its own (temporary) range.
    attachment_ = std::make_unique<juce::SliderParameterAttachment>(param, slider_);

    // The slider's VALUE stays the param's own dB range (start/end/interval copied field-for-field,
    // same as before this ticket) -- only convertTo0to1/convertFrom0to1 change, to
    // faderDbToFraction/faderFractionToDb (MixerFaderTaper.h), so the on-screen THUMB POSITION
    // follows Cubase's taper while getValue()/setValue() (and therefore the bound parameter) never
    // leave linear dB. snapToLegalValue keeps the pre-existing 0.1 dB granularity, just re-expressed
    // as a lambda since the range's own `interval` field is ignored once a custom
    // snapToLegalValueFunction is supplied (juce::NormalisableRange::snapToLegalValue).
    const auto floatRange = param.getNormalisableRange();
    const double rangeStartDb = (double)floatRange.start;
    const double rangeEndDb = (double)floatRange.end;
    const double intervalDb = (double)floatRange.interval;
    slider_.setNormalisableRange(juce::NormalisableRange<double>(
        rangeStartDb, rangeEndDb,
        [](double, double, double proportion) { return (double)faderFractionToDb((float)proportion); },
        [](double, double, double valueDb) { return (double)faderDbToFraction((float)valueDb); },
        [intervalDb](double start, double end, double valueDb) {
            double snapped = valueDb;
            if (intervalDb > 0.0)
                snapped = start + std::round((valueDb - start) / intervalDb) * intervalDb;
            return juce::jlimit(start, end, snapped);
        }));
    applyDbAccessibilityText(slider_);

    // FRO150: every gesture MixerFaderSlider starts (drag, Cmd-click/double-click reset, a wheel
    // step) calls these directly -- see MixerFaderSlider.h's class comment for why it can't rely on
    // juce::SliderParameterAttachment's own Slider::Listener gesture notifications the way a stock
    // Slider drag would.
    slider_.onDragStart = [this] {
        if (param_ != nullptr)
            param_->beginChangeGesture();
    };
    slider_.onDragEnd = [this] {
        if (param_ != nullptr)
            param_->endChangeGesture();
    };
    param_->addListener(this);
    readout_.setText(juce::String(param_->get(), 1) + " dB", juce::dontSendNotification);
}

void MixerFader::unbind() {
    if (param_ != nullptr) {
        ++liveUnbindCallCountForTest_;
        param_->removeListener(this);
    }
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

bool MixerFader::nudge(float deltaDb) {
    if (param_ == nullptr)
        return false;
    const auto range = param_->getNormalisableRange();
    const float target = juce::jlimit(range.start, range.end, param_->get() + deltaDb);
    // Same bracket a real slider drag produces (juce::SliderParameterAttachment calls these two
    // around every drag) -- parameterGestureChanged (already listening, see the class comment)
    // brackets exactly one captureBeforeState()/pushSnapshotFromCapture() pair around them, so a
    // key nudge costs one undo step, same as a mouse drag.
    param_->beginChangeGesture();
    param_->setValueNotifyingHost(range.convertTo0to1(target));
    param_->endChangeGesture();
    return true;
}

void MixerFader::setChannelName(const juce::String& name) {
    slider_.setTitle(name.isEmpty() ? juce::String("Fader") : name + " fader");
}

void MixerFader::grabAccessibilityFocus() {
    if (auto* handler = slider_.getAccessibilityHandler())
        handler->grabFocus();
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
