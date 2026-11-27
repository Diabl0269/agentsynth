// The engine's worker switch on real saved projects
// (docs/architecture/audio-engine.md#multi-core-render-track-workers): a live device render on track workers is
// byte-identical to the single-thread render, playing and idle, through varied block sizes and a loop wrap; the plan
// exists only while a device runs; graph edits while the workers render swap the plan in without a changed bit, and
// never let the audio thread see a half-built plan.
#include "../FakeAudioIODevice.h"
#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "../TestWait.h"
#include "../UniqueTempDir.h"
#include "TestPaths.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <vector>

namespace {

struct ProjectCopy {
    juce::File dir;
    explicit ProjectCopy(const juce::File& original)
        : dir(synth::test::uniqueTempPath("TrackWorkerProject", ".agsproj")) {
        if (!original.copyDirectoryTo(dir)) {
            dir = juce::File();
            return;
        }
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "autosave*.json"))
            f.deleteFile();
    }
    ~ProjectCopy() {
        if (dir != juce::File())
            dir.deleteRecursively();
    }
};

struct RenderSpec {
    int workers = 0;
    bool playing = true;
    bool varySizes = false;
    bool loop = false;
    int blocks = 300;
};

struct RenderResult {
    std::uint64_t hash = 1469598103934665603ull;
    double energy = 0.0;
    bool workersActive = false;
    bool opened = false;
};

constexpr int kSizes[] = {512, 64, 333, 1, 128, 512, 17, 256};

// Opens a fresh copy of the project in its own MainComponent and drives the device callback by hand. Every
// render starts from the same just-opened state, so two renders differ only by the worker count.
RenderResult renderProject(const juce::File& project, const RenderSpec& spec) {
    RenderResult result;
    const ProjectCopy copy(project);
    if (copy.dir == juce::File())
        return result;
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 1000);
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    if (!mc.openProjectForTest(copy.dir))
        return result;
    result.opened = true;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    engine.setTrackWorkerCount(spec.workers);
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    result.workersActive = engine.isTrackWorkerRenderActive();
    if (spec.loop)
        engine.getTransport().setLoop(0.0, 1.0, true); // one beat: wraps every ~47 blocks at 120 bpm
    if (spec.playing)
        engine.getTransport().play();

    std::vector<float> inL(512), inR(512), outL(512), outR(512);
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {outL.data(), outR.data()};
    for (int k = 0; k < spec.blocks; ++k) {
        const int n = spec.varySizes ? kSizes[k % std::size(kSizes)] : 512;
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, n, {});
        for (const auto* channel : {&outL, &outR})
            for (int i = 0; i < n; ++i) {
                result.hash ^= std::bit_cast<std::uint32_t>((*channel)[(size_t)i]);
                result.hash *= 1099511628211ull;
                result.energy += (double)(*channel)[(size_t)i] * (*channel)[(size_t)i];
            }
    }
    engine.getTransport().stop();
    engine.audioDeviceStopped();
    return result;
}

void expectSameRender(const juce::File& project, RenderSpec spec, bool expectSound) {
    spec.workers = 0;
    const auto single = renderProject(project, spec);
    ASSERT_TRUE(single.opened) << project.getFullPathName();
    EXPECT_FALSE(single.workersActive);
    spec.workers = 3;
    const auto multi = renderProject(project, spec);
    ASSERT_TRUE(multi.opened);
    EXPECT_TRUE(multi.workersActive) << "the plan was not usable, so this compared the single path with itself";
    if (expectSound)
        EXPECT_GT(single.energy, 0.0) << "the project made no sound, so equality proves little";
    EXPECT_EQ(single.hash, multi.hash) << project.getFileName() << (spec.playing ? " playing" : " idle")
                                       << " single=" << std::hex << single.hash << " multi=" << multi.hash;
    std::printf("[workers] %s %s%s%s hash single=%016llx multi=%016llx\n", project.getFileName().toRawUTF8(),
                spec.playing ? "playing" : "idle", spec.varySizes ? " varied-sizes" : "", spec.loop ? " loop" : "",
                (unsigned long long)single.hash, (unsigned long long)multi.hash);
}

