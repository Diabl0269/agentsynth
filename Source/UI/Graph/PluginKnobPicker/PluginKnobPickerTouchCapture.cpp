// PluginKnobPickerTouchCapture.cpp -- gesture (FRO132) + value-change fallback (FRO241) "touch to
// add". See the header's class comment for the two-signal / any-thread contract.
#include "PluginKnobPickerTouchCapture.h"
#include <algorithm>
#include <utility>

namespace synth::ui {

PluginKnobPickerTouchCapture::PluginKnobPickerTouchCapture(HostedPluginModule& module)
    : module_(module) {}

PluginKnobPickerTouchCapture::~PluginKnobPickerTouchCapture() { setArmed(false); }

// Message thread. Iterates getParameters() on the CURRENT instance only -- if the instance is
// replaced while armed, the new instance's parameters are never listened to (a known v1 limitation;
// the picker popover is short-lived enough that this has not been worth solving yet). Disarming
// cancels any update already queued AND any open burst window, so a touch reported just before the
// checkbox is unticked can never fire after this call returns.
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
        {
            const juce::ScopedLock sl(queueLock_);
            pendingIndices_.clear();
        }
        {
            const juce::SpinLock::ScopedLockType sl(valueChangeLock_);
            pendingValueChangeCount_ = 0;
            valueChangeOverflowed_ = false;
        }
        stopTimer();
        burstCandidates_.clear();
        burstWindowOpen_ = false;
        burstExceeded_ = false;
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

// ANY thread, INCLUDING the audio thread (automation drives this from processHostBlock). Never
// allocates: `pendingValueChangeIndices_` is a fixed-size array, and the SpinLock never blocks on the
// OS. A full ring just marks the batch overflowed rather than growing -- handleAsyncUpdate treats an
// overflow as "burst, definitely" rather than trying to recover which indices got dropped.
void PluginKnobPickerTouchCapture::parameterValueChanged(int parameterIndex, float) {
    const juce::SpinLock::ScopedLockType sl(valueChangeLock_);
    if (pendingValueChangeCount_ < kMaxPendingValueChanges)
        pendingValueChangeIndices_[static_cast<size_t>(pendingValueChangeCount_++)] = parameterIndex;
    else
        valueChangeOverflowed_ = true;
    triggerAsyncUpdate();
}

// Message thread. Drains both queues:
//  - Gesture indices are reported immediately, as v1 always has (a gesture never debounces).
//  - Value-change candidates go through the burst filter: a candidate already in the layout (the
//    owner's own tick, or the card's own knob attachment moving a value that's already a slot) is
//    dropped before it can open or extend a window at all. A genuinely new candidate opens the window
//    (starting a `kBurstWindowMs` timer) if none is open, or joins the current one; more than
//    `kBurstMaxDistinctParams` distinct candidates marks the window exceeded. The window's own
//    `timerCallback` -- not this function -- decides whether to report or discard once it closes, so
//    a candidate arriving late in the window still counts against the same deadline the first one set.
void PluginKnobPickerTouchCapture::handleAsyncUpdate() {
    std::vector<int> gestureIndices;
    {
        const juce::ScopedLock sl(queueLock_);
        gestureIndices.swap(pendingIndices_);
    }
    for (int index : gestureIndices)
        if (onParameterTouched)
            onParameterTouched(index);

    std::array<int, kMaxPendingValueChanges> drained{};
    int drainedCount = 0;
    bool overflowed = false;
    {
        const juce::SpinLock::ScopedLockType sl(valueChangeLock_);
        drainedCount = pendingValueChangeCount_;
        drained = pendingValueChangeIndices_;
        overflowed = valueChangeOverflowed_;
        pendingValueChangeCount_ = 0;
        valueChangeOverflowed_ = false;
    }
    if (drainedCount == 0 && !overflowed)
        return;

    if (overflowed)
        burstExceeded_ = true;

    for (int i = 0; i < drainedCount; ++i) {
        const int parameterIndex = drained[static_cast<size_t>(i)];
        if (isParameterAlreadyInLayout && isParameterAlreadyInLayout(parameterIndex))
            continue;
        if (std::find(burstCandidates_.begin(), burstCandidates_.end(), parameterIndex) != burstCandidates_.end())
            continue; // already counted in this window

        burstCandidates_.push_back(parameterIndex);
        if (!burstWindowOpen_) {
            burstWindowOpen_ = true;
            startTimer(burstWindowMs_);
        }
        if (static_cast<int>(burstCandidates_.size()) > kBurstMaxDistinctParams)
            burstExceeded_ = true;
    }
}

// Message thread. Fires once, `kBurstWindowMs` after the window's first candidate (or immediately,
// via forceBurstWindowCloseForTest -- the test clock seam). More than kBurstMaxDistinctParams
// distinct parameters in the window means NONE of them are reported (a burst is either an automation
// sweep or a preset load, never a deliberate touch); kBurstMaxDistinctParams or fewer are all reported.
void PluginKnobPickerTouchCapture::timerCallback() {
    stopTimer();
    if (!burstWindowOpen_)
        return;
    const bool exceeded = burstExceeded_;
    const std::vector<int> candidates = std::move(burstCandidates_);
    burstCandidates_.clear();
    burstWindowOpen_ = false;
    burstExceeded_ = false;
    if (exceeded)
        return;
    for (int index : candidates)
        if (onParameterTouched)
            onParameterTouched(index);
}

} // namespace synth::ui
