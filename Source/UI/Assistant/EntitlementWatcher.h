#pragma once

#include <functional>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/**
 * @class EntitlementWatcher
 * @brief Keeps the signed-in plan and requests counter current without a sign-out/in: re-fetches
 *        the entitlement when the app returns to the foreground, and polls for a few minutes
 *        after the user opens the checkout page until the plan turns Pro.
 *
 * Knows nothing about AccountService: the owner passes a refresh action and a "is the plan Pro
 * now" probe, so the schedule is testable without a network. Both run on the message thread.
 */
class EntitlementWatcher
    : private juce::Timer
    , private juce::FocusChangeListener {
public:
    EntitlementWatcher(std::function<void()> refreshAction, std::function<bool()> isProProbe);
    ~EntitlementWatcher() override;

    /** The user just opened checkout: poll until the plan is Pro or the window runs out. */
    void startWatchingForUpgrade();

    bool isPolling() const { return isTimerRunning(); }

    // The poll schedule, public so tests can use a short one.
    int pollIntervalMs = 5000;
    int pollWindowMs = 10 * 60 * 1000;

    /** One poll step; the timer calls this. Public for tests. */
    void pollOnce();

private:
    void timerCallback() override { pollOnce(); }
    void globalFocusChanged(juce::Component* focused) override;

    std::function<void()> refresh;
    std::function<bool()> isPro;
    juce::uint32 pollStartMs = 0;
    bool appWasForeground = true;
};

} // namespace synth
