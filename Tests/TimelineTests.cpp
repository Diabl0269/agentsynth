#include "../Source/Modules/TimelineModule.h"
#include "../Source/TransportManager.h"
#include <gtest/gtest.h>

TEST(TransportManagerTests, InitialStateIsStopped) {
    auto& transport = TransportManager::getInstance();
    transport.stop();
    EXPECT_FALSE(transport.isPlaying());
}

TEST(TransportManagerTests, PlaySetsPlayingState) {
    auto& transport = TransportManager::getInstance();
    transport.play();
    EXPECT_TRUE(transport.isPlaying());
    transport.stop(); // Reset for other tests
}

TEST(TimelineModuleTests, HasCorrectModuleType) {
    TimelineModule timeline;
    EXPECT_EQ(timeline.getModuleType(), ModuleType::Timeline);
}
