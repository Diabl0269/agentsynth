// AllocationCounter.cpp -- replaced global operator new/delete (malloc/free) with a per-thread
// counter that only an open AllocationScope arms.
#include "AllocationCounter.h"
#include <cstdlib>
#include <new>

// ThreadSanitizer's C++ runtime defines operator new/delete itself, so replacing them here fails the
// link ("multiple definition of operator new"). Under TSan the replacement is
// compiled out and count() stays 0: the "no allocation" checks pass without proving anything there,
// and the ordinary jobs keep proving them.
#if defined(__SANITIZE_THREAD__)
#define SYNTH_TEST_UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define SYNTH_TEST_UNDER_TSAN 1
#endif
#endif

namespace {
thread_local int tlArmed = 0;
thread_local int tlCount = 0;

#ifndef SYNTH_TEST_UNDER_TSAN
void* countedAlloc(std::size_t size) {
    if (tlArmed > 0)
        ++tlCount;
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
#endif
} // namespace

#ifndef SYNTH_TEST_UNDER_TSAN
void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace synth::test {

AllocationScope::AllocationScope() {
    ++tlArmed;
    tlCount = 0;
}

AllocationScope::~AllocationScope() { --tlArmed; }

int AllocationScope::count() const { return tlCount; }

} // namespace synth::test
