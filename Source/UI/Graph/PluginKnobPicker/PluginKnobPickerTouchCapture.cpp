// PluginKnobPickerTouchCapture.cpp -- gesture-only "touch to add" (v1; see the header's FRO241 note).
#include "PluginKnobPickerTouchCapture.h"

namespace synth::ui {

PluginKnobPickerTouchCapture::PluginKnobPickerTouchCapture(HostedPluginModule& module)
    : module_(module) {}

PluginKnobPickerTouchCapture::~PluginKnobPickerTouchCapture() { setArmed(false); }

// Message thread. Iterates getParameters() on the CURRENT instance only -- if the instance is
// replaced while armed, the new instance's parameters are never listened to (a known v1 limitation;
// the picker popover is short-lived enough that this has not been worth solving yet). Disarming
// cancels any update already queued, so a gesture reported just before the checkbox is unticked can
// never fire after this call returns.
void PluginKnobPickerTouchCapture::setArmed(bool armed) {
    if (armed == armed_)
        return;
    armed_ = armed;

    if (auto* instance = module_.getActiveInstanceForEditor())
        for (auto* param : instance->getParameters()) {
            if (armed)
                param->addListener(this);
            else
                param->removeListener(this);
        }

    if (!armed) {
        cancelPendingUpdate();
        const juce::ScopedLock sl(queueLock_);
        pendingIndices_.clear();
    } else if (onRequestOpenEditor) {
        onRequestOpenEditor();
    }
}

// ANY thread. Only a gesture START adds a parameter -- the end is not interesting to "touch to add".
// Queues under a lock (this is a UI/editor-driven gesture, not an audio-thread callback, so a small
// CriticalSection here is fine) and hops via AsyncUpdater; never touches onParameterTouched directly.
void PluginKnobPickerTouchCapture::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    if (!gestureIsStarting)
        return;
    {
        const juce::ScopedLock sl(queueLock_);
        pendingIndices_.push_back(parameterIndex);
    }
    triggerAsyncUpdate();
}

// Message thread. Drains whatever queued since the last run and reports each index in order; a
// parameter touched twice before this ran is reported twice, which the owner (a set-membership
// check before it adds a slot) already treats as a no-op the second time.
void PluginKnobPickerTouchCapture::handleAsyncUpdate() {
    std::vector<int> indices;
    {
        const juce::ScopedLock sl(queueLock_);
        indices.swap(pendingIndices_);
    }
    for (int index : indices)
        if (onParameterTouched)
            onParameterTouched(index);
}

} // namespace synth::ui
