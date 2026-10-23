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

namespace {

// A user folder keeps its own name unless the user gave it one; the place and every entry in it carry the same name.
void applyFolderNames(std::vector<SoundPlace>& places, std::vector<SoundEntry>& entries,
                      const std::map<juce::String, juce::String>& names) {
    std::map<juce::String, juce::String> byPlace;
    for (auto& place : places) {
        if (place.isPack)
            continue;
        const auto given = names.find(place.root.getFullPathName());
        place.name = given != names.end() && given->second.isNotEmpty() ? given->second : place.root.getFileName();
        byPlace[place.id] = place.name;
    }
    for (auto& e : entries)
        if (const auto it = byPlace.find(e.placeId); it != byPlace.end())
            e.placeName = it->second;
}

} // namespace

void SoundIndex::setUserFolderNames(const std::map<juce::String, juce::String>& names) {
    std::shared_ptr<const SoundSnapshot> current;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (names == folderNames_)
            return;
        folderNames_ = names;
        current = snapshot_;
    }
    publish(current->places, current->entries, current->truncated, current->scanning);
}

bool SoundIndex::rescanIfFoldersChanged() {
    if (worker_ == nullptr)
        return false;
    for (const auto& place : snapshot()->places)
        if (!place.isPack && place.missing == place.root.isDirectory()) {
            rescan();
            return true;
        }
    return false;
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
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        applyFolderNames(places, entries, folderNames_);
    }
    std::map<juce::String, int> counts;
    for (size_t i = 0; i < entries.size(); ++i) {
        snap->byPath[entries[i].path] = i;
        ++counts[entries[i].placeId];
    }
    for (auto& place : places)
        if (const auto it = counts.find(place.id); it != counts.end())
            place.soundCount = it->second;
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
    std::map<juce::String, juce::String> names;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        users = userFolders_;
        cap = cap_;
        names = folderNames_;
    }
    auto places = discoverPlaces(packRoots_(), users, names);
    for (auto& place : places)
        place.scanned = false;

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
        publish(places, std::move(shown), cachedTruncated, true);
    }

    ScanLimits limits;
    limits.cap = cap;
    limits.headersRead = &headersRead_;
    limits.shouldExit = shouldExit;
    limits.progressIntervalMs = progressIntervalMs_;
    limits.beforeFile = scanHold_;
    // What a mid-scan publish shows: the entries found so far, plus the cached ones of every place the walk has not
    // reached, so a place waiting its turn does not blink out of the lists.
    const auto partial = [&places, &previous](const std::vector<SoundEntry>& sofar) {
        std::set<juce::String> reached;
        for (const auto& e : sofar)
            reached.insert(e.placeId);
        std::set<juce::String> waiting;
        for (const auto& place : places)
            if (!place.scanned && reached.count(place.id) == 0)
                waiting.insert(place.id);
        auto shown = sofar;
        for (const auto& [path, e] : previous)
            if (waiting.count(e.placeId) != 0)
                shown.push_back(e);
        return shown;
    };
    limits.progress = [this, &places, partial](const std::vector<SoundEntry>& sofar) {
        publish(places, partial(sofar), false, true);
    };
    limits.placeDone = [this, &places, partial](size_t done, const std::vector<SoundEntry>& sofar) {
        places[done].scanned = true;
        publish(places, partial(sofar), false, true);
    };
    auto result = scanPlaces(places, previous, limits);

    if (shouldExit())
        return;
    for (auto& place : places)
        place.scanned = true;
    if (cacheFile_ != juce::File())
        writeIndexCache(cacheFile_, result.entries, result.truncated);
    publish(std::move(places), std::move(result.entries), result.truncated, false);
}

} // namespace synth::soundbrowser
