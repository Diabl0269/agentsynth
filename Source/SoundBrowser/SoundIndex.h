#pragma once

// The sound index: every sound in the installed packs and the user's folders, found by a background scan that reads
// file headers only. Readers take an immutable snapshot, so no lock is held while searching.

#include "SoundBrowser/SoundEntry.h"
#include <atomic>
#include <functional>
#include <juce_events/juce_events.h>
#include <mutex>

namespace synth::soundbrowser {

class SoundIndex : private juce::AsyncUpdater {
public:
    /** Called on the message thread after a scan published new results (progress or the end). */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void soundIndexChanged() = 0;
    };

    using PackRoots = std::function<std::vector<juce::File>()>;

    static constexpr size_t kEntryCap = 50000;

    /** `cacheFile` is where the index persists (empty: no cache). `packRoots` defaults to the installed pack roots. */
    explicit SoundIndex(juce::File cacheFile = {}, PackRoots packRoots = {});
    ~SoundIndex() override;

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    /** The folders searched besides packs; rescans when the list changed. */
    void setUserFolders(const std::vector<juce::File>& folders);
    std::vector<juce::File> userFolders() const;

    /** Starts (or queues) a background scan; the first one also loads the cache. Message thread. */
    void rescan();
    bool isScanning() const;

    std::shared_ptr<const SoundSnapshot> snapshot() const;
    std::vector<SoundEntry> search(const juce::String& query, const SoundFilter& filter = {}) const;
    SoundListing childrenOf(const juce::String& placeId, const juce::String& relativeFolder,
                            const SoundFilter& filter = {}) const;
    std::optional<SoundEntry> find(const juce::String& path) const;

    // ---- Test seams ----
    void setEntryCapForTests(size_t cap) { cap_ = cap; }
    /** Blocks until no scan is running or queued; false on timeout. Does not pump the message loop. */
    bool waitUntilIdleForTests(int timeoutMs);
    int headersReadForTests() const { return headersRead_.load(); }

private:
    class Worker;

    void handleAsyncUpdate() override;
    void runScan(const std::function<bool()>& shouldExit);
    void publish(std::vector<SoundPlace> places, std::vector<SoundEntry> entries, bool truncated, bool scanning);

    juce::File cacheFile_;
    PackRoots packRoots_;
    juce::ListenerList<Listener> listeners_;

    mutable std::mutex mutex_;
    std::shared_ptr<const SoundSnapshot> snapshot_;
    std::vector<juce::File> userFolders_;
    size_t cap_ = kEntryCap;

    std::atomic<int> headersRead_{0};
    bool cacheLoaded_ = false;
    std::unique_ptr<Worker> worker_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundIndex)
};

} // namespace synth::soundbrowser
