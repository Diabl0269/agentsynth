// The mixer's solo gate and Master splice (P9-2, docs/mixer.md §5.1, §5.3).
//
//   • module gate    -- with the playhead's mixer-solo flag set, a non-soloed strip (bypassed or not)
//                        and Master's Direct input output silence; a soloed strip and Master's Mix
//                        pass
//   • engine gate    -- AudioEngine owns the soloed-strip count and publishes it per render pass:
//                        soloing one strip silences every other strip and Direct, un-soloing the
//                        last one restores them, and solo never touches any mute parameter
//   • never stuck    -- deleting a soloed strip, and undo/redo across a graph rebuild, both settle
//                        the gate at the ONE seam every graph change already reaches
//                        (publishTimeline)
//   • Master splice  -- one undo step that undoes cleanly; strips land on Mix, everything else on
//                        Direct; singleton; goes in front of an existing Rec Tap

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/AudioEngine.h"
#include "../Source/Mixer/MasterSplice.h"
#include "../Source/Modules/ChannelStripModule.h"
#include "../Source/Modules/MasterModule.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;

// A stereo source that writes one constant to both outputs — enough to tell every contributor to a
// mix apart by its value.
class ConstantSource : public juce::AudioProcessor {
public:
    explicit ConstantSource(float value)
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
        , value_(value) {}

    const juce::String getName() const override { return "Constant"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), value_, buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0.0; }
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

private:
    float value_;
};

juce::AudioBuffer<float> stripInput(float left, float right) {
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumChannels, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, left);
        buffer.setSample(kRight, i, right);
    }
    return buffer;
}

float processOnce(juce::AudioProcessor& processor, juce::AudioBuffer<float>& buffer, int channel) {
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    return buffer.getSample(channel, kBlockSize - 1);
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

// The engine-level mix: strips A (0.1) and B (0.2) into Master's Mix, a bare source (0.4) straight
// into Master's Direct, Master into Audio Output. Built by hand — the splice has its own tests below.
struct SoloRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    NodeID stripA, stripB, master;

    SoloRig() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        const auto out = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        stripA = addFactoryNode(graph, "Channel Strip");
        stripB = addFactoryNode(graph, "Channel Strip");
        const auto srcA = graph.addNode(std::make_unique<ConstantSource>(0.1f))->nodeID;
        const auto srcB = graph.addNode(std::make_unique<ConstantSource>(0.2f))->nodeID;
        const auto srcDirect = graph.addNode(std::make_unique<ConstantSource>(0.4f))->nodeID;

        for (auto [src, strip] : {std::pair{srcA, stripA}, std::pair{srcB, stripB}}) {
            graph.addConnection({{src, 0}, {strip, 0}});
            graph.addConnection({{src, 1}, {strip, kRight}});
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{srcDirect, 0}, {master, MasterModule::kDirectLeft}});
        graph.addConnection({{srcDirect, 1}, {master, MasterModule::kDirectRight}});
        graph.addConnection({{master, 0}, {out, 0}});
        graph.addConnection({{master, 1}, {out, 1}});

        engine.prepareForHost(kSampleRate, kBlockSize, 2, 2);
    }

    ~SoloRig() { engine.releaseFromHost(); }

    // One host block; returns the output's left sample (both legs carry the same mix here).
    float render() {
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
        EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), buffer.getSample(1, kBlockSize - 1), 1.0e-6f);
        return buffer.getSample(0, kBlockSize - 1);
    }
};

} // namespace

// ============================================================================
// The gate as each module sees it
// ============================================================================

TEST(MixerSoloTest, NonSoloedStripIsSilentWhileTheGateIsActiveAndASoloedOnePasses) {
    synth::TransportService transport;
    transport.setMixerSoloActiveForBlock(true);

    ChannelStripModule other;
    other.setPlayHead(&transport);
    other.prepareToPlay(kSampleRate, kBlockSize);
    auto otherBuffer = stripInput(0.5f, 0.5f);
    EXPECT_EQ(processOnce(other, otherBuffer, 0), 0.0f);
    EXPECT_EQ(otherBuffer.getSample(kRight, 0), 0.0f);
    EXPECT_EQ(other.getMeterPeak(0), 0.0f) << "the meter shows what the strip actually outputs";

    ChannelStripModule soloed;
    soloed.setSoloed(true);
    soloed.setPlayHead(&transport);
    soloed.prepareToPlay(kSampleRate, kBlockSize);
    auto soloedBuffer = stripInput(0.5f, 0.5f);
    EXPECT_NEAR(processOnce(soloed, soloedBuffer, 0), 0.5f, 1.0e-6f);
}

