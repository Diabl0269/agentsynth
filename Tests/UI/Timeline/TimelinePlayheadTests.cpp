// TimelinePlayheadTests.cpp
//
// The timeline playhead — the SECOND documented exception to the app's
// no-unconditional-per-tick-repaint rule (the AI thinking spinner is the first). The exception is
// granted under a confinement contract (playing-only, strip-only, explicit stop), and this file is
// what holds the contract to account.
//
// THE PAINT-COUNT PATTERN (new here; reuse it for any future timed repaint):
// `synth::ui::TimelinePlayheadOverlay` routes EVERY repaint it asks for through one protected
// virtual — `requestRepaintStrip(Rectangle<int>)`. A test subclasses the component and overrides
// that seam to count the calls and record the rects, which turns "how many repaints does an idle
// app cost?" into an ordinary headless assertion: no peer, no message loop, no screenshot diffing.
// The subclass also exposes the (protected) frame so frames can be driven deterministically
// instead of waiting on wall-clock time.
//
// Groups:
//   1. The overlay in isolation, driven with synthetic PositionSnapshots or a raw TransportService.
//      Nothing here needs MainComponent.
//   2. MainComponent's 10 Hz poll.

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeStateSource.h"
#include "TimelinePanel/TimelinePanelTestFixture.h" // timelinePanelIsOpen
#include "Transport/TransportService.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/PlayheadLocateGlide.h"
#include "UI/Timeline/PlayheadWrapCompensation.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelinePlayheadOverlay.h"
#include "UI/Timeline/TimelineViewState.h"
#include "UserSettings.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

constexpr int kOverlayWidth = 800;
constexpr int kOverlayHeight = 180;
// 100 px per beat makes every expected pixel offset in these tests an exact integer.
constexpr double kPixelsPerBeat = 100.0;
// 48 kHz at 120 BPM is exactly 24000 samples per beat, so a 2400-sample block is exactly 0.1 beat.
constexpr double kSampleRate = 48000.0;
constexpr int kTenthBeatSamples = 2400;

// The widest strip a single frame may request: the movement plus the line's own margin either side.
int maxStripWidthFor(int movementPx) {
    return movementPx + 2 * synth::ui::TimelinePlayheadOverlay::kStripHalfWidth + 1;
}

// ---------------------------------------------------------------------------
// The paint-count harness. Counts (and records) every repaint the overlay asks for, and exposes
// the display frame so frames are driven by the test rather than by wall-clock time.
// ---------------------------------------------------------------------------
struct CountingPlayhead : synth::ui::TimelinePlayheadOverlay {
    using TimelinePlayheadOverlay::TimelinePlayheadOverlay;

    int requests = 0;
    juce::Rectangle<int> lastStrip;
    std::vector<juce::Rectangle<int>> strips;

    void requestRepaintStrip(juce::Rectangle<int> r) override {
        ++requests;
        lastStrip = r;
        strips.push_back(r);
        TimelinePlayheadOverlay::requestRepaintStrip(r);
    }

    // The protected display frame, driven by hand, on a clock the test sets: 0 places every frame before the position
    // was published, which switches the extrapolation off (a frame then sees the raw position).
    double fakeNowMs = 0.0;
    double nowMs() const override { return fakeNowMs; }
    void tick() { onFrame(); }

    void resetCounts() {
        requests = 0;
        strips.clear();
        lastStrip = {};
    }
};

synth::TransportService::PositionSnapshot snapshotAt(double ppq, bool playing, double bpm = 120.0) {
    synth::TransportService::PositionSnapshot snap;
    snap.ppq = ppq;
    snap.bpm = bpm;
    snap.playing = playing;
    return snap;
}

struct Fixture {
    synth::ui::TimelineViewState view;
    CountingPlayhead playhead{view};

    Fixture() {
        view.pixelsPerBeat = kPixelsPerBeat;
        view.firstVisibleBeat = 0.0;
        playhead.setSize(kOverlayWidth, kOverlayHeight);
    }
};

class MockProviderPlayhead : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPlayhead"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

// ============================================================================
// 1. The overlay in isolation.
// ============================================================================

