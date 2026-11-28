// Disabled benches, not tests: what waking the track workers costs per block
// (docs/architecture/audio-engine.md#multi-core-render-track-workers). Run by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*TrackWorkerWakeProfile*'
// DISABLED_TrackWorkerWakeProfile drives a synthetic engine-shaped pass (one shared job, 20 strip jobs, one post job,
// work scaled to the block like the Load test project's) from a real-time thread at the device's own period, for 64,
// 128 and 512 samples at 48 kHz, and compares the idle policies: single thread, sleep at once (atomic::wait), poll
// for a while then sleep, and poll through the whole gap. It prints the pass time, how late the first worker joins
// and the process CPU, the figure Activity Monitor shows.
// The same policies on a real output device: DISABLED_TrackWorkerRealDeviceProfile in TrackWorkerProjectTests.cpp.
#include "AudioEngine/TrackJobRunner.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <thread>
#include <vector>

namespace {

double ticksToUs(std::int64_t ticks) { return juce::Time::highResolutionTicksToSeconds(ticks) * 1.0e6; }

void burnFor(double micros) {
    const auto until = juce::Time::getHighResolutionTicks() + juce::Time::secondsToHighResolutionTicks(micros * 1e-6);
    while (juce::Time::getHighResolutionTicks() < until) {
    }
}

double cpuSeconds() { return (double)std::clock() / CLOCKS_PER_SEC; }

struct Stats {
    double mean = 0.0, p99 = 0.0, max = 0.0;
};

Stats statsOf(std::vector<double> values) {
    Stats s;
    if (values.empty())
        return s;
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (const double v : values)
        sum += v;
    s.mean = sum / (double)values.size();
    s.p99 = values[std::min(values.size() - 1, (size_t)((double)values.size() * 0.99))];
    s.max = values.back();
    return s;
}

// The engine's job shape (TrackCut): a shared job, `strips` strip jobs that wait on it, a post job that waits on all.
struct EngineShapedJobs : synth::TrackJobs {
    std::vector<int> fromShared, toPost;
    std::vector<std::vector<int>> succs;
    double stripMicros = 0.0, sharedMicros = 0.0;
    std::thread::id caller;
    std::atomic<std::int64_t> firstWorkerStart{0};

    explicit EngineShapedJobs(int strips)
        : succs((size_t)strips + 2) {
        for (int s = 1; s <= strips; ++s) {
            succs[0].push_back(s);
            succs[(size_t)s].push_back(strips + 1);
        }
    }
    int numJobs() const noexcept override { return (int)succs.size(); }
    int numWaits(int job) const noexcept override { return job == 0 ? 0 : (job == numJobs() - 1 ? numJobs() - 2 : 1); }
    std::span<const int> successors(int job) const noexcept override { return succs[(size_t)job]; }
    void runJob(int job) noexcept override {
        if (std::this_thread::get_id() != caller) {
            std::int64_t expected = 0;
            firstWorkerStart.compare_exchange_strong(expected, juce::Time::getHighResolutionTicks());
        }
        burnFor(job == 0 || job == numJobs() - 1 ? sharedMicros : stripMicros);
    }
};

struct WakeRow {
    int block = 0;
    const char* mode = "";
    Stats pass, wake;
    double cpuPercent = 0.0;
};

// A device-like thread: one pass per period, at the period's own pace.
class PeriodicDriver : public juce::Thread {
public:
    PeriodicDriver(synth::TrackJobRunner& runner, EngineShapedJobs& jobs, double periodUs, int passes)
        : juce::Thread("Bench device")
        , runner_(runner)
        , jobs_(jobs)
        , periodTicks_(juce::Time::secondsToHighResolutionTicks(periodUs * 1e-6))
        , passes_(passes) {
        passUs.reserve((size_t)passes);
        wakeUs.reserve((size_t)passes);
    }
    void run() override {
        jobs_.caller = std::this_thread::get_id();
        auto next = juce::Time::getHighResolutionTicks() + periodTicks_;
        for (int p = 0; p < passes_; ++p) {
            while (juce::Time::getHighResolutionTicks() < next - juce::Time::secondsToHighResolutionTicks(300e-6))
                juce::Thread::sleep(0);
            while (juce::Time::getHighResolutionTicks() < next) {
            }
            next += periodTicks_;
            jobs_.firstWorkerStart.store(0);
            const auto start = juce::Time::getHighResolutionTicks();
            runner_.run(jobs_);
            const auto end = juce::Time::getHighResolutionTicks();
            passUs.push_back(ticksToUs(end - start));
            if (const auto w = jobs_.firstWorkerStart.load(); w != 0)
                wakeUs.push_back(ticksToUs(w - start));
        }
    }
    std::vector<double> passUs, wakeUs;

private:
    synth::TrackJobRunner& runner_;
    EngineShapedJobs& jobs_;
    std::int64_t periodTicks_;
    int passes_;
};

WakeRow measureWake(int block, const char* mode, int workers, int spinMicros) {
    constexpr double kRate = 48000.0;
    const double periodUs = 1.0e6 * block / kRate;
    EngineShapedJobs jobs(20);
    jobs.stripMicros = 34.0 * block / 512.0; // the Load test project: about 680 us per 512-sample block on one core
    jobs.sharedMicros = 2.0 * block / 512.0;
    synth::TrackJobRunner runner(workers, juce::AudioWorkgroup{}, block, kRate, spinMicros);
    const int passes = (int)(3.0e6 / periodUs);
    PeriodicDriver driver(runner, jobs, periodUs, passes);
    const double cpu0 = cpuSeconds();
    const double wall0 = juce::Time::getMillisecondCounterHiRes();
    if (!driver.startRealtimeThread(juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime(block, kRate)))
        driver.startThread(juce::Thread::Priority::highest);
    while (driver.isThreadRunning())
        juce::Thread::sleep(20);
    const double wallSeconds = (juce::Time::getMillisecondCounterHiRes() - wall0) / 1000.0;
    WakeRow row{block, mode, statsOf(driver.passUs), statsOf(driver.wakeUs), 0.0};
    row.cpuPercent = 100.0 * (cpuSeconds() - cpu0) / wallSeconds;
    return row;
}

} // namespace

TEST(TrackWorkerWakeProfile, DISABLED_TrackWorkerWakeProfile) {
    struct Mode {
        const char* name;
        int workers, spin;
    };
    const Mode modes[] = {{"single", 0, 0},     {"wait", 3, 0},         {"poll 20us", 3, 20},
                          {"poll 50us", 3, 50}, {"poll 200us", 3, 200}, {"poll gap", 3, 1000000}};
    std::printf("| block | mode | pass mean | pass p99 | pass max | wake mean | wake p99 | process CPU |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (const int block : {64, 128, 512})
        for (const auto& mode : modes) {
            const auto r = measureWake(block, mode.name, mode.workers, mode.spin);
            std::printf("| %d | %s | %.1f us | %.1f us | %.1f us | %.1f us | %.1f us | %.0f%% |\n", r.block, r.mode,
                        r.pass.mean, r.pass.p99, r.pass.max, r.wake.mean, r.wake.p99, r.cpuPercent);
        }
}
