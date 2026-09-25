// MixerMasterColumnInsertTests.cpp -- FRO148 (docs/mixer/mixer.md#master-inserts): the Master column's insert list.
//
//   * list      -- MixerInsertList with source = Master / end = the chain terminator: add splices Master ->
//                  insert -> terminator on BOTH channels, a second add appends, move/remove work, undo restores;
//   * menu      -- the add menu offers Limiter and Gate (and every entry resolves through the module factory);
//   * layout    -- the real MixerPanelComponent's Master column shows its rows and lays out without overlap;
//   * meter     -- with no inserts the column reads Master's own latch, with >= 1 it reads the engine's post-graph
//                  output peak, and a Limiter's ceiling shows on the meter (the acceptance test).
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/MasterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerInsertList.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Mixer/MixerMeterScale.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <cmath>
#include <gtest/gtest.h>

#include "../../Mixer/MixerModel/MixerModelTestFixture.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// A stereo source writing one constant to both legs every block (same shape as NonFiniteOutputGuardTests.cpp's
// FixedSource, kept local -- that one is in a different translation unit's anonymous namespace).
class ConstantSourceMMCI : public juce::AudioProcessor {
public:
    explicit ConstantSourceMMCI(float value)
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

// A bare engine + editor + Master -> [Rec Tap ->] Audio Output, and a Master-configured MixerInsertList kept in step
// with the model the way MixerPanelComponent::rebuild() does (every mutation re-reads a fresh snapshot).
struct MasterListFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::TimelineDoc doc;
    synth::ui::MixerInsertList list;
    NodeID masterId, outputId, recTapId;
    int mutations = 0;

    explicit MasterListFixture(bool withRecTap = false) {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(900, 600);
        const auto rig = buildMasterRigMMT(graph, withRecTap);
        masterId = rig.master->nodeID;
        outputId = rig.output->nodeID;
        if (rig.recTap != nullptr)
            recTapId = rig.recTap->nodeID;
        list.configure(graph, undoManager, editor.getMacros(), editor);
        list.onMutated = [this] {
            ++mutations;
            refresh();
        };
        refresh();
    }

    synth::MixerSnapshot snapshot() { return synth::buildMixerSnapshot(engine.getGraph(), doc, editor.getMacros()); }

    // Re-feeds the list from a fresh snapshot, and returns the Master column's insert names in order.
    juce::StringArray refresh() {
        const auto snap = snapshot();
        const auto* column = findMasterColumnMMT(snap);
        juce::StringArray names;
        if (column == nullptr)
            return names;
        list.setEntries(column->inserts, column->insertChainIsLinear, column->editOnCanvasTargetUuid,
                        column->sourceNodeId, column->chainEndNodeId);
        for (const auto& entry : column->inserts)
            names.add(entry.name);
        return names;
    }

    juce::StringArray names() { return refresh(); }

    // True when every stereo leg (left ch0, right = each module's own right leg) runs from `from` to `to`.
    bool wired(NodeID from, NodeID to) {
        auto& graph = engine.getGraph();
        auto rightLegOf = [&](NodeID id) {
            auto* node = graph.getNodeForId(id);
            auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
            // Master (no dual-I/O parameter) reports -1 for its right leg; its right jack is ch1 like every stereo bus.
            const int right = module != nullptr ? module->rightAudioLegChannel() : 1;
            return right >= 0 ? right : 1;
        };
        return graph.isConnected({{from, 0}, {to, 0}}) &&
               graph.isConnected({{from, rightLegOf(from)}, {to, rightLegOf(to)}});
    }

    NodeID insertIdAt(int index) {
        const auto snap = snapshot();
        const auto* column = findMasterColumnMMT(snap);
        return column != nullptr && index < (int)column->inserts.size() ? column->inserts[(size_t)index].nodeId
                                                                        : NodeID{};
    }
};

class MockProviderMMCI : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMMCI"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

// ---- list ------------------------------------------------------------------------------------------------------

