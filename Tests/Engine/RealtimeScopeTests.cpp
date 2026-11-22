// The run-rtsan build (cmake -DAGENTSYNTH_RTSAN=ON, clang RealtimeSanitizer) must stop a thread that allocates
// while a RealtimeScope is open, and must leave a thread that does not allocate alone. Both tests also compile
// and pass in every other build, where the scope is an empty object.

#include <cstdlib>
#include <gtest/gtest.h>

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
    EXPECT_DEATH(
        {
            const synth::RealtimeScope scope;
            allocateAndFree();
        },
        "RealtimeSanitizer");
}
#endif