// THE required test: a stopped transport must cost ZERO repaints, no matter how long the app sits
// there. This is the whole reason the exception is grantable.
TEST(TimelinePlayheadTest, ZeroRepaintsOver100IdleFrames) {
    Fixture f;

    const auto stopped = snapshotAt(2.0, /*playing=*/false);
    for (int i = 0; i < 100; ++i)
        f.playhead.updateFromTransport(stopped, 0.0);

    EXPECT_FALSE(f.playhead.isPlayheadFrameRunning()) << "the display frames must never run while stopped";
    EXPECT_EQ(f.playhead.requests, 0) << "100 idle polls must not ask for a single repaint";

    // Belt and braces: even if something drove a frame anyway, an unmoved playhead repaints
    // nothing.
    for (int i = 0; i < 100; ++i)
        f.playhead.tick();
    EXPECT_EQ(f.playhead.requests, 0);
}

TEST(TimelinePlayheadTest, PlayingRequestsConfinedStrips) {
    Fixture f;

    synth::TransportService transport;
    transport.prepare(kSampleRate, kTenthBeatSamples);
    f.playhead.setTransport(&transport);

    // play() is drained by the first tick, which publishes the position at the START of that block
    // (beat 0, playing).
    ASSERT_TRUE(transport.play());
    transport.tick(kTenthBeatSamples);
    f.playhead.updateFromTransport(transport.getPositionSnapshot(), 0.0);
    ASSERT_TRUE(f.playhead.isPlayheadFrameRunning());
    ASSERT_EQ(f.playhead.getLineX(), 0);
    // The first half block still shows where the whole one ended (10 px away); the frames after it move 5 px each.
    transport.tick(kTenthBeatSamples / 2);
    f.playhead.tick();
    f.playhead.resetCounts();

    // Each half block advances exactly 0.05 beat == 5 px at this zoom, and each frame must repaint
    // only the strip between the old and the new line.
    constexpr int kFrames = 5;
    constexpr int kMovementPx = 5;
    for (int i = 0; i < kFrames; ++i) {
        transport.tick(kTenthBeatSamples / 2);
        f.playhead.tick();
    }

    ASSERT_EQ(f.playhead.requests, kFrames) << "one strip per frame that moved, no more";
    ASSERT_EQ((int)f.playhead.strips.size(), kFrames);

    int previousX = -1;
    for (int i = 0; i < kFrames; ++i) {
        const auto strip = f.playhead.strips[(size_t)i];
        EXPECT_LE(strip.getWidth(), maxStripWidthFor(kMovementPx))
            << "frame " << i << " repainted more than the movement plus the line margin";
        EXPECT_TRUE(f.playhead.getLocalBounds().contains(strip)) << "frame " << i << " strip escaped the bounds";
        EXPECT_EQ(strip.getHeight(), kOverlayHeight) << "a strip spans the full height — ruler through lanes";
        EXPECT_GT(strip.getX(), previousX) << "strips must advance with the playhead";
        previousX = strip.getX();
    }

    EXPECT_EQ(f.playhead.getLineX(), 10 + kFrames * kMovementPx);

    transport.stop();
    transport.tick(kTenthBeatSamples);
    f.playhead.updateFromTransport(transport.getPositionSnapshot(), 0.0);
}

TEST(TimelinePlayheadTest, StopEmitsOneFinalStripThenSilence) {
    Fixture f;

    f.playhead.updateFromTransport(snapshotAt(0.0, /*playing=*/true), 0.0);
    ASSERT_TRUE(f.playhead.isPlayheadFrameRunning());
    f.playhead.resetCounts();

    // Stop at a position the last frame had not drawn yet.
    f.playhead.updateFromTransport(snapshotAt(0.05, /*playing=*/false), 0.0);
    EXPECT_FALSE(f.playhead.isPlayheadFrameRunning()) << "the stop transition must stop the frames";
    EXPECT_EQ(f.playhead.requests, 1) << "stopping settles the line with exactly one strip";
    EXPECT_EQ(f.playhead.getLineX(), 5);

    const auto stopped = snapshotAt(0.05, /*playing=*/false);
    for (int i = 0; i < 100; ++i) {
        f.playhead.updateFromTransport(stopped, 0.0);
        f.playhead.tick();
    }
    EXPECT_EQ(f.playhead.requests, 1) << "and then goes completely silent";
}

