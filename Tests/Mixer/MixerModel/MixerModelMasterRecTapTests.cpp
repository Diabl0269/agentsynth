// MixerModelMasterRecTapTests.cpp -- FRO148 (docs/mixer/mixer.md#master-inserts): the master Rec Tap is spliced in
// AFTER Master's insert chain when it is created later. ensureMasterRecordTap() re-routes whatever feeds Audio Output
// into the tap, so a Limiter already sitting between Master and Audio Output ends up ahead of the tap
// (Master -> Limiter -> Rec Tap -> Audio Output) and the Master column's list is still exactly [Limiter].
//
// Drives the real path -- a MainComponent, the mixer panel's own Master column adding the Limiter, then the transport
// bar's Record button (which is what calls ensureMasterRecordTap) -- because that function is private to
// MainComponent and the ordering it produces is the whole point.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "MixerModelTestFixture.h"
#include "Modules/RecordTapModule.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"

namespace {

class MockProviderMMRT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMMRT"; }
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

// Same shared-settings-file hygiene as RecordTapTests.cpp's RecordFlowTest: the MainComponent ctor reads the on-disk
// "Agent Synth" settings, so the keys the record flow depends on are pinned before AND after.
void pinRecordFlowKeys() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("librarySidebarVisible", "1");
        s->setValue("aiPanelVisible", "0");
        s->setValue("minimapVisible", "1");
        s->setValue("timelinePanelVisible", "0");
        s->setValue("timelineCountInBars", 0);
        s->saveIfNeeded();
    }
}

} // namespace

class MixerModelMasterRecTapTest : public ::testing::Test {
protected:
    void SetUp() override { pinRecordFlowKeys(); }
    void TearDown() override { pinRecordFlowKeys(); }
};

TEST_F(MixerModelMasterRecTapTest, RecTapCreatedAfterALimiterLandsAfterTheLimiterAndTheListIsStillJustTheLimiter) {
    MainComponent mc(std::make_unique<MockProviderMMRT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick(); // the first channel creates Master

    auto& graph = mc.getAudioEngine().getGraph();
    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* masterColumn = mixerPanel.getMasterColumnForTest();
    ASSERT_NE(masterColumn, nullptr);
    ASSERT_EQ(masterColumn->getInsertListForTest().getEntryCountForTest(), 0);

    // The Limiter goes in through the Master column's own insert list -- no Rec Tap exists yet.
    masterColumn->getInsertListForTest().addModule("Limiter");

    auto snapshotOf = [&] {
        return synth::buildMixerSnapshot(graph, mc.getTimelineDoc(), mc.getGraphEditor().getMacros());
    };
    {
        const auto snapshot = snapshotOf();
        const auto* column = findMasterColumnMMT(snapshot);
        ASSERT_NE(column, nullptr);
        ASSERT_EQ(column->inserts.size(), 1u);
        ASSERT_EQ(column->inserts[0].name, "Limiter");
        auto* endNode = graph.getNodeForId(column->chainEndNodeId);
        ASSERT_NE(endNode, nullptr);
        EXPECT_EQ(endNode->getProcessor()->getName(), "Audio Output") << "no Rec Tap yet: the chain ends at the output";
    }

    // Record-on is what creates the master tap (MainComponent::ensureMasterRecordTap).
    const auto& tracks = mc.getTimelineDoc().getTracks();
    ASSERT_FALSE(tracks.empty());
    ASSERT_TRUE(mc.getTimelineDoc().setTrackArmed(tracks.front().id, true));
    auto& bar = mc.getTimelinePanel().getTransportBar();
    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());

    RecordTapModule* tap = nullptr;
    juce::AudioProcessorGraph::NodeID tapId;
    for (auto* node : graph.getNodes())
        if (auto* t = dynamic_cast<RecordTapModule*>(node->getProcessor())) {
            tap = t;
            tapId = node->nodeID;
        }
    ASSERT_NE(tap, nullptr);

    const auto snapshot = snapshotOf();
    const auto* column = findMasterColumnMMT(snapshot);
    ASSERT_NE(column, nullptr);
    ASSERT_EQ(column->inserts.size(), 1u) << "the Rec Tap is the new terminator, never an insert";
    EXPECT_EQ(column->inserts[0].name, "Limiter");
    EXPECT_TRUE(column->insertChainIsLinear);
    EXPECT_EQ(column->chainEndNodeId, tapId) << "Master -> Limiter -> Rec Tap -> Audio Output";
    for (int channel = 0; channel < 2; ++channel)
        EXPECT_TRUE(graph.isConnected({{column->inserts[0].nodeId, channel}, {tapId, channel}}))
            << "the Limiter feeds the tap on ch" << channel;

    bar.getRecordButton().onClick(); // stop the (empty) take before teardown
}
