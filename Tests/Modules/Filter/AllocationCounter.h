// AllocationCounter.h -- counts global operator new calls made on the current thread while a
// scope is open, for "no allocation on the audio thread" tests. The replacement operator new
// lives in AllocationCounter.cpp and is a plain malloc pass-through whenever no scope is open, so
// the rest of the test binary is unaffected. It does not see direct malloc calls.
#pragma once

namespace synth::test {

class AllocationScope {
public:
    AllocationScope();
    ~AllocationScope();
    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;

    /** operator new calls on this thread since the scope opened. */
    int count() const;
};

} // namespace synth::test
