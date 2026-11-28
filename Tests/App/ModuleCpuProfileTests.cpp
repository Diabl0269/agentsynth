// ModuleCpuProfileTests.cpp
//
// A disabled profiling bench, not a test: opens a copy of the saved Load test project in a real MainComponent, plays
// its timeline and renders blocks through the AudioEngine's device callback (a fake 48 kHz / 512-sample device), then
// prints the per-block render time. Run by hand (docs/architecture/audio-engine.md#measuring-render-cost):
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*ModuleCpuProfile*'
// PROFILE_PROJECT=<bundle> (default ~/Music/AgentSynth/Load test.agsproj), PROFILE_BLOCKS=<n> (default 2000) and
// PROFILE_IDLE=1 renders with the transport stopped (the idle cost of an open project) and
// PROFILE_NO_SHORTCUTS=1 renders the reference paths (synth::RenderShortcuts off) to compare against, and
// PROFILE_WORKERS=<n> renders on n track workers (the multi-core render; 0 or unset is single-thread),
// PROFILE_PACED=1 waits out each block's real-time length between blocks, as a real device does, and
// PROFILE_SPIN=<s> to keep rendering for that long after the timed pass, to attach `sample $(pgrep -n Tests)` to for
// the per-module split. DISABLED_GraphNodeOverheadProfile times the graph's own cost per node with processors that do
// no work.

#include "../FakeAudioIODevice.h"
#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "../UniqueTempDir.h"
#include "AudioEngine/TrackCut.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/RenderShortcuts.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

double nowUs() { return juce::Time::getMillisecondCounterHiRes() * 1000.0; }

