// ModuleCpuProfileTests.cpp
//
// A disabled profiling bench, not a test: opens a copy of the saved Load test project in a real MainComponent, plays
// its timeline and renders blocks through the AudioEngine's device callback (a fake 48 kHz / 512-sample device), then
// prints the per-block render time. Run by hand (docs/architecture/audio-engine.md#measuring-render-cost):
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*ModuleCpuProfile*'
// PROFILE_PROJECT=<bundle> (default ~/Music/AgentSynth/Load test.agsproj), PROFILE_BLOCKS=<n> (default 2000) and
// PROFILE_SPIN=<s> to keep rendering for that long after the timed pass, to attach `sample $(pgrep -n Tests)` to for
// the per-module split. DISABLED_GraphNodeOverheadProfile times the graph's own cost per node with processors that do
// no work.

#include "../FakeAudioIODevice.h"
#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "Modules/AttenuverterModule.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

double nowUs() { return juce::Time::getMillisecondCounterHiRes() * 1000.0; }

// The bench never opens or writes the real bundle: the MainComponent autosaves, and an autosave left in the original
// is what that bundle would reopen to. The copy leaves the autosave sidecars behind for the same reason.
struct ProjectCopy {
    juce::File dir;
    explicit ProjectCopy(const juce::File& original)
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getNonexistentChildFile("ModuleCpuBench", ".agsproj")) {
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
    synth::test::FakeAudioIODevice fake(2, 2, 0, 0);
    engine.audioDeviceAboutToStart(&fake);
    engine.getTransport().play();
    DeviceDriver driver(engine);
    for (int i = 0; i < 200; ++i) // warm-up: caches, envelopes reaching steady state, the first notes
        driver.block();

    std::vector<double> times;
    times.reserve((size_t)blocks);
    double outEnergy = 0.0;
    std::uint64_t hash = 1469598103934665603ull; // FNV-1a over every output sample's bits: the bit-identity check
    const auto mix = [&hash](const std::vector<float>& channel) {
        for (float v : channel) {
            hash ^= std::bit_cast<std::uint32_t>(v);
            hash *= 1099511628211ull;
        }
    };
    for (int i = 0; i < blocks; ++i) {
        const double t0 = nowUs();
        driver.block();
        times.push_back(nowUs() - t0);
        for (float v : driver.outL)
            outEnergy += (double)v * v;
        mix(driver.outL);
        mix(driver.outR);
    }
    const auto s = summarise(times);
    const double budgetUs = 1.0e6 * synth::test::kFakeDeviceBlockSize / synth::test::kFakeDeviceSampleRate;
    std::printf("[render] %s nodes=%d blocks=%d block=512@48k mean=%.1fus median=%.1fus p99=%.1fus max=%.1fus "
                "(%.1f%% of the %.0fus budget) outRms=%.4f hash=%016llx\n",
                original.getFileName().toRawUTF8(), (int)engine.getGraph().getNumNodes(), blocks, s.meanUs, s.medianUs,
                s.p99Us, s.maxUs, 100.0 * s.meanUs / budgetUs, budgetUs,
                std::sqrt(outEnergy / (blocks * (double)synth::test::kFakeDeviceBlockSize)), (unsigned long long)hash);
    std::fflush(stdout);

    if (const char* spin = std::getenv("PROFILE_SPIN")) { // keep rendering for `sample` to attach to
        std::printf("[spin]\n");                          // attach with: sample $(pgrep -n Tests) 10
        std::fflush(stdout);
        const double until = nowUs() + 1.0e6 * std::atof(spin);
        while (nowUs() < until)
            driver.block();
    }
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
