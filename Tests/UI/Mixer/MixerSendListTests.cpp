// MixerSendListTests.cpp -- FRO15 (P9-9, docs/mixer/sends-and-buses.md): the mixer column's send rows,
// driven through a real off-screen MainComponent (newPatchForTest() + simulateAddAudioTrackClick(),
// the same rig style as MixerPanelComponentTests.cpp) so the whole chain is exercised -- the Core
// flow, the undo transaction around it, the snapshot rebuild, and the row's own parameter
// attachment.
//
//   * add / remove   -- a row appears and the cable is really in the graph; removing clears both
//   * level knob     -- attached to the strip's own sendNLevel parameter, so moving it moves the
//                        parameter (and therefore host automation) with no lane plumbing
//   * PRE/POST       -- one undo step, restored by undo
//   * bus column     -- "+ Bus" on the dock adds a Kind::Bus column
//   * unbind         -- a document replacement tears the row attachments down FIRST, the same
//                        crash MixerPanelUndoUnbindTests.cpp pins for the fader and pan, plus a
//                        full undo drain as the crash pin

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Same minimal mock as every other headless MainComponent test in this suite -- a unique name to
// avoid an ODR clash across test translation units.
class MockProviderMSLT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMSLT"; }
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

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

/** One audio track (its own channel strip) plus one empty bus, both already showing as columns. */
struct SendRig {
    std::unique_ptr<MainComponent> mc;
    NodeID bus;

    SendRig() {
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderMSLT>());
        mc->setSize(1400, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();
        mc->simulateAddAudioTrackClick();
        bus = panel().createBus();
        panel().rebuild();
    }

    synth::ui::MixerPanelComponent& panel() { return mc->getBottomDock().getMixerPanel(); }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }

    /** The track's own strip column -- always the first, since buses are appended after every
     *  track-driven strip (docs/mixer/sends-and-buses.md D6). */
    synth::ui::MixerColumnComponent* sourceColumn() { return panel().getStripColumnForTest(0); }
};

} // namespace

TEST(MixerSendListTests, AddBusPutsABusColumnAfterTheTrackStrips) {
    SendRig rig;
    ASSERT_NE(rig.bus, NodeID{}) << "createBus must build a channel";
    EXPECT_TRUE(synth::isBusStrip(rig.graph(), rig.bus));

    auto* busColumn = rig.panel().getStripColumnForTest(1);
    ASSERT_NE(busColumn, nullptr) << "the bus gets a column of its own, after the track strip";
    EXPECT_EQ(busColumn->getNodeId(), rig.bus);
    EXPECT_EQ(rig.panel().getStripColumnForTest(2), nullptr);
}

TEST(MixerSendListTests, AddingASendWiresTheCableAndShowsARow) {
    SendRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    const auto sourceId = column->getNodeId();

    auto& sendList = column->getSendListForTest();
    EXPECT_TRUE(sendList.canAddSend());
    const auto targets = sendList.availableTargets();
    EXPECT_NE(std::find(targets.begin(), targets.end(), rig.bus), targets.end())
        << "the bus must be offered as a target";

    sendList.addSendTo(rig.bus);
    rig.panel().rebuild();

    auto* strip = stripAt(rig.graph(), sourceId);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->isSendActive(0));
    EXPECT_TRUE(rig.graph().isConnected({{sourceId, ChannelStripModule::sendLeftChannel(0)}, {rig.bus, 0}}));
    EXPECT_TRUE(rig.graph().isConnected(
        {{sourceId, ChannelStripModule::sendRightChannel(0)}, {rig.bus, ChannelStripModule::kRightBase}}));
    EXPECT_EQ(rig.sourceColumn()->getSendListForTest().getRowCountForTest(), 1);
}

TEST(MixerSendListTests, TheLevelKnobDrivesTheStripsOwnSendLevelParameter) {
    SendRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    const auto sourceId = column->getNodeId();
    column->getSendListForTest().addSendTo(rig.bus);
    rig.panel().rebuild();

    auto& sendList = rig.sourceColumn()->getSendListForTest();
    ASSERT_TRUE(sendList.isAttachedForTest(0)) << "the row must attach to the parameter, not shadow it";
    auto* knob = sendList.getKnobForTest(0);
    ASSERT_NE(knob, nullptr);

    auto* param = stripAt(rig.graph(), sourceId)->getSendLevelParameter(0);
    ASSERT_NE(param, nullptr);
    EXPECT_NEAR(param->get(), 0.0f, 1.0e-4f) << "a new send is unity, not silent";

    knob->setValue(-12.0, juce::sendNotificationSync);
    EXPECT_NEAR(param->get(), -12.0f, 0.05f) << "the knob writes the host-visible parameter directly";
}

