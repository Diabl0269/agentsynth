// Concern: the real-time worker pool that renders one pass of track jobs (TrackJobRunner.h).
#include "AudioEngine/TrackJobRunner.h"
#include "AudioEngine/RealtimeScope.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace synth {

namespace {
// The low half of a job's state word while it is being run: it can never be a wait count.
constexpr std::uint64_t kClaimed = 0xffffffffull;

inline void cpuRelax() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    asm volatile("yield");
#elif defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#endif
}

inline std::uint64_t readyWord(std::uint32_t pass) noexcept { return (std::uint64_t)pass << 32; }
} // namespace

// A pool thread. It joins the device's workgroup once (the pool is rebuilt on every device start, so the workgroup
// never changes under a running worker), then alternates between polling the pass counter, sleeping on it and helping
// with a pass. It polls for a fixed time after each pass (the owner's idle spin, measured in ticks so it means the
// same on every CPU): long enough to catch the back-to-back passes of a sliced callback, short enough not to burn a
// core through the gap between device blocks. Before it sleeps it counts itself a sleeper (seq_cst, paired with
// run()'s seq_cst pass store and sleeper load), so run() only pays for the wake-up call when someone sleeps; the sleep
// itself is std::atomic::wait, which re-checks the counter in the kernel, so a pass that starts between the last poll
// and the sleep is never missed.
class TrackJobRunner::Worker : public juce::Thread {
public:
    explicit Worker(TrackJobRunner& owner)
        : juce::Thread("Track worker")
        , owner_(owner) {}

    void run() override {
        juce::WorkgroupToken token;
        if (owner_.workgroup_)
            owner_.workgroup_.join(token);
        std::uint32_t seen = owner_.pass_.load(std::memory_order_acquire);
        std::int64_t idleSince = juce::Time::getHighResolutionTicks();
        int polls = 0;
        for (;;) {
            const std::uint32_t pass = owner_.pass_.load(std::memory_order_acquire);
            if (owner_.exiting_.load(std::memory_order_acquire)) {
                owner_.exited_.fetch_add(1, std::memory_order_release); // the last touch of the owner
                return;
            }
            if (pass != seen) {
                seen = pass;
                owner_.workPass(pass);
                idleSince = juce::Time::getHighResolutionTicks();
                polls = 0;
                continue;
            }
            // The clock is read once every 32 polls: a poll is a few nanoseconds, a clock read tens.
            if ((++polls & 31) != 0 || juce::Time::getHighResolutionTicks() - idleSince < owner_.idleSpinTicks_) {
                cpuRelax();
                continue;
            }
            owner_.sleepers_.fetch_add(1, std::memory_order_seq_cst);
            if (owner_.pass_.load(std::memory_order_seq_cst) == seen &&
                !owner_.exiting_.load(std::memory_order_seq_cst))
                owner_.pass_.wait(seen, std::memory_order_acquire);
            owner_.sleepers_.fetch_sub(1, std::memory_order_relaxed);
            idleSince = juce::Time::getHighResolutionTicks();
            polls = 0;
        }
    }

private:
    TrackJobRunner& owner_;
};

// Each job's state is one 64-bit word: the pass it was last reset for (high half) and how many of its predecessors
// are still running (low half), or kClaimed while a thread runs it. A thread may claim a job only with a
// compare-exchange from "this pass, zero waits", so a worker that wakes late, still holding the previous pass
// number, can never claim (or run twice) a job of the pass that has started since.
TrackJobRunner::TrackJobRunner(int numWorkers, const juce::AudioWorkgroup& workgroup, int blockSize, double sampleRate,
                               int idleSpinMicros)
    : state_(new std::atomic<std::uint64_t>[kMaxJobs])
    , idleSpinTicks_(juce::Time::secondsToHighResolutionTicks(std::max(0, idleSpinMicros) * 1.0e-6))
    , workgroup_(workgroup) {
    for (int i = 0; i < kMaxJobs; ++i)
        state_[(size_t)i].store(0, std::memory_order_relaxed);
    const auto options = juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime(
        std::max(1, blockSize), sampleRate > 0.0 ? sampleRate : 48000.0);
    for (int i = 0; i < numWorkers; ++i) {
        auto worker = std::make_unique<Worker>(*this);
        if (!worker->startRealtimeThread(options))
            worker->startThread(juce::Thread::Priority::highest);
        workers_.push_back(std::move(worker));
    }
}