// The drawn position is offset backwards by the output latency so the line matches what is HEARD —
// but only WHILE PLAYING. Stopped, nothing is sounding, so the raw position is the truth: a ruler
// click that snapped exactly onto a bar must draw exactly ON that bar line (the always-on offset
// used to park it a few pixels left, which read as a snapping bug).
TEST(TimelinePlayheadTest, LatencyOffsetShiftsTheLineOnlyWhilePlaying) {
    Fixture f;

    const auto playingAtBeat4 = snapshotAt(4.0, /*playing=*/true);

    f.playhead.updateFromTransport(playingAtBeat4, 0.0);
    const int xNoLatency = f.playhead.getLineX();
    EXPECT_EQ(xNoLatency, 400);

    // 50 ms at 120 BPM (2 beats/second) is 0.1 beat — exactly 10 px at 100 px/beat.
    f.playhead.updateFromTransport(playingAtBeat4, 0.05);
    EXPECT_EQ(xNoLatency - f.playhead.getLineX(), 10);

    // Stopped, the same latency shifts NOTHING: the line sits on the exact position.
    f.playhead.updateFromTransport(snapshotAt(4.0, /*playing=*/false), 0.05);
    EXPECT_EQ(f.playhead.getLineX(), 400);

    // The offset never drags the line before the start of the timeline.
    f.playhead.updateFromTransport(snapshotAt(0.0, /*playing=*/true), 0.5);
    EXPECT_EQ(f.playhead.getLineX(), 0);
}

// With looping on, the transport wraps as soon as the rendered block crosses the loop end, while the
// heard audio is still the output latency short of it. The line must travel on to the loop end
// before it jumps back, and must not dip below the loop start afterwards.
TEST(TimelinePlayheadTest, LoopWrapCarriesTheLineToTheLoopEndBeforeItJumpsBack) {
    Fixture f;
    const auto looped = [](double ppq) {
        auto snap = snapshotAt(ppq, /*playing=*/true);
        snap.looping = true;
        snap.loopStartPpq = 0.0;
        snap.loopEndPpq = 4.0;
        return snap;
    };
    constexpr double kLatency = 0.03; // 0.06 beat at 120 BPM

    f.playhead.updateFromTransport(looped(3.90), kLatency);
    f.playhead.updateFromTransport(looped(3.98), kLatency);
    const double beforeWrap = f.playhead.getDrawnBeat();
    EXPECT_NEAR(beforeWrap, 3.92, 1e-9);

    double highest = beforeWrap;
    for (const double ppq : {0.02, 0.04, 0.055}) {
        f.playhead.updateFromTransport(looped(ppq), kLatency);
        const double drawn = f.playhead.getDrawnBeat();
        EXPECT_GE(drawn, beforeWrap) << "ppq " << ppq << ": the line keeps moving forward through the wrap";
        EXPECT_LT(drawn, 4.0 + 1e-9);
        highest = std::max(highest, drawn);
    }
    EXPECT_GE(highest, 4.0 - 1.0 / kPixelsPerBeat) << "the line gets within a pixel of the loop end";

    f.playhead.updateFromTransport(looped(0.08), kLatency);
    EXPECT_NEAR(f.playhead.getDrawnBeat(), 0.02, 1e-9) << "once heard audio is past the loop start it draws plainly";
    f.playhead.updateFromTransport(looped(0.30), kLatency);
    EXPECT_NEAR(f.playhead.getDrawnBeat(), 0.24, 1e-9);
}

TEST(TimelinePlayheadTest, FreshPlayFromLoopStartDrawsTheLoopStart) {
    Fixture f;
    auto stopped = snapshotAt(0.0, /*playing=*/false);
    stopped.looping = true;
    stopped.loopEndPpq = 4.0;
    f.playhead.updateFromTransport(stopped, 0.03);

    auto playing = stopped;
    playing.playing = true;
    playing.ppq = 0.02;
    f.playhead.updateFromTransport(playing, 0.03);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 0.0);
    EXPECT_EQ(f.playhead.getLineX(), 0);
}

