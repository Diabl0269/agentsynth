// Guards that the test binary never reaches the developer's real sign-in credential: TestMain
// switches default-constructed KeychainTokenStores to memory, because a read of the real item by
// an unsigned or re-signed Tests binary raises the login-keychain password prompt.
#include "Auth/KeychainTokenStore.h"
#include <gtest/gtest.h>

TEST(KeychainTokenStoreIsolationTest, DefaultStoreInTheSuiteKeepsTheTokenInMemory) {
    synth::KeychainTokenStore store;
    EXPECT_TRUE(store.isMemoryOnly()) << "TestMain must call KeychainTokenStore::useInMemoryStoreForProcess(true)";

    EXPECT_TRUE(store.load().isEmpty());
    ASSERT_TRUE(store.save("isolation-token"));
    EXPECT_EQ(store.load(), juce::String("isolation-token"));

    // A second default store shares nothing with the first: it is not backed by a shared item.
    synth::KeychainTokenStore other;
    EXPECT_TRUE(other.load().isEmpty());

    store.clear();
    EXPECT_TRUE(store.load().isEmpty());
}

TEST(KeychainTokenStoreIsolationTest, ExplicitServiceStoreIsStillBackedByThePlatformStore) {
    synth::KeychainTokenStore store{"com.agentsynth.app.refreshtoken.test.isolation-flag"};
    EXPECT_FALSE(store.isMemoryOnly());
}