juce::File fixtureProject() { return juce::File(UI_VERIFY_FIXTURES_DIR).getChildFile("two-tracks-folded.agsproj"); }

juce::File loadTestProject() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("AgentSynth/Load test.agsproj");
}

} // namespace

TEST_F(ChannelFlowTest, TrackWorkersRenderFixtureProjectIdentically) {
    expectSameRender(fixtureProject(), {.playing = true}, false);
    expectSameRender(fixtureProject(), {.playing = false}, false);
}

TEST_F(ChannelFlowTest, TrackWorkersStressVariedBlocksAndLoopWrap) {
    expectSameRender(fixtureProject(), {.playing = true, .varySizes = true, .loop = true, .blocks = 3000}, false);
}

// Load test is a 20-track project on the developer's machine, not in the repo; skipped where it is absent.
TEST_F(ChannelFlowTest, TrackWorkersRenderLoadTestIdentically) {
    if (!loadTestProject().isDirectory())
        GTEST_SKIP() << "no " << loadTestProject().getFullPathName();
    expectSameRender(loadTestProject(), {.playing = true}, true);
    expectSameRender(loadTestProject(), {.playing = false}, false);
    expectSameRender(loadTestProject(), {.playing = true, .varySizes = true, .loop = true, .blocks = 1500}, true);
}

// The plan exists only between a device's start and stop, the window a bounce closes (it stops the device callback)
// before it renders; a hosted block is kept off the workers by the engine's device-callback flag.
TEST_F(ChannelFlowTest, TrackWorkerPlanLivesOnlyWhileADeviceRuns) {
    const ProjectCopy copy(fixtureProject());
    MainComponent mc(std::make_unique<MockProviderCFT>());
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(copy.dir));
    engine.setTrackWorkerCount(2);
    EXPECT_EQ(engine.getTrackWorkerCount(), 2);
    EXPECT_FALSE(engine.isTrackWorkerRenderActive()) << "no device has started, so no plan exists yet";
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    EXPECT_TRUE(engine.isTrackWorkerRenderActive());
    engine.audioDeviceStopped();
    EXPECT_FALSE(engine.isTrackWorkerRenderActive()) << "a stopped device (as a bounce stops it) drops the plan";
    engine.setTrackWorkerCount(0);
    EXPECT_FALSE(engine.isTrackWorkerRenderActive());
}

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// A module added the way a project load adds one, with a fixed uuid so two runs build the very same node.
NodeID addModule(juce::AudioProcessorGraph& graph, const juce::String& type, const juce::String& uuid) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    if (node == nullptr)
        return {};
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node->nodeID;
}

NodeID audioOutputNode(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor()->getName() == "Audio Output")
            return node->nodeID;
    return {};
}

