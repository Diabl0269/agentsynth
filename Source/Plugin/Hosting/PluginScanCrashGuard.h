#pragma once

namespace synth {

/**
 * FRO59: installs a handler for the fatal signals a plugin-scan crash turns into (SIGSEGV, SIGBUS,
 * SIGILL, SIGTRAP, SIGABRT, SIGFPE) that terminates the process immediately via `_exit()`, before
 * macOS's own crash reporter (ReportCrash) gets a chance to run. ReportCrash's "quit unexpectedly"
 * dialog and its DiagnosticReports .ips file are produced by a SEPARATE, later stage of a fatal
 * signal's default handling — the kernel's EXC_CRASH escalation, raised right before it would
 * otherwise force-terminate the process for an unhandled signal. A handler that calls `_exit()`
 * instead of returning or re-raising takes the process down through an ordinary, voluntary exit and
 * never reaches that stage, so no dialog and no report.
 *
 * Call ONCE, as early as possible, in any process whose own crash must never surface a dialog —
 * today that is only the real out-of-process plugin-scan child (see the "why a child process" note
 * atop PluginScanService.h). Calling this from the main app itself would be wrong: a real crash
 * there should still report normally, the same as before FRO59. A no-op on every platform but
 * macOS, since only macOS's ReportCrash dialog is what FRO59 is about.
 *
 * Side effect worth knowing: in a Debug build of the scan child specifically, this also swallows
 * `jassert`/`JUCE_BREAK_IN_DEBUGGER` (both raise SIGTRAP), so the child no longer drops into a
 * debugger on an assertion — it exits quietly like any other crash. That is the point: an assertion
 * failure while probing a stranger's plugin is exactly the kind of crash this guard exists for.
 */
void installQuietCrashHandlers();

/**
 * Test-only: installs the same handlers as installQuietCrashHandlers() and then immediately raises
 * `signalNumber` on the calling thread. Never returns. Exists so a test can fork a throwaway child,
 * call this in it, and assert from the parent that the child exits promptly via `_exit()` rather
 * than hanging on (or surfacing) the crash-reporter pipeline this guard exists to bypass.
 */
[[noreturn]] void installQuietCrashHandlersAndFault(int signalNumber);

} // namespace synth
