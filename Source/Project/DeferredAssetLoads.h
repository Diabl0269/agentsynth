#pragma once

// DeferredAssetLoads.h -- decoding a project's sample and wavetable files off the message thread while a project
// opens on screen (docs/architecture/project-bundle.md#opening-a-project-on-screen). Outside an open Scope nothing
// changes: a module restoring its file decodes it synchronously, as tests, paste, undo and the AI do.

#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <vector>

namespace synth {

class DeferredAssetLoads : private juce::AsyncUpdater {
public:
    enum class Kind { Sample, Wavetable };
    /** Installs a decoded result on the message thread; it checks for itself that its module still wants it. */
    using Install = std::function<void()>;
    /** Runs on a worker thread and must touch no module: it reads the file and returns what installs the result. */
    using Decode = std::function<Install()>;

    DeferredAssetLoads();
    ~DeferredAssetLoads() override;
    DeferredAssetLoads(const DeferredAssetLoads&) = delete;
    DeferredAssetLoads& operator=(const DeferredAssetLoads&) = delete;

    /** Message thread. The deferral a module restoring its file should use, or nullptr: decode synchronously. */
    static DeferredAssetLoads* active() noexcept;

    /** Makes `loads` active for its lifetime (nullptr: none). Message thread; nests. */
    class Scope {
    public:
        explicit Scope(DeferredAssetLoads* loads) noexcept;
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        DeferredAssetLoads* previous_;
    };

    /** Message thread. Decodes on the worker for `owner` (a module, used only as a key) and installs it here. */
    void enqueue(Kind kind, const void* owner, Decode decode);
    /** Message thread. Forgets every item; results still decoding are dropped when they arrive. */
    void reset();

    struct Counts {
        int total = 0;
        int done = 0;
    };
    Counts counts(Kind kind) const;
    /** The owners whose decode has not been installed yet, in the order they were queued. */
    std::vector<const void*> pendingOwners() const;
    bool hasPending() const { return !pendingOwners().empty(); }

    /** Message thread, after each install: `owner` is the module whose file just landed. */
    std::function<void(const void* owner)> onInstalled;

    /** Test seam: waits for every queued decode and installs them all before returning. */
    void finishAllForTest();
    /** Test seam: holds every decode until released, so a test can see the load while it is pending. */
    void holdDecodesForTest(bool hold);

private:
    struct Item {
        Kind kind;
        const void* owner;
        bool done = false;
    };
    struct Finished {
        int generation;
        size_t index;
        Install install;
    };
    void handleAsyncUpdate() override;
    void installFinished();

    std::unique_ptr<juce::ThreadPool> pool_;
    std::vector<Item> items_;
    int generation_ = 0;
    std::mutex mutex_; // guards finished_ and held_
    std::vector<Finished> finished_;
    std::vector<std::function<void()>> held_;
    bool holding_ = false;
};

} // namespace synth
