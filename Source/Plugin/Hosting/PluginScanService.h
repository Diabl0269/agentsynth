#pragma once

#include "HostedPluginBackend.h"
#include <atomic>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace synth {

/**
 * The scan list: what third-party plugins this machine has, and the out-of-process scan that finds
 * them.
 *
 * -- Why a child process ------------------------------------------------------------------------
 *
 * Scanning means loading a stranger's binary into our address space and calling into it. A fair
 * number of shipping plugins crash, hang, or pop a modal window when probed. Doing that in the app
 * means one bad plugin takes the whole editor down — with the user's unsaved patch — every launch,
 * and there is nothing they can do about it short of deleting the plugin.
 *
 * So each candidate is scanned by a SEPARATE process: we re-launch our own executable with
 * `--scan-plugin <format> <fileOrIdentifier>`, that process loads exactly one plugin, prints its
 * juce::PluginDescription as XML, and exits. A crash kills the child; a hang is killed by us on a
 * timeout. Either way the parent records the failure, blacklists the candidate so the next scan does
 * not step on the same mine, and moves on to the next one.
 *
 * -- The three seams ----------------------------------------------------------------------------
 *
 * There is no third-party plugin we can check into this repo, and CI machines have none installed,
 * so everything above would be untestable if it were hard-wired. Three injection points make the
 * whole scan exercisable with no plugin binaries at all:
 *
 *   • `CandidateSource`  — what to scan. Default: the format's own default search paths, which are
 *                          by definition machine-dependent.
 *   • `ChildLauncher`    — how to scan one candidate. Default: launchScanChildProcess() below.
 *                          A test hands back canned XML for a "good" plugin and `false` for a
 *                          "crashing" one, which is precisely what a crash looks like from here.
 *   • `setScanTimeoutMs` — how long to wait. Tests turn 15 s into milliseconds.
 *
 * -- Ownership and persistence -------------------------------------------------------------------
 *
 * The service never touches settings: Core does not know about juce::ApplicationProperties (house
 * rule, same as the audio device state). The OWNER — MainComponent on the standalone path —
 * calls toXml() after a scan and stashes the string under "pluginScanList", and calls loadFromXml()
 * on startup. A plugin build of ourselves installs no service at all: inside a host, the host owns
 * plugin discovery, and a nested scan would fork the DAW.
 *
 * -- Threading ------------------------------------------------------------------------------------
 *
 * scanAsync() runs the whole scan on one background thread; progress and completion are posted back
 * to the message thread. Every read (resolve, getKnownPlugins, toXml, the blacklist) is mutex-guarded
 * against that thread, so the UI can query the list mid-scan. The destructor cancels and joins, and
 * posted callbacks carry a shared liveness flag, so a service destroyed with a scan in flight cannot
 * leave a callback pointing at freed memory.
 */
class PluginScanService {
public:
    //==============================================================================
    // Seams
    //==============================================================================

    /** Scans ONE candidate out of process. Returns true and fills `xmlOut` with the plugin's
     *  description XML (a `<KNOWNPLUGINS>` document, or a bare `<PLUGIN>` element) on success;
     *  returns false for a crash, a timeout, a non-zero exit, or unparseable output — the scan
     *  treats all four identically, because from here they are indistinguishable and the response
     *  is the same. */
    using ChildLauncher = std::function<bool(const juce::String& formatName, const juce::String& fileOrIdentifier,
                                             int timeoutMs, juce::String& xmlOut)>;

    /** Every fileOrIdentifier worth probing for `formatName`, in scan order. */
    using CandidateSource = std::function<juce::StringArray(const juce::String& formatName)>;

    /** Message thread. `scanned` counts candidates finished including this one; `total` is fixed for
     *  the whole scan (candidates are enumerated up front so progress is monotonic). */
    using ProgressFn = std::function<void(const juce::String& fileOrIdentifier, int scanned, int total)>;

    struct Result {
        int total = 0;   ///< candidates enumerated
        int added = 0;   ///< NEW descriptions added to the list
        int failed = 0;  ///< crashed / timed out / produced nothing — all newly blacklisted
        int skipped = 0; ///< already blacklisted, so never launched
        bool cancelled = false;
    };

    /** Message thread, exactly once per scanAsync() call — including a cancelled one. */
    using CompletionFn = std::function<void(const Result&)>;

    /** Generous on purpose: a cold-cache VST3 on a spinning disk can genuinely take ten seconds to
     *  report itself, and killing a slow-but-honest plugin blacklists it for good. */
    static constexpr int kDefaultScanTimeoutMs = 15000;

    /** The argv flag the parent passes and runPluginScanChildMode() looks for. */
    static constexpr const char* kScanArgvFlag = "--scan-plugin";

    PluginScanService();
    ~PluginScanService();

    PluginScanService(const PluginScanService&) = delete;
    PluginScanService& operator=(const PluginScanService&) = delete;

    //==============================================================================
    // The list
    //==============================================================================

    /**
     * Identity -> description, or nullopt. Precedence, in order:
     *
     *   1. **format + uniqueId**, when the identity carries a uid. A uid is stable across machines
     *      and survives the user renaming the plugin file, so it is the strongest key we have.
     *      If it matches EXACTLY one entry, that entry wins outright.
     *   2. **format + exact name**, used when the identity has no uid (an old or hand-written
     *      patch), when nothing matched by uid, or when SEVERAL entries share the uid — the last
     *      case being real: VST3 shells and some vendors ship families that collide, and picking an
     *      arbitrary one of two plugins the user can tell apart by name is worse than using the name.
     *   3. Otherwise nullopt, which is what leaves a HostedPluginModule a "not installed"
     *      placeholder that still remembers what it wants.
     *
     * Name matching is exact and case-sensitive; a near-miss is a different plugin.
     */
    std::optional<juce::PluginDescription> resolve(const PluginIdentity& identity) const;

