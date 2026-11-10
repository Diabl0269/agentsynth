#pragma once

#include "CrashReportPluginFinder.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

/**
 * What the next launch can tell the user about the previous one ending badly, and the pieces that
 * work it out. Everything here takes its directories as arguments, so tests run on temporary folders.
 *
 * 1. A **running sentinel**: a file named for the process id, written at start and deleted on a clean
 *    quit. Several instances may run at once, so a leftover file only counts as a crash when its
 *    process is gone.
 * 2. On macOS the **crash report** the system wrote after the crash (`Agent Synth-*.ips`, newer than
 *    the sentinel) names the plugin the crashing thread was in (CrashReportPluginFinder).
 * 3. A **strike** per plugin counts how often it has been blamed.
 *
 * Elsewhere there is no report: the sentinel alone says "quit unexpectedly" without a plugin name.
 */
class RunSentinel {
public:
    /** True when a process with this id is alive. Injected so tests need no real processes. */
    using IsProcessAlive = std::function<bool(int pid)>;

    /** Writes `<dir>/running-<pid>`. */
    RunSentinel(const juce::File& dir, int pid);

    /** Deletes the file: the process is leaving normally. */
    void markCleanExit();

    /** The start time of the most recent run that left a sentinel behind with its process gone (that
     *  run ended without a clean quit), or nullopt. Deletes every such stale sentinel, so each
     *  crash is reported once. */
    static std::optional<juce::Time> takeStaleRun(const juce::File& dir, const IsProcessAlive& isAlive);

    static bool processIsAlive(int pid);
    static int currentProcessId();

private:
    juce::File file_;
};

enum class PreviousRunKind {
    CleanOrUnknown, ///< Nothing to say.
    InsidePlugin,   ///< A crash report names a plugin.
    Unexpected      ///< Ended without a clean quit; no plugin is named (no report on this platform).
};

struct PreviousRunOutcome {
    PreviousRunKind kind = PreviousRunKind::CleanOrUnknown;
    CrashedPlugin plugin; ///< Set when kind is InsidePlugin.
    int strikes = 0;      ///< How often this plugin has been blamed, this time included.
};

/** The folder macOS writes crash reports to, empty where there is none. */
juce::File defaultCrashReportsDirectory();

/** Looks at the previous run. `reportsDir` empty means "this platform has no reports", which turns a
 *  stale sentinel into Unexpected; a real folder with no matching report gives CleanOrUnknown (a
 *  force-quit or power loss is not worth a dialog). Records the strike. */
PreviousRunOutcome inspectPreviousRun(const juce::File& stateDir, const juce::File& reportsDir,
                                      const RunSentinel::IsProcessAlive& isAlive);

/** How often `pluginName` has been blamed so far. */
int pluginCrashStrikes(const juce::File& stateDir, const juce::String& pluginName);

} // namespace synth
