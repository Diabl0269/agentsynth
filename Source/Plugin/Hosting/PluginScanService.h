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
 * `--scan-plugin <format> <fileOrIdentifier> <token>`, that process loads exactly one plugin, prints
 * its juce::PluginDescription as XML wrapped in sentinels stamped with `token`, and exits. The token
 * is fresh per launch and the parent accepts only the last block carrying its own — a plugin that
 * prints a forged description while it loads must not be able to write the parent's plugin list.
 *
 * A crash kills the child; a hang is killed by us on a timeout. Either way the parent records the
 * failure, blacklists the candidate so the next scan does not step on the same mine, and moves on to
 * the next one.
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
 *
 * -- One shared owner, several consumers (FRO44) --------------------------------------------------
 *
 * The library sidebar is no longer the only thing that wants this list — an Instrument-track "Plugin
 * picker" wants it too, before the user has ever opened the sidebar's PLUGINS section. Rather than
 * each consumer owning (and re-scanning with) its own service, every consumer reads the SAME
 * instance (`MainComponent::getPluginScanService()`) and calls `ensureScanned()` when it wants the
 * list populated:
 *
 *   - The FIRST call actually starts a scan (`scanAsync()`, on the usual background thread).
 *   - Every later call, from any consumer, while that scan is in flight OR after it has already
 *     completed, is a no-op — this is "populate the list once without waiting for the sidebar",
 *     not "keep rescanning on every access". A cheap re-run of the cached-from-disk list is still
 *     visible immediately via `getKnownPluginIdentities()`; nothing here re-launches a child process
 *     per candidate on every call.
 *   - Every registered `Listener` is notified (`pluginScanCompleted`, message thread) once the scan
 *     finishes, regardless of which consumer's call actually triggered it — so a picker opened AFTER
 *     the sidebar already asked still finds out when the scan it never itself started completes.
 *
 * A caller that wants an unconditional fresh scan (the sidebar's own "Scan for plugins..." row)
 * still calls `scanAsync()` directly — `ensureScanned()` is purely the "make sure this has happened
 * at least once" entry point, not a replacement for the manual rescan.
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

    /** FRO44: registered by every consumer of the ONE shared service (the library sidebar, a future
     *  picker) so each finds out when a scan completes without having to be the one that triggered
     *  it — see the class comment's "One shared owner, several consumers" section. */
    class Listener {
    public:
        virtual ~Listener() = default;

        /** Message thread. Fired once per `scanAsync()` call that actually ran to completion or was
         *  cancelled — i.e. once per real scan, not once per `ensureScanned()`/`scanAsync()` call
         *  site. Never fired for the "a scan was already running" early-return inside `scanAsync()`
         *  (that caller gets `Result::cancelled` back through its own `completion`, if it gave one;
         *  the scan actually in flight will notify every listener, including that caller if it is
         *  also registered, when it finishes). */
        virtual void pluginScanCompleted(const Result& result) = 0;
    };

    /** No-op if `listener` is null or already registered. Never call from inside
     *  `pluginScanCompleted` on a DIFFERENT listener's callback — only the listener's own removal of
     *  itself is safe there (the notification loop works off a snapshot). */
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

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

    /** FRO44: "make sure a scan has been requested at least once" — the eager-population entry
     *  point every consumer (app startup, the sidebar, a future picker) can call without worrying
     *  about who else already asked. The FIRST call this service instance ever sees starts
     *  `scanAsync(formatNames, nullptr, nullptr)`; every later call — concurrent with that scan or
     *  after it has already finished — is a no-op. Every registered `Listener` still hears
     *  `pluginScanCompleted` when the one real scan finishes, whether or not it was this call that
     *  started it. Message thread only, like `scanAsync()`.
     *
     *  IMPORTANT for a caller that arrives AFTER the one real scan has already completed (the normal
     *  case once the app has been running a while — the eager startup scan is long done by the time
     *  a picker opens): its own `ensureScanned()` call is a no-op and it gets NO `pluginScanCompleted`
     *  for a scan that already happened before it registered. Such a caller must read
     *  `getKnownPluginIdentities()` synchronously right after calling `ensureScanned()` (which is
     *  correct immediately whether or not a scan is still running — it is never empty-then-magically-
     *  fills for a reason other than a *listened-for* completion) AND register a `Listener` for any
     *  scan that starts later. Reading the list only from inside `pluginScanCompleted` misses
     *  whatever was already there. */
    void ensureScanned(const juce::StringArray& formatNames);

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

    /** Sentinel prefixes the child wraps its XML in, so a plugin that prints its own banner to stdout
     *  during a scan cannot corrupt the document we parse. A complete sentinel is prefix + the
     *  launch's token + ">>>" — see childXmlBeginMarker(). */
    static constexpr const char* kChildXmlBeginPrefix = "<<<AGENTSYNTH-SCAN-BEGIN:";
    static constexpr const char* kChildXmlEndPrefix = "<<<AGENTSYNTH-SCAN-END:";

    /** The sentinels for one launch's token. */
    static juce::String childXmlBeginMarker(const juce::String& token);
    static juce::String childXmlEndMarker(const juce::String& token);

    /** A fresh token for one child launch: the parent generates it, passes it as the third
     *  `--scan-plugin` operand, and accepts only output stamped with it. Plain hex — see
     *  isValidScanToken(), which both halves apply before the token reaches a sentinel. */
    static juce::String makeScanToken();

    /** Non-empty and hex only. A token that fails this never gets as far as building a sentinel, on
     *  either side of the protocol. */
    static bool isValidScanToken(const juce::String& token);

    /** Pulls the description document back out of a child's raw stdout, discarding anything printed
     *  outside the sentinels; empty when the child never emitted a complete pair stamped with
     *  `token`. The LAST such block wins: the scanned plugin runs inside that child and can print
     *  whatever it likes while it loads, but the real document is printed on the way out. Public
     *  because it is the only part of launchScanChildProcess() with logic of its own — the rest is
     *  juce::ChildProcess plumbing, which needs a real child (and therefore the real app binary) to
     *  exercise. */
    static juce::String extractChildXml(const juce::String& processOutput, const juce::String& token);

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

    /** Posts `pluginScanCompleted(result)` to every registered Listener, message thread, snapshotting
     *  the list first so a listener that removes itself (or another) mid-callback cannot invalidate
     *  the loop. Called once per real scan (see the Listener class comment). */
    void notifyListeners(const Result& result);

    mutable std::mutex mutex_;
    juce::KnownPluginList knownPlugins_;

    ChildLauncher childLauncher_;
    CandidateSource candidateSource_;
    int scanTimeoutMs_ = kDefaultScanTimeoutMs;

    std::thread scanThread_;
    std::atomic<bool> scanning_{false};
    std::atomic<bool> cancelRequested_{false};

    // FRO44's "exactly once" latch for ensureScanned(): true the instant the first call starts a
    // scan, so a second/third caller (whether concurrent with that scan or long after it finished)
    // never launches another one. Independent of `scanning_`, which only reflects "right now".
    std::atomic<bool> ensureScanRequested_{false};

    // Listeners are plain observer pointers, exactly like JUCE's own ChangeBroadcaster — the owner
    // (MainComponent, a future picker) is responsible for removeListener() before it is destroyed.
    std::vector<Listener*> listeners_; // guarded by mutex_

    // Shared with every posted callback: the destructor clears it, so a callback that outlives us is
    // dropped instead of dereferencing freed memory. Both the store and the loads happen on the
    // message thread, so no ordering subtleties beyond the atomic itself.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

//==============================================================================
// Child mode
//==============================================================================

/**
 * The `--scan-plugin <format> <fileOrIdentifier> <token>` half of the out-of-process scan.
 *
 * Returns nullopt when `args` is an ordinary app launch — the caller then proceeds to start the
 * application normally. Otherwise this IS the whole process: it scans exactly one plugin, writes the
 * description document (wrapped in the sentinels for `token`) into `xmlOut` for the caller to print,
 * and returns the process exit code — 0 when at least one plugin type was found, 1 for bad arguments
 * (a missing or malformed token included), an unknown format, or a file that yielded nothing.
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