TEST(TimelinePlayheadTest, StopOrLocateOrLoopChangeEndsTheWrappedState) {
    const auto looped = [](double ppq, bool playing, double loopEnd) {
        auto snap = snapshotAt(ppq, playing);
        snap.looping = true;
        snap.loopEndPpq = loopEnd;
        return snap;
    };
    synth::ui::PlayheadWrapCompensation wrap;
    const auto enterWrapped = [&wrap, &looped] {
        wrap.observe(looped(3.98, true, 4.0), 0.06);
        wrap.observe(looped(0.02, true, 4.0), 0.06);
    };

    enterWrapped();
    ASSERT_TRUE(wrap.isWrapped());
    wrap.observe(looped(0.02, false, 4.0), 0.06);
    EXPECT_FALSE(wrap.isWrapped()) << "stop";

    enterWrapped();
    ASSERT_TRUE(wrap.isWrapped());
    wrap.observe(looped(2.0, true, 4.0), 0.06);
    EXPECT_FALSE(wrap.isWrapped()) << "locate";

    enterWrapped();
    ASSERT_TRUE(wrap.isWrapped());
    wrap.observe(looped(0.03, true, 8.0), 0.06);
    EXPECT_FALSE(wrap.isWrapped()) << "loop change";
}

// Regression test for FRO1412: a locate while playing put the
// line the output latency before its target, since the latency shift was applied to the first block after the jump.
// The line must sit exactly on the target, then carry on with the sound.
TEST(TimelinePlayheadTest, ALocateWhilePlayingDrawsTheLineOnTheTargetAndThenFollowsTheSound) {
    Fixture f;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    transport.play();
    constexpr double kLatency = 0.05; // 0.1 beat at 120 BPM
    const auto block = [&] {
        transport.tick(512);
        f.playhead.updateFromTransport(transport.getPositionSnapshot(), kLatency);
    };
    for (int i = 0; i < 20; ++i)
        block();
    ASSERT_GT(transport.getPositionSnapshot().ppq, 0.3);

    ASSERT_TRUE(transport.locateBeat(8.0));
    block(); // the audio thread applies it at the start of this block
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 8.0) << "the cursor is on the target, not the latency before it";
    EXPECT_EQ(f.playhead.getLineX(), 800);

    for (int i = 0; i < 30; ++i) {
        const double before = f.playhead.getDrawnBeat();
        block();
        EXPECT_GE(f.playhead.getDrawnBeat(), before) << "block " << i << ": playback continues from the target";
    }
    const auto snap = transport.getPositionSnapshot();
    EXPECT_GT(snap.ppq - kLatency * 2.0, 8.0);
    EXPECT_NEAR(f.playhead.getDrawnBeat(), snap.ppq - kLatency * 2.0, 1e-9) << "once the sound catches up it is plain";
}

TEST(TimelinePlayheadTest, ALocateBackwardsAndOneOnAStoppedTransportDrawOnTheTargetToo) {
    Fixture f;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    transport.play();
    for (int i = 0; i < 400; ++i)
        transport.tick(512);
    f.playhead.updateFromTransport(transport.getPositionSnapshot(), 0.05); // seen playing before the jump
    transport.locateBeat(1.0);
    transport.tick(512);
    f.playhead.updateFromTransport(transport.getPositionSnapshot(), 0.05);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 1.0);

    transport.stop();
    transport.locateBeat(3.0);
    transport.tick(512);
    f.playhead.updateFromTransport(transport.getPositionSnapshot(), 0.05);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 3.0);
}

// A zoom changes the beat->pixel mapping, so the pixels that must be repainted are where the line
// ACTUALLY WAS — not where its old beat maps to under the new zoom.
TEST(TimelinePlayheadTest, ZoomWhilePlayingRepaintsStalePixels) {
    Fixture f;

    f.playhead.updateFromTransport(snapshotAt(2.0, /*playing=*/true), 0.0);
    f.playhead.tick();
    ASSERT_EQ(f.playhead.getLastRequestedLineX(), 200) << "the line is drawn at beat 2 * 100 px";
    f.playhead.resetCounts();

    // Zoom out 4x, and let the transport advance a little at the same time. The old beat (2) now
    // maps to x = 50 — nowhere near the stale pixels at x = 200.
    f.view.pixelsPerBeat = 25.0;
    f.playhead.updateFromTransport(snapshotAt(2.2, /*playing=*/true), 0.0);
    f.playhead.tick();

    // 150 px apart: the old and the new strip are requested apart.
    ASSERT_EQ(f.playhead.requests, 2);
    const auto stale = f.playhead.strips[0];
    const auto fresh = f.playhead.strips[1];
    EXPECT_TRUE(stale.contains(200, 10))
        << "the strip must cover the STALE pixels the line was drawn on before the zoom";
    EXPECT_TRUE(fresh.contains(55, 10)) << "and the new line position (beat 2.2 * 25 px)";
}

