#pragma once

#include "CardStepper.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace synth::ui {

/**
 * Binds a number field to an integer parameter through `attachment` (a plain ParameterAttachment on `param`): a
 * click, a hold, a drag or a key is one gesture, so one undo step; a typed value is one complete gesture. The field's
 * range, default and widest text come from the parameter. `alive` (optional) is cleared when the owner goes, so a
 * late typed value is dropped.
 */
inline void bindStepperToParameter(CardStepper& stepper, juce::ParameterAttachment& attachment,
                                   juce::AudioParameterInt& param, std::shared_ptr<bool> alive = nullptr) {
    const auto range = param.getRange();
    const juce::RangedAudioParameter& base = param; // getDefaultValue and getText are private on AudioParameterInt
    stepper.setRange(range.getStart(), range.getEnd(), juce::roundToInt(base.convertFrom0to1(base.getDefaultValue())));
    const auto lowText = base.getText(0.0f, 0), highText = base.getText(1.0f, 0);
    stepper.setWidestValueText(highText.length() >= lowText.length() ? highText : lowText);
    stepper.onGestureBegin = [&attachment] { attachment.beginGesture(); };
    stepper.onStep = [&attachment, &param](int delta) {
        const auto r = param.getRange();
        attachment.setValueAsPartOfGesture((float)juce::jlimit(r.getStart(), r.getEnd(), param.get() + delta));
    };
    stepper.onGestureEnd = [&attachment] { attachment.endGesture(); };
    stepper.setTypable(true);
    stepper.onTypedValue = [&stepper, &attachment, &param, alive](const juce::String& text) {
        if (alive != nullptr && !*alive)
            return;
        const auto r = param.getRange();
        const int typed = juce::jlimit(r.getStart(), r.getEnd(), text.getIntValue());
        if (text.containsAnyOf("0123456789") && typed != param.get())
            attachment.setValueAsCompleteGesture((float)typed);
        stepper.setValue(param.get());
        stepper.setValueText(param.getCurrentValueAsText());
    };
}

} // namespace synth::ui
