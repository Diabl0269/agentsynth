#include <juce_core/juce_core.h>

#if JUCE_MAC

#include "Auth/KeychainTokenStore.h"
#include "Branding.h"
#include <Security/Security.h>
#include <gtest/gtest.h>

namespace {
// Derived the same way KeychainTokenStore's default constructor builds the production service
// string (Branding.h's bundle id + ".refreshtoken"), with a ".test" suffix appended so this can
// never collide with — or clobber — a real user's stored token on the machine running these
// tests, and so a future bundle-id rename can't silently decouple this from production naming.
const juce::String kTestService = juce::String(synth::branding::kBundleIdentifier) + ".refreshtoken.test";
} // namespace

namespace {
// The Keychain item's creation date, as seconds since 1970, or -1 when no item matches.
double storedItemCreationSeconds() {
    CFStringRef service = kTestService.toCFString();
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecReturnAttributes, kCFBooleanTrue);
    CFTypeRef result = nullptr;
    double seconds = -1.0;
    if (SecItemCopyMatching(query, &result) == errSecSuccess && result != nullptr) {
        auto date =
            static_cast<CFDateRef>(CFDictionaryGetValue(static_cast<CFDictionaryRef>(result), kSecAttrCreationDate));
        if (date != nullptr)
            seconds = CFDateGetAbsoluteTime(date);
        CFRelease(result);
    }
    CFRelease(query);
    CFRelease(service);
    return seconds;
}
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

// Saving must update the one item in place. Delete + add re-created it on every save, and a build
// whose code signature differs from the item's creator is prompted for (or refused) that delete.
TEST_F(KeychainTokenStoreTest, SavingAgainUpdatesTheSameItemInsteadOfReplacingIt) {
    synth::KeychainTokenStore store{kTestService};
    ASSERT_TRUE(store.save("first-token"));
    const double created = storedItemCreationSeconds();
    ASSERT_GT(created, 0.0);

    juce::Thread::sleep(1100); // creation dates have one-second resolution
    ASSERT_TRUE(store.save("second-token"));

    EXPECT_EQ(storedItemCreationSeconds(), created);
}

TEST_F(KeychainTokenStoreTest, AFreshStoreSeesWhatAnotherStoreSaved) {
    {
        synth::KeychainTokenStore writer{kTestService};
        ASSERT_TRUE(writer.save("persisted-token"));
    }
    synth::KeychainTokenStore reader{kTestService};
    EXPECT_EQ(reader.load(), juce::String("persisted-token"));
}

// load() reads the Keychain once per process: every read can raise a permission prompt, so a value
// changed behind the store's back is deliberately not re-read.
TEST_F(KeychainTokenStoreTest, LoadReadsTheKeychainOnlyOncePerStore) {
    synth::KeychainTokenStore store{kTestService};
    store.clear();
    ASSERT_TRUE(store.save("cached-token"));

    synth::KeychainTokenStore other{kTestService};
    ASSERT_TRUE(other.save("changed-behind-back"));

    EXPECT_EQ(store.load(), juce::String("cached-token"));
}

TEST_F(KeychainTokenStoreTest, SavingAnUnchangedTokenSucceeds) {
    synth::KeychainTokenStore store{kTestService};
    ASSERT_TRUE(store.save("same-token"));
    EXPECT_TRUE(store.save("same-token"));
    EXPECT_EQ(store.load(), juce::String("same-token"));
}

#endif // JUCE_MAC