TEST(TimelinePlayheadTest, FrameLifecycle) {
    Fixture f;

    EXPECT_FALSE(f.playhead.isPlayheadFrameRunning());

    f.playhead.updateFromTransport(snapshotAt(0.0, /*playing=*/true), 0.0);
    EXPECT_TRUE(f.playhead.isPlayheadFrameRunning()) << "the play transition starts the display frames";

    // Staying in play must not restart or double-start anything.
    f.playhead.updateFromTransport(snapshotAt(0.5, /*playing=*/true), 0.0);
    EXPECT_TRUE(f.playhead.isPlayheadFrameRunning());

    f.playhead.updateFromTransport(snapshotAt(1.0, /*playing=*/false), 0.0);
    EXPECT_FALSE(f.playhead.isPlayheadFrameRunning()) << "the stop transition stops it";
}

// While stopped the overlay asks for nothing — but it still DRAWS whenever something else paints
// it. Painting is not what the confinement contract restricts; asking for a repaint is.
TEST(TimelinePlayheadTest, SnapshotSmoke) {
    Fixture f;
    f.playhead.updateFromTransport(snapshotAt(2.0, /*playing=*/false), 0.0);
    ASSERT_EQ(f.playhead.getLineX(), 200);

    const juce::Image img = f.playhead.createComponentSnapshot(f.playhead.getLocalBounds());
    ASSERT_FALSE(img.isNull());
    ASSERT_EQ(img.getWidth(), kOverlayWidth);
    ASSERT_EQ(img.getHeight(), kOverlayHeight);

    EXPECT_GT(img.getPixelAt(200, kOverlayHeight / 2).getAlpha(), 0) << "the line is drawn at beat 2";
    EXPECT_EQ(img.getPixelAt(400, kOverlayHeight / 2).getAlpha(), 0) << "and nowhere else — the overlay is transparent";
}

// A big move asks for the strip it left and the strip it arrived at, not the whole span between them.
TEST(TimelinePlayheadTest, ABigMoveRequestsTheTwoStripsApartAndASmallOneTheirUnion) {
    Fixture f;
    f.playhead.updateFromTransport(snapshotAt(1.0, /*playing=*/true), 0.0);
    f.playhead.resetCounts();

    f.playhead.updateFromTransport(snapshotAt(2.0, /*playing=*/true), 0.0); // 100 px
    ASSERT_EQ(f.playhead.requests, 2);
    for (const auto& strip : f.playhead.strips)
        EXPECT_EQ(strip.getWidth(), 2 * synth::ui::TimelinePlayheadOverlay::kStripHalfWidth + 1);
    EXPECT_EQ(f.playhead.strips[0].getCentreX(), 100);
    EXPECT_EQ(f.playhead.strips[1].getCentreX(), 200);

    f.playhead.resetCounts();
    f.playhead.updateFromTransport(snapshotAt(2.05, /*playing=*/true), 0.0); // 5 px
    EXPECT_EQ(f.playhead.requests, 1);
}

// ---- Frame stats (read by test mode) ----

TEST(TimelinePlayheadTest, FrameStatsCountFramesAndTakeTheMedianGapOfTheLast64) {
    Fixture f;
    EXPECT_EQ(f.playhead.getFrameCount(), 0);
    EXPECT_DOUBLE_EQ(f.playhead.getMedianFrameIntervalMs(), 0.0);

    for (int i = 0; i < 100; ++i) {
        f.playhead.fakeNowMs = 1000.0 + i * (1000.0 / 60.0) + (i == 70 ? 40.0 : 0.0); // one late frame
        f.playhead.tick();
    }
    EXPECT_EQ(f.playhead.getFrameCount(), 100);
    EXPECT_NEAR(f.playhead.getMedianFrameIntervalMs(), 1000.0 / 60.0, 1e-6) << "one late frame does not move a median";

    juce::DynamicObject state;
    for (auto* child : f.playhead.getChildren())
        if (child->getComponentID() == "timelinePlayhead")
            if (auto* source = dynamic_cast<synth::testmode::StateSource*>(child))
                source->addTestModeState(state);
    EXPECT_EQ((int)state.getProperty("frameCount"), 100) << "test mode reads the stats from the child timelinePlayhead";
}

