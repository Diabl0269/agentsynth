// Concern: Phase-3 chrome -- toolbar layout at every width bucket, minimum window size, the
// collapsible library/AI-panel sidebars, and the status bar (bounds + timer-gated transport
// updates + its own play/stop button).
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"

// ===========================================================================
// Phase-3 chrome: toolbar layout, min window size, collapsible panels, status bar
// ===========================================================================

TEST_F(MainComponentTest, ToolbarFitsInsideMinimumWindowWidth) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);

    auto buttons = collectToolbarButtons(mc);
    ASSERT_EQ((int)buttons.size(), 9);
    for (auto* b : buttons) {
        EXPECT_GT(b->getWidth(), 0);
        EXPECT_GE(b->getX(), 0);
        EXPECT_LE(b->getRight(), 480);
    }
}

TEST_F(MainComponentTest, ToolbarFitsAtHalfMinimumWidth) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(640, 480);
    auto buttons = collectToolbarButtons(mc);
    ASSERT_EQ((int)buttons.size(), 9);
    for (auto* b : buttons) {
        EXPECT_GE(b->getX(), 0);
        EXPECT_LE(b->getRight(), 640);
    }
}

TEST_F(MainComponentTest, ToolbarNarrowModeAt480) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);
    EXPECT_TRUE(mc.getToolbar().isNarrowMode());
}

TEST_F(MainComponentTest, ToolbarWideModeAt1600) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    EXPECT_FALSE(mc.getToolbar().isNarrowMode());
}

TEST_F(MainComponentTest, StatusBarOccupiesCorrectBounds) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    auto& sb = mc.getStatusBar();
    // Status bar height token = 24; sits flush at the bottom.
    EXPECT_EQ(sb.getHeight(), 24);
    EXPECT_EQ(sb.getY(), mc.getHeight() - 24);
}

TEST_F(MainComponentTest, CanvasRemainsNonZeroAtMinimumSize) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);
    auto bounds = mc.getGraphEditor().getBounds();
    EXPECT_GT(bounds.getWidth(), 0);
    EXPECT_GT(bounds.getHeight(), 0);
}

TEST_F(MainComponentTest, LibrarySidebarDefaultVisible) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
}

TEST_F(MainComponentTest, LibrarySidebarTogglePersists) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());

    mc.simulateToggleLibraryClick();
    EXPECT_FALSE(mc.isLibraryConfiguredVisible());
    // Persistence is written + read back within the same session.
    EXPECT_FALSE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("librarySidebarVisible", true));

    mc.simulateToggleLibraryClick();
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("librarySidebarVisible", false));
}

TEST_F(MainComponentTest, LibrarySidebarCollapsedNarrowsGraphEditor) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);

    // Shown by default: library occupies the left 200 px, so the graph editor starts at x=200.
    ASSERT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 200);

    // Hidden: graph editor reclaims the full left edge (x=0).
    mc.simulateToggleLibraryClick();
    ASSERT_FALSE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 0);
}

TEST_F(MainComponentTest, AiPanelTogglePersists) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_FALSE(mc.isAiPanelConfiguredVisible());

    mc.simulateToggleAiPanelClick();
    EXPECT_TRUE(mc.isAiPanelConfiguredVisible());
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("aiPanelVisible", false));

    mc.simulateToggleAiPanelClick();
    EXPECT_FALSE(mc.isAiPanelConfiguredVisible());
    EXPECT_FALSE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("aiPanelVisible", true));
}

TEST_F(MainComponentTest, StatusBarTimerGating) {
    MainComponent mc(std::make_unique<MockProvider>());
    // Startup runs the timer setup; the tick counter starts at 0.
    EXPECT_EQ(mc.getStatusBarTickCountForTest(), 0);

    // 1st tick: counter increments to 1, no status-bar update yet.
    mc.timerCallback();
    EXPECT_EQ(mc.getStatusBarTickCountForTest(), 1);

    // 2nd tick: counter hits 2 -> status bar updates -> resets to 0.
    mc.timerCallback();
    EXPECT_EQ(mc.getStatusBarTickCountForTest(), 0);
}

// The always-visible transport cluster (see docs/layout/chrome.md): the status bar must receive
// play-state/position/BPM from the SAME unconditional PositionSnapshot poll that already exists in
// timerCallback(), so it works identically whether the timeline panel is open or closed.
TEST_F(MainComponentTest, StatusBarReceivesTransportStateFromTimerCallback) {
    MainComponent mc(std::make_unique<MockProvider>());

    // Two ticks reach the 5 Hz status-bar sub-tick (see StatusBarTimerGating above).
    mc.timerCallback();
    mc.timerCallback();

    auto& sb = mc.getStatusBar();
    EXPECT_FALSE(sb.getTransportButton().getToggleState()) << "transport starts stopped";
    const juce::String text = sb.getTransportDisplayTextForTest();
    EXPECT_TRUE(text.contains(synth::ui::TimelineTransportBar::formatBarBeat(0.0, 4, 4)))
        << "reuses TimelineTransportBar's own formatBarBeat(), not a reimplementation";
    EXPECT_TRUE(text.contains("120.0")) << "default transport tempo";
}

// The status bar's play/stop button must drive the SAME TransportService the timeline transport
// bar uses — not a parallel play/stop of its own.
//
// play()/stop() only POST a command onto TransportService's lock-free FIFO (see its threading
// contract); only tick(), called from the audio thread's device callback, actually drains it into
// getPositionSnapshot().playing. A real device's callback thread would do that asynchronously, so
// asserting on it right after the click would race the real hardware. Same fix LatencyAlignmentTests.cpp
// uses: suspend the real callback and drive one block by hand with the shared FakeAudioIODevice, so
// the command is guaranteed to have drained before the assertion runs.
TEST_F(MainComponentTest, StatusBarPlayStopButtonDrivesTheSameTransport) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto& engine = mc.getAudioEngine();
    auto& transport = engine.getTransport();

    engine.suspendDeviceCallback();
    synth::test::FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    const auto driveOneBlock = [&engine] {
        constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
        std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outLeft.data(), outRight.data()};
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    };

    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    // NOT triggerClick(): that posts a command message, which never dispatches in a headless test
    // (see StatusBarTests.cpp's own TransportButtonClickFiresOwnerWiredCallback).
    mc.getStatusBar().getTransportButton().onClick();
    driveOneBlock();
    EXPECT_TRUE(transport.getPositionSnapshot().playing);

    mc.getStatusBar().getTransportButton().onClick();
    driveOneBlock();
    EXPECT_FALSE(transport.getPositionSnapshot().playing);

    engine.audioDeviceStopped();
}
