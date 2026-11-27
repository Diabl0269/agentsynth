#pragma once

#include <atomic>

namespace synth {

/** Process-wide switch for the render shortcuts: work a module skips only when it is provably bit-identical to doing
 * it. A shortcut takes its fast path only while enabled() is true; a test or the bench turns it off to render the
 *  reference path and compares. Any thread. */
struct RenderShortcuts {
    static bool enabled() noexcept { return flag().load(std::memory_order_relaxed); }
    static void setEnabled(bool on) noexcept { flag().store(on, std::memory_order_relaxed); }

    /** Test hook: how many blocks a module put to sleep on this thread (renderSilentBlock returned true). */
    static unsigned& sleptBlocks() noexcept {
        thread_local unsigned count = 0;
        return count;
    }
    static void noteSleep() noexcept { ++sleptBlocks(); }

private:
    static std::atomic<bool>& flag() noexcept {
        static std::atomic<bool> value{true};
        return value;
    }
};

} // namespace synth
