// The engine's worker switch on real saved projects
// (docs/architecture/audio-engine.md#multi-core-render-track-workers): a live device render on track workers is
// byte-identical to the single-thread render, playing and idle, through varied block sizes and a loop wrap; the plan
// exists only while a device runs.
#include "../FakeAudioIODevice.h"
#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "../UniqueTempDir.h"
#include "TestPaths.h"
#include <bit>
#include <cstdint>
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
