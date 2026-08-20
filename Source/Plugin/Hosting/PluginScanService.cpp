#include "PluginScanService.h"
#include <algorithm>
#include <chrono>

namespace synth {

//==============================================================================
// Construction
//==============================================================================

PluginScanService::PluginScanService()
    : childLauncher_(&PluginScanService::launchScanChildProcess)
    , candidateSource_(&PluginScanService::defaultCandidatesForFormat) {}

PluginScanService::~PluginScanService() {
    alive_->store(false);
    cancelScan();
}

//==============================================================================
// The list
//==============================================================================

std::optional<juce::PluginDescription> PluginScanService::resolve(const PluginIdentity& identity) const {
    if (!identity.isValid())
        return std::nullopt;

    const std::lock_guard<std::mutex> lock(mutex_);
    const auto types = knownPlugins_.getTypes();

    // 1. format + uid, and only when it is UNAMBIGUOUS. Two entries sharing a uid (VST3 shells, and
    //    some vendors' families) mean the uid is not actually identifying anything here, so fall
    //    through to the name rather than guessing between plugins the user can tell apart.
    if (identity.uid != 0) {
        const juce::PluginDescription* match = nullptr;
        int matches = 0;
        for (const auto& candidate : types) {
            if (candidate.pluginFormatName == identity.format && candidate.uniqueId == identity.uid) {
                match = &candidate;
                ++matches;
            }
        }
        if (matches == 1)
            return *match;
    }

    // 2. format + exact name.
    if (identity.name.isNotEmpty()) {
        const juce::PluginDescription* match = nullptr;
        int matches = 0;
        for (const auto& candidate : types) {
            if (candidate.pluginFormatName == identity.format && candidate.name == identity.name) {
                match = &candidate;
                ++matches;
            }
        }
        if (matches == 1)
            return *match;
    }

    // 3. Nothing we can name confidently.
    return std::nullopt;
}

std::vector<juce::PluginDescription> PluginScanService::getKnownPlugins() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto types = knownPlugins_.getTypes();
    return std::vector<juce::PluginDescription>(types.begin(), types.end());
}

int PluginScanService::getNumKnownPlugins() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return knownPlugins_.getNumTypes();
}

std::vector<PluginIdentity> PluginScanService::getKnownPluginIdentities() const {
    auto descriptions = getKnownPlugins();

    std::vector<PluginIdentity> identities;
    identities.reserve(descriptions.size());
    for (const auto& description : descriptions)
        identities.push_back(PluginIdentity::fromDescription(description));

    std::sort(identities.begin(), identities.end(), [](const PluginIdentity& a, const PluginIdentity& b) {
        const int byName = a.name.compareIgnoreCase(b.name);
        return byName != 0 ? byName < 0 : a.format < b.format;
    });
    return identities;
}

juce::StringArray PluginScanService::getBlacklist() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return knownPlugins_.getBlacklistedFiles();
}

void PluginScanService::clearBlacklist() {
    const std::lock_guard<std::mutex> lock(mutex_);
    knownPlugins_.clearBlacklistedFiles();
}

void PluginScanService::clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    knownPlugins_.clear();
    knownPlugins_.clearBlacklistedFiles();
}

//==============================================================================
// Persistence
//==============================================================================

std::unique_ptr<juce::XmlElement> PluginScanService::toXml() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return knownPlugins_.createXml();
}

void PluginScanService::loadFromXml(const juce::XmlElement& xml) {
    const std::lock_guard<std::mutex> lock(mutex_);
    knownPlugins_.recreateFromXml(xml);
}

//==============================================================================
// Seams
//==============================================================================

void PluginScanService::setChildLauncher(ChildLauncher launcher) {
    const std::lock_guard<std::mutex> lock(mutex_);
    childLauncher_ = launcher ? std::move(launcher) : ChildLauncher(&PluginScanService::launchScanChildProcess);
}

void PluginScanService::setCandidateSource(CandidateSource source) {
    const std::lock_guard<std::mutex> lock(mutex_);
    candidateSource_ = source ? std::move(source) : CandidateSource(&PluginScanService::defaultCandidatesForFormat);
}

void PluginScanService::setScanTimeoutMs(int timeoutMs) {
    const std::lock_guard<std::mutex> lock(mutex_);
    scanTimeoutMs_ = juce::jmax(1, timeoutMs);
}

int PluginScanService::getScanTimeoutMs() const noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    return scanTimeoutMs_;
}

//==============================================================================
// Scanning
//==============================================================================

void PluginScanService::postToMessageThread(std::function<void()> fn) {
    if (fn == nullptr)
        return;

    juce::MessageManager::callAsync([alive = alive_, fn = std::move(fn)] {
        if (alive->load())
            fn();
    });
}

