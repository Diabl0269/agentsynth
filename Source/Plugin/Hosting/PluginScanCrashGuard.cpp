#include "PluginScanCrashGuard.h"
#include <cstdlib>
#include <juce_core/juce_core.h>

#if JUCE_MAC
#include <csignal>
#include <unistd.h>
#endif

namespace synth {

#if JUCE_MAC

namespace {

// Only has to be non-zero: PluginScanService::runScan treats any failed/non-zero-exit child
// exactly like a "not a plugin we can host" result and blacklists the candidate, same as always.
constexpr int kQuietCrashExitCode = 132;

extern "C" void quietCrashSignalHandler(int) {
    // Async-signal-safe only from here down: no malloc, no juce::String, no stdio, no logging --
    // the process may be in an arbitrarily corrupted state. _exit() skips atexit handlers and C++
    // static destructors on purpose; in a process that exists to scan exactly one plugin and print
    // its result, there is nothing left worth running.
    _exit(kQuietCrashExitCode);
}

} // namespace

void installQuietCrashHandlers() {
    struct sigaction action{};
    action.sa_handler = &quietCrashSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    for (int signalNumber : {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE})
        sigaction(signalNumber, &action, nullptr);
}

[[noreturn]] void installQuietCrashHandlersAndFault(int signalNumber) {
    installQuietCrashHandlers();
    raise(signalNumber);
    _exit(1); // unreachable: the handler installed above always calls _exit() first
}

#else // !JUCE_MAC

void installQuietCrashHandlers() {}

[[noreturn]] void installQuietCrashHandlersAndFault(int) {
    // Test-only entry point; nothing on this platform needs the guard (see the header comment), so
    // there is no "does it work" to exercise here -- tests for this function are macOS-only.
    std::abort();
}

#endif // JUCE_MAC

} // namespace synth
