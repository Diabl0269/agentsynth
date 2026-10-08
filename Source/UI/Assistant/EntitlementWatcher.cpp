#include "EntitlementWatcher.h"

namespace synth {

EntitlementWatcher::EntitlementWatcher(std::function<void()> refreshAction, std::function<bool()> isProProbe)
    : refresh(std::move(refreshAction))
    , isPro(std::move(isProProbe)) {
    juce::Desktop::getInstance().addFocusChangeListener(this);
}

EntitlementWatcher::~EntitlementWatcher() {
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    stopTimer();
}

void EntitlementWatcher::startWatchingForUpgrade() {
    pollStartMs = juce::Time::getMillisecondCounter();
    startTimer(pollIntervalMs);
}

void EntitlementWatcher::pollOnce() {
    if (isPro() || juce::Time::getMillisecondCounter() - pollStartMs > static_cast<juce::uint32>(pollWindowMs)) {
        stopTimer();
        return;
    }
    refresh();
}

void EntitlementWatcher::globalFocusChanged(juce::Component*) {
    // Focus moves constantly inside the app; only the background -> foreground edge means the user
    // may have just come back from the checkout page.
    const bool foreground = juce::Process::isForegroundProcess();
    if (foreground && !appWasForeground)
        refresh();
    appWasForeground = foreground;
}

} // namespace synth
