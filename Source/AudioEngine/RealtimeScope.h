#pragma once

#if defined(AGENTSYNTH_RTSAN)
// The sanitizer runtime exports these (clang's own nonblocking instrumentation calls them) but its public
// header does not declare them.
extern "C" {
void __rtsan_realtime_enter();
void __rtsan_realtime_exit();
}
#endif

namespace synth {

// While one is alive on a thread, a build with -fsanitize=realtime (cmake -DAGENTSYNTH_RTSAN=ON) stops the
// process with the exact call stack the moment that thread allocates, locks or does I/O. In every other build
// it is an empty object, so the audio path pays nothing.
class RealtimeScope {
public:
#if defined(AGENTSYNTH_RTSAN)
    RealtimeScope() noexcept { __rtsan_realtime_enter(); }
    ~RealtimeScope() { __rtsan_realtime_exit(); }
#else
    RealtimeScope() noexcept = default;
    ~RealtimeScope() = default;
#endif
    RealtimeScope(const RealtimeScope&) = delete;
    RealtimeScope& operator=(const RealtimeScope&) = delete;
};

// The render thread passes through code that is not ours: juce::AudioProcessorGraph's own bookkeeping locks a
// per-node callback lock, resizes its scratch buffers and copies MidiBuffers on every block (thousands of
// reports in the first run-rtsan run). A suppressions file cannot tell that code from a module it
// calls (a module's stack runs through the same JUCE frames), so the graph call is wrapped in a
// RealtimePause and every module re-arms the sanitizer for its own render (RealtimeModuleScope): the JUCE
// graph is skipped, every Source/ module render stays checked, and nothing a module does is ever suppressed.
// In every other build both are empty objects.
namespace detail {
// Per thread: how many RealtimePause objects are alive, so a module rendered outside a graph (a unit test
// calling processBlock directly) is not armed and does not report the test harness's own setup.
inline thread_local int realtimePauseDepth = 0;
} // namespace detail

class RealtimePause {
public:
#if defined(AGENTSYNTH_RTSAN)
    RealtimePause() noexcept {
        __rtsan_realtime_exit();
        ++detail::realtimePauseDepth;
    }
    ~RealtimePause() {
        --detail::realtimePauseDepth;
        __rtsan_realtime_enter();
    }
#else
    RealtimePause() noexcept {} // user-provided so an unused guard variable does not warn
    ~RealtimePause() {}
#endif
    RealtimePause(const RealtimePause&) = delete;
    RealtimePause& operator=(const RealtimePause&) = delete;
};

class RealtimeModuleScope {
public:
#if defined(AGENTSYNTH_RTSAN)
    RealtimeModuleScope() noexcept
        : armed(detail::realtimePauseDepth > 0) {
        if (armed)
            __rtsan_realtime_enter();
    }
    ~RealtimeModuleScope() {
        if (armed)
            __rtsan_realtime_exit();
    }

private:
    const bool armed;

public:
#else
    RealtimeModuleScope() noexcept {} // user-provided so an unused guard variable does not warn
    ~RealtimeModuleScope() {}
#endif
    RealtimeModuleScope(const RealtimeModuleScope&) = delete;
    RealtimeModuleScope& operator=(const RealtimeModuleScope&) = delete;
};

} // namespace synth
