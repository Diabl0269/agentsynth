// FRO324: render-time L/Mono normalling -- Right borrows Left while Right is unpatched, for both
// Audio Output and any Dual I/O module with a genuine stereo AUDIO input pair. Never a graph edge:
// every test here drives a real AudioEngine graph through the actual Standalone device callback
// (audioDeviceIOCallbackWithContext -> renderNextBlock), the same pattern
// NonFiniteOutputGuardTests.cpp uses, so the assertions exercise the exact render path a user
// would hear rather than the raw juce::AudioProcessorGraph some GraphEditor tests call directly.
//
// AudioEngine::refreshNormalling() is called by hand after each connection change here (the same
// call `publishTimeline()`/the plugin's `setStateInformation` make on every real graph change --
// undo/redo, preset load, a module delete -- per docs/architecture/audio-engine.md#normalling-fro324).
//
// Headless house rules as everywhere else: no real audio device, no sleeps.

#include "../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/OscillatorModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
using NodeID = juce::AudioProcessorGraph::NodeID;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;

// Flips a module's own "dualIO" toggle without going through GraphEditor -- the same minimal
// helper every GraphEditorDualIOTests.cpp-style test uses, copied locally rather than reaching
// into that UI test file from a headless Engine test.
void setDualIOParam(juce::AudioProcessor& proc, bool value) {
    for (auto* p : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            p->setValueNotifyingHost(value ? 1.0f : 0.0f);
}

// Renders one block through the real Standalone device callback and returns the interleaved
// output as separate per-channel vectors, dropping the first `skipSamples` (a fresh Oscillator's
// pitch/level ramps have not settled yet).
std::vector<std::vector<float>> renderOneBlock(AudioEngine& engine, int numOutChannels) {
    FakeAudioIODevice fake(0, numOutChannels);
    engine.audioDeviceAboutToStart(&fake);

    std::vector<std::vector<float>> out(static_cast<std::size_t>(numOutChannels),
                                        std::vector<float>(static_cast<std::size_t>(kBlockSize), -9999.0f));
    std::vector<float*> outPtrs;
    for (auto& channel : out)
        outPtrs.push_back(channel.data());

    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outPtrs.data(), numOutChannels, kBlockSize, {});
    engine.audioDeviceStopped();
    return out;
}

} // namespace

// ============================================================================
// Audio Output normalling
// ============================================================================

TEST(NormallingTest, MonoIntoOutputLeftOnlyGivesIdenticalSamplesOnBothChannels) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);

    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 0}})) << "Left only -- Right stays unpatched";

    engine.refreshNormalling();
    ASSERT_TRUE(engine.isOutputRightNormalledFromLeft());

    const auto rendered = renderOneBlock(engine, 2);
    ASSERT_GT(rendered[0][kBlockSize / 2], -9999.0f) << "sanity: the callback actually wrote something";
    for (int i = 0; i < kBlockSize; ++i)
        ASSERT_EQ(rendered[0][static_cast<std::size_t>(i)], rendered[1][static_cast<std::size_t>(i)])
            << "sample " << i << ": Right must be an exact copy of Left while unpatched";
}

TEST(NormallingTest, PatchingRightStopsNormalling) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);

    auto oscLeft = graph.addNode(std::make_unique<OscillatorModule>());
    auto oscRight = graph.addNode(std::make_unique<OscillatorModule>());
    // A distinctly different level so Left and Right can never coincidentally read equal.
    for (auto* p : oscRight->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "level")
            withId->setValueNotifyingHost(0.2f);

    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{oscLeft->nodeID, 0}, {out->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscRight->nodeID, 0}, {out->nodeID, 1}})) << "Right now has its OWN source";

    engine.refreshNormalling();
    EXPECT_FALSE(engine.isOutputRightNormalledFromLeft()) << "Right is patched -- normalling must not apply";

    const auto rendered = renderOneBlock(engine, 2);
    bool anySampleDiffers = false;
    for (int i = 0; i < kBlockSize; ++i) {
        if (rendered[0][static_cast<std::size_t>(i)] != rendered[1][static_cast<std::size_t>(i)]) {
            anySampleDiffers = true;
            break;
        }
    }
    EXPECT_TRUE(anySampleDiffers)
        << "Right must carry only its own source (a lower-level, different oscillator), never a copy of Left";
}

TEST(NormallingTest, UnpatchingRightRestoresNormalling) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);

    auto oscLeft = graph.addNode(std::make_unique<OscillatorModule>());
    auto oscRight = graph.addNode(std::make_unique<OscillatorModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{oscLeft->nodeID, 0}, {out->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscRight->nodeID, 0}, {out->nodeID, 1}}));
    engine.refreshNormalling();
    ASSERT_FALSE(engine.isOutputRightNormalledFromLeft());

    ASSERT_TRUE(graph.removeConnection({{oscRight->nodeID, 0}, {out->nodeID, 1}}));
    engine.refreshNormalling();
    EXPECT_TRUE(engine.isOutputRightNormalledFromLeft()) << "unplugging Right must restore the borrow";

    const auto rendered = renderOneBlock(engine, 2);
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_EQ(rendered[0][static_cast<std::size_t>(i)], rendered[1][static_cast<std::size_t>(i)]);
}