    std::vector<juce::PluginDescription> getKnownPlugins() const;
    int getNumKnownPlugins() const;

    /** The identities the module library shows, sorted by name. */
    std::vector<PluginIdentity> getKnownPluginIdentities() const;

    /** fileOrIdentifiers a previous scan proved unsafe. Skipped by every later scan until cleared. */
    juce::StringArray getBlacklist() const;

    /** Forgets every blacklisted entry, so the next scan retries them. The only way back in for a
     *  plugin the user has since updated or repaired. */
    void clearBlacklist();

    /** Drops the whole list AND the blacklist. */
    void clear();

    //==============================================================================
    // Persistence — driven by the owner (see the class comment)
    //==============================================================================

    /** The list and the blacklist as one XML document (juce::KnownPluginList's own format). */
    std::unique_ptr<juce::XmlElement> toXml() const;

    /** Replaces the list and the blacklist from a document toXml() produced. */
    void loadFromXml(const juce::XmlElement& xml);

    //==============================================================================
    // Seam installation (message thread, and never mid-scan)
    //==============================================================================

    void setChildLauncher(ChildLauncher launcher);
    void setCandidateSource(CandidateSource source);
    void setScanTimeoutMs(int timeoutMs);
    int getScanTimeoutMs() const noexcept;

    //==============================================================================
    // Scanning
    //==============================================================================

    /** Message thread. Enumerates candidates for each format, then probes each one that is not
     *  blacklisted through the child launcher. A second call while a scan is running is ignored
     *  (its completion callback still fires, with `cancelled` set, so a caller never hangs waiting
     *  for a callback that will not come). */
    void scanAsync(const juce::StringArray& formatNames, ProgressFn progress, CompletionFn completion);

    bool isScanning() const noexcept { return scanning_.load(std::memory_order_acquire); }

    /** Asks the scan to stop after the candidate in flight and waits for the thread. Safe to call
     *  when nothing is running. */
    void cancelScan();

    //==============================================================================
    // The default seams — public so the child-mode entry point and tests can reach them
    //==============================================================================

    /** The default ChildLauncher: re-launches this executable with `--scan-plugin`. */
    static bool launchScanChildProcess(const juce::String& formatName, const juce::String& fileOrIdentifier,
                                       int timeoutMs, juce::String& xmlOut);

    /** The default CandidateSource: the format's own default locations, searched recursively. */
    static juce::StringArray defaultCandidatesForFormat(const juce::String& formatName);

    /** Sentinels the child wraps its XML in, so a plugin that prints its own banner to stdout during
     *  a scan cannot corrupt the document we parse. */
    static constexpr const char* kChildXmlBegin = "<<<AGENTSYNTH-SCAN-BEGIN>>>";
    static constexpr const char* kChildXmlEnd = "<<<AGENTSYNTH-SCAN-END>>>";

    /** Pulls the description document back out of a child's raw stdout, discarding anything printed
     *  outside the sentinels; empty when the child never emitted a complete pair. Public because it
     *  is the only part of launchScanChildProcess() with logic of its own — the rest is
     *  juce::ChildProcess plumbing, which needs a real child (and therefore the real app binary) to
     *  exercise. */
    static juce::String extractChildXml(const juce::String& processOutput);

private:
    struct XmlFold {
        int parsed = 0; ///< descriptions the document contained — 0 means the child said nothing useful
        int added = 0;  ///< of those, how many the list did not already have
    };

    /** Background thread. Folds a child's XML document into the list. */
    XmlFold addTypesFromXml(const juce::String& xmlText);

    void runScan(juce::StringArray formatNames, ProgressFn progress, CompletionFn completion);

    /** Posts `fn` to the message thread, dropped if this service is gone by the time it runs. */
    void postToMessageThread(std::function<void()> fn);

    mutable std::mutex mutex_;
    juce::KnownPluginList knownPlugins_;

    ChildLauncher childLauncher_;
    CandidateSource candidateSource_;
    int scanTimeoutMs_ = kDefaultScanTimeoutMs;

    std::thread scanThread_;
    std::atomic<bool> scanning_{false};
    std::atomic<bool> cancelRequested_{false};

    // Shared with every posted callback: the destructor clears it, so a callback that outlives us is
    // dropped instead of dereferencing freed memory. Both the store and the loads happen on the
    // message thread, so no ordering subtleties beyond the atomic itself.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

//==============================================================================
// Child mode
//==============================================================================

/**
 * The `--scan-plugin <format> <fileOrIdentifier>` half of the out-of-process scan.
 *
 * Returns nullopt when `args` is an ordinary app launch — the caller then proceeds to start the
 * application normally. Otherwise this IS the whole process: it scans exactly one plugin, writes the
 * description document (sentinel-wrapped) into `xmlOut` for the caller to print, and returns the
 * process exit code — 0 when at least one plugin type was found, 1 for bad arguments, an unknown
 * format, or a file that yielded nothing.
 *
 * No GUI, no AudioEngine, no settings file: a scan child that touched the settings file would race
 * the parent that spawned it, and one that built an engine would open an audio device the user is
 * already using.
 *
 * Lives in Core (not in Main.cpp) so its argument handling and exit-code semantics are unit-testable
 * in process. Only the standalone app calls it: a VST3/AU build of ourselves never scans, so it has
 * no entry point to intercept.
 */
std::optional<int> runPluginScanChildMode(const juce::StringArray& args, juce::String& xmlOut);

} // namespace synth
