// DeferredAssetLoads.cpp -- the worker, the hop back to the message thread and the progress counts.

#include "DeferredAssetLoads.h"

namespace synth {

namespace {
DeferredAssetLoads* activeLoads = nullptr; // message thread only
} // namespace

DeferredAssetLoads::DeferredAssetLoads() = default;

// A decode already running cannot be interrupted (a reader is mid-file), so the pool is given a while to finish it;
// its result is dropped with the rest. Queued decodes that never started are removed.
DeferredAssetLoads::~DeferredAssetLoads() {
    cancelPendingUpdate();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        held_.clear();
    }
    if (pool_ != nullptr)
        pool_->removeAllJobs(true, 4000);
}

DeferredAssetLoads* DeferredAssetLoads::active() noexcept { return activeLoads; }

DeferredAssetLoads::Scope::Scope(DeferredAssetLoads* loads) noexcept
    : previous_(activeLoads) {
    activeLoads = loads;
}

DeferredAssetLoads::Scope::~Scope() { activeLoads = previous_; }

// The worker posts (generation, index, install) and pokes the message thread; nothing on the worker reads items_.
// A result whose generation is old belongs to a load reset() abandoned, so it is dropped unrun.
void DeferredAssetLoads::enqueue(Kind kind, const void* owner, Decode decode) {
    if (pool_ == nullptr)
        pool_ = std::make_unique<juce::ThreadPool>(
            juce::ThreadPoolOptions{}.withThreadName("Asset decode").withNumberOfThreads(1));
    items_.push_back({kind, owner, false});
    const size_t index = items_.size() - 1;
    const int generation = generation_;
    auto job = [this, index, generation, decode = std::move(decode)] {
        auto install = decode();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            finished_.push_back({generation, index, std::move(install)});
        }
        triggerAsyncUpdate();
    };
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (holding_) {
            held_.push_back(std::move(job));
            return;
        }
    }
    pool_->addJob(std::move(job));
}

void DeferredAssetLoads::reset() {
    ++generation_;
    items_.clear();
}

DeferredAssetLoads::Counts DeferredAssetLoads::counts(Kind kind) const {
    Counts c;
    for (const auto& item : items_)
        if (item.kind == kind) {
            ++c.total;
            c.done += item.done ? 1 : 0;
        }
    return c;
}

std::vector<const void*> DeferredAssetLoads::pendingOwners() const {
    std::vector<const void*> owners;
    for (const auto& item : items_)
        if (!item.done)
            owners.push_back(item.owner);
    return owners;
}

void DeferredAssetLoads::handleAsyncUpdate() { installFinished(); }

void DeferredAssetLoads::installFinished() {
    std::vector<Finished> ready;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(finished_);
    }
    for (auto& result : ready) {
        if (result.generation != generation_ || result.index >= items_.size())
            continue;
        if (result.install)
            result.install();
        auto& item = items_[result.index];
        item.done = true;
        if (onInstalled)
            onInstalled(item.owner);
    }
}

void DeferredAssetLoads::holdDecodesForTest(bool hold) {
    std::vector<std::function<void()>> released;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        holding_ = hold;
        if (!hold)
            released.swap(held_);
    }
    for (auto& job : released)
        pool_->addJob(std::move(job));
}

void DeferredAssetLoads::finishAllForTest() {
    holdDecodesForTest(false);
    if (pool_ != nullptr)
        while (pool_->getNumJobs() > 0)
            juce::Thread::sleep(1);
    cancelPendingUpdate();
    installFinished();
}

} // namespace synth
