// synth::BounceRunner — the chunked, message-thread-timer-driven twin of BounceExporter::bounce(),
// used by the Export dialog so a progress bar can update and Cancel can work without blocking the
// message thread for the whole render. The render loop itself (BounceSession) is already covered,
// block-for-block identically, by BounceExporterTests.cpp; what is unique here is the state
// machine that steps it a few blocks per timer tick and delivers exactly one completion callback.
//
// Headless/deterministic house rules apply: HostMode::Hosted, empty graph (silent by
// construction), no audio device. Ticks are pumped via juce::MessageManager::runDispatchLoopUntil,
// the same idiom FeedbackSettingsTabTests/GraphEditorTests use for other Timer-driven code.

#include "../Source/AudioEngine.h"
#include "../Source/Transport/BounceRunner.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

using synth::BounceOptions;
using synth::BounceResult;
using synth::BounceRunner;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;

// Empty Hosted engine — mirrors MetronomeTests.cpp's Fixture (same driver, same teardown order):
// the fixture's own driver is what makes the engine live at the render format before a bounce
// touches it.
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;

    void build() {
        engine.initialise();
        engine.getGraph().clear();
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);
    }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }

    juce::File file;
};

BounceOptions eightBeatOptions() {
    BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 8.0; // 375 blocks at 512/48kHz/120bpm - many more than any chunk size below
    options.tailSeconds = 0.0;
    options.sampleRate = kSampleRate;
    options.blockSize = kBlockSize;
    options.bitDepth = 24;
    options.numChannels = kNumChannels;
    return options;
}

/** Pumps the dispatch loop in small slices until `done` is true or the deadline passes. */
void pumpUntil(const std::function<bool()>& done, int deadlineMs = 5000) {
    const auto start = juce::Time::getMillisecondCounter();
    while (!done() && juce::Time::getMillisecondCounter() - start < (juce::uint32)deadlineMs)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
}

} // namespace

TEST(BounceRunnerTest, RunsToCompletionOverMultipleTicksAndReportsProgress) {
    Fixture f;
    f.build();

    ScopedTempFile out("agentsynth_bounce_runner.wav");

    bool completed = false;
    BounceResult result;
    double lastProgress = 0.0;
    bool progressWentBackwards = false;

    BounceRunner runner(
        f.engine, out.file, eightBeatOptions(),
        [&](BounceResult r) {
            completed = true;
            result = r;
        },
        /*chunkBlocks=*/4, /*tickMs=*/1);

    pumpUntil([&] {
        const double progress = runner.getProgress();
        if (progress < lastProgress)
            progressWentBackwards = true;
        lastProgress = progress;
        return completed;
    });

    ASSERT_TRUE(completed) << "the runner never completed - a timer tick must be stuck";
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(progressWentBackwards);
    ASSERT_TRUE(out.file.existsAsFile());
}

TEST(BounceRunnerTest, CancelStopsTheRenderAndLeavesNoFile) {
    Fixture f;
    f.build();

    ScopedTempFile out("agentsynth_bounce_runner_cancel.wav");

    bool completed = false;
    BounceResult result;

    BounceRunner runner(
        f.engine, out.file, eightBeatOptions(),
        [&](BounceResult r) {
            completed = true;
            result = r;
        },
        /*chunkBlocks=*/4, /*tickMs=*/1);

    // Let a couple of chunks render, then cancel mid-flight.
    pumpUntil([&] { return completed || runner.getProgress() > 0.05; });
    ASSERT_FALSE(completed) << "the render finished before cancel had a chance to matter - not testing anything";
    runner.cancel();

    pumpUntil([&] { return completed; });

    ASSERT_TRUE(completed);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("cancel")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile());
}