TEST(MixerSoloTest, BypassedNonSoloedStripIsSilentToo) {
    // Bypass disables the strip's own gain/pan; it must not become a way to leak into a soloed mix.
    synth::TransportService transport;
    transport.setMixerSoloActiveForBlock(true);

    ChannelStripModule strip;
    strip.setBypassed(true);
    strip.setPlayHead(&transport);
    strip.prepareToPlay(kSampleRate, kBlockSize);
    auto buffer = stripInput(0.5f, 0.5f);
    EXPECT_EQ(processOnce(strip, buffer, 0), 0.0f);
    EXPECT_EQ(buffer.getSample(kRight, 0), 0.0f);

    // And with the gate open, the same bypassed strip is dry again.
    transport.setMixerSoloActiveForBlock(false);
    auto dry = stripInput(0.5f, 0.5f);
    EXPECT_NEAR(processOnce(strip, dry, 0), 0.5f, 1.0e-6f);
}

TEST(MixerSoloTest, MasterSilencesDirectButNotMixWhileTheGateIsActive) {
    synth::TransportService transport;
    MasterModule master;
    master.setPlayHead(&transport);
    master.prepareToPlay(kSampleRate, kBlockSize);

    auto makeInput = [] {
        juce::AudioBuffer<float> buffer(MasterModule::kNumInputs, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) {
            buffer.setSample(MasterModule::kMixLeft, i, 0.1f);
            buffer.setSample(MasterModule::kMixRight, i, 0.1f);
            buffer.setSample(MasterModule::kDirectLeft, i, 0.4f);
            buffer.setSample(MasterModule::kDirectRight, i, 0.4f);
        }
        return buffer;
    };

    transport.setMixerSoloActiveForBlock(true);
    auto gated = makeInput();
    EXPECT_NEAR(processOnce(master, gated, 0), 0.1f, 1.0e-6f) << "Mix passes, Direct is gated";

    master.setBypassed(true);
    auto gatedBypassed = makeInput();
    EXPECT_NEAR(processOnce(master, gatedBypassed, 0), 0.1f, 1.0e-6f) << "the gate applies to bypass's sum too";

    transport.setMixerSoloActiveForBlock(false);
    auto open = makeInput();
    EXPECT_NEAR(processOnce(master, open, 0), 0.5f, 1.0e-6f);
}

TEST(MixerSoloTest, NoTransportMeansNoGating) {
    // A foreign host or a bare module has no TransportService playhead: nothing is soloed.
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    auto buffer = stripInput(0.5f, 0.5f);
    EXPECT_NEAR(processOnce(strip, buffer, 0), 0.5f, 1.0e-6f);
}

// ============================================================================
// The engine-owned count
// ============================================================================

TEST(MixerSoloTest, SoloingOneStripSilencesEveryOtherStripAndDirect) {
    SoloRig rig;
    EXPECT_NEAR(rig.render(), 0.1f + 0.2f + 0.4f, 1.0e-5f) << "nothing soloed: the whole mix";

    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, true));
    EXPECT_EQ(rig.engine.getSoloedStripCount(), 1);
    EXPECT_NEAR(rig.render(), 0.1f, 1.0e-5f) << "only strip A: strip B and Direct are gated";

    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripB, true));
    EXPECT_NEAR(rig.render(), 0.1f + 0.2f, 1.0e-5f) << "both strips soloed; Direct is still gated";
}

TEST(MixerSoloTest, UnsoloingTheLastSoloedStripRestoresEveryone) {
    SoloRig rig;
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, true));
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripB, true));
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, false));
    EXPECT_NEAR(rig.render(), 0.2f, 1.0e-5f) << "B still soloed";

    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripB, false));
    EXPECT_EQ(rig.engine.getSoloedStripCount(), 0);
    EXPECT_NEAR(rig.render(), 0.1f + 0.2f + 0.4f, 1.0e-5f);
}

