// Message thread. Applies a decoded RemoteEvent exactly as a mouse would: beginChangeGesture /
// setValueNotifyingHost / endChangeGesture (docs/control/midi-remote.md#how-does-a-hardware-value-reach-a-parameter),
// or invokes an action command on press. This is where takeover (jump/pick-up/scale) and the 250 ms gesture-idle rule
// live.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteEngine/RemoteEngineInternal.h"

namespace synth::midi {

void RemoteEngine::applyEvent(const RemoteMappingSnapshot& snapshot, const RemoteEvent& event) {
    if (event.slotIndex < 0 || static_cast<std::size_t>(event.slotIndex) >= snapshot.slots.size())
        return;

    const auto& slot = snapshot.slots[static_cast<std::size_t>(event.slotIndex)];
    if (slot.target.isAction())
        applyToAction(slot, event);
    else
        applyToParameter(slot, event);
}

void RemoteEngine::applyToAction(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event) {
    // doc §4.9: momentary and toggle alike fire the action on press only.
    if (event.kind != RemoteEventKind::buttonPress)
        return;
    if (actionInvoker_ == nullptr || slot.commandId == 0)
        return;
    actionInvoker_->invokeRemoteCommand(slot.commandId);
}

void RemoteEngine::applyToParameter(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event) {
    if (slot.orphaned || slot.param == nullptr)
        return;

    const auto existingIt = gestures_.find(slot.assignmentId);
    const bool hasActiveGesture = existingIt != gestures_.end() && existingIt->second.gestureActive;
    if (isClaimedByOther_ && !hasActiveGesture && isClaimedByOther_(slot.param))
        return; // a real mouse wins (docs/control/midi-remote.md#how-does-a-hardware-value-reach-a-parameter)

    const bool isNewGesture = existingIt == gestures_.end();
    GestureState& state = gestures_[slot.assignmentId];

    // A button target on a bool parameter is begin+set+end in one go -- never a lingering 250 ms
    // gesture (momentary sets 1.0/0.0 on press/release; toggle flips on press only).
    if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(slot.param)) {
        if (event.kind != RemoteEventKind::buttonPress && event.kind != RemoteEventKind::buttonRelease)
            return;

        float target;
        if (slot.buttonMode == ButtonMode::toggle) {
            if (event.kind != RemoteEventKind::buttonPress)
                return; // toggle flips on press only
            state.toggleOn = !state.toggleOn;
            target = state.toggleOn ? 1.0f : 0.0f;
        } else {
            target = event.kind == RemoteEventKind::buttonPress ? 1.0f : 0.0f;
        }

        boolParam->beginChangeGesture();
        boolParam->setValueNotifyingHost(target);
        boolParam->endChangeGesture();
        state.lastEventMs = clock_();
        return;
    }

    if (event.kind == RemoteEventKind::buttonPress || event.kind == RemoteEventKind::buttonRelease)
        return; // no defined behaviour for a button target on a non-bool parameter

    float target = 0.0f;
    bool shouldApply = true;

    if (event.kind == RemoteEventKind::relativeDelta) {
        // Relative encodings bypass takeover entirely.
        target = juce::jlimit(0.0f, 1.0f, slot.param->getValue() + event.value);
    } else {
        const float hw = detail::mapThroughRange(event.value, slot.rangeMin, slot.rangeMax);
        const float cur = slot.param->getValue();
        const Takeover resolved = slot.takeover == Takeover::useDefault ? defaultTakeover_ : slot.takeover;

        target = hw;
        if (resolved == Takeover::pickup) {
            if (state.takeoverEngaged) {
                // target already hw
            } else if (isNewGesture) {
                shouldApply = false;
            } else if (detail::pickupHasCrossed(state.lastValue, cur, hw)) {
                state.takeoverEngaged = true;
            } else {
                shouldApply = false;
            }
        } else if (resolved != Takeover::jump) {
            // Scale (and the defensive useDefault fallback, which should never resolve here since
            // defaultTakeover_ is never itself useDefault).
            if (isNewGesture)
                shouldApply = false;
            else
                target = detail::scaleTarget(cur, state.lastValue, hw);
        }
        state.lastValue = hw; // always stored, even on a "first event, apply nothing" branch
    }

    if (!shouldApply)
        return;

    if (!state.gestureActive) {
        slot.param->beginChangeGesture();
        state.gestureActive = true;
        state.param = slot.param;
    }
    slot.param->setValueNotifyingHost(target);
    state.lastEventMs = clock_();
}

void RemoteEngine::expireIdleGestures() {
    const double now = clock_();
    for (auto it = gestures_.begin(); it != gestures_.end();) {
        if (it->second.gestureActive && now - it->second.lastEventMs >= kGestureIdleMs) {
            if (it->second.param != nullptr)
                it->second.param->endChangeGesture();
            it = gestures_.erase(it);
        } else {
            ++it;
        }
    }
}

void RemoteEngine::endAllGestures() {
    for (auto& entry : gestures_)
        if (entry.second.gestureActive && entry.second.param != nullptr)
            entry.second.param->endChangeGesture();
    gestures_.clear();
}

} // namespace synth::midi