TEST(NormallingTest, MultichannelOutputNeverNormalsPastTheFirstPair) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 4, kSampleRate, kBlockSize);

    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 0}})); // ch1-3 all unpatched

    engine.refreshNormalling();
    ASSERT_TRUE(engine.isOutputRightNormalledFromLeft());

    const auto rendered = renderOneBlock(engine, 4);
    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_EQ(rendered[0][static_cast<std::size_t>(i)], rendered[1][static_cast<std::size_t>(i)])
            << "the first pair still normals";
        EXPECT_EQ(rendered[2][static_cast<std::size_t>(i)], 0.0f) << "ch2 must never inherit the borrow";
        EXPECT_EQ(rendered[3][static_cast<std::size_t>(i)], 0.0f) << "ch3 must never inherit the borrow";
    }
}

TEST(NormallingTest, FlagRecomputesOnEveryPublishTimelineCallLikeUndoRedoAndPresetLoad) {
    // publishTimeline() is the one seam every graph-replacing path (undo/redo restore, preset
    // load, a module delete) already has to reach -- Source/CLAUDE.md's own "every graph change
    // has to tell it" invariant -- and refreshNormalling() rides inside it exactly like
    // refreshSoloGate(). This proves the flag tracks a plain publishTimeline() call rather than
    // needing its own separate call site.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);

    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 0}}));

    const synth::TimelineDoc emptyDoc;
    engine.publishTimeline(emptyDoc);
    EXPECT_TRUE(engine.isOutputRightNormalledFromLeft());

    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 1}})); // simulate a redo that repatched Right
    engine.publishTimeline(emptyDoc);
    EXPECT_FALSE(engine.isOutputRightNormalledFromLeft());
}

// ============================================================================
// Dual I/O module normalling
// ============================================================================

TEST(NormallingTest, DualIOEffectWithOnlyLeftPatchedProducesStereoIdenticalToTheCollapsedJackCase) {
    constexpr int kRenderSamples = kBlockSize;

    // Reference: Delay's jack COLLAPSED (Dual I/O off) -- the established "one Audio jack owning
    // both raw legs" behaviour, wired by hand exactly the way a collapsed jack fans a mono cable.
    AudioEngine collapsedEngine(AudioEngine::HostMode::Standalone);
    {
        auto& graph = collapsedEngine.getGraph();
        graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        auto delay = graph.addNode(std::make_unique<DelayModule>());
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        setDualIOParam(*delay->getProcessor(), false);
        ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {delay->nodeID, 0}}));
        ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {delay->nodeID, 1}})); // collapsed jack's own fan
        ASSERT_TRUE(graph.addConnection({{delay->nodeID, 0}, {out->nodeID, 0}}));
        ASSERT_TRUE(graph.addConnection({{delay->nodeID, 1}, {out->nodeID, 1}}));
        collapsedEngine.refreshNormalling();
    }
    const auto reference = renderOneBlock(collapsedEngine, 2);

    // Split, Left only: Dual I/O on, only Delay's raw ch0 patched -- normalling must borrow Left
    // into Delay's own Right leg BEFORE the delay line runs, so its output matches the reference.
    AudioEngine splitEngine(AudioEngine::HostMode::Standalone);
    {
        auto& graph = splitEngine.getGraph();
        graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        auto delay = graph.addNode(std::make_unique<DelayModule>());
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        setDualIOParam(*delay->getProcessor(), true);
        ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {delay->nodeID, 0}})); // Left only
        ASSERT_TRUE(graph.addConnection({{delay->nodeID, 0}, {out->nodeID, 0}}));
        ASSERT_TRUE(graph.addConnection({{delay->nodeID, 1}, {out->nodeID, 1}}));
        splitEngine.refreshNormalling();
        auto* delayModule = dynamic_cast<ModuleBase*>(delay->getProcessor());
        ASSERT_NE(delayModule, nullptr);
        ASSERT_TRUE(delayModule->isNormalLeftToRight()) << "Left patched, Right not -- must be normalled";
    }
    const auto split = renderOneBlock(splitEngine, 2);

    for (int i = 0; i < kRenderSamples; ++i) {
        EXPECT_FLOAT_EQ(reference[0][static_cast<std::size_t>(i)], split[0][static_cast<std::size_t>(i)]);
        EXPECT_FLOAT_EQ(reference[1][static_cast<std::size_t>(i)], split[1][static_cast<std::size_t>(i)])
            << "sample " << i << ": a split Dual I/O module with only Left patched must sound identical to the "
            << "collapsed-jack case";
    }
}