void PluginScanService::scanAsync(const juce::StringArray& formatNames, ProgressFn progress, CompletionFn completion) {
    if (scanning_.exchange(true)) {
        // Already scanning. Report a no-op completion rather than silently dropping the request:
        // a caller that put the UI into a "scanning" state needs its callback either way.
        if (completion != nullptr) {
            Result result;
            result.cancelled = true;
            postToMessageThread([completion, result] { completion(result); });
        }
        return;
    }

    // The previous scan's thread has finished (scanning_ was false) but may not be joined yet.
    if (scanThread_.joinable())
        scanThread_.join();

    cancelRequested_.store(false);

    scanThread_ =
        std::thread([this, formatNames, progress = std::move(progress), completion = std::move(completion)]() mutable {
            runScan(formatNames, std::move(progress), std::move(completion));
        });
}

void PluginScanService::runScan(juce::StringArray formatNames, ProgressFn progress, CompletionFn completion) {
    // Snapshot the seams once: they are message-thread state and must not be read per candidate.
    ChildLauncher launcher;
    CandidateSource candidateSource;
    int timeoutMs = kDefaultScanTimeoutMs;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        launcher = childLauncher_;
        candidateSource = candidateSource_;
        timeoutMs = scanTimeoutMs_;
    }

    // Enumerate everything up front so `total` is fixed and progress is monotonic — a bar that grows
    // its own maximum halfway through reads as the scan going backwards.
    struct Candidate {
        juce::String format;
        juce::String fileOrIdentifier;
    };
    std::vector<Candidate> candidates;
    for (const auto& formatName : formatNames) {
        if (cancelRequested_.load())
            break;
        if (candidateSource == nullptr)
            continue;
        for (const auto& fileOrIdentifier : candidateSource(formatName))
            candidates.push_back({formatName, fileOrIdentifier});
    }

    Result result;
    result.total = (int)candidates.size();

    int scanned = 0;
    for (const auto& candidate : candidates) {
        if (cancelRequested_.load()) {
            result.cancelled = true;
            break;
        }

        ++scanned;
        if (progress != nullptr) {
            const int total = result.total;
            const auto what = candidate.fileOrIdentifier;
            postToMessageThread([progress, what, scanned, total] { progress(what, scanned, total); });
        }

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (knownPlugins_.getBlacklistedFiles().contains(candidate.fileOrIdentifier)) {
                // Never launched again: the last scan of this file took a process down with it, and
                // repeating that every launch is how a scan turns into an unrecoverable crash loop.
                ++result.skipped;
                continue;
            }
        }

        juce::String xmlText;
        const bool ok =
            launcher != nullptr && launcher(candidate.format, candidate.fileOrIdentifier, timeoutMs, xmlText);
        const XmlFold fold = ok ? addTypesFromXml(xmlText) : XmlFold{};

        // A crash, a timeout, a non-zero exit and "ran fine but described nothing" all land here and
        // are all treated the same: the file sits in a plugin folder and is not a plugin we can host,
        // so probing it again every launch costs a process and buys nothing.
        // NOTE: `parsed`, not `added` — a rescan of an already-known plugin legitimately adds zero.
        if (!ok || fold.parsed == 0) {
            const std::lock_guard<std::mutex> lock(mutex_);
            knownPlugins_.addToBlacklist(candidate.fileOrIdentifier);
            ++result.failed;
            continue;
        }

        result.added += fold.added;
    }

    scanning_.store(false, std::memory_order_release);

    if (completion != nullptr)
        postToMessageThread([completion, result] { completion(result); });
}

void PluginScanService::cancelScan() {
    cancelRequested_.store(true);
    if (scanThread_.joinable())
        scanThread_.join();
    scanning_.store(false, std::memory_order_release);
    cancelRequested_.store(false);
}

PluginScanService::XmlFold PluginScanService::addTypesFromXml(const juce::String& xmlText) {
    XmlFold fold;

    auto xml = juce::parseXML(xmlText);
    if (xml == nullptr)
        return fold;

    juce::KnownPluginList parsed;
    if (xml->hasTagName("PLUGIN")) {
        // A bare description element — accepted so a launcher does not have to wrap a single result.
        juce::PluginDescription description;
        if (!description.loadFromXml(*xml))
            return fold;
        parsed.addType(description);
    } else {
        parsed.recreateFromXml(*xml);
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& description : parsed.getTypes()) {
        ++fold.parsed;
        if (knownPlugins_.addType(description))
            ++fold.added;
    }
    return fold;
}

//==============================================================================
// The default seams
//==============================================================================

bool PluginScanService::isValidScanToken(const juce::String& token) {
    return token.isNotEmpty() && token.containsOnly("0123456789abcdefABCDEF");
}

juce::String PluginScanService::childXmlBeginMarker(const juce::String& token) {
    return isValidScanToken(token) ? juce::String(kChildXmlBeginPrefix) + token + ">>>" : juce::String();
}

juce::String PluginScanService::childXmlEndMarker(const juce::String& token) {
    return isValidScanToken(token) ? juce::String(kChildXmlEndPrefix) + token + ">>>" : juce::String();
}

juce::String PluginScanService::makeScanToken() { return juce::Uuid().toString(); }