// ---- Locate glide ----

namespace {
struct GlideFixture : Fixture {
    synth::TransportService transport;
    double latency = 0.0;

    GlideFixture() {
        synth::ui::TimelinePlayheadOverlay::setGlideOffScreenForTest(true);
        synth::ui::setReducedMotionForTest(false);
        transport.prepare(kSampleRate, 512);
        playhead.setTransport(&transport);
    }
    ~GlideFixture() {
        synth::ui::TimelinePlayheadOverlay::setGlideOffScreenForTest(false);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }

    void block() { transport.tick(512); }
    void frameAt(double ms) {
        playhead.fakeNowMs = ms;
        playhead.tick();
    }
    // A stopped transport sitting on `beat`, seen once so the overlay has a baseline.
    void settleOn(double beat) {
        transport.locateBeat(beat);
        block();
        playhead.fakeNowMs = 0.0;
        playhead.updateFromTransport(transport.getPositionSnapshot(), latency);
    }
};
} // namespace

TEST(TimelinePlayheadGlideTest, AnArmedLocateDrawsTheOldPlaceThenGlidesAndLandsOnTheTargetAt160Ms) {
    GlideFixture f;
    f.settleOn(2.0);
    ASSERT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 2.0);

    f.playhead.fakeNowMs = 1000.0;
    f.playhead.armLocateGlide();
    f.transport.locateBeat(10.0);
    f.block();

    f.frameAt(1000.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 2.0) << "the first frame still draws the old place";
    EXPECT_TRUE(f.playhead.isPlayheadFrameRunning()) << "a stopped glide runs frames for its duration";

    f.frameAt(1080.0);
    EXPECT_GT(f.playhead.getDrawnBeat(), 2.0);
    EXPECT_LT(f.playhead.getDrawnBeat(), 10.0) << "mid-glide lies strictly between";

    double previous = f.playhead.getDrawnBeat();
    for (const double ms : {1100.0, 1130.0, 1150.0}) {
        f.frameAt(ms);
        EXPECT_GE(f.playhead.getDrawnBeat(), previous) << "the glide only goes forward";
        previous = f.playhead.getDrawnBeat();
    }

    f.frameAt(1160.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 10.0) << "lands exactly on the target";
    EXPECT_FALSE(f.playhead.isPlayheadFrameRunning()) << "and the stopped frames stop with it";
}

TEST(TimelinePlayheadGlideTest, ALocateNobodyArmedJumpsAtOnce) {
    GlideFixture f;
    f.settleOn(2.0);

    f.transport.locateBeat(10.0); // a scrub, or a held cursor move
    f.block();
    f.frameAt(1000.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 10.0);
    EXPECT_FALSE(f.playhead.isPlayheadFrameRunning());
}

TEST(TimelinePlayheadGlideTest, AnArmThatNoLocateFollowedLapsesAndCannotTurnALaterScrubIntoAGlide) {
    GlideFixture f;
    f.settleOn(2.0);

    f.playhead.fakeNowMs = 1000.0;
    f.playhead.armLocateGlide();
    f.transport.locateBeat(10.0);
    f.block();
    f.frameAt(1000.0 + synth::ui::PlayheadLocateGlide::kArmWindowMs + 1.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 10.0);
}

TEST(TimelinePlayheadGlideTest, ReduceMotionAndAHiddenPanelLandAtOnce) {
    GlideFixture f;
    f.settleOn(2.0);
    synth::ui::setReducedMotionForTest(true);
    f.playhead.armLocateGlide();
    f.transport.locateBeat(10.0);
    f.block();
    f.frameAt(0.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 10.0) << "Reduce Motion";

    synth::ui::setReducedMotionForTest(false);
    synth::ui::TimelinePlayheadOverlay::setGlideOffScreenForTest(false); // not showing: a headless component
    f.playhead.armLocateGlide();
    f.transport.locateBeat(3.0);
    f.block();
    f.frameAt(10.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 3.0) << "not on screen";
}

