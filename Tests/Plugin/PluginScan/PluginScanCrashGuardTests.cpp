// FRO59: the crash-guard building block that keeps a plugin-scan child's crash from popping macOS's
// own "quit unexpectedly" dialog. A `raise()`d signal is not a real hardware fault, and a synthetic
// SIGTRAP is not the EXC_BREAKPOINT a real trapping plugin produces, so this proves the handler
// itself installs and _exit()s promptly -- not that it wins the race against ReportCrash for every
// real fault shape. That end-to-end claim can only be checked by watching for new
// ~/Library/Logs/DiagnosticReports/Agent Synth-*.ips files across a real launch (see FRO59's PR).

#include "Plugin/Hosting/PluginScanCrashGuard.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#if JUCE_MAC

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Forks, faults with `signalNumber` inside the guard, and asserts the child terminates promptly via
// the guard's own _exit() rather than hanging on (or surfacing) the OS crash-reporter pipeline the
// guard exists to bypass. No juce/gtest state is touched in the child between fork() and the fault --
// it never returns to anything that could deadlock on a lock inherited mid-held from the parent.
void expectQuietFault(int signalNumber) {
    const pid_t child = fork();
    ASSERT_NE(child, -1) << "fork() failed";

    if (child == 0) {
        synth::installQuietCrashHandlersAndFault(signalNumber);
        _exit(111); // unreachable: installQuietCrashHandlersAndFault() never returns
    }

    int status = 0;
    pid_t waited = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        waited = waitpid(child, &status, WNOHANG);
        if (waited == child)
            break;
        usleep(10 * 1000);
    }

    ASSERT_EQ(waited, child) << "scan-crash-guard child did not exit within the deadline";
    ASSERT_TRUE(WIFEXITED(status)) << "the child should have _exit()ed rather than being taken down "
                                      "by the signal itself";
    EXPECT_EQ(WEXITSTATUS(status), 132);
}

} // namespace

TEST(PluginScanCrashGuardTest, SegfaultExitsQuietlyAndPromptly) { expectQuietFault(SIGSEGV); }
TEST(PluginScanCrashGuardTest, TrapExitsQuietlyAndPromptly) { expectQuietFault(SIGTRAP); }
TEST(PluginScanCrashGuardTest, AbortExitsQuietlyAndPromptly) { expectQuietFault(SIGABRT); }

#endif // JUCE_MAC
