// A project saved before Level, Pan and the shared Mix took modulation cables:
// Tools/ui-verify/fixtures/pre-level-cv.agsproj was written by the app before those channels existed and must never be
// regenerated. Opening it must keep every cable on its raw channel, and it must sound as it did.

#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "TestPaths.h"
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 200;

juce::File oldProject() {
    return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("pre-level-cv.agsproj");
}

// Rendered by the app that wrote the fixture (0.04617), then raised about 0.5 dB when the Filter's resonance curve was
// fitted to a measured reference. The chain's LFOs run freely, so a tolerance is needed: runs differ by a few parts in
// ten thousand.
constexpr float kOldProjectRms = 0.0488f;

// Each module's cables as the old app wrote them: its highest and its lowest modulation channel, with what they drive.
struct SavedCables {
    const char* type;
    int high;
    const char* highTarget;
    int low;
};
const std::vector<SavedCables> kSavedCables = {
    {"Delay", 4, "mix", 2},
    {"Reverb", 7, "preDelay", 2},
    {"Chorus", 6, "mix", 2},
    {"Phaser", 6, "mix", 2},
    {"Flanger", 6, "mix", 2},
    {"Distortion", 3, "mix", 2},
    {"Bitcrusher", 5, "dither", 2},
    {"Pitch Shifter", 7, "window", 2},
    {"Ring Modulator", 4, "character", 2},
    {"Gate", 6, "range", 2},
    {"Filter", 31, "depth", 1},
    {"Compressor", 9, "knee", 2},
    {"Limiter", 5, "ceiling", 2},
    {"Parametric EQ", 14, "outputGain", 2},
};

juce::AudioProcessorGraph::Node* nodeOfType(AudioEngine& engine, const juce::String& type) {
    for (auto* node : engine.getGraph().getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
            module != nullptr && module->getName().startsWith(type))
            return node;
    return nullptr;
}

// The raw channels of cables that arrive from a modulator, not from the audio chain before it.
std::set<int> modulationChannels(AudioEngine& engine, juce::AudioProcessorGraph::NodeID node) {
    std::set<int> channels;
    for (const auto& connection : engine.getGraph().getConnections())
        if (connection.destination.nodeID == node && connection.destination.channelIndex != 0)
            channels.insert(connection.destination.channelIndex);
    return channels;
}

// Peak and RMS of the graph's output over a fixed render, from a stopped transport.
std::pair<float, float> render(AudioEngine& engine) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kRate, kBlock);
    graph.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(juce::jmax(2, graph.getMainBusNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    double sum = 0.0;
    for (int b = 0; b < kBlocks; ++b) {
        buffer.clear();
        graph.processBlock(buffer, midi);
        peak = juce::jmax(peak, buffer.getMagnitude(0, 0, kBlock));
        const float rms = buffer.getRMSLevel(0, 0, kBlock);
        sum += (double)rms * rms;
    }
    return {peak, (float)std::sqrt(sum / kBlocks)};
}

} // namespace

class SavedCablesKeepChannels : public ChannelFlowTest {};

// Regression test for FRO1156: Level, Pan and the shared Mix took new channels; a saved project's cables stay on
// theirs.
TEST_F(SavedCablesKeepChannels, AnOldProjectKeepsEveryCableAndSoundsTheSame) {
    ASSERT_TRUE(oldProject().getChildFile("project.json").existsAsFile());
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(oldProject()));
    auto& engine = mc.getAudioEngine();

    for (const auto& saved : kSavedCables) {
        SCOPED_TRACE(saved.type);
        auto* node = nodeOfType(engine, saved.type);
        ASSERT_NE(node, nullptr);
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        const auto channels = modulationChannels(engine, node->nodeID);
        EXPECT_EQ(channels, (std::set<int>{saved.low, saved.high}));
        for (const auto& target : module->getModulationTargets())
            if (target.channelIndex == saved.high)
                EXPECT_EQ(target.paramId, juce::String(saved.highTarget));
        for (const juce::String shared : {"outputLevel", "pan"})
            if (const int channel = module->modulationChannelForParam(shared); channel >= 0)
                EXPECT_EQ(channels.count(channel), 0u) << shared;
    }

    const auto [peak, rms] = render(engine);
    EXPECT_GT(peak, 0.0f);
    EXPECT_NEAR(rms, kOldProjectRms, kOldProjectRms * 0.02f);
    printf("[old project] rms now %.6f, when saved %.6f\n", rms, kOldProjectRms);
}
