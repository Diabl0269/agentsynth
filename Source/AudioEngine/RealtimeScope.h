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

} // namespace synth
