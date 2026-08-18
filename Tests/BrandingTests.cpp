#include "../Source/Branding.h"
#include <cstdlib>
#include <gtest/gtest.h>

namespace {

// Fixture ensures AGENTSYNTH_LOCAL_API_URL never leaks between tests (or into any other test file
// run in the same process) — SetUp clears it before each case, TearDown clears it again after.
class ResolveApiBaseUrlTest : public ::testing::Test {
protected:
    void SetUp() override { unsetenv("AGENTSYNTH_LOCAL_API_URL"); }
    void TearDown() override { unsetenv("AGENTSYNTH_LOCAL_API_URL"); }
};

} // namespace

// Note: this codebase's test builds are never Release, so all three cases below only exercise the
// #ifndef NDEBUG branch of resolveApiBaseUrl() — that's expected, not a gap in coverage (the
// Release-compiled-out branch has no behavior to test).

TEST_F(ResolveApiBaseUrlTest, NoEnvVarReturnsProductionApiBaseUrl) {
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), synth::branding::kApiBaseUrl);
}

TEST_F(ResolveApiBaseUrlTest, NonEmptyEnvVarOverridesApiBaseUrl) {
    setenv("AGENTSYNTH_LOCAL_API_URL", "http://localhost:8787", 1);
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), "http://localhost:8787");
}

TEST_F(ResolveApiBaseUrlTest, EmptyEnvVarFallsThroughToProductionApiBaseUrl) {
    setenv("AGENTSYNTH_LOCAL_API_URL", "", 1);
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), synth::branding::kApiBaseUrl);
}