// The exit flag goes first, then a pass bump wakes every sleeper: a worker that wakes sees the flag before it looks
// at the (empty) pass. Each worker counts itself out with release order as its last touch of this object, and the
// acquire here pairs with it, so the state words are freed strictly after every worker's last read of them (JUCE's
// stopThread polls for the exit and gives no such ordering on its own).
TrackJobRunner::~TrackJobRunner() {
    exiting_.store(true, std::memory_order_seq_cst);
    pass_.fetch_add(1, std::memory_order_seq_cst);
    pass_.notify_all();
    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    while (exited_.load(std::memory_order_acquire) < (int)workers_.size() &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::yield();
    for (auto& worker : workers_)
        worker->stopThread(2000);
}

// Every state word, the job count and the jobs pointer are written before the pass counter, and a worker reads the
// counter with acquire before it touches any of them. The calling thread works through the pass too, so a pass with
// no free worker (or a pool of none) still finishes; it only spins while every ready job is already taken.
bool TrackJobRunner::run(TrackJobs& jobs) noexcept {
    const int count = jobs.numJobs();
    if (count > kMaxJobs)
        return false;
    if (count <= 0)
        return true;
    const std::uint32_t pass = pass_.load(std::memory_order_relaxed) + 1;
    for (int j = 0; j < count; ++j)
        state_[(size_t)j].store(readyWord(pass) | (std::uint32_t)jobs.numWaits(j), std::memory_order_relaxed);
    remaining_.store(count, std::memory_order_relaxed);
    jobs_.store(&jobs, std::memory_order_relaxed);
    numJobs_.store(count, std::memory_order_relaxed);
    pass_.store(pass, std::memory_order_seq_cst);
    // Waking a sleeper is the one system call on the audio thread's path, and a deliberate one: a futex (Linux) or
    // ulock (macOS) wake never blocks, takes no user-space lock and allocates nothing, the same wake a semaphore post
    // makes. It is the only way a sleeping worker hears the pass, so the sanitizer is paused around exactly this call.
    if (sleepers_.load(std::memory_order_seq_cst) > 0) {
        const RealtimePause wakeIsBounded;
        pass_.notify_all();
    }

    while (remaining_.load(std::memory_order_acquire) > 0)
        if (!tryRunOne(pass, jobs, count))
            cpuRelax();
    return true;
}

// A worker's share of one pass. It leaves as soon as the pass is done or a newer one has started (it is then late,
// and every claim it tried would fail anyway).
void TrackJobRunner::workPass(std::uint32_t pass) noexcept {
    [[maybe_unused]] const RealtimeScope realtime;
    TrackJobs* jobs = jobs_.load(std::memory_order_relaxed);
    const int count = numJobs_.load(std::memory_order_relaxed);
    if (jobs == nullptr)
        return;
    while (remaining_.load(std::memory_order_acquire) > 0 && pass_.load(std::memory_order_relaxed) == pass)
        if (!tryRunOne(pass, *jobs, count))
            cpuRelax();
}

// Claims the first ready job of `pass` and runs it. Each successor's wait count drops with release order after the
// job's own writes, so whichever thread claims the successor (an acquire on the same word) sees this job's output;
// the remaining count drops last, so run() returning means every job's writes are visible to the caller.
bool TrackJobRunner::tryRunOne(std::uint32_t pass, TrackJobs& jobs, int count) noexcept {
    const std::uint64_t ready = readyWord(pass);
    for (int j = 0; j < count; ++j) {
        auto& word = state_[(size_t)j];
        std::uint64_t expected = ready;
        if (word.load(std::memory_order_acquire) != ready ||
            !word.compare_exchange_strong(expected, ready | kClaimed, std::memory_order_acq_rel))
            continue;
        jobs.runJob(j);
        for (const int next : jobs.successors(j))
            state_[(size_t)next].fetch_sub(1, std::memory_order_acq_rel);
        remaining_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
    return false;
}

} // namespace synth