// One graph edit of the scripted sequence: a new Oscillator cabled to the output, a cable taken away, a Filter
// spliced in, the Oscillator deleted (with its cables), the Filter deleted. `round` keeps uuids apart across repeats.
void applyEdit(juce::AudioProcessorGraph& graph, int edit, int round) {
    const auto out = audioOutputNode(graph);
    const juce::String tag = juce::String(round);
    const auto byUuid = [&](const juce::String& uuid) {
        for (auto* node : graph.getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return node->nodeID;
        return NodeID{};
    };
    const auto osc = byUuid("swap-osc-" + tag);
    const auto filter = byUuid("swap-filter-" + tag);
    switch (edit) {
    case 0: {
        const auto added = addModule(graph, "Oscillator", "swap-osc-" + tag);
        graph.addConnection({{added, 0}, {out, 0}});
        graph.addConnection({{added, 1}, {out, 1}});
        break;
    }
    case 1:
        graph.removeConnection({{osc, 1}, {out, 1}});
        break;
    case 2: {
        const auto added = addModule(graph, "Filter", "swap-filter-" + tag);
        graph.addConnection({{osc, 0}, {added, 0}});
        graph.addConnection({{added, 0}, {out, 1}});
        break;
    }
    case 3:
        graph.removeNode(osc);
        break;
    default:
        graph.removeNode(filter);
        break;
    }
}
constexpr int kEdits = 5;

struct SwapRun {
    std::uint64_t hash = 1469598103934665603ull;
    double energy = 0.0;
    int plansBefore = 0;
    int plansAfter = 0;
    bool activeAfterEveryEdit = true;
};

// Renders the fixture from the test thread, one scripted edit every 40 blocks. After each edit the message queue is
// flushed (JUCE's own rebuild and the topology broadcast both arrive through it), so the single and the multi run
// render every block on the same topology and must match bit for bit.
SwapRun renderWithEdits(int workers) {
    SwapRun run;
    const ProjectCopy copy(fixtureProject());
    MainComponent mc(std::make_unique<MockProviderCFT>());
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    if (!mc.openProjectForTest(copy.dir))
        return run;
    synth::test::flushMessageQueue();
    engine.setTrackWorkerCount(workers);
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    engine.getTransport().play();
    run.plansBefore = engine.getTrackWorkerPlansBuilt();

    std::vector<float> inL(512), inR(512), outL(512), outR(512);
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {outL.data(), outR.data()};
    constexpr int kBlocksPerEdit = 40;
    for (int k = 0; k < kBlocksPerEdit * (kEdits + 1); ++k) {
        if (k > 0 && k % kBlocksPerEdit == 0) {
            applyEdit(engine.getGraph(), k / kBlocksPerEdit - 1, 0);
            synth::test::flushMessageQueue();
            run.activeAfterEveryEdit = run.activeAfterEveryEdit && (workers == 0 || engine.isTrackWorkerRenderActive());
        }
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 512, {});
        for (const auto* channel : {&outL, &outR})
            for (const float sample : *channel) {
                run.hash ^= std::bit_cast<std::uint32_t>(sample);
                run.hash *= 1099511628211ull;
                run.energy += (double)sample * sample;
            }
    }
    run.plansAfter = engine.getTrackWorkerPlansBuilt();
    engine.getTransport().stop();
    engine.audioDeviceStopped();
    return run;
}

} // namespace

TEST_F(ChannelFlowTest, TrackWorkerPlanSwapOnGraphEditsStaysIdentical) {
    const auto single = renderWithEdits(0);
    const auto multi = renderWithEdits(3);
    EXPECT_GT(single.energy, 0.0) << "the edits made no sound, so equality proves little";
    EXPECT_EQ(single.plansAfter, single.plansBefore) << "no plan is ever built with the workers off";
    EXPECT_GE(multi.plansAfter - multi.plansBefore, kEdits) << "every edit swaps a new plan in";
    EXPECT_TRUE(multi.activeAfterEveryEdit) << "an edit left the workers without a usable plan";
    EXPECT_EQ(single.hash, multi.hash) << std::hex << "single=" << single.hash << " multi=" << multi.hash;
}

