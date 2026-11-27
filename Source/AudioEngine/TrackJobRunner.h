#pragma once

#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <span>
#include <vector>

namespace synth {

/** One render pass's work for TrackJobRunner: a fixed set of jobs, each waiting on a known number of others. */
class TrackJobs {
public:
    virtual ~TrackJobs() = default;
    virtual int numJobs() const noexcept = 0;
    /** How many jobs must finish before `job` may start. */
    virtual int numWaits(int job) const noexcept = 0;
    /** The jobs that wait on `job`; each counts it once in numWaits. */
    virtual std::span<const int> successors(int job) const noexcept = 0;
    /** Called on one thread per pass, once every job it waits on has finished; must not lock or allocate. */
    virtual void runJob(int job) noexcept = 0;
};

/** A fixed pool of real-time worker threads that renders a TrackJobs set together with the calling audio thread. */
class TrackJobRunner {
public:
    static constexpr int kMaxJobs = 1024;

    /** Message thread. Starts `numWorkers` threads (0 is allowed: run() then does every job itself). */
    TrackJobRunner(int numWorkers, const juce::AudioWorkgroup& workgroup, int blockSize, double sampleRate);
    /** Message thread; no run() may be in progress. */
    ~TrackJobRunner();

    int numWorkers() const noexcept { return (int)workers_.size(); }

    /** Audio thread, one caller at a time. Returns once every job has run; false (nothing run) past kMaxJobs. */
    bool run(TrackJobs& jobs) noexcept;

private:
    class Worker;
    void workPass(std::uint32_t pass) noexcept;
    bool tryRunOne(std::uint32_t pass, TrackJobs& jobs, int count) noexcept;

    std::unique_ptr<std::atomic<std::uint64_t>[]> state_;
    std::atomic<std::uint32_t> pass_{0};
    std::atomic<TrackJobs*> jobs_{nullptr};
    std::atomic<int> numJobs_{0};
    std::atomic<int> remaining_{0};
    std::atomic<bool> exiting_{false};
    std::atomic<int> exited_{0};
    juce::AudioWorkgroup workgroup_;
    std::vector<std::unique_ptr<Worker>> workers_;
};

} // namespace synth
