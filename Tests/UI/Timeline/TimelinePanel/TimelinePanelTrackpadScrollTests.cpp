// TimelinePanelTrackpadScrollTests.cpp
//
// A simulated trackpad swipe (smooth wheel events at the pad's own rate, then an inertial tail) fed to a 30-track
// timeline while the display runs at 60 or 120 Hz. The panel is off screen, so each display frame is run by hand
// (ScrollFrameSmoother::frameForTest) after the events that arrived during it. Checks the step per frame is even, the
// header column and the content read one device-pixel offset, and the view syncs and repaints once per frame.
// Prints its measurements (set FRO1778_PRINT=1) for docs/timeline/view.md.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestEvents.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <vector>

namespace {

struct Frame {
    double y = 0.0;      // trackScrollY after the frame
    double step = 0.0;   // how far it moved this frame
    int events = 0;      // wheel events that arrived during it
    int syncs = 0;       // syncTrackScroll calls during it
    double repaint = 0;  // area the syncs asked to repaint
    int headerDevPx = 0; // header column offset, device px at scale 2
    int contentDevPx = 0;
};

struct Bench {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    Bench() {
        panel.setTimelineDoc(&doc);
        panel.setSize(1400, 500);
        for (int i = 0; i < 30; ++i) {
            const auto id = doc.addTrack(synth::TrackKind::Midi, "Track " + juce::String(i));
            for (int c = 0; c < 4; ++c)
                doc.addClip(id, c * 8.0, 6.0, "Clip");
        }
        panel.getTrackpadScrollForTest().setOffScreenForTest(true);
    }

    void event(float deltaY, bool inertial) {
        juce::MouseWheelDetails wheel{};
        wheel.deltaY = deltaY;
        wheel.isSmooth = true;
        wheel.isInertial = inertial;
        panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    }

    // `swipeMs` of steady finger speed, then `tailMs` of inertia decaying by 0.93 per event.
    std::vector<Frame> swipe(double displayHz, double eventHz, double swipeMs, double tailMs) {
        const double dt = 1000.0 / displayHz, evDt = 1000.0 / eventHz;
        auto& probe = panel.getTrackpadScrollForTest();
        std::vector<Frame> out;
        double prevY = panel.getViewState().trackScrollY, nextEvent = evDt, tail = 1.0;
        for (int f = 0; (f + 1) * dt <= swipeMs + tailMs + 4 * dt; ++f) {
            Frame fr;
            const int syncs0 = probe.syncCalls;
            const double area0 = probe.repaintedPx;
            while (nextEvent <= (f + 1) * dt) {
                if (nextEvent <= swipeMs)
                    event(-0.02f, false);
                else if (nextEvent <= swipeMs + tailMs) {
                    tail *= 0.93;
                    event((float)(-0.02 * tail), true);
                } else
                    break;
                ++fr.events;
                nextEvent += evDt;
            }
            probe.frameForTest(dt);
            fr.y = panel.getViewState().trackScrollY;
            fr.step = fr.y - prevY;
            prevY = fr.y;
            fr.syncs = probe.syncCalls - syncs0;
            fr.repaint = probe.repaintedPx - area0;
            fr.headerDevPx = panel.getTrackHeaderViewport().getViewPositionY() * 2;
            fr.contentDevPx = (int)std::llround(fr.y) * 2;
            out.push_back(fr);
        }
        return out;
    }
};

void report(const char* name, const std::vector<Frame>& frames, int steadyFrom, int steadyTo) {
    if (std::getenv("FRO1778_PRINT") == nullptr)
        return;
    int maxSyncs = 0, zeroSteps = 0, shear = 0;
    double minStep = 1e9, maxStep = 0, repaint = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        maxSyncs = std::max(maxSyncs, frames[i].syncs);
        repaint = std::max(repaint, frames[i].repaint);
        shear += frames[i].headerDevPx != frames[i].contentDevPx;
        if ((int)i >= steadyFrom && (int)i < steadyTo) {
            zeroSteps += frames[i].step == 0.0;
            minStep = std::min(minStep, frames[i].step);
            maxStep = std::max(maxStep, frames[i].step);
        }
    }
    std::printf("FRO1778 %s: frames=%zu maxSyncs/frame=%d maxRepaintPx/frame=%.0f shearFrames=%d steady step min=%.2f "
                "max=%.2f zeroFrames=%d\n",
                name, frames.size(), maxSyncs, repaint, shear, minStep, maxStep, zeroSteps);
}

