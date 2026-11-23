// The run-rtsan build (cmake -DAGENTSYNTH_RTSAN=ON, clang RealtimeSanitizer) must stop a thread that allocates
// while a RealtimeScope is open, and must leave a thread that does not allocate alone. Both tests also compile
// and pass in every other build, where the scope is an empty object.

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

#include "AudioEngine/RealtimeScope.h"

namespace {

void allocateAndFree() {
    void* volatile block = std::malloc(64);
    std::free(block);
}

} // namespace

TEST(RealtimeScopeTest, ScopeWithoutAllocationRunsToTheEnd) {
    float sum = 0.0f;
    {
        const synth::RealtimeScope scope;
        for (int i = 0; i < 64; ++i)
            sum += static_cast<float>(i);
    }
    EXPECT_GT(sum, 0.0f);
    allocateAndFree(); // outside the scope allocation is allowed again
}

#if defined(AGENTSYNTH_RTSAN)
TEST(RealtimeScopeDeathTest, AllocationInsideTheScopeAbortsAndNamesTheCall) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    // The CI job runs with halt_on_error=false to list every report; the child process of this death test
    // starts fresh (threadsafe style), so it reads the stricter option set here and aborts on the first one.
    const char* const previous = std::getenv("RTSAN_OPTIONS");
    const std::string saved = previous != nullptr ? previous : "";
    setenv("RTSAN_OPTIONS", "halt_on_error=true", 1);
    EXPECT_DEATH(
        {
            const synth::RealtimeScope scope;
            allocateAndFree();
        },
        "RealtimeSanitizer");
    if (previous != nullptr)
        setenv("RTSAN_OPTIONS", saved.c_str(), 1);
    else
        unsetenv("RTSAN_OPTIONS");
}
#endif