TEST(MixerSoloTest, SoloNeverTouchesAnyMuteParameter) {
    SoloRig rig;
    auto& graph = rig.engine.getGraph();
    auto* b = stripAt(graph, rig.stripB);
    ASSERT_NE(b, nullptr);

    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, true));
    rig.render();
    EXPECT_FALSE(b->isMuted()) << "solo is a render-time gate, never a setMuted() fan-out";
    EXPECT_FALSE(stripAt(graph, rig.stripA)->isMuted());

    // A user mute set before the solo survives it unchanged, in both directions.
    b->setMuted(true);
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, false));
    EXPECT_TRUE(b->isMuted());
    EXPECT_NEAR(rig.render(), 0.1f + 0.4f, 1.0e-5f) << "B's own mute still applies after un-solo";
}

TEST(MixerSoloTest, SetChannelStripSoloedRefusesANodeThatIsNotAStrip) {
    SoloRig rig;
    EXPECT_FALSE(rig.engine.setChannelStripSoloed(rig.master, true));
    EXPECT_FALSE(rig.engine.setChannelStripSoloed(NodeID{9999}, true));
    EXPECT_EQ(rig.engine.getSoloedStripCount(), 0);
}

TEST(MixerSoloTest, DeletingASoloedStripReleasesTheGateAtPublishTimeline) {
    SoloRig rig;
    synth::TimelineDoc doc;
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, true));
    rig.engine.getGraph().removeNode(rig.stripA);

    // The graph change has not reached the seam yet: the stale count still gates everything left —
    // exactly the stuck-silent mix the seam exists to prevent.
    EXPECT_NEAR(rig.render(), 0.0f, 1.0e-6f);

    rig.engine.publishTimeline(doc); // what every graph-change path already calls
    EXPECT_EQ(rig.engine.getSoloedStripCount(), 0);
    EXPECT_NEAR(rig.render(), 0.2f + 0.4f, 1.0e-5f);
}

TEST(MixerSoloTest, UndoRedoAcrossAGraphRebuildSettlesTheGate) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto& graph = engine.getGraph();
    AppUndoManager undo;
    synth::TimelineDoc doc;
    // Mirrors MainComponent: every undo/redo restore ends in the reconcile + publish seam.
    undo.setRestoreHooks({}, [&] { engine.publishTimeline(doc); });

    const auto strip = addFactoryNode(graph, "Channel Strip");
    addFactoryNode(graph, "Channel Strip");
    ASSERT_TRUE(engine.setChannelStripSoloed(strip, true));

    undo.recordStructuralChange(graph, [&] { graph.removeNode(strip); });
    engine.publishTimeline(doc);
    EXPECT_EQ(engine.getSoloedStripCount(), 0) << "the soloed strip is gone";

    // Undo rebuilds the graph from JSON: the restored strip carries solo in its extra state.
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(engine.getSoloedStripCount(), 1);
    auto* restored = stripAt(graph, strip);
    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->isSoloed());

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(engine.getSoloedStripCount(), 0);
}

// ============================================================================
// The Master splice
// ============================================================================

TEST(MixerSoloTest, MasterSpliceIsOneUndoStepAndUndoesCleanly) {
    juce::AudioProcessorGraph graph;
    // Without a play config the graph's Audio Output node has no channels and refuses every wire.
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    AppUndoManager undo;
    synth::TimelineDoc doc;
    const auto out = addFactoryNode(graph, "Audio Output");
    const auto osc = addFactoryNode(graph, "Oscillator");
    graph.addConnection({{osc, 0}, {out, 0}});
    graph.addConnection({{osc, 0}, {out, 1}});
    ASSERT_TRUE(graph.isConnected(Connection{{osc, 0}, {out, 1}})) << "fixture wiring must land";

    auto* master = synth::ensureMasterNode(graph, undo, doc, {40, 60});
    ASSERT_NE(master, nullptr);
    const auto masterId = master->nodeID;
    EXPECT_EQ((int)master->properties["x"], 40);
    EXPECT_TRUE(master->properties["uuid"].toString().isNotEmpty());

    // Everything that went straight to the output now arrives on Direct.
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {masterId, MasterModule::kDirectLeft}}));
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {masterId, MasterModule::kDirectRight}}));
    EXPECT_TRUE(graph.isConnected(Connection{{masterId, 0}, {out, 0}}));
    EXPECT_TRUE(graph.isConnected(Connection{{masterId, 1}, {out, 1}}));
    EXPECT_FALSE(graph.isConnected(Connection{{osc, 0}, {out, 0}}));

    ASSERT_TRUE(undo.canUndo());
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(synth::findMasterNode(graph), nullptr);
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {out, 0}}));
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {out, 1}}));
    EXPECT_FALSE(undo.canUndo()) << "the node, its wiring and the re-route were ONE step";

    ASSERT_TRUE(undo.redo());
    auto* redone = synth::findMasterNode(graph);
    ASSERT_NE(redone, nullptr);
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {redone->nodeID, MasterModule::kDirectLeft}}));
    EXPECT_TRUE(graph.isConnected(Connection{{redone->nodeID, 0}, {out, 0}}));
}

