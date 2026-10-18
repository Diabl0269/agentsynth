#pragma once

// The app's one sound pack installer: fetches the hosted pack list, downloads and installs packs on background
// threads, removes them, and reports progress to whoever is listening (Settings > Sound packs, the Sampler picker).

#include "SoundPacks/SoundPackFetch.h"
#include "SoundPacks/SoundPackIndex.h"
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace synth::soundpacks {

struct InstallJob;

class SoundPackInstaller {
public:
    enum class State { NotInstalled, Downloading, Verifying, Installing, Installed, UpdateAvailable };
    enum class IndexStatus { NotLoaded, Loading, Loaded, Failed };

    /** One pack as the UI shows it: the hosted entry merged with what is installed on disk. */
    struct Row {
        juce::String id, name;
        juce::String installedVersion; ///< Empty when not installed.
        juce::String latestVersion;    ///< From the hosted list; empty for a pack the list does not know.
        juce::String licence, licenceUrl, credit;
        juce::int64 downloadBytes = 0;
        juce::int64 installedBytes = 0;
        std::vector<IndexInstrument> instruments;
        State state = State::NotInstalled;
        float progress = 0.0f; ///< 0 to 1 while downloading; negative when the size is not known.
        juce::String error;    ///< The last attempt's failure in plain words; cleared by the next attempt.
        bool inIndex = false;
        bool removable = false; ///< In the user's folder, so Remove may delete it.
        juce::File sources;     ///< LICENSES/SOURCES.md of the installed pack; invalid when it has none.
    };

    /** Called on the message thread after anything a row shows changed. */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void soundPacksChanged() = 0;
    };

    /** The real index URL, the user's SoundPacks folder and the HTTP fetcher. Message thread only, like every member.
     */
    SoundPackInstaller();
    ~SoundPackInstaller();

    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }

    /** Fetches the hosted pack list again (a no-op while one is loading). */
    void refreshIndex();
    IndexStatus indexStatus() const noexcept { return indexStatus_; }
    juce::String indexError() const { return indexError_; }

    /** Hosted packs first in list order, then installed packs the list does not know. */
    std::vector<Row> rows() const;
    std::optional<Row> row(const juce::String& packId) const;

    /** Downloads and installs (or updates) a pack from the loaded list. False when the pack is unknown or busy. */
    bool install(const juce::String& packId);
    /** Stops a running install and removes its partial files. */
    void cancel(const juce::String& packId);
    /** Deletes an installed pack's folder. False (with the row's error set) when it is not removable or in use. */
    bool remove(const juce::String& packId);

    /** Opens the installed pack's SOURCES.md; the default hands it to the operating system. */
    void showSources(const juce::String& packId);

    // ---- Test seams ----
    void setIndexUrlForTesting(const juce::String& url) { indexUrl_ = url; }
    void setFetcherForTesting(Fetcher fetcher) { fetcher_ = std::move(fetcher); }
    void setInstallRootForTesting(const juce::File& root) { installRoot_ = root; }
    /** Runs every background step and its delivery on the calling thread, so a test needs no message loop. */
    void setRunInlineForTesting(bool runInline) { runInline_ = runInline; }
    void setFileOpenerForTesting(std::function<void(const juce::File&)> opener) { fileOpener_ = std::move(opener); }

private:
    struct InstalledPack;

    void notifyChanged();
    void postChanged();
    void applyIndex(bool ok, SoundPackIndex index, const juce::String& error);
    void finishJob(const juce::String& packId, const std::shared_ptr<InstallJob>& job);
    std::vector<InstalledPack> scanInstalled() const;
    Row makeRow(const IndexPack* hosted, const InstalledPack* installed) const;
    void run(std::function<void()> work, std::function<void()> deliver);

    juce::ListenerList<Listener> listeners_;
    juce::String indexUrl_;
    juce::File installRoot_;
    Fetcher fetcher_;
    std::function<void(const juce::File&)> fileOpener_;
    bool runInline_ = false;

    IndexStatus indexStatus_ = IndexStatus::NotLoaded;
    juce::String indexError_;
    SoundPackIndex index_;
    std::map<juce::String, std::shared_ptr<InstallJob>> jobs_;
    std::map<juce::String, juce::String> errors_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SoundPackInstaller)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPackInstaller)
};

} // namespace synth::soundpacks
