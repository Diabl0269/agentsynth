#include "PluginCrashRecovery.h"
#include "Branding.h"

#include <juce_data_structures/juce_data_structures.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace synth {

namespace {

constexpr const char* kSentinelPrefix = "running-";
constexpr const char* kStrikesFileName = "plugin-crash-strikes.settings";

std::unique_ptr<juce::PropertiesFile> openStrikes(const juce::File& stateDir) {
    stateDir.createDirectory();
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;
    return std::make_unique<juce::PropertiesFile>(stateDir.getChildFile(kStrikesFileName), options);
}

/** The newest `Agent Synth-*.ips` modified after `since`, or no file. */
juce::File newestReportSince(const juce::File& reportsDir, juce::Time since) {
    juce::File newest;
    juce::Time newestTime;
    const juce::String pattern = juce::String(branding::kProductName) + "-*.ips";
    for (const auto& entry : juce::RangedDirectoryIterator(reportsDir, false, pattern, juce::File::findFiles)) {
        const auto modified = entry.getLastModificationTime();
        if (modified > since && (newest == juce::File() || modified > newestTime)) {
            newest = entry.getFile();
            newestTime = modified;
        }
    }
    return newest;
}

} // namespace

RunSentinel::RunSentinel(const juce::File& dir, int pid)
    : file_(dir.getChildFile(juce::String(kSentinelPrefix) + juce::String(pid))) {
    dir.createDirectory();
    file_.replaceWithText(juce::String(pid));
}

void RunSentinel::markCleanExit() { file_.deleteFile(); }

std::optional<juce::Time> RunSentinel::takeStaleRun(const juce::File& dir, const IsProcessAlive& isAlive) {
    std::optional<juce::Time> latest;
    for (const auto& entry :
         juce::RangedDirectoryIterator(dir, false, juce::String(kSentinelPrefix) + "*", juce::File::findFiles)) {
        const auto file = entry.getFile();
        const int pid = file.getFileName().fromFirstOccurrenceOf(kSentinelPrefix, false, false).getIntValue();
        if (pid > 0 && isAlive != nullptr && isAlive(pid))
            continue; // another instance, still running
        const auto started = entry.getLastModificationTime();
        if (!latest.has_value() || started > *latest)
            latest = started;
        file.deleteFile();
    }
    return latest;
}

bool RunSentinel::processIsAlive(int pid) {
#if JUCE_WINDOWS
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr)
        return false;
    CloseHandle(handle);
    return true;
#else
    return ::kill(pid, 0) == 0 || errno == EPERM;
#endif
}

int RunSentinel::currentProcessId() {
#if JUCE_WINDOWS
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}

juce::File defaultCrashReportsDirectory() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Logs/DiagnosticReports");
#else
    return {};
#endif
}

PreviousRunOutcome inspectPreviousRun(const juce::File& stateDir, const juce::File& reportsDir,
                                      const RunSentinel::IsProcessAlive& isAlive) {
    PreviousRunOutcome outcome;
    const auto started = RunSentinel::takeStaleRun(stateDir, isAlive);
    if (!started.has_value())
        return outcome;

    if (reportsDir == juce::File()) {
        outcome.kind = PreviousRunKind::Unexpected;
        return outcome;
    }

    // ReportCrash writes the file a moment after the process dies, so a report is only ever newer than the run's start.
    const auto report = newestReportSince(reportsDir, *started);
    if (report == juce::File())
        return outcome;
    const auto plugin = CrashReportPluginFinder::find(report.loadFileAsString());
    if (!plugin.has_value())
        return outcome;

    outcome.kind = PreviousRunKind::InsidePlugin;
    outcome.plugin = *plugin;
    auto strikes = openStrikes(stateDir);
    outcome.strikes = strikes->getIntValue(plugin->name, 0) + 1;
    strikes->setValue(plugin->name, outcome.strikes);
    strikes->saveIfNeeded();
    return outcome;
}

int pluginCrashStrikes(const juce::File& stateDir, const juce::String& pluginName) {
    return openStrikes(stateDir)->getIntValue(pluginName, 0);
}

} // namespace synth
