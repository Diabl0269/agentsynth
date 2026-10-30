#pragma once

// TestWait.h -- the one way a test waits for asynchronous work (header-only, included relatively
// like UniqueTempDir.h). A fixed `runDispatchLoopUntil(50)` followed by an assertion passes only
// when the work happens to finish inside those 50 ms; on a loaded machine it fails, and when the
// work is faster it wastes the rest. waitUntil() pumps the message loop in short steps until the
// condition holds or a generous timeout runs out, so a test is as fast as the work and as patient
// as the machine needs. scripts/check-fixed-waits.sh flags the fixed-time shape.

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace synth::test {

/** Pumps the message loop in `stepMs` slices (at least once, so a message posted just before the
 *  call is always delivered) until `predicate()` is true; false if `timeoutMs` passes first. Keep
 *  the assertion after the call: `waitUntil([&] { return done; }); EXPECT_TRUE(done);` reports
 *  the failing state on a timeout. For a "nothing may happen" check, keep a fixed pump instead and
 *  list it in scripts/fixed-waits-baseline.txt. */
inline bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 2000, int stepMs = 10) {
    auto* mm = juce::MessageManager::getInstance();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    do {
        mm->runDispatchLoopUntil(stepMs);
        if (predicate())
            return true;
    } while ((juce::int32)(deadline - juce::Time::getMillisecondCounter()) > 0);
    return predicate();
}

/** Runs every message already posted to the message thread (an async command dispatch, an AsyncUpdater, a
 *  callAsync): posts a marker behind them and pumps until it runs, so it is as fast as the queue and as patient as the
 *  machine needs, unlike a fixed slice that a loaded or first-in-process turn can overrun. Work those messages post
 *  in turn is not waited for; use waitUntil() for a state that arrives later than the queue. False on a timeout. */
inline bool flushMessageQueue(int timeoutMs = 5000) {
    auto reached = std::make_shared<bool>(false);
    juce::MessageManager::callAsync([reached] { *reached = true; });
    return waitUntil([reached] { return *reached; }, timeoutMs, 5);
}

} // namespace synth::test