TEST(MixerMasterColumnInsertTests, AddingALimiterSplicesMasterToLimiterToOutputOnBothChannels) {
    MasterListFixture f;
    ASSERT_TRUE(f.wired(f.masterId, f.outputId)) << "precondition: Master feeds Audio Output directly";

    f.list.addModule("Limiter");

    EXPECT_EQ(f.mutations, 1);
    ASSERT_EQ(f.names().size(), 1);
    EXPECT_EQ(f.names()[0], "Limiter");
    const auto limiter = f.insertIdAt(0);
    ASSERT_NE(limiter, NodeID{});
    EXPECT_TRUE(f.wired(f.masterId, limiter)) << "Master -> Limiter on ch0 AND the right leg";
    EXPECT_TRUE(f.wired(limiter, f.outputId)) << "Limiter -> Audio Output on ch0 AND the right leg";
    EXPECT_FALSE(f.engine.getGraph().isConnected({{f.masterId, 0}, {f.outputId, 0}}))
        << "the direct Master -> Output edge is gone on ch0";
    EXPECT_FALSE(f.engine.getGraph().isConnected({{f.masterId, 1}, {f.outputId, 1}}))
        << "the direct Master -> Output edge is gone on ch1";
}

TEST(MixerMasterColumnInsertTests, AddingWithARecTapPutsTheInsertAheadOfTheTap) {
    MasterListFixture f(/*withRecTap=*/true);
    f.list.addModule("Limiter");

    ASSERT_EQ(f.names().size(), 1);
    const auto limiter = f.insertIdAt(0);
    EXPECT_TRUE(f.wired(f.masterId, limiter));
    EXPECT_TRUE(f.wired(limiter, f.recTapId)) << "the chain terminator is the Rec Tap, not Audio Output";
    EXPECT_TRUE(f.wired(f.recTapId, f.outputId)) << "the tap -> output leg is untouched";
}

TEST(MixerMasterColumnInsertTests, ASecondAddAppendsAfterTheFirst) {
    MasterListFixture f;
    f.list.addModule("Compressor");
    f.list.addModule("Limiter");

    const auto names = f.names();
    ASSERT_EQ(names.size(), 2);
    EXPECT_EQ(names[0], "Compressor");
    EXPECT_EQ(names[1], "Limiter") << "a new Master insert lands last, right before the terminator";
    EXPECT_TRUE(f.wired(f.masterId, f.insertIdAt(0)));
    EXPECT_TRUE(f.wired(f.insertIdAt(0), f.insertIdAt(1)));
    EXPECT_TRUE(f.wired(f.insertIdAt(1), f.outputId));
}

TEST(MixerMasterColumnInsertTests, MoveRowReordersTheChainAndRemoveRowRestoresTheDirectEdge) {
    MasterListFixture f;
    f.list.addModule("Compressor");
    f.list.addModule("Limiter");
    ASSERT_EQ(f.names().size(), 2);

    f.list.moveRow(0, 1); // Compressor down -> [Limiter, Compressor]
    auto names = f.names();
    ASSERT_EQ(names.size(), 2);
    EXPECT_EQ(names[0], "Limiter");
    EXPECT_EQ(names[1], "Compressor");
    EXPECT_TRUE(f.wired(f.masterId, f.insertIdAt(0)));
    EXPECT_TRUE(f.wired(f.insertIdAt(0), f.insertIdAt(1)));
    EXPECT_TRUE(f.wired(f.insertIdAt(1), f.outputId));

    f.list.removeRow(0); // remove the Limiter
    names = f.names();
    ASSERT_EQ(names.size(), 1);
    EXPECT_EQ(names[0], "Compressor");
    EXPECT_TRUE(f.wired(f.masterId, f.insertIdAt(0)));
    EXPECT_TRUE(f.wired(f.insertIdAt(0), f.outputId));

    f.list.removeRow(0);
    EXPECT_TRUE(f.names().isEmpty());
    EXPECT_TRUE(f.wired(f.masterId, f.outputId)) << "removing the last insert bridges Master straight to the output";
}

TEST(MixerMasterColumnInsertTests, UndoRestoresTheChainAfterAddMoveAndRemove) {
    MasterListFixture f;
    f.list.addModule("Compressor");
    f.list.addModule("Limiter");
    f.list.moveRow(0, 1);
    f.list.removeRow(0);
    ASSERT_EQ(f.names().size(), 1);

    ASSERT_TRUE(f.undoManager.undo()); // the remove
    auto names = f.names();
    ASSERT_EQ(names.size(), 2);
    EXPECT_EQ(names[0], "Limiter");
    EXPECT_EQ(names[1], "Compressor");

    ASSERT_TRUE(f.undoManager.undo()); // the move
    names = f.names();
    ASSERT_EQ(names.size(), 2);
    EXPECT_EQ(names[0], "Compressor");
    EXPECT_EQ(names[1], "Limiter");

    ASSERT_TRUE(f.undoManager.undo()); // the second add
    ASSERT_TRUE(f.undoManager.undo()); // the first add
    EXPECT_TRUE(f.names().isEmpty());
    const auto snap = f.snapshot();
    const auto* column = findMasterColumnMMT(snap);
    ASSERT_NE(column, nullptr);
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_TRUE(f.wired(column->nodeId, column->chainEndNodeId)) << "Master feeds the terminator directly again";
}