TEST(MixerSendListTests, PreFaderToggleIsOneUndoStepAndUndoRestoresIt) {
    SendRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    column->getSendListForTest().addSendTo(rig.bus);
    rig.panel().rebuild();

    const auto sourceId = rig.sourceColumn()->getNodeId();
    ASSERT_FALSE(stripAt(rig.graph(), sourceId)->isSendPreFader(0));

    rig.sourceColumn()->getSendListForTest().togglePreFaderForRow(0);
    rig.panel().rebuild();
    EXPECT_TRUE(stripAt(rig.graph(), rig.sourceColumn()->getNodeId())->isSendPreFader(0));

    ASSERT_TRUE(rig.mc->getUndoManager().undo());
    rig.panel().rebuild();
    auto* afterUndo = rig.sourceColumn();
    ASSERT_NE(afterUndo, nullptr);
    EXPECT_FALSE(stripAt(rig.graph(), afterUndo->getNodeId())->isSendPreFader(0))
        << "the PRE/POST flip is one undo step, carried in the strip's trusted extra state";
    EXPECT_TRUE(stripAt(rig.graph(), afterUndo->getNodeId())->isSendActive(0)) << "and the send itself survives";
}

TEST(MixerSendListTests, RemovingASendClearsTheCableAndTheRow) {
    SendRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    column->getSendListForTest().addSendTo(rig.bus);
    rig.panel().rebuild();

    const auto sourceId = rig.sourceColumn()->getNodeId();
    rig.sourceColumn()->getSendListForTest().removeRow(0);
    rig.panel().rebuild();

    EXPECT_FALSE(stripAt(rig.graph(), rig.sourceColumn()->getNodeId())->isSendActive(0));
    EXPECT_FALSE(rig.graph().isConnected({{sourceId, ChannelStripModule::sendLeftChannel(0)}, {rig.bus, 0}}));
    EXPECT_EQ(rig.sourceColumn()->getSendListForTest().getRowCountForTest(), 0);
}

TEST(MixerSendListTests, SendRowsUnbindBeforeADocumentReplacementFreesTheirParameters) {
    // The same crash MixerPanelUndoUnbindTests.cpp pins for the fader/pan pair: a row holds a
    // SliderParameterAttachment onto the strip's sendNLevel, and anything that frees that strip
    // while the row still points at it is a use-after-free inside
    // AudioProcessorParameter::removeListener. New Patch is the cleanest way to prove the seam --
    // GraphEditor::newPatch()'s doClear() calls detachAllModuleComponents() directly, which is
    // MixerPanelComponent::unbindAllColumns() -> MixerColumnComponent::unbindFromGraph() ->
    // MixerSendList::unbindFromGraph(). The counter only moves for a row that really was attached,
    // so this asserts the hook fired and did work, not merely that nothing crashed.
    SendRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    column->getSendListForTest().addSendTo(rig.bus);
    rig.panel().rebuild();
    ASSERT_TRUE(rig.sourceColumn()->getSendListForTest().isAttachedForTest(0));

    const int liveUnbindsBefore = synth::ui::MixerSendList::getLiveUnbindCallCountForTest();
    rig.mc->newPatchForTest();

    EXPECT_GT(synth::ui::MixerSendList::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "New Patch must drop the send row's live sendNLevel attachment BEFORE it clears the graph";
    EXPECT_EQ(rig.panel().getStripColumnForTest(0), nullptr);
}

TEST(MixerSendListTests, AFullUndoDrainNeverTouchesAFreedSendParameter) {
    // Undoing the send itself does NOT go through that hook, and must not: AppUndoManager's
    // preRestore is deliberately LAZY (see its comment) -- a send is connections only, so the
    // node-preserving apply keeps the strip and its sendNLevel parameters alive and there is
    // nothing to detach from. The hazard is the steps AFTER it, which do remove nodes. A
    // regression here looks like a crash or a deadlock inside AudioProcessorParameter::
    // removeListener, not a failed EXPECT.
    SendRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    column->getSendListForTest().addSendTo(rig.bus);
    rig.panel().rebuild();
    ASSERT_TRUE(rig.sourceColumn()->getSendListForTest().isAttachedForTest(0));

    // Undo the send, then the bus, then the track: each later step is a graph-replacing restore
    // that frees whatever the mixer was last bound to.
    while (rig.mc->getUndoManager().canUndo())
        ASSERT_TRUE(rig.mc->getUndoManager().undo());

    rig.panel().rebuild();
    EXPECT_EQ(rig.panel().getStripColumnForTest(0), nullptr) << "every channel is gone and nothing dangled";
}
