#include "SoundBrowser/SoundIndex.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "SoundBrowser/SoundIndexScan.h"
#include "SoundBrowser/SoundSearch.h"

namespace synth::soundbrowser {

// One long-lived thread that sleeps until a scan is requested, so requests made while a scan runs coalesce into
// exactly one more scan after it.
class SoundIndex::Worker : public juce::Thread {
public:
    explicit Worker(SoundIndex& owner)
        : juce::Thread("SoundIndex scan")
        , owner_(owner) {}

    void request() {
        pending_.store(true);
        notify();
    }
    bool busy() const { return pending_.load() || running_.load(); }

    void run() override {
        while (!threadShouldExit()) {
            if (!pending_.exchange(false)) {
                wait(-1);
                continue;
            }
            running_.store(true);
            owner_.runScan([this] { return threadShouldExit(); });
            running_.store(false);
        }
    }

private:
    SoundIndex& owner_;
    std::atomic<bool> pending_{false};
    std::atomic<bool> running_{false};
};

SoundIndex::SoundIndex(juce::File cacheFile, PackRoots packRoots)
    : cacheFile_(std::move(cacheFile))
    , packRoots_(std::move(packRoots)) {
    if (!packRoots_)
        packRoots_ = [] { return synth::sampler::SoundPackLibrary::roots(); };
    snapshot_ = std::make_shared<const SoundSnapshot>();
}

SoundIndex::~SoundIndex() {
    cancelPendingUpdate();
    if (worker_ != nullptr)
        worker_->stopThread(10000);
}

void SoundIndex::setUserFolders(const std::vector<juce::File>& folders) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (folders == userFolders_)
            return;
        userFolders_ = folders;
    }
    rescan();
}

std::vector<juce::File> SoundIndex::userFolders() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return userFolders_;
}

void SoundIndex::rescan() {
    if (worker_ == nullptr) {
        worker_ = std::make_unique<Worker>(*this);
        worker_->startThread();
    }
    worker_->request();
}

std::shared_ptr<const SoundSnapshot> SoundIndex::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::vector<SoundEntry> SoundIndex::search(const juce::String& query, const SoundFilter& filter) const {
    return searchSnapshot(*snapshot(), query, filter);
}

SoundListing SoundIndex::childrenOf(const juce::String& placeId, const juce::String& relativeFolder,
                                    const SoundFilter& filter) const {
    return listFolder(*snapshot(), placeId, relativeFolder, filter);
}

std::optional<SoundEntry> SoundIndex::find(const juce::String& path) const {
    const auto snap = snapshot();
    const auto it = snap->byPath.find(path);
    if (it == snap->byPath.end())
        return std::nullopt;
    return snap->entries[it->second];
}

bool SoundIndex::isScanning() const { return worker_ != nullptr && worker_->busy(); }

bool SoundIndex::waitUntilIdleForTests(int timeoutMs) {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    while (worker_ != nullptr && worker_->busy()) {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::Thread::sleep(2);
    }
    return true;
}

void SoundIndex::handleAsyncUpdate() {
    listeners_.call([](Listener& l) { l.soundIndexChanged(); });
}

// Builds the lookup tables and per-place counts, then swaps the snapshot in under the lock.
void SoundIndex::publish(std::vector<SoundPlace> places, std::vector<SoundEntry> entries, bool truncated,
                         bool scanning) {
    auto snap = std::make_shared<SoundSnapshot>();
    for (size_t i = 0; i < entries.size(); ++i)
        snap->byPath[entries[i].path] = i;
    for (auto& place : places)
        for (const auto& e : entries)
            if (e.placeId == place.id)
                ++place.soundCount;
    snap->places = std::move(places);
    snap->entries = std::move(entries);
    snap->truncated = truncated;
    snap->scanning = scanning;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(snap);
    }
    triggerAsyncUpdate();
}

void SoundIndex::runScan(const std::function<bool()>& shouldExit) {
    std::vector<juce::File> users;
    size_t cap;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        users = userFolders_;
        cap = cap_;
    }
    auto places = discoverPlaces(packRoots_(), users);

    // The cached entries are both shown at once and the "before" of the incremental pass.
    std::map<juce::String, SoundEntry> previous;
    bool cachedTruncated = false;
    if (!cacheLoaded_) {
        cacheLoaded_ = true;
        std::vector<SoundEntry> cached;
        if (cacheFile_ != juce::File())
            cached = readIndexCache(cacheFile_, cachedTruncated);
        for (auto& e : cached)
            previous[e.path] = std::move(e);
    } else {
        for (const auto& e : snapshot()->entries)
            previous[e.path] = e;
    }
    {
        std::vector<SoundEntry> shown;
        for (const auto& place : places)
            for (const auto& [path, e] : previous)
                if (e.placeId == place.id)
                    shown.push_back(e);
        if (!shown.empty())
            publish(places, std::move(shown), cachedTruncated, true);
    }

    ScanLimits limits;
    limits.cap = cap;
    limits.headersRead = &headersRead_;
    limits.shouldExit = shouldExit;
    limits.progress = [this, &places](const std::vector<SoundEntry>& sofar) { publish(places, sofar, false, true); };
    auto result = scanPlaces(places, previous, limits);

    if (shouldExit())
        return;
    if (cacheFile_ != juce::File())
        writeIndexCache(cacheFile_, result.entries, result.truncated);
    publish(std::move(places), std::move(result.entries), result.truncated, false);
}

} // namespace synth::soundbrowser
