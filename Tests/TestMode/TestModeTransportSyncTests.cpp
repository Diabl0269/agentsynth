// The test-mode transport sync: a launch with no audio device still shows a ruler loop to the timeline.

#include "AudioEngine/AudioEngine.h"
#include "TestMode/TestModeTransportSync.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>

using namespace synth;

TEST(TestModeTransportSync, ALoopPostedWithNoAudioThreadStaysInvisibleUntilTheSync) {
    TransportService transport;
    ASSERT_TRUE(transport.setLoop(0.0, 8.0, true));
    EXPECT_FALSE(transport.getPositionSnapshot().looping); // the FIFO is only drained by tick()

    transport.syncWithoutAudioThread();
    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 0.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);
}

TEST(TestModeTransportSync, TheSyncDoesNotAdvanceThePlayhead) {
    TransportService transport;
    transport.locateBeat(3.0);
    transport.play();
    transport.syncWithoutAudioThread();
    transport.syncWithoutAudioThread();
    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.playing);
    EXPECT_DOUBLE_EQ(snap.ppq, 3.0);
}

TEST(TestModeTransportSync, AnEngineWithNoDeviceCallbackIsSynced) {
    AudioEngine engine;
    ASSERT_FALSE(engine.isReceivingDeviceCallbacks());
    ASSERT_TRUE(engine.getTransport().setLoop(4.0, 12.0, true));
    EXPECT_TRUE(synth::testmode::syncTransportWithoutAudio(engine));
    const auto snap = engine.getTransport().getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 12.0);
}
