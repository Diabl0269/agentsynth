// Message thread. RemoteEngine::drain()'s feedback pass (FRO139,
// docs/control/midi-remote.md#controller-feedback): for every parameter slot whose value has moved
// since the last drain, encode it back into the mapped control's own message shape and hand it to
// the app-layer RemoteFeedbackSink.
//
// WHY POLLING IN THE DRAIN, NOT A PARAMETER LISTENER. A mapped parameter can change from four
// places -- a mouse drag, automation playback (AutomationApplier moves it with a bare setValue(),
// which notifies no juce::AudioProcessorParameter::Listener at all), undo/redo, and project load --
// and the drain already runs on the message thread at kDrainHz for the apply path, so reading
// param->getValue() there once per assignment costs nothing extra and needs no new listener
// bookkeeping (no listener to add/remove as slots come and go across every reconcile). It also can
// never re-enter apply: this only ever calls RemoteFeedbackSink::sendFeedback(), never
// beginChangeGesture/setValueNotifyingHost, so a controller's own LED update can't loop back into a
// second hardware-looking event.
//
// WHY THE COOLDOWN. A motor fader or an LED ring that receives an echo of the exact value it just
// sent, WHILE the user is still turning it, visibly hunts (the motor fights the hand, or the ring
// flickers a frame behind the finger). kFeedbackCooldownMs (== kGestureIdleMs) treats "a hardware
// event arrived recently" as "the user is still touching this control" and holds off; sending the
// FINAL value once that window closes is exactly what re-syncs a pick-up/scale takeover's own
// picture of where the parameter really is, and what a motor fader needs to snap to on release.
//
// WHY CHANNEL 0 -> 1. A MessageSpec's channel 0 means "any channel" for the INCOMING lookup
// (RemoteMappingSnapshot::findSlot's fallback) -- it is not a real MIDI channel a message can be
// sent ON. Feedback always needs one real channel, so 0 becomes 1, same convention as "channel 1"
// being where an unconfigured control's messages arrive.
//
// The plugin build never sees this at all: MainComponent only calls setFeedbackSink() when
// !audioEngine.isHosted() (Source/MainComponent/MainComponentSetup.cpp), because a hosted plugin
// has no MIDI output of its own to send through -- feedbackSink_ stays null there and every slot in
// this loop is skipped at the very first check.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

namespace synth::midi {

namespace {

// Inverse of RemoteEngineApply.cpp's detail::mapThroughRange: the parameter's current normalised
// value, mapped back through [rangeMin, rangeMax] to the 0..1 hardware-facing value the assignment's
// range implies. rangeMin == rangeMax collapses the whole range to one hardware value -- 0 is as
// good a convention as any and matches mapThroughRange's own clamp-to-0 behaviour at that
// degenerate point. rangeMin > rangeMax (an inverted assignment) falls out of the same formula with
// no special case, exactly as the doc comment on mapThroughRange promises for the forward direction.
float inverseMapThroughRange(float paramValue, double rangeMin, double rangeMax) noexcept {
    if (rangeMax == rangeMin)
        return 0.0f;
    const double x = (static_cast<double>(paramValue) - rangeMin) / (rangeMax - rangeMin);
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(x));
}

const ControllerProfile* findProfileWithOutput(const std::vector<ControllerProfile>& profiles,
                                               const juce::String& profileId) {
    for (const auto& profile : profiles) {
        if (profile.id != profileId)
            continue;
        return profile.hasOutput ? &profile : nullptr;
    }
    return nullptr;
}

int encodeFeedbackValue(const RemoteMappingSnapshot::Slot& slot, float x) {
    switch (slot.spec.type) {
    case MessageType::pitchBend:
        return juce::roundToInt(x * 16383.0f);
    case MessageType::note: {
        const bool discreteOnOff = slot.buttonLike || dynamic_cast<juce::AudioParameterBool*>(slot.param) != nullptr;
        if (discreteOnOff)
            return x >= 0.5f ? 127 : 0;
        return juce::roundToInt(x * 127.0f);
    }
    case MessageType::cc:
    default:
        return juce::roundToInt(x * 127.0f);
    }
}

juce::MidiMessage buildFeedbackMessage(const RemoteMappingSnapshot::Slot& slot, int channel, int encoded) {
    switch (slot.spec.type) {
    case MessageType::pitchBend:
        return juce::MidiMessage::pitchWheel(channel, encoded);
    case MessageType::note:
        // Velocity 0 is the standard note-off spelling a note-on carries -- LED rings and pad
        // lights that key off note messages expect exactly this, not a separate noteOff() call.
        return juce::MidiMessage::noteOn(channel, slot.spec.number, static_cast<juce::uint8>(encoded));
    case MessageType::cc:
    default:
        return juce::MidiMessage::controllerEvent(channel, slot.spec.number, encoded);
    }
}

} // namespace

void RemoteEngine::sendFeedback(const RemoteMappingSnapshot& snapshot) {
    if (feedbackSink_ == nullptr)
        return;

    const double now = clock_();

    for (const auto& slot : snapshot.slots) {
        if (!slot.target.isParameter() || slot.orphaned || slot.param == nullptr)
            continue;
        if (slot.spec.type != MessageType::cc && slot.spec.type != MessageType::note &&
            slot.spec.type != MessageType::pitchBend)
            continue;

        const ControllerProfile* profile = findProfileWithOutput(profiles_, slot.profileId);
        if (profile == nullptr)
            continue;

        FeedbackState& fb = feedback_[slot.assignmentId];
        if (fb.hasHardware && now - fb.lastHardwareMs < kFeedbackCooldownMs)
            continue; // still being touched -- a motor fader/LED ring would hunt on its own echo

        const float x = inverseMapThroughRange(slot.param->getValue(), slot.rangeMin, slot.rangeMax);
        const int encoded = encodeFeedbackValue(slot, x);
        if (encoded == fb.lastSent)
            continue;

        const int channel = slot.spec.channel == 0 ? 1 : slot.spec.channel;
        feedbackSink_->sendFeedback(profile->output, buildFeedbackMessage(slot, channel, encoded));
        fb.lastSent = encoded;
    }
}

} // namespace synth::midi