TEST(MixerMasterColumnInsertTests, AddIsANoOpWhenThereIsNoTerminatorToSpliceInFrontOf) {
    MasterListFixture f;
    for (int channel = 0; channel < 2; ++channel)
        f.engine.getGraph().removeConnection({{f.masterId, channel}, {f.outputId, channel}});
    f.refresh();
    const int nodesBefore = f.engine.getGraph().getNumNodes();

    f.list.addModule("Limiter");

    EXPECT_EQ(f.mutations, 0);
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodesBefore) << "nothing was added to the graph";
}

// ---- menu ------------------------------------------------------------------------------------------------------

TEST(MixerMasterColumnInsertTests, AddMenuOffersLimiterAndGateAndEveryEntryCreatesAModule) {
    const auto& types = synth::ui::MixerInsertList::getAddableModuleTypes();
    EXPECT_TRUE(types.contains("Limiter"));
    EXPECT_TRUE(types.contains("Gate"));
    for (const auto& name : types)
        EXPECT_NE(synth::AIStateMapper::createModule(name), nullptr) << name << " must resolve through the factory";
}

TEST(MixerMasterColumnInsertTests, GateSplicesIntoTheMasterChainLikeAnyOtherInsert) {
    MasterListFixture f;
    f.list.addModule("Gate");
    ASSERT_EQ(f.names().size(), 1);
    EXPECT_EQ(f.names()[0], "Gate");
    EXPECT_TRUE(f.wired(f.masterId, f.insertIdAt(0)));
    EXPECT_TRUE(f.wired(f.insertIdAt(0), f.outputId));
}

// ---- layout ----------------------------------------------------------------------------------------------------

