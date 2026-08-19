#include "../Source/Branding.h"
#include <cstdlib>
#include <gtest/gtest.h>

namespace {

// setenv()/unsetenv() are POSIX-only (MSVC has neither) -- _putenv_s(name, "") is the portable
// equivalent for our purposes: resolveApiBaseUrl() already treats an empty value as "not set"
// (see its override[0] != '\0' check), so it doesn't need a true unset.
#ifdef _WIN32
void setTestEnv(const char* name, const char* value) { _putenv_s(name, value); }
void clearTestEnv(const char* name) { _putenv_s(name, ""); }
#else
void setTestEnv(const char* name, const char* value) { setenv(name, value, 1); }
void clearTestEnv(const char* name) { unsetenv(name); }
#endif

// Fixture ensures AGENTSYNTH_LOCAL_API_URL never leaks between tests (or into any other test file
// run in the same process) — SetUp clears it before each case, TearDown clears it again after.
class ResolveApiBaseUrlTest : public ::testing::Test {
protected:
    void SetUp() override { clearTestEnv("AGENTSYNTH_LOCAL_API_URL"); }
    void TearDown() override { clearTestEnv("AGENTSYNTH_LOCAL_API_URL"); }
};

} // namespace

// CI builds the Tests target with -DCMAKE_BUILD_TYPE=Release (see ci.yml), so NDEBUG *is* defined
// there -- resolveApiBaseUrl()'s env var override is compiled out in that configuration, same as a
// shipped Release binary. A local `-DENABLE_TESTS=ON` dev build with no explicit build type is
// typically Debug, where the override is live. Both configurations are exercised correctly below
// by branching on the same #ifndef NDEBUG the production code uses, rather than assuming one or
// the other.

TEST_F(ResolveApiBaseUrlTest, NoEnvVarReturnsProductionApiBaseUrl) {
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), synth::branding::kApiBaseUrl);
}

#ifndef NDEBUG

TEST_F(ResolveApiBaseUrlTest, NonEmptyEnvVarOverridesApiBaseUrl) {
    setTestEnv("AGENTSYNTH_LOCAL_API_URL", "http://localhost:8787");
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), "http://localhost:8787");
}

TEST_F(ResolveApiBaseUrlTest, EmptyEnvVarFallsThroughToProductionApiBaseUrl) {
    setTestEnv("AGENTSYNTH_LOCAL_API_URL", "");
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), synth::branding::kApiBaseUrl);
}

#else

// Release builds (including CI's Tests target -- see the file comment above) compile the override
// out entirely: prove the env var is inert here, which is the actual security property
// resolveApiBaseUrl()'s Release/NDEBUG guard exists for (a tampered environment can't redirect a
// shipped binary's auth traffic away from production).
TEST_F(ResolveApiBaseUrlTest, EnvVarIsIgnoredInReleaseBuilds) {
    setTestEnv("AGENTSYNTH_LOCAL_API_URL", "http://localhost:8787");
    EXPECT_STREQ(synth::branding::resolveApiBaseUrl(), synth::branding::kApiBaseUrl);
}

#endif
