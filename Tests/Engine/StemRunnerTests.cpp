// synth::StemRunner — the chunked, message-thread-timer-driven twin of StemExporter::exportStems(),
// used by the Export Stems dialog so a progress bar can update and Cancel can work without blocking
// the message thread for the whole render. The render loop itself (StemSession) is already covered,
// block-for-block identically, by StemExportTests.cpp; what is unique here is the state machine that
// steps it a few blocks per timer tick and delivers exactly one completion callback — the same thing
// BounceRunnerTests.cpp proves for BounceRunner, StemRunner's direct sibling.
//
// Headless/deterministic house rules apply: HostMode::Hosted, no audio device. Ticks are pumped via
// juce::MessageManager::runDispatchLoopUntil, the same idiom BounceRunnerTests.cpp uses.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Transport/OfflineTransportDriver.h"
#include "Transport/StemRunner.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_events/juce_events.h>
#include <memory>

using synth::BounceOptions;
using synth::StemResult;
using synth::StemRunner;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;
constexpr int kRight = ChannelStripModule::kRightBase;
using NodeID = juce::AudioProcessorGraph::NodeID;

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

// Two Channel Strip nodes feeding Master, silent by construction (no source feeds them) - this
// runner's own state machine, not per-sample content, is what's under test here.
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;

    bool build() {
        engine.initialise();
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        const auto out = addFactoryNode(graph, "Audio Output");
        const auto master = addFactoryNode(graph, "Master");
        const auto stripA = addFactoryNode(graph, "Channel Strip");
        const auto stripB = addFactoryNode(graph, "Channel Strip");
        if (out == NodeID{} || master == NodeID{} || stripA == NodeID{} || stripB == NodeID{})
            return false;

        for (auto strip : {stripA, stripB}) {
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {out, 0}});
        graph.addConnection({{master, 1}, {out, 1}});

        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);
        return true;
    }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

struct ScopedTempDir {
    explicit ScopedTempDir(const juce::String& name)
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        dir.deleteRecursively();
        dir.createDirectory();
    }
    ~ScopedTempDir() { dir.deleteRecursively(); }

    juce::File dir;
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

TEST(StemRunnerTest, RunsToCompletionOverMultipleTicksAndReportsMonotonicProgress) {
    Fixture f;
    ASSERT_TRUE(f.build());

    ScopedTempDir out("agentsynth_stem_runner");

    bool completed = false;
    StemResult result;
    double lastProgress = 0.0;
    bool progressWentBackwards = false;

    StemRunner runner(
        f.engine, out.dir, eightBeatOptions(),
        [&](StemResult r) {
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
    EXPECT_DOUBLE_EQ(runner.getProgress(), 1.0);
    ASSERT_EQ(result.stemFiles.size(), 2);
    for (auto& file : result.stemFiles)
        EXPECT_TRUE(file.existsAsFile()) << file.getFullPathName();
}

TEST(StemRunnerTest, CancelStopsTheRenderAndLeavesNoStemFiles) {
    Fixture f;
    ASSERT_TRUE(f.build());

    ScopedTempDir out("agentsynth_stem_runner_cancel");

    bool completed = false;
    StemResult result;

    // Held by pointer so it can be destroyed explicitly below, before the filesystem check - see
    // the comment there for why that matters.
    auto runner = std::make_unique<StemRunner>(
        f.engine, out.dir, eightBeatOptions(),
        [&](StemResult r) {
            completed = true;
            result = r;
        },
        /*chunkBlocks=*/4, /*tickMs=*/1);

    // Let a couple of chunks render, then cancel mid-flight.
    pumpUntil([&] { return completed || runner->getProgress() > 0.05; });
    ASSERT_FALSE(completed) << "the render finished before cancel had a chance to matter - not testing anything";
    runner->cancel();

    pumpUntil([&] { return completed; });

    ASSERT_TRUE(completed);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("cancel")) << result.message;
    EXPECT_TRUE(result.stemFiles.isEmpty());

    // A cancelled StemSession's temp files "die with stems_" (StemSession.cpp's own comment) - i.e.
    // on the SESSION's destruction, not synchronously inside finish(). StemRunner holds its session
    // as a long-lived member for the whole render, so those temp files are still on disk right after
    // onComplete fires; real usage destroys the runner promptly on completion (the dialog drops it),
    // which is what actually deletes them - mirror that here before checking the folder.
    runner.reset();

    EXPECT_EQ(out.dir.getNumberOfChildFiles(juce::File::findFiles), 0)
        << "a cancelled export must leave no stem files behind";
}

// onComplete may destroy `this` (StemRunner.cpp moves onComplete_ out before invoking it for
// exactly this reason) - prove a callback that does so is actually safe, the same contract
// BounceRunner documents for itself.
TEST(StemRunnerTest, OnCompleteMayDestroyTheRunner) {
    Fixture f;
    ASSERT_TRUE(f.build());

    ScopedTempDir out("agentsynth_stem_runner_selfdestroy");

    bool completed = false;
    std::unique_ptr<StemRunner> runner;
    runner = std::make_unique<StemRunner>(
        f.engine, out.dir, eightBeatOptions(),
        [&](StemResult) {
            completed = true;
            runner.reset(); // destroys the StemRunner from inside its own completion callback
        },
        /*chunkBlocks=*/64, /*tickMs=*/1);

    pumpUntil([&] { return completed; });

    ASSERT_TRUE(completed);
    EXPECT_EQ(runner, nullptr);
}