TEST(MixerSoloTest, MasterSpliceRoutesStripsToMixAndEverythingElseToDirect) {
    juce::AudioProcessorGraph graph;
    // Without a play config the graph's Audio Output node has no channels and refuses every wire.
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    AppUndoManager undo;
    synth::TimelineDoc doc;
    const auto out = addFactoryNode(graph, "Audio Output");
    const auto strip = addFactoryNode(graph, "Channel Strip");
    const auto osc = addFactoryNode(graph, "Oscillator");
    graph.addConnection({{strip, 0}, {out, 0}});
    graph.addConnection({{strip, kRight}, {out, 1}});
    graph.addConnection({{osc, 0}, {out, 0}});

    auto* master = synth::ensureMasterNode(graph, undo, doc, {});
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected(Connection{{strip, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_TRUE(graph.isConnected(Connection{{strip, kRight}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {master->nodeID, MasterModule::kDirectLeft}}));
}

TEST(MixerSoloTest, MasterIsASingleton) {
    juce::AudioProcessorGraph graph;
    // Without a play config the graph's Audio Output node has no channels and refuses every wire.
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    AppUndoManager undo;
    synth::TimelineDoc doc;
    addFactoryNode(graph, "Audio Output");

    auto* first = synth::ensureMasterNode(graph, undo, doc, {});
    ASSERT_NE(first, nullptr);
    auto* second = synth::ensureMasterNode(graph, undo, doc, {});
    EXPECT_EQ(first, second);

    ASSERT_TRUE(undo.undo());
    EXPECT_FALSE(undo.canUndo()) << "the second call pushed nothing";
}

TEST(MixerSoloTest, MasterGoesInFrontOfAnExistingRecTap) {
    // strips -> Master -> Rec Tap -> Audio Output, even when the Rec Tap was spliced first.
    juce::AudioProcessorGraph graph;
    // Without a play config the graph's Audio Output node has no channels and refuses every wire.
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    AppUndoManager undo;
    synth::TimelineDoc doc;
    const auto out = addFactoryNode(graph, "Audio Output");
    const auto tap = addFactoryNode(graph, "Rec Tap");
    const auto osc = addFactoryNode(graph, "Oscillator");
    graph.addConnection({{osc, 0}, {tap, 0}});
    graph.addConnection({{tap, 0}, {out, 0}});
    graph.addConnection({{tap, 1}, {out, 1}});

    auto* master = synth::ensureMasterNode(graph, undo, doc, {});
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected(Connection{{osc, 0}, {master->nodeID, MasterModule::kDirectLeft}}));
    EXPECT_TRUE(graph.isConnected(Connection{{master->nodeID, 0}, {tap, 0}}));
    EXPECT_TRUE(graph.isConnected(Connection{{master->nodeID, 1}, {tap, 1}}));
    EXPECT_TRUE(graph.isConnected(Connection{{tap, 0}, {out, 0}})) << "the tap's own output is untouched";
    EXPECT_FALSE(graph.isConnected(Connection{{master->nodeID, 0}, {out, 0}}));
}

TEST(MixerSoloTest, MasterSpliceNeedsAnOutput) {
    juce::AudioProcessorGraph graph;
    // Without a play config the graph's Audio Output node has no channels and refuses every wire.
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    AppUndoManager undo;
    synth::TimelineDoc doc;
    addFactoryNode(graph, "Oscillator");
    EXPECT_EQ(synth::ensureMasterNode(graph, undo, doc, {}), nullptr);
    EXPECT_FALSE(undo.canUndo());
}