juce::String PluginScanService::extractChildXml(const juce::String& processOutput, const juce::String& token) {
    const juce::String beginMarker = childXmlBeginMarker(token);
    const juce::String endMarker = childXmlEndMarker(token);
    if (beginMarker.isEmpty() || endMarker.isEmpty())
        return {};

    // LAST block, not the first: the plugin being scanned runs inside that child, so anything it
    // printed while loading came BEFORE the document the child prints on its way out.
    const int begin = processOutput.lastIndexOf(beginMarker);
    if (begin < 0)
        return {};

    const int bodyStart = begin + beginMarker.length();
    const int end = processOutput.indexOf(bodyStart, endMarker);
    if (end < 0)
        return {};

    return processOutput.substring(bodyStart, end).trim();
}

juce::StringArray PluginScanService::defaultCandidatesForFormat(const juce::String& formatName) {
    juce::AudioPluginFormatManager manager;
    addHostedPluginFormats(manager);

    for (auto* format : manager.getFormats()) {
        if (format == nullptr || format->getName() != formatName)
            continue;
        // AudioUnit returns an empty search path and enumerates through the OS instead; VST3 walks
        // the platform's plugin folders. `true` for asynchronous-instantiation plugins: refusing
        // them here would hide plugins we can in fact host.
        return format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), /*recursive=*/true,
                                             /*allowPluginsWhichRequireAsynchronousInstantiation=*/true);
    }
    return {};
}

bool PluginScanService::launchScanChildProcess(const juce::String& formatName, const juce::String& fileOrIdentifier,
                                               int timeoutMs, juce::String& xmlOut) {
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    if (!executable.existsAsFile())
        return false;

    // Fresh per launch: the output we accept back has to be stamped with THIS token.
    const juce::String token = makeScanToken();

    juce::StringArray command;
    command.add(executable.getFullPathName());
    command.add(PluginScanService::kScanArgvFlag);
    command.add(formatName);
    command.add(fileOrIdentifier);
    command.add(token);

    juce::ChildProcess child;
    // stdout only: a plugin's own stderr chatter stays on our stderr instead of being interleaved
    // into the document we are about to parse.
    if (!child.start(command, juce::ChildProcess::wantStdOut))
        return false;

    // readAllProcessOutput() blocks until the pipe closes, which is exactly the hang we are guarding
    // against — so a watchdog kills the child on the deadline, closing the pipe and freeing the read.
    // Reading in the scanning thread rather than the watchdog keeps the pipe drained, so a chatty
    // plugin cannot deadlock by filling the buffer while we wait.
    std::atomic<bool> finished{false};
    std::atomic<bool> timedOut{false};
    std::thread watchdog([&child, &finished, &timedOut, timeoutMs] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!finished.load()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                timedOut.store(true);
                child.kill();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    const juce::String output = child.readAllProcessOutput();
    finished.store(true);
    watchdog.join();

    if (timedOut.load())
        return false;

    child.waitForProcessToFinish(1000);
    if (child.getExitCode() != 0)
        return false;

    const auto xml = extractChildXml(output, token);
    if (xml.isEmpty())
        return false;

    xmlOut = xml;
    return true;
}

//==============================================================================
// Child mode
//==============================================================================

std::optional<int> runPluginScanChildMode(const juce::StringArray& args, juce::String& xmlOut) {
    const int flagIndex = args.indexOf(PluginScanService::kScanArgvFlag);
    if (flagIndex < 0)
        return std::nullopt; // an ordinary app launch — the caller starts the application

    // From here on this process IS the scanner, so every exit path returns a code.
    if (flagIndex + 3 >= args.size())
        return 1; // needs a format, a file/identifier and the parent's token

    const auto formatName = args[flagIndex + 1];
    const auto fileOrIdentifier = args[flagIndex + 2];
    const auto token = args[flagIndex + 3];
    if (formatName.isEmpty() || fileOrIdentifier.isEmpty())
        return 1;
    if (!PluginScanService::isValidScanToken(token))
        return 1; // no token, no way to stamp output the parent will accept

    // GUI initialisation before touching a format: AudioUnit needs a run loop to enumerate
    // components, and several VST3s assume a message thread exists during instantiation. This is
    // NOT the app — no window, no engine, no settings file.
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioPluginFormatManager manager;
    addHostedPluginFormats(manager);

    juce::AudioPluginFormat* format = nullptr;
    for (auto* candidate : manager.getFormats())
        if (candidate != nullptr && candidate->getName() == formatName)
            format = candidate;

    if (format == nullptr)
        return 1; // a format this build cannot host — the parent asked for something impossible

    // THE line this whole process exists to isolate: a stranger's binary is loaded and interrogated.
    // If it crashes or hangs, it takes this process and nothing else.
    juce::OwnedArray<juce::PluginDescription> found;
    juce::KnownPluginList list;
    list.scanAndAddFile(fileOrIdentifier, /*dontRescanIfAlreadyInList=*/false, found, *format);
    list.scanFinished();

    if (list.getNumTypes() == 0)
        return 1; // not a plugin we can host; the parent blacklists it

    if (auto xml = list.createXml())
        xmlOut = PluginScanService::childXmlBeginMarker(token) + "\n" + xml->toString() + "\n" +
                 PluginScanService::childXmlEndMarker(token);

    return xmlOut.isEmpty() ? 1 : 0;
}

} // namespace synth
