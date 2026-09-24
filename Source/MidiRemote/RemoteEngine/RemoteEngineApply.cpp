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
    else if (slot.target.isNodeCommand())
        applyToNodeCommand(slot, event);
    else
        applyToParameter(slot, event);
}

void RemoteEngine::applyToAction(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event) {
    // docs/control/midi-remote.md#action-targets: momentary and toggle alike fire the action on press only.
    if (event.kind != RemoteEventKind::buttonPress)
        return;
    if (actionInvoker_ == nullptr || slot.commandId == 0)
        return;
    actionInvoker_->invokeRemoteCommand(slot.commandId);
}

// FRO253 (docs/control/midi-remote.md#node-command-targets): same "press only, momentary and
// toggle alike" rule as applyToAction above -- a pad press toggles solo, like a mouse click;
// hold-to-solo is not built.
void RemoteEngine::applyToNodeCommand(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event) {
    if (event.kind != RemoteEventKind::buttonPress)
        return;
    if (slot.orphaned || actionInvoker_ == nullptr)
        return;
    actionInvoker_->invokeNodeCommand(slot.nodeId, slot.target.nodeCommand.command);
}

void RemoteEngine::applyToParameter(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event) {
    if (slot.orphaned || slot.param == nullptr)
        return;

    // FRO139 (docs/control/midi-remote.md#controller-feedback): every real hardware event on this
    // control counts, even one a claim takeover below is about to reject -- a controller that just
    // sent something is a controller the drain must not immediately echo a stale value back to.
    {
        FeedbackState& fb = feedback_[slot.assignmentId];
        fb.hasHardware = true;
        fb.lastHardwareMs = clock_();
    }

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