// A real audio thread renders on the workers while the message thread edits the graph and changes the worker count,
// as a user patching during playback does. Every block must come out finite and the render must keep going; under
// ThreadSanitizer this is the race check of the plan and pool swap (install, drain, the held node references).
TEST_F(ChannelFlowTest, TrackWorkerPlanSwapsWhileTheAudioThreadRenders) {
    const ProjectCopy copy(fixtureProject());
    MainComponent mc(std::make_unique<MockProviderCFT>());
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(copy.dir));
    synth::test::flushMessageQueue();
    engine.setTrackWorkerCount(3);
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    ASSERT_TRUE(engine.isTrackWorkerRenderActive());
    engine.getTransport().play();
    const int plansBefore = engine.getTrackWorkerPlansBuilt();

    std::atomic<bool> stop{false};
    std::atomic<int> blocks{0}, nonFinite{0};
    std::thread audio([&] {
        std::vector<float> inL(128), inR(128), outL(128), outR(128);
        const float* inputs[] = {inL.data(), inR.data()};
        float* outputs[] = {outL.data(), outR.data()};
        while (!stop.load(std::memory_order_relaxed)) {
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 128, {});
            for (const auto* channel : {&outL, &outR})
                for (const float sample : *channel)
                    if (!std::isfinite(sample))
                        nonFinite.fetch_add(1, std::memory_order_relaxed);
            blocks.fetch_add(1, std::memory_order_relaxed);
        }
    });

    constexpr int kRounds = 6;
    for (int round = 0; round < kRounds; ++round) {
        for (int edit = 0; edit < kEdits; ++edit) {
            const int before = blocks.load();
            applyEdit(engine.getGraph(), edit, round);
            synth::test::flushMessageQueue();
            // Let the audio thread render a few blocks on the new plan before the next edit.
            synth::test::waitUntil([&] { return blocks.load() >= before + 4; }, 10000, 1);
        }
        if (round == kRounds / 2) {
            engine.setTrackWorkerCount(1); // a new pool under the running audio thread
            engine.setTrackWorkerCount(3);
        }
    }
    const int rendered = blocks.load();
    synth::test::waitUntil([&] { return blocks.load() >= rendered + 4; }, 10000, 1);
    stop.store(true);
    audio.join();

    EXPECT_EQ(nonFinite.load(), 0);
    EXPECT_GE(blocks.load(), kRounds * kEdits * 4);
    EXPECT_GE(engine.getTrackWorkerPlansBuilt() - plansBefore, kRounds * kEdits);
    EXPECT_TRUE(engine.isTrackWorkerRenderActive());
    engine.getTransport().stop();
    engine.audioDeviceStopped();
}

