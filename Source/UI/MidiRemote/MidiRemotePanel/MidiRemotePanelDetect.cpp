// Concern: FRO134 -- the panel's Detect mode and the Inspector's encoder auto-detect
// (docs/control/midi-remote-ui.md#detect-mode, #inspector-right). Both are fed from the ONE
// activity drain in MidiRemotePanelComponent::refreshActivity(); this unit holds what happens
// around it: the toggle, persisting what Detect found, the pulse, and the two-step prompt.
#include "MidiRemote/ControllerDetect.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemotePanelComponent.h"

namespace synth::ui {

void MidiRemotePanelComponent::setDetectActive(bool active) {
    if (active && findSelectedProfile() == nullptr)
        active = false;
    detect_.setActive(active);
    toolbar_.setDetectOn(active);
    if (!active)
        controllerSurface_.setDetectPulseControlId({});
    resized(); // the hint row changes the toolbar's height
}

// Detect's persistence and feedback, after the drain. The new controls are written with ONE
// updateProfile (which republishes the engine's snapshot and reaches the deferred live refresh),
// then the grid is rebuilt right here -- not from a cell's mouse stack -- and the drain's events
// replayed onto the fresh cells, because a rebuilt cell starts at rest and would otherwise show 0
// for the very control the user just touched.
void MidiRemotePanelComponent::commitDetectStep(const std::optional<synth::ControllerProfile>& working,
                                                bool profileChanged, const std::vector<juce::String>& litControlIds,
                                                const std::vector<synth::midi::RemoteEvent>& events) {
    if (profileChanged && working.has_value() && learnController_ != nullptr) {
        learnController_->updateProfile(*working);
        refreshSurfaceForSelectedProfile();
        controllerSurface_.setSelectedControlId(selectedControlId_);
        for (const auto& event : events)
            if (const auto* control = synth::midi::findControlForEvent(working->controls, event))
                controllerSurface_.noteActivity(control->id, event.kind, event.value);
    }

    controllerSurface_.setDetectPulseControlId(detect_.getPulsingControlId());
    for (const auto& id : litControlIds)
        controllerSurface_.flashControl(id);
    controllerSurface_.tickDetectHighlights();
}

// ---- Encoder auto-detect ---------------------------------------------------------------------------

void MidiRemotePanelComponent::showPrompt(const juce::String& title, const juce::String& message, bool cancellable,
                                          std::function<void(bool ok)> done) {
    if (promptHook_) {
        promptHook_(title, message, cancellable, std::move(done));
        return;
    }
    // Async on purpose: a modal loop would stop the dock's activity tick, and auto-detect needs the
    // events that arrive WHILE its prompt is up.
    const auto options =
        cancellable
            ? juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::QuestionIcon, title, message,
                                                           "Next", "Cancel", this)
            : juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::InfoIcon, title, message, "OK", this);
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, done = std::move(done)](int result) {
        if (safeThis != nullptr && done)
            done(result == 1);
    });
}

void MidiRemotePanelComponent::beginEncoderAutoDetect(const synth::Control& control) {
    if (findSelectedProfile() == nullptr)
        return;
    encoderTargetControlId_ = control.id;
    encoderDetect_.start(control.message);
    promptEncoderStep();
}

void MidiRemotePanelComponent::promptEncoderStep() {
    using Phase = synth::midi::EncoderAutoDetect::Phase;
    const bool left = encoderDetect_.phase() == Phase::turnLeft;
    showPrompt("Auto-detect encoder",
               left ? "Turn the control to the left a few clicks, then press Next."
                    : "Now turn it to the right a few clicks, then press Next.",
               true, [this](bool ok) {
                   if (!ok) {
                       encoderDetect_.cancel();
                       return;
                   }
                   advanceEncoderAutoDetect();
               });
}

void MidiRemotePanelComponent::advanceEncoderAutoDetect() {
    using Phase = synth::midi::EncoderAutoDetect::Phase;
    switch (encoderDetect_.advance()) {
    case Phase::turnRight:
        promptEncoderStep();
        break;
    case Phase::done:
    case Phase::undetermined:
        finishEncoderAutoDetect();
        break;
    default:
        break;
    }
}

// The result is applied through the same updateControl the Inspector's own combo uses. A relative
// result on a plain knob also retypes it as an encoder -- that is what the hardware is.
void MidiRemotePanelComponent::finishEncoderAutoDetect() {
    const auto result = encoderDetect_.result();
    const auto controlId = encoderTargetControlId_;
    encoderDetect_.cancel();

    if (!result.has_value()) {
        showPrompt("Auto-detect encoder",
                   "Couldn't tell how this control encodes. Try again, turning it a few clicks each way.", false, {});
        return;
    }

    const auto* profile = findSelectedProfile();
    if (profile == nullptr || learnController_ == nullptr)
        return;
    const auto it = std::find_if(profile->controls.begin(), profile->controls.end(),
                                 [&](const synth::Control& c) { return c.id == controlId; });
    if (it == profile->controls.end())
        return;

    synth::Control edited = *it;
    edited.encoding = *result;
    if (*result != synth::Encoding::abs7 && edited.kind == synth::ControlKind::knob)
        edited.kind = synth::ControlKind::encoder;
    handleControlEdited(edited);
}

} // namespace synth::ui