// The line holds on a locate's target until the sound reaches it; the glide ends on that held place.
TEST(TimelinePlayheadGlideTest, WhilePlayingTheGlideLandsOnTheHeldTarget) {
    GlideFixture f;
    f.latency = 0.05; // 0.1 beat at 120 BPM
    f.transport.play();
    for (int i = 0; i < 20; ++i) {
        f.block();
        f.playhead.updateFromTransport(f.transport.getPositionSnapshot(), f.latency);
    }
    const double before = f.playhead.getDrawnBeat();
    ASSERT_GT(before, 0.2);

    f.playhead.fakeNowMs = 1000.0;
    f.playhead.armLocateGlide();
    f.transport.locateBeat(8.0);
    f.block();
    f.frameAt(1000.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), before) << "first frame: the old place";
    f.frameAt(1160.0);
    EXPECT_DOUBLE_EQ(f.playhead.getDrawnBeat(), 8.0) << "the held target, as without a glide";
    EXPECT_TRUE(f.playhead.isPlayheadFrameRunning()) << "playing keeps its frames";
}

// ---------------------------------------------------------------------------
// Panel wiring.
// ---------------------------------------------------------------------------

TEST(TimelinePlayheadPanelTest, OverlaySpansRulerAndLanesAndPassesClicksThrough) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setVisible(true); // a parentless component is not visible by default, and hit-testing needs it

    auto& playhead = panel.getPlayhead();
    EXPECT_EQ(playhead.getBounds(), panel.getLanesBounds()) << "the overlay covers the ruler AND the lanes";
    EXPECT_TRUE(playhead.isVisible());
    // The ruler underneath owns click-to-seek / drag-to-loop; the overlay must never swallow them.
    EXPECT_EQ(panel.getComponentAt(panel.getRuler().getBounds().getCentre()),
              static_cast<juce::Component*>(&panel.getRuler()));
}

TEST(TimelinePlayheadPanelTest, PollForwardsThePositionToThePlayhead) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    synth::TransportService::PositionSnapshot snap;
    snap.loopEndPpq = 4.0;

    // The position alone moves constantly and must NOT be part of the ruler's diff.
    panel.updateFromTransport(snap, 0.0);
    for (int i = 0; i < 10; ++i) {
        snap.ppq += 0.25;
        panel.updateFromTransport(snap, 0.0);
    }
    EXPECT_EQ(panel.getTransportUpdateCountForTest(), 11);
    EXPECT_EQ(panel.getPlayhead().getLineX(), (int)std::llround(panel.getViewState().beatToX(2.5)));
}

// ============================================================================
// 2. MainComponent's 10 Hz poll.
// ============================================================================

class TimelinePlayheadPollTest : public ::testing::Test {
protected:
    // Same hermetic reset as TimelinePanelTests.cpp: MainComponent's delegating ctor reads the
    // shared on-disk "Agent Synth" properties, so panel-visibility keys are restored around the
    // test.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts = synth::userSettingsOptions();

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("bottomDockVisible", "0"); // default: hidden
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

TEST_F(TimelinePlayheadPollTest, TenHzPollOnlyReachesAVisiblePanel) {
    MainComponent mc(std::make_unique<MockProviderPlayhead>());
    mc.setSize(1600, 900);

    auto& panel = mc.getTimelinePanel();
    // timelinePanelIsOpen(), not panel.isVisible() -- the panel now lives inside
    // BottomDockComponent's tab strip, so its own visibility flag reflects only "the Timeline tab
    // is selected" (true by default), not "the dock is open". timelinePanelIsOpen's own comment
    // (TimelinePanelTestFixture.h) explains why this composes the two local isVisible() flags
    // rather than isShowing() (which needs a real Desktop peer this headless test never has).
    ASSERT_FALSE(timelinePanelIsOpen(mc)) << "the timeline panel is hidden by default";

    for (int i = 0; i < 10; ++i)
        mc.timerCallback();
    EXPECT_EQ(panel.getTransportUpdateCountForTest(), 0) << "a hidden timeline costs exactly what it did before";
    EXPECT_FALSE(panel.getPlayhead().isPlayheadFrameRunning());

    mc.simulateToggleBottomPanelClick();
    ASSERT_TRUE(timelinePanelIsOpen(mc));

    mc.timerCallback();
    EXPECT_EQ(panel.getTransportUpdateCountForTest(), 1) << "a visible panel is polled once per 10 Hz tick";

    // A stopped transport (nothing opens an audio device in a test) still starts no display frames.
    EXPECT_FALSE(panel.getPlayhead().isPlayheadFrameRunning());

    mc.simulateToggleBottomPanelClick();
}