namespace {

double ticksToUs(std::int64_t ticks) { return juce::Time::highResolutionTicksToSeconds(ticks) * 1.0e6; }
double cpuSeconds() { return (double)std::clock() / CLOCKS_PER_SEC; }

struct CallbackStats {
    double mean = 0.0, p99 = 0.0, max = 0.0;
};

CallbackStats statsOf(std::vector<double> values) {
    CallbackStats s;
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

// Forwards a real device's callback to the engine, times each one, and mutes what reaches the speakers.
class TimedEngineCallback : public juce::AudioIODeviceCallback {
public:
    explicit TimedEngineCallback(AudioEngine& engine)
        : engine_(engine) {
        times_.resize(1 << 20);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override { engine_.audioDeviceAboutToStart(device); }
    void audioDeviceStopped() override { engine_.audioDeviceStopped(); }
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut,
                                          int numSamples, const juce::AudioIODeviceCallbackContext& context) override {
        const auto start = juce::Time::getHighResolutionTicks();
        engine_.audioDeviceIOCallbackWithContext(in, numIn, out, numOut, numSamples, context);
        const auto end = juce::Time::getHighResolutionTicks();
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::clear(out[ch], numSamples);
        if (recording_.load(std::memory_order_acquire)) {
            const int i = count_.fetch_add(1, std::memory_order_relaxed);
            if (i < (int)times_.size())
                times_[(size_t)i] = ticksToUs(end - start);
        }
    }
    void startRecording() {
        count_.store(0);
        recording_.store(true, std::memory_order_release);
    }
    std::vector<double> stopRecording() {
        recording_.store(false, std::memory_order_release);
        juce::Thread::sleep(50); // a callback in flight finishes its write
        return {times_.begin(), times_.begin() + std::min(count_.load(), (int)times_.size())};
    }

private:
    AudioEngine& engine_;
    std::vector<double> times_;
    std::atomic<int> count_{0};
    std::atomic<bool> recording_{false};
};

} // namespace

// A disabled bench, not a test: plays a copy of the Load test project (PROFILE_PROJECT=<bundle> for another) through
// the machine's default output device, output muted, and prints the callback time, the callbacks over budget and the
// device's own xrun count per worker idle policy and buffer size; skips when no output device opens. Run by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*TrackWorkerRealDeviceProfile*'
// The synthetic counterpart, with no project, is DISABLED_TrackWorkerWakeProfile (TrackWorkerWakeProfileTests.cpp).
TEST_F(ChannelFlowTest, DISABLED_TrackWorkerRealDeviceProfile) {
    const char* projectEnv = std::getenv("PROFILE_PROJECT");
    const juce::File project = projectEnv != nullptr ? juce::File(projectEnv)
                                                     : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                                           .getChildFile("AgentSynth/Load test.agsproj");
    if (!project.isDirectory())
        GTEST_SKIP() << "no " << project.getFullPathName();
    const juce::File dir = synth::test::uniqueTempPath("TrackWorkerRealDevice", ".agsproj");
    ASSERT_TRUE(project.copyDirectoryTo(dir));
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "autosave*.json"))
        f.deleteFile();

    {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        auto& engine = mc.getAudioEngine();
        engine.suspendDeviceCallback();
        ASSERT_TRUE(mc.openProjectForTest(dir));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // any deferred graph rebuild lands first

        juce::AudioDeviceManager devices; // real-audio-device-ok: a disabled bench timing the real device
        if (devices.initialiseWithDefaultDevices(0, 2).isNotEmpty() || devices.getCurrentAudioDevice() == nullptr) {
            dir.deleteRecursively();
            GTEST_SKIP() << "no output device opens here";
        }
        TimedEngineCallback timed(engine);
        devices.addAudioCallback(&timed);
        engine.getTransport().play();

        struct Mode {
            const char* name;
            int workers, spin;
        };
        const Mode modes[] = {
            {"single", 0, 0}, {"wait", 3, 0}, {"poll 20us", 3, 20}, {"poll 50us", 3, 50}, {"poll 200us", 3, 200}};
        std::printf("device: %s\n", devices.getCurrentAudioDevice()->getName().toRawUTF8());
        std::printf("| block | mode | callback mean | p99 | max | over 80%% budget | xruns | process CPU |\n");
        std::printf("|---|---|---|---|---|---|---|---|\n");
        for (const int block : {64, 128, 512}) {
            auto setup = devices.getAudioDeviceSetup();
            setup.bufferSize = block;
            devices.setAudioDeviceSetup(setup, true);
            auto* device = devices.getCurrentAudioDevice();
            const int actual = device->getCurrentBufferSizeSamples();
            const double budgetUs = 1.0e6 * actual / device->getCurrentSampleRate();
            for (const auto& mode : modes) {
                engine.setTrackWorkerIdleSpinMicros(mode.spin);
                engine.setTrackWorkerCount(0);
                engine.setTrackWorkerCount(mode.workers);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(500); // settle: caches, the pool's start
                const int xruns0 = device->getXRunCount();
                const double cpu0 = cpuSeconds();
                const double wall0 = juce::Time::getMillisecondCounterHiRes();
                timed.startRecording();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(4000);
                const auto times = timed.stopRecording();
                const double wall = (juce::Time::getMillisecondCounterHiRes() - wall0) / 1000.0;
                const auto s = statsOf(times);
                const auto over =
                    std::count_if(times.begin(), times.end(), [&](double t) { return t > 0.8 * budgetUs; });
                std::printf("| %d | %s | %.0f us | %.0f us | %.0f us | %d of %d | %d | %.0f%% |\n", actual, mode.name,
                            s.mean, s.p99, s.max, (int)over, (int)times.size(), device->getXRunCount() - xruns0,
                            100.0 * (cpuSeconds() - cpu0) / wall);
            }
        }
        engine.getTransport().stop();
        devices.removeAudioCallback(&timed);
        devices.closeAudioDevice();
    }
    dir.deleteRecursively();
}