// The bench never opens or writes the real bundle: the MainComponent autosaves, and an autosave left in the original
// is what that bundle would reopen to. The copy leaves the autosave sidecars behind for the same reason.
struct ProjectCopy {
    juce::File dir;
    explicit ProjectCopy(const juce::File& original)
        : dir(synth::test::uniqueTempPath("ModuleCpuBench", ".agsproj")) {
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

struct BlockStats {
    double meanUs = 0, medianUs = 0, p99Us = 0, maxUs = 0;
};

BlockStats summarise(std::vector<double> t) {
    BlockStats s;
    if (t.empty())
        return s;
    for (double v : t)
        s.meanUs += v;
    s.meanUs /= (double)t.size();
    std::sort(t.begin(), t.end());
    s.medianUs = t[t.size() / 2];
    s.p99Us = t[std::min(t.size() - 1, (size_t)((double)t.size() * 0.99))];
    s.maxUs = t.back();
    return s;
}

// Drives the engine exactly as a device would: one audioDeviceIOCallbackWithContext per block.
struct DeviceDriver {
    AudioEngine& engine;
    std::vector<float> inL, inR, outL, outR;
    explicit DeviceDriver(AudioEngine& e)
        : engine(e)
        , inL(synth::test::kFakeDeviceBlockSize)
        , inR(synth::test::kFakeDeviceBlockSize)
        , outL(synth::test::kFakeDeviceBlockSize)
        , outR(synth::test::kFakeDeviceBlockSize) {}
    void block() {
        const float* inputs[] = {inL.data(), inR.data()};
        float* outputs[] = {outL.data(), outR.data()};
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, synth::test::kFakeDeviceBlockSize, {});
    }
};

// One `[cut]` line (strip jobs and their sizes, the Shared and Post sizes, whether the job graph is acyclic) and a
// `[cut-job]` line per job with its node count by module type name.
void printTrackCut(const juce::AudioProcessorGraph& graph) {
    const auto cut = synth::cutIntoTrackJobs(graph);
    std::vector<size_t> stripSizes;
    size_t sharedSize = 0, postSize = 0;
    for (const auto& job : cut.jobs) {
        if (job.kind == synth::TrackJob::Kind::Strip)
            stripSizes.push_back(job.nodes.size());
        else
            (job.kind == synth::TrackJob::Kind::Shared ? sharedSize : postSize) = job.nodes.size();
    }
    std::sort(stripSizes.begin(), stripSizes.end());
    std::printf("[cut] strip-jobs=%zu nodes-per-strip-job min=%zu median=%zu max=%zu shared=%zu post=%zu acyclic=%d\n",
                stripSizes.size(), stripSizes.empty() ? 0 : stripSizes.front(),
                stripSizes.empty() ? 0 : stripSizes[stripSizes.size() / 2], stripSizes.empty() ? 0 : stripSizes.back(),
                sharedSize, postSize, cut.acyclic ? 1 : 0);
    int index = 0;
    for (const auto& job : cut.jobs) {
        std::map<std::string, int> byType;
        for (const auto& id : job.nodes)
            if (auto* node = graph.getNodeForId(id))
                ++byType[node->getProcessor()->getName().trimCharactersAtEnd("0123456789").trimEnd().toStdString()];
        const char* kind = job.kind == synth::TrackJob::Kind::Strip    ? "strip"
                           : job.kind == synth::TrackJob::Kind::Shared ? "shared"
                                                                       : "post";
        std::printf("[cut-job] %d %s nodes=%zu:", index++, kind, job.nodes.size());
        for (const auto& [name, count] : byType)
            std::printf(" %s=%d", name.c_str(), count);
        std::printf("\n");
    }
    std::fflush(stdout);
}

void profileProjectRender(const juce::File& original, int blocks) {
    const ProjectCopy copy(original);
    if (copy.dir == juce::File()) {
        std::printf("[render] could not copy %s\n", original.getFullPathName().toRawUTF8());
        return;
    }
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 1000);
    auto& engine = mc.getAudioEngine();
    engine.suspendDeviceCallback();
    if (!mc.openProjectForTest(copy.dir)) {
        std::printf("[render] could not open %s\n", original.getFullPathName().toRawUTF8());
        return;
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // any deferred graph rebuild lands first
    if (const char* workers = std::getenv("PROFILE_WORKERS"))      // the multi-core render: that many track workers
        engine.setTrackWorkerCount(std::atoi(workers));
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    if (engine.getTrackWorkerCount() > 0)
        std::printf("[workers] %d active=%d\n", engine.getTrackWorkerCount(),
                    engine.isTrackWorkerRenderActive() ? 1 : 0);
    const bool idle = std::getenv("PROFILE_IDLE") != nullptr;
    if (!idle)
        engine.getTransport().play();
    synth::RenderShortcuts::setEnabled(std::getenv("PROFILE_NO_SHORTCUTS") == nullptr);
    DeviceDriver driver(engine);
    for (int i = 0; i < 200; ++i) // warm-up: caches, envelopes reaching steady state, the first notes
        driver.block();

    std::vector<double> times;
    times.reserve((size_t)blocks);
    double outEnergy = 0.0;
    std::uint64_t hash = 1469598103934665603ull; // FNV-1a over every output sample's bits: the bit-identity check
    const auto mix = [&hash](const std::vector<float>& channel) {
        for (float v : channel) {
            if (v == 0.0f)
                v = 0.0f; // -0 and +0 are the same sample value; a skipped block writes +0 where the full path can
                          // write -0
            hash ^= std::bit_cast<std::uint32_t>(v);
            hash *= 1099511628211ull;
        }
    };
    // PROFILE_PACED=1 waits out each block's real-time length, as a device does, so the core idles between blocks
    // the way it does in the app (and may clock down); unpaced, the bench renders back to back on a busy core.
    const bool paced = std::getenv("PROFILE_PACED") != nullptr;
    const double blockUs = 1.0e6 * synth::test::kFakeDeviceBlockSize / synth::test::kFakeDeviceSampleRate;
    const double pacedStart = nowUs();
    for (int i = 0; i < blocks; ++i) {
        const double t0 = nowUs();
        driver.block();
        times.push_back(nowUs() - t0);
        if (paced)
            std::this_thread::sleep_for(
                std::chrono::microseconds((long long)std::max(0.0, pacedStart + (i + 1) * blockUs - nowUs())));
        for (float v : driver.outL)
            outEnergy += (double)v * v;
        mix(driver.outL);
        mix(driver.outR);
    }
    const auto s = summarise(times);
    const double budgetUs = 1.0e6 * synth::test::kFakeDeviceBlockSize / synth::test::kFakeDeviceSampleRate;
    std::printf("[render] %s %s%s nodes=%d blocks=%d block=512@48k mean=%.1fus median=%.1fus p99=%.1fus max=%.1fus "
                "(%.1f%% of the %.0fus budget) outRms=%.4f hash=%016llx\n",
                original.getFileName().toRawUTF8(), idle ? "idle" : "playing", paced ? " paced" : "",
                (int)engine.getGraph().getNumNodes(), blocks, s.meanUs, s.medianUs, s.p99Us, s.maxUs,
                100.0 * s.meanUs / budgetUs, budgetUs,
                std::sqrt(outEnergy / (blocks * (double)synth::test::kFakeDeviceBlockSize)), (unsigned long long)hash);
    std::fflush(stdout);
    printTrackCut(engine.getGraph());

    if (const char* spin = std::getenv("PROFILE_SPIN")) { // keep rendering for `sample` to attach to
        std::printf("[spin]\n");                          // attach with: sample $(pgrep -n Tests) 10
        std::fflush(stdout);
        const double until = nowUs() + 1.0e6 * std::atof(spin);
        while (nowUs() < until)
            driver.block();
    }
    synth::RenderShortcuts::setEnabled(true);
    engine.getTransport().stop();
}

// A processor that does nothing, so a graph of them times the graph alone.
class NullProcessor : public juce::AudioProcessor {
public:
    NullProcessor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("In", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Out", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Null"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

// Mean microseconds per graph block for a chain of `n` nodes made by `make`, each fed from the one before.
template <typename Make>
double chainUsPerBlock(int n, Make make) {
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000.0, 512);
    juce::AudioProcessorGraph::NodeID prev{};
    for (int i = 0; i < n; ++i) {
        auto node = graph.addNode(make(), {}, juce::AudioProcessorGraph::UpdateKind::none);
        if (i > 0)
            graph.addConnection({{prev, 0}, {node->nodeID, 0}}, juce::AudioProcessorGraph::UpdateKind::none);
        prev = node->nodeID;
    }
    graph.rebuild();
    graph.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)
        graph.processBlock(buffer, midi);
    constexpr int kBlocks = 2000;
    const double t0 = nowUs();
    for (int i = 0; i < kBlocks; ++i)
        graph.processBlock(buffer, midi);
    return (nowUs() - t0) / kBlocks;
}

} // namespace

TEST_F(ChannelFlowTest, DISABLED_ModuleCpuProfile) {
    const char* path = std::getenv("PROFILE_PROJECT");
    const auto project = path != nullptr ? juce::File(path)
                                         : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                               .getChildFile("AgentSynth/Load test.agsproj");
    const char* n = std::getenv("PROFILE_BLOCKS");
    profileProjectRender(project, n != nullptr ? std::atoi(n) : 2000);
}

TEST(ModuleCpuBench, DISABLED_GraphNodeOverheadProfile) {
    for (int n : {50, 200}) {
        const double nullUs = chainUsPerBlock(n, [] { return std::make_unique<NullProcessor>(); });
        const double attUs = chainUsPerBlock(n, [] { return std::make_unique<AttenuverterModule>(); });
        std::printf("[graph] nodes=%d null=%.1fus (%.3fus/node) attenuverter=%.1fus (%.3fus/node)\n", n, nullUs,
                    nullUs / n, attUs, attUs / n);
    }
    std::fflush(stdout);
}