TEST(MixerMasterColumnInsertTests, MasterColumnInThePanelShowsItsInsertRowsWithoutOverlap) {
    MainComponent mc(std::make_unique<MockProviderMMCI>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick(); // the first channel creates Master

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* master = mixerPanel.getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    ASSERT_EQ(master->getInsertListForTest().getEntryCountForTest(), 0);
    ASSERT_TRUE(master->getInsertListForTest().isLinearForTest());

    // The real gesture path: the list's own addModule -> onMutated -> panel rebuild.
    master->getInsertListForTest().addModule("Limiter");
    master = mixerPanel.getMasterColumnForTest();
    ASSERT_EQ(master->getInsertListForTest().getEntryCountForTest(), 1) << "the rebuild fed the new row in";
    master->getInsertListForTest().addModule("Compressor");
    master = mixerPanel.getMasterColumnForTest();
    ASSERT_EQ(master->getInsertListForTest().getEntryCountForTest(), 2);

    master->setSize(140, 320);
    auto& list = master->getInsertListForTest();
    auto& meter = master->getMeterForTest();
    auto& readout = master->getMeterReadoutForTest();
    auto& faderSlider = master->getAccessibilityFocusTargetForTest();
    const auto faderBounds = master->getLocalArea(&faderSlider, faderSlider.getLocalBounds());

    EXPECT_GE(list.getY(), 2 + 24) << "the list sits below the 24 px header";
    EXPECT_EQ(list.getHeight(), list.getPreferredHeight()) << "both rows fit at this height";
    EXPECT_LE(list.getBottom(), readout.getY()) << "no overlap with the readout row";
    EXPECT_LE(list.getBottom(), meter.getY());
    EXPECT_LE(list.getBottom(), faderBounds.getY());
    EXPECT_LE(readout.getBottom(), meter.getY());
    EXPECT_GT(meter.getHeight(), 0);
    EXPECT_GT(faderBounds.getHeight(), 0) << "the fader keeps real height under the list";
    EXPECT_TRUE(master->getLocalBounds().contains(list.getBounds()));
}

TEST(MixerMasterColumnInsertTests, MasterColumnListShrinksToAThirdOfATightColumnInsteadOfCrowdingTheFader) {
    MainComponent mc(std::make_unique<MockProviderMMCI>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* master = mixerPanel.getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    for (const char* name : {"Limiter", "Compressor", "Gate", "Parametric EQ"}) {
        master->getInsertListForTest().addModule(name);
        master = mixerPanel.getMasterColumnForTest();
    }
    ASSERT_EQ(master->getInsertListForTest().getEntryCountForTest(), 4);

    master->setSize(140, 181); // the dock's real Mixer-tab column height
    auto& list = master->getInsertListForTest();
    auto& faderSlider = master->getAccessibilityFocusTargetForTest();
    const auto faderBounds = master->getLocalArea(&faderSlider, faderSlider.getLocalBounds());
    EXPECT_LE(list.getHeight(), (181 - 4 - 24) / 3) << "capped at a third of what's under the header";
    EXPECT_LE(list.getBottom(), faderBounds.getY());
    EXPECT_GT(faderBounds.getHeight(), 0);
}

// ---- meter source ----------------------------------------------------------------------------------------------

namespace {

struct MasterMeterFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::TimelineDoc doc;
    synth::ui::MixerMasterColumn column;
    NodeID masterId;
    int providerCalls = 0;
    float providerValue = 0.0f;

    MasterMeterFixture() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(900, 600);
        const auto rig = buildMasterRigMMT(graph, false);
        masterId = rig.master->nodeID;
        column.configure(graph, undoManager, editor.getMacros(), editor);
        column.onMutated = [this] { bind(); };
        column.setSize(140, 300);
        column.outputPeakProvider = [this](int) {
            ++providerCalls;
            return providerValue;
        };
        bind();
    }

    void bind() {
        const auto snap = synth::buildMixerSnapshot(engine.getGraph(), doc, editor.getMacros());
        if (const auto* master = findMasterColumnMMT(snap))
            column.setColumn(*master);
    }

    MasterModule* master() {
        auto* node = engine.getGraph().getNodeForId(masterId);
        return node != nullptr ? dynamic_cast<MasterModule*>(node->getProcessor()) : nullptr;
    }

    // Runs one 0.5-peak block through Master so its own (pre-insert) latch holds 0.5.
    void latchMasterPeak(float level) {
        auto* module = master();
        module->prepareToPlay(44100.0, 512);
        juce::AudioBuffer<float> buffer(MasterModule::kNumInputs, 512);
        buffer.clear();
        for (int ch = MasterModule::kMixLeft; ch <= MasterModule::kMixRight; ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), level, 512);
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);
    }
};

} // namespace

TEST(MixerMasterColumnInsertTests, WithNoInsertsTheMeterStillReadsMastersOwnLatch) {
    MasterMeterFixture f;
    f.providerValue = 0.01f; // would read about -40 dB if the column wrongly used the output tap
    f.latchMasterPeak(0.5f);

    f.column.refreshMeter(0.1f);

    EXPECT_EQ(f.providerCalls, 0) << "the engine output tap is not consulted with an empty chain";
    EXPECT_NEAR(f.column.getMeterForTest().getDisplayedDbForTest(0), synth::ui::meterLinearToDb(0.5f), 0.05f);
    EXPECT_NEAR(f.column.getMeterForTest().getDisplayedDbForTest(1), synth::ui::meterLinearToDb(0.5f), 0.05f);
}

TEST(MixerMasterColumnInsertTests, WithAnInsertTheMeterReadsTheEngineOutputTapInstead) {
    MasterMeterFixture f;
    f.column.getInsertListForTest().addModule("Limiter"); // onMutated -> bind() feeds the row in
    ASSERT_EQ(f.column.getInsertListForTest().getEntryCountForTest(), 1);
    f.providerValue = 0.125f; // post-insert level: about -18 dB
    f.latchMasterPeak(0.5f);  // pre-insert level: about -6 dB

    f.column.refreshMeter(0.1f);

    EXPECT_GT(f.providerCalls, 0);
    EXPECT_NEAR(f.column.getMeterForTest().getDisplayedDbForTest(0), synth::ui::meterLinearToDb(0.125f), 0.05f)
        << "the meter shows what LEAVES the chain, not Master's pre-insert level";
    EXPECT_LT(f.column.getMeterForTest().getDisplayedDbForTest(0), synth::ui::meterLinearToDb(0.5f) - 6.0f);
}

