#pragma once

// A timer that measures how long the message loop was blocked (a frozen UI), for the disabled profiling benches.
// Header-only; not registered in Tests/CMakeLists.txt.

#include <algorithm>
#include <functional>
#include <juce_events/juce_events.h>
#include <string>

namespace bench {

struct StallProbe : juce::Timer {
    double last = 0, worst = 0;
    void timerCallback() override {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (last > 0)
            worst = std::max(worst, now - last);
        last = now;
    }
};

/** Runs the message loop for `ms` and returns the longest stretch it was blocked, measured by a 1 ms timer: animation
 *  frames, deferred refreshes and async graph work all run inside it. */
inline double worstStallMs(double ms) {
    StallProbe probe;
    probe.startTimer(1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil((int)ms);
    probe.stopTimer();
    return probe.worst;
}

/** One timed call against a message-thread budget: `ms` is how long `fn` held the thread, `exceeded` is true past
 *  `budgetMs`, and `report()` names the action and both numbers for a test's failure message. */
struct BudgetResult {
    std::string what;
    double ms = 0, budgetMs = 0;
    bool exceeded = false;
    std::string report() const {
        return what + " held the message thread for " + std::to_string((int)ms) + " ms (budget " +
               std::to_string((int)budgetMs) + " ms)";
    }
};

/** Runs `fn` on the calling (message) thread and checks how long it took against `budgetMs`. A test asserts
 *  `!result.exceeded` with `result.report()`, so any action that outgrows its budget fails the test that times it. */
inline BudgetResult timeAgainstBudget(const std::string& what, double budgetMs, const std::function<void()>& fn) {
    const double start = juce::Time::getMillisecondCounterHiRes();
    fn();
    BudgetResult result;
    result.what = what;
    result.budgetMs = budgetMs;
    result.ms = juce::Time::getMillisecondCounterHiRes() - start;
    result.exceeded = result.ms > budgetMs;
    return result;
}

} // namespace bench
