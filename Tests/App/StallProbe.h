#pragma once

// A timer that measures how long the message loop was blocked (a frozen UI), for the disabled profiling benches.
// Header-only; not registered in Tests/CMakeLists.txt.

#include <algorithm>
#include <juce_events/juce_events.h>

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

} // namespace bench