// The engine latch keeps the loudest peak since its last read and is only read once the chain has inserts, so the first
// tick after the first insert would otherwise report everything the session ever played as a clip.
TEST(MixerMasterColumnInsertTests, SwitchingToTheOutputTapDropsThePeakItAccumulatedWhileUnread) {
    MasterMeterFixture f;
    f.providerValue = 1.5f; // +3.5 dB "since the session began"
    f.column.getInsertListForTest().addModule("Limiter");
    f.providerValue = 0.0f; // the real level now that the chain is live

    f.column.refreshMeter(0.1f);

    EXPECT_LE(f.column.getMeterForTest().getDisplayedDbForTest(0), -59.0f);
    EXPECT_LE(f.column.getMeterForTest().getDisplayedDbForTest(1), -59.0f);
}

TEST(MixerMasterColumnInsertTests, WithAnInsertButNoOutputProviderTheColumnFallsBackToMastersLatch) {
    MasterMeterFixture f;
    f.column.getInsertListForTest().addModule("Limiter");
    f.column.outputPeakProvider = nullptr;
    f.latchMasterPeak(0.5f);

    f.column.refreshMeter(0.1f);

    EXPECT_NEAR(f.column.getMeterForTest().getDisplayedDbForTest(0), synth::ui::meterLinearToDb(0.5f), 0.05f);
}

// THE ACCEPTANCE TEST. A Limiter added through the Master column's own list, a hot constant source (+6 dBFS) straight
// into Master, real render blocks through a hosted engine: the level the column meter shows is what left the graph --
// under full scale, the limiter's own doing -- while Master's pre-insert latch, the reading the column used before this
// change, still sees the +6 dB the limiter took off.
//
// NOTE the module's own contract (LimiterModule.h): its "threshold" carries automatic makeup gain, so it is a
// loudness maximiser rather than a passive ceiling -- a -6 dB threshold does NOT hold the output at -6 dBFS (measured:
// it lands at ~0 dBFS). The default -1 dB threshold measures about -1.2 dBFS here, which is what is asserted.
TEST(MixerMasterColumnInsertTests, MeterShowsThePostLimiterLevelNotMastersPreInsertLevel) {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlock = 512;
    constexpr float kSourceLevel = 2.0f; // +6 dBFS: over full scale, so the limiter has real work to do

    MasterMeterFixture f;
    auto& graph = f.engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlock);
    const auto source = graph.addNode(std::make_unique<ConstantSourceMMCI>(kSourceLevel))->nodeID;
    graph.addConnection({{source, 0}, {f.masterId, MasterModule::kMixLeft}});
    graph.addConnection({{source, 1}, {f.masterId, MasterModule::kMixRight}});

    f.column.getInsertListForTest().addModule("Limiter"); // onMutated -> bind() feeds the row in
    ASSERT_EQ(f.column.getInsertListForTest().getEntryCountForTest(), 1);
    f.column.outputPeakProvider = [&f](int leg) {
        return f.engine.takeOutputMeterPeak(synth::MeterReader::Mixer, leg);
    };

    f.engine.prepareForHost(kSampleRate, kBlock, 2, 2);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    auto renderBlocks = [&](int count) {
        for (int i = 0; i < count; ++i) {
            buffer.clear();
            midi.clear();
            f.engine.processHostBlock(buffer, midi);
        }
    };

    renderBlocks(100); // let the limiter's envelope settle
    // Drop whatever latched while settling (each latch is consume-on-read), then measure a clean window.
    f.engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 0);
    f.engine.takeOutputMeterPeak(synth::MeterReader::Mixer, 1);
    f.master()->takeMeterPeak(synth::MeterReader::Mixer, 0);
    renderBlocks(20);
    const float preInsertPeak = f.master()->takeMeterPeak(synth::MeterReader::Mixer, 0);

    // The fixture's meter starts at its floor and attacks instantly, so one tick shows this window's peak.
    f.column.refreshMeter(0.1f);

    const float displayedDb = f.column.getMeterForTest().getDisplayedDbForTest(0);
    const float preInsertDb = synth::ui::meterLinearToDb(preInsertPeak);
    EXPECT_GT(preInsertDb, 5.0f) << "Master's own latch sees the hot pre-insert signal";
    EXPECT_LE(displayedDb, -0.5f) << "the column meter shows the limited, post-insert level";
    EXPECT_LT(displayedDb, preInsertDb - 6.0f);
    f.engine.releaseFromHost();
}
