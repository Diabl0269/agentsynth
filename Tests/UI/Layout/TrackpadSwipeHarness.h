#pragma once

// TrackpadSwipeHarness.h
//
// A simulated trackpad swipe (smooth wheel events at the pad's own rate, then an inertial tail) fed to any scroll
// surface while its display runs at 60 or 120 Hz. The surface is off screen, so each display frame is run by hand
// (WheelScrollRouter::frameForTest) after the events that arrived during it. Shared by the per-surface tests of the
// trackpad glide; header-only, not registered in Tests/CMakeLists.txt.

#include "UI/Layout/ScrollTween.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <vector>

namespace trackpad_test {

struct Frame {
    double pos = 0.0;
    double step = 0.0;
    int events = 0;
    int moves = 0; // how often the view was moved this frame (one is the aim)
};

struct Surface {
    std::function<void(float delta, bool inertial)> send; // one wheel event along the axis under test
    std::function<double()> position;
    synth::ui::WheelScrollRouter* router = nullptr;
    int axis = 0;
    double posDir = 1.0;                // which way the position moves for a swipe of sign +1 (set by movingSign)
    double quantum = 0.0;               // the surface lands on whole pixels: a step may be off by this much
    std::function<int()> notifications; // how often the surface told its observers the view moved (optional)

    synth::ui::ScrollFrameSmoother& pad() { return router->pad(axis); }
};

inline std::vector<Frame> swipe(Surface& s, double displayHz, double eventHz, double swipeMs, double tailMs,
                                double sign) {
    const double dt = 1000.0 / displayHz, evDt = 1000.0 / eventHz;
    std::vector<Frame> out;
    double prev = s.position(), nextEvent = evDt, tail = 1.0;
    for (int f = 0; (f + 1) * dt <= swipeMs + tailMs + 4 * dt; ++f) {
        Frame fr;
        const int moves0 = s.pad().movedFrames;
        while (nextEvent <= (f + 1) * dt) {
            if (nextEvent <= swipeMs)
                s.send((float)(0.04 * sign), false);
            else if (nextEvent <= swipeMs + tailMs) {
                tail *= 0.93;
                s.send((float)(0.04 * sign * tail), true);
            } else
                break;
            ++fr.events;
            nextEvent += evDt;
        }
        s.router->frameForTest(dt);
        fr.pos = s.position();
        fr.step = fr.pos - prev;
        prev = fr.pos;
        fr.moves = s.pad().movedFrames - moves0;
        out.push_back(fr);
    }
    return out;
}

// The sign of the swipe that moves the view (the other may start against an end); records which way the position goes.
inline double movingSign(Surface& s) {
    for (const double sign : {1.0, -1.0}) {
        const double before = s.position();
        swipe(s, 60.0, 125.0, 300.0, 300.0, sign);
        if (s.position() != before) {
            s.posDir = s.position() > before ? 1.0 : -1.0;
            swipe(s, 60.0, 125.0, 12000.0, 700.0, -sign); // back to where it started
            return sign;
        }
    }
    return 0.0;
}

inline void expectEven(Surface& s, const std::vector<Frame>& frames, int from, int to) {
    const double sign = s.posDir;
    double sum = 0;
    for (int i = from; i < to; ++i)
        sum += frames[(size_t)i].step * sign;
    const double mean = sum / (to - from);
    ASSERT_GT(mean, 0.0);
    for (int i = from; i < to; ++i) {
        const double step = frames[(size_t)i].step * sign;
        EXPECT_GT(step, 0.0) << "frame " << i << " did not move";
        EXPECT_NEAR(step, mean, 0.4 * mean + s.quantum) << "frame " << i;
    }
}

inline void expectOneMovePerFrame(const std::vector<Frame>& frames) {
    for (size_t i = 0; i < frames.size(); ++i)
        EXPECT_LE(frames[i].moves, 1) << "frame " << i << ": the view moved more than once";
}

// The 60 Hz display with the faster pad, and the 120 Hz display with the slower one.
inline void checkEvenAtBothRates(const std::function<Surface&()>& make) {
    {
        auto& s = make();
        const double sign = movingSign(s);
        ASSERT_NE(sign, 0.0);
        const auto frames = swipe(s, 60.0, 125.0, 500.0, 700.0, sign);
        expectEven(s, frames, 6, 28);
        expectOneMovePerFrame(frames);
    }
    {
        auto& s = make();
        const double sign = movingSign(s);
        const auto frames = swipe(s, 120.0, 60.0, 500.0, 700.0, sign);
        expectEven(s, frames, 10, 56);
        expectOneMovePerFrame(frames);
    }
}

// A long fling back into an end: the view stops there and does not move back, and nothing is left queued.
inline void checkCoastsToAStopAtTheEnd(Surface& s) {
    const double sign = movingSign(s);
    ASSERT_NE(sign, 0.0);
    swipe(s, 60.0, 125.0, 400.0, 0.0, sign); // away from the end first
    const auto frames = swipe(s, 60.0, 125.0, 12000.0, 700.0, -sign);
    double lowest = 1e18;
    for (const auto& f : frames) {
        EXPECT_LE(f.step * s.posDir, 1e-9) << "the view moved back after hitting the end";
        lowest = std::min(lowest, f.pos * s.posDir);
    }
    EXPECT_DOUBLE_EQ(s.position() * s.posDir, lowest);
    EXPECT_FALSE(s.pad().hasBacklog());
}

// More swipe into the end it already sits on moves nothing and tells nobody.
inline void checkNoMoveNoNotification(Surface& s) {
    const double sign = movingSign(s);
    ASSERT_NE(sign, 0.0);
    swipe(s, 60.0, 125.0, 12000.0, 0.0, -sign); // run into the end
    const double at = s.position();
    const int told = s.notifications ? s.notifications() : 0;
    const auto frames = swipe(s, 60.0, 125.0, 300.0, 300.0, -sign);
    EXPECT_EQ(s.position(), at);
    for (const auto& f : frames)
        EXPECT_EQ(f.step, 0.0);
    if (s.notifications)
        EXPECT_EQ(s.notifications(), told) << "a move that went nowhere was announced";
}

} // namespace trackpad_test
