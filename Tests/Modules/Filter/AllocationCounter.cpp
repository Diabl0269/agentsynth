// AllocationCounter.cpp -- replaced global operator new/delete (malloc/free) with a per-thread
// counter that only an open AllocationScope arms.
#include "AllocationCounter.h"
#include <cstdlib>
#include <new>

namespace {
thread_local int tlArmed = 0;
thread_local int tlCount = 0;

void* countedAlloc(std::size_t size) {
    if (tlArmed > 0)
        ++tlCount;
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace synth::test {

AllocationScope::AllocationScope() {
    ++tlArmed;
    tlCount = 0;
}

AllocationScope::~AllocationScope() { --tlArmed; }

int AllocationScope::count() const { return tlCount; }

} // namespace synth::test
