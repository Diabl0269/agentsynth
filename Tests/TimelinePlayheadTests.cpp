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
// The subclass also exposes the (protected) timer callback so frames can be driven deterministically
// instead of waiting on wall-clock time.
//
// Groups:
//   1. The overlay in isolation, driven with synthetic PositionSnapshots or a raw TransportService.
//      Nothing here needs MainComponent, so none of it is SYNTH_ENABLE_TIMELINE-gated.
//   2. MainComponent's 10 Hz poll — gated, because the poll itself compiles out with the flag.

#include "../Source/AI/AIProvider.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TimelinePlayheadOverlay.h"
#include "../Source/UI/TimelineViewState.h"
#include "MainComponent.h"
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
// the timer tick so frames are driven by the test rather than by wall-clock time.
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

    // The protected juce::Timer callback, driven by hand.
    void tick() { timerCallback(); }

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

private:
    juce::String model = "MockModel";
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

    EXPECT_FALSE(f.playhead.isPlayheadTimerRunning()) << "the 30 Hz timer must never run while stopped";
    EXPECT_EQ(f.playhead.requests, 0) << "100 idle polls must not ask for a single repaint";

    // Belt and braces: even if something drove the timer anyway, an unmoved playhead repaints
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
    ASSERT_TRUE(f.playhead.isPlayheadTimerRunning());
    ASSERT_EQ(f.playhead.getLineX(), 0);
    f.playhead.resetCounts();

    // Each audio block advances exactly 0.1 beat == 10 px at this zoom, and each frame must repaint
    // only the strip between the old and the new line.
    constexpr int kFrames = 5;
    constexpr int kMovementPx = 10;
    for (int i = 0; i < kFrames; ++i) {
        transport.tick(kTenthBeatSamples);
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

    EXPECT_EQ(f.playhead.getLineX(), kFrames * kMovementPx);

    transport.stop();
    transport.tick(kTenthBeatSamples);
    f.playhead.updateFromTransport(transport.getPositionSnapshot(), 0.0);
}

TEST(TimelinePlayheadTest, StopEmitsOneFinalStripThenSilence) {
    Fixture f;

    f.playhead.updateFromTransport(snapshotAt(0.0, /*playing=*/true), 0.0);
    ASSERT_TRUE(f.playhead.isPlayheadTimerRunning());
    f.playhead.resetCounts();

    // Stop at a position the last frame had not drawn yet.
    f.playhead.updateFromTransport(snapshotAt(1.0, /*playing=*/false), 0.0);
    EXPECT_FALSE(f.playhead.isPlayheadTimerRunning()) << "the stop transition must stop the timer";
    EXPECT_EQ(f.playhead.requests, 1) << "stopping settles the line with exactly one strip";
    EXPECT_EQ(f.playhead.getLineX(), 100);

    const auto stopped = snapshotAt(1.0, /*playing=*/false);
    for (int i = 0; i < 100; ++i) {
        f.playhead.updateFromTransport(stopped, 0.0);
        f.playhead.tick();
    }
    EXPECT_EQ(f.playhead.requests, 1) << "and then goes completely silent";
}

// The drawn position is offset backwards by the output latency so the line matches what is HEARD.
TEST(TimelinePlayheadTest, LatencyOffsetShiftsTheLine) {
    Fixture f;

    const auto atBeat4 = snapshotAt(4.0, /*playing=*/false);

    f.playhead.updateFromTransport(atBeat4, 0.0);
    const int xNoLatency = f.playhead.getLineX();
    EXPECT_EQ(xNoLatency, 400);

    // 50 ms at 120 BPM (2 beats/second) is 0.1 beat — exactly 10 px at 100 px/beat.
    f.playhead.updateFromTransport(atBeat4, 0.05);
    EXPECT_EQ(xNoLatency - f.playhead.getLineX(), 10);

    // The offset never drags the line before the start of the timeline.
    f.playhead.updateFromTransport(snapshotAt(0.0, /*playing=*/false), 0.5);
    EXPECT_EQ(f.playhead.getLineX(), 0);
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

    ASSERT_EQ(f.playhead.requests, 1);
    const auto strip = f.playhead.lastStrip;
    EXPECT_LE(strip.getX(), 55 - synth::ui::TimelinePlayheadOverlay::kStripHalfWidth)
        << "the strip must reach the new line position (beat 2.2 * 25 px)";
    EXPECT_GE(strip.getRight(), 200)
        << "the strip must still cover the STALE pixels the line was drawn on before the zoom";
}

TEST(TimelinePlayheadTest, TimerLifecycle) {
    Fixture f;

    EXPECT_FALSE(f.playhead.isPlayheadTimerRunning());

    f.playhead.updateFromTransport(snapshotAt(0.0, /*playing=*/true), 0.0);
    EXPECT_TRUE(f.playhead.isPlayheadTimerRunning()) << "the play transition starts the 30 Hz timer";

    // Staying in play must not restart or double-start anything.
    f.playhead.updateFromTransport(snapshotAt(0.5, /*playing=*/true), 0.0);
    EXPECT_TRUE(f.playhead.isPlayheadTimerRunning());

    f.playhead.updateFromTransport(snapshotAt(1.0, /*playing=*/false), 0.0);
    EXPECT_FALSE(f.playhead.isPlayheadTimerRunning()) << "the stop transition stops it";
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

#if SYNTH_ENABLE_TIMELINE

class TimelinePlayheadPollTest : public ::testing::Test {
protected:
    // Same hermetic reset as TimelinePanelTests.cpp: MainComponent's delegating ctor reads the
    // shared on-disk "Agent Synth" properties, so panel-visibility keys are restored around the
    // test.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0"); // default: hidden
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
    ASSERT_FALSE(panel.isVisible()) << "the timeline panel is hidden by default";

    for (int i = 0; i < 10; ++i)
        mc.timerCallback();
    EXPECT_EQ(panel.getTransportUpdateCountForTest(), 0) << "a hidden timeline costs exactly what it did before";
    EXPECT_FALSE(panel.getPlayhead().isPlayheadTimerRunning());

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(panel.isVisible());

    mc.timerCallback();
    EXPECT_EQ(panel.getTransportUpdateCountForTest(), 1) << "a visible panel is polled once per 10 Hz tick";

    // A stopped transport (nothing opens an audio device in a test) still starts no 30 Hz timer.
    EXPECT_FALSE(panel.getPlayhead().isPlayheadTimerRunning());

    mc.simulateToggleTimelineClick();
}

#endif // SYNTH_ENABLE_TIMELINE
