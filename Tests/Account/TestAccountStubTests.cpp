// The fixed test account (AccountService::signInAsTestAccount): a signed-in state for test mode with no sign-in flow,
// no credential store and no network, which must be impossible to switch on in a user's launch.
#include "AI/AccountService.h"
#include "Auth/InMemoryTokenStore.h"
#include "AutomationLaunch.h"
#include <atomic>
#include <gtest/gtest.h>
#include <memory>

namespace {

using synth::AccountService;
using synth::AccountState;

// A store that is not memory-only (stands in for the real keychain item); counts every touch.
class PersistentLikeStore : public synth::TokenStore {
public:
    bool save(const juce::String&) override {
        ++touches;
        return true;
    }
    juce::String load() const override {
        ++touches;
        return {};
    }
    void clear() override { ++touches; }
    mutable std::atomic<int> touches{0};
};

class TestAccountStubTest : public ::testing::Test {
protected:
    void SetUp() override { AccountService::allowTestAccountForProcess(false); }
    void TearDown() override { AccountService::allowTestAccountForProcess(false); }

    // A performer that records any request: the stub must make none.
    std::atomic<int> requests{0};
    AccountService makeService(std::unique_ptr<synth::TokenStore> store) {
        return AccountService(
            "http://never-contacted.invalid",
            [this](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                   const std::atomic<bool>&) {
                ++requests;
                return synth::AuthClient::HttpResult{};
            },
            std::move(store));
    }
};

TEST_F(TestAccountStubTest, RefusedUntilTheProcessAllowsIt) {
    auto service = makeService(std::make_unique<synth::InMemoryTokenStore>());
    EXPECT_FALSE(service.signInAsTestAccount());
    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_TRUE(service.getAccessToken().isEmpty());
}

TEST_F(TestAccountStubTest, RefusedWhenTheTokenStoreIsNotMemoryOnly) {
    AccountService::allowTestAccountForProcess(true);
    auto store = std::make_unique<PersistentLikeStore>();
    auto* raw = store.get();
    auto service = makeService(std::move(store));
    EXPECT_FALSE(service.signInAsTestAccount());
    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_EQ(raw->touches.load(), 0) << "the real credential store must stay untouched";
}

TEST_F(TestAccountStubTest, SignsInWithAFakeTokenAndNoNetworkWhenAllowed) {
    AccountService::allowTestAccountForProcess(true);
    auto store = std::make_unique<synth::InMemoryTokenStore>();
    auto* raw = store.get();
    auto service = makeService(std::move(store));
    ASSERT_TRUE(service.signInAsTestAccount());
    const auto snapshot = service.getSnapshot();
    EXPECT_EQ(snapshot.state, AccountState::SignedIn);
    EXPECT_EQ(snapshot.email, juce::String(AccountService::kTestAccountEmail));
    EXPECT_EQ(service.getAccessToken(), juce::String(AccountService::kTestAccountToken));
    EXPECT_TRUE(raw->load().isEmpty()) << "nothing is persisted";
    EXPECT_EQ(requests.load(), 0);
}

TEST_F(TestAccountStubTest, TheFakeTokenIsNotShapedLikeACredential) {
    // A real access token is a three-part JWT; this one can never verify against a server.
    EXPECT_EQ(juce::String(AccountService::kTestAccountToken).indexOfChar('.'), -1);
}

TEST(TestAccountLaunchTest, OnlyAnAutomationLaunchCanAskForIt) {
    using synth::isTestAccountLaunch;
    EXPECT_TRUE(isTestAccountLaunch(juce::StringArray{"--automation", "0", "--test-account"}, {}));
    EXPECT_TRUE(isTestAccountLaunch(juce::StringArray{"--automation", "0"}, "1"));
    EXPECT_FALSE(isTestAccountLaunch(juce::StringArray{"--test-account"}, {})) << "flag without --automation";
    EXPECT_FALSE(isTestAccountLaunch({}, "1")) << "env without --automation";
    EXPECT_FALSE(isTestAccountLaunch(juce::StringArray{"--automation", "0"}, {})) << "automation alone";
}

} // namespace
