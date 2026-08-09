#include <juce_core/juce_core.h>

#if JUCE_MAC

#include "../Source/Auth/KeychainTokenStore.h"
#include "../Source/Branding.h"
#include <gtest/gtest.h>

namespace {
// Derived the same way KeychainTokenStore's default constructor builds the production service
// string (Branding.h's bundle id + ".refreshtoken"), with a ".test" suffix appended so this can
// never collide with — or clobber — a real user's stored token on the machine running these
// tests, and so a future bundle-id rename can't silently decouple this from production naming.
const juce::String kTestService = juce::String(synth::branding::kBundleIdentifier) + ".refreshtoken.test";
} // namespace

class KeychainTokenStoreTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Always clears, even after a failed assertion, so a broken test run never leaves a
        // stray Keychain item behind on the dev machine.
        synth::KeychainTokenStore store{kTestService};
        store.clear();
    }
};

TEST_F(KeychainTokenStoreTest, LoadWithNothingStoredReturnsEmptyString) {
    synth::KeychainTokenStore store{kTestService};
    store.clear(); // in case a previous crashed run left something behind
    EXPECT_TRUE(store.load().isEmpty());
}

TEST_F(KeychainTokenStoreTest, SaveThenLoadRoundTrips) {
    synth::KeychainTokenStore store{kTestService};

    ASSERT_TRUE(store.save("test-refresh-token-abc123"));
    EXPECT_EQ(store.load(), juce::String("test-refresh-token-abc123"));
}

TEST_F(KeychainTokenStoreTest, SaveTwiceReplacesThePreviousValue) {
    synth::KeychainTokenStore store{kTestService};

    ASSERT_TRUE(store.save("first-token"));
    ASSERT_TRUE(store.save("second-token"));
    EXPECT_EQ(store.load(), juce::String("second-token"));
}

TEST_F(KeychainTokenStoreTest, ClearRemovesTheStoredValue) {
    synth::KeychainTokenStore store{kTestService};

    ASSERT_TRUE(store.save("to-be-cleared"));
    ASSERT_FALSE(store.load().isEmpty());

    store.clear();
    EXPECT_TRUE(store.load().isEmpty());
}

#endif // JUCE_MAC