// Steady frames: after the backlog has warmed up, before the swipe ends.
void expectEven(const std::vector<Frame>& frames, int from, int to) {
    double sum = 0;
    for (int i = from; i < to; ++i)
        sum += frames[(size_t)i].step;
    const double mean = sum / (to - from);
    ASSERT_GT(mean, 1.0);
    for (int i = from; i < to; ++i) {
        EXPECT_GT(frames[(size_t)i].step, 0.0) << "frame " << i << " did not move";
        EXPECT_NEAR(frames[(size_t)i].step, mean, 0.4 * mean) << "frame " << i;
    }
}

void expectOnePerFrameAndInStep(const std::vector<Frame>& frames) {
    for (size_t i = 0; i < frames.size(); ++i) {
        EXPECT_LE(frames[i].syncs, 1) << "frame " << i << ": the view synced more than once";
        EXPECT_EQ(frames[i].headerDevPx, frames[i].contentDevPx) << "frame " << i << ": header and content differ";
        if (frames[i].syncs == 0)
            EXPECT_EQ(frames[i].repaint, 0.0);
    }
}

} // namespace

TEST(TimelinePanelTrackpadScrollTest, Swipe60HzDisplayWithFasterPadIsEvenAndSyncsOncePerFrame) {
    Bench b;
    const auto frames = b.swipe(60.0, 125.0, 500.0, 700.0);
    report("60Hz display / 125Hz pad", frames, 6, 28);
    expectEven(frames, 6, 28);
    expectOnePerFrameAndInStep(frames);
}

TEST(TimelinePanelTrackpadScrollTest, Swipe120HzDisplayWithSlowerPadIsEvenAndSyncsOncePerFrame) {
    Bench b;
    const auto frames = b.swipe(120.0, 60.0, 500.0, 700.0);
    report("120Hz display / 60Hz pad", frames, 10, 56);
    expectEven(frames, 10, 56);
    expectOnePerFrameAndInStep(frames);
}

TEST(TimelinePanelTrackpadScrollTest, RepaintPerFrameIsBoundedToTheScrollingRegion) {
    Bench b;
    const auto frames = b.swipe(60.0, 125.0, 500.0, 700.0);
    const double bound = (double)b.panel.getWidth() * b.panel.getHeight();
    for (const auto& f : frames)
        EXPECT_LE(f.repaint, bound);
}

TEST(TimelinePanelTrackpadScrollTest, InertialTailAtTheEndClampsWithoutBackingOff) {
    Bench b;
    auto& state = b.panel.getViewState();
    const auto frames = b.swipe(60.0, 125.0, 12000.0, 700.0); // far longer than the track list: runs into the end
    double high = 0.0;
    for (const auto& f : frames) {
        EXPECT_GE(f.step, 0.0) << "the view moved back after hitting the end";
        high = std::max(high, f.y);
    }
    EXPECT_DOUBLE_EQ(state.trackScrollY, high);
    EXPECT_FALSE(b.panel.getTrackpadScrollForTest().hasBacklog());
}

TEST(TimelinePanelTrackpadScrollTest, PaintCostOfTheThirtyTrackPanel) {
    Bench b;
    juce::Image image(juce::Image::ARGB, 1400, 500, true, juce::SoftwareImageType());
    auto paint = [&] {
        juce::Graphics g(image);
        b.panel.paintEntireComponent(g, true);
    };
    paint(); // warm-up
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 10; ++i)
        paint();
    const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 10.0;
    if (std::getenv("FRO1778_PRINT") != nullptr)
        std::printf("FRO1778 full paint of the 30-track panel: %.2f ms\n", ms);
    EXPECT_GT(ms, 0.0);
}
