// MixerSendListMacroPortTests.cpp (docs/macros/auto-ports.md#programmatic-connections): a mixer
// send between two channels whose strips sit inside their own channel macros enters and leaves
// those macros through macro ports, exactly like a hand-drawn cable, when the auto-create-ports
// preference is on (the default) -- and wires straight through when it is off.
//
// Driven through a real off-screen MainComponent: each "Add audio track" builds a channel whose
// strip is a member of that track's channel macro. The send is added by clicking "+ Send" through
// the real mouse path and choosing the target's menu item.
//
//   * pref on    -- one outlet on the source macro, one Stereo inlet on the target macro, wired
//                   strip -> outlet -> inlet -> strip; the mixer still names the target; one undo
//                   step, and undo removes the send and both ports
//   * pref off   -- the send is the two direct cables it always was, and no port appears
//   * remove     -- removing the send takes both ports with it; undo brings all of it back
//   * retarget   -- the old target's ports go, the new target gets its own inlet
//   * reorder    -- moving a row never mints a port

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Same minimal mock as every other headless MainComponent test in this suite -- a unique name to
// avoid an ODR clash across test translation units.
class MockProviderMSLMP : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMSLMP"; }
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

struct PortCounts {
    int inlets = 0;
    int outlets = 0;
};

/** `trackCount` audio tracks; column 0 is always the send's source. */
struct MacroSendRig {
    std::unique_ptr<MainComponent> mc;

    explicit MacroSendRig(int trackCount = 2) {
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderMSLMP>());
        mc->setSize(1400, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();
        for (int i = 0; i < trackCount; ++i)
            mc->simulateAddAudioTrackClick();
        refresh();
    }

    void refresh() {
        panel().rebuild();
        panel().setSize(1400, 300);
        panel().resized();
    }

    synth::ui::MixerPanelComponent& panel() { return mc->getBottomDock().getMixerPanel(); }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    GraphEditor& editor() { return mc->getGraphEditor(); }
    synth::ui::MixerColumnComponent* column(int index) { return panel().getStripColumnForTest(index); }
    NodeID strip(int index) { return column(index) != nullptr ? column(index)->getNodeId() : NodeID{}; }
    synth::ui::MixerSendList& sends() { return column(0)->getSendListForTest(); }

    const synth::Macro* macroOf(NodeID id) {
        auto* node = graph().getNodeForId(id);
        return node != nullptr ? editor().getMacros().findByMember(node->properties["uuid"].toString()) : nullptr;
    }

    PortCounts portsOn(NodeID member) {
        PortCounts counts;
        if (const auto* macro = macroOf(member))
            for (const auto& port : macro->ports)
                (port.isInput ? counts.inlets : counts.outlets)++;
        return counts;
    }

    /** Clicks "+ Send" through the real mouse path and picks the item naming strip `target`. */
    bool addSendThroughMenu(NodeID target) {
        const auto label = synth::sendTargetName(graph(), &editor().getMacros(), target);
        auto& list = sends();
        std::function<void()> chosen;
        list.setShowMenuHookForTest([&](juce::PopupMenu& menu) {
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                if (!it.getItem().isSeparator && it.getItem().text == label && !chosen)
                    chosen = it.getItem().action;
        });
        const juce::Point<float> pos(5.0f, 5.0f + (float)(list.getRowCountForTest() * 20));
        const auto now = juce::Time::getCurrentTime();
        const juce::ModifierKeys mods(juce::ModifierKeys::leftButtonModifier);
        list.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, &list, &list, now, pos, now, 1, false));
        list.setShowMenuHookForTest(nullptr);
        if (!chosen)
            return false;
        chosen();
        refresh();
        return true;
    }

    template <typename Module>
    bool isA(NodeID id) {
        auto* node = graph().getNodeForId(id);
        return node != nullptr && dynamic_cast<Module*>(node->getProcessor()) != nullptr;
    }

    /** The first cable leaving (`from`, `channel`), or an invalid destination. */
    juce::AudioProcessorGraph::NodeAndChannel next(NodeID from, int channel) {
        for (const auto& c : graph().getConnections())
            if (c.source.nodeID == from && c.source.channelIndex == channel)
                return c.destination;
        return {};
    }
};

} // namespace

TEST(MixerSendListMacroPortTests, ASendIntoAChannelMacroEntersThroughAPortAndUndoRemovesIt) {
    MacroSendRig rig;
    ASSERT_TRUE(rig.editor().getAutoCreateMacroPortsOnDragEnabled()) << "the preference defaults on";
    const auto kick = rig.strip(0);
    const auto bass = rig.strip(1);
    ASSERT_NE(rig.macroOf(kick), nullptr) << "a track's strip sits inside its channel macro";
    ASSERT_NE(rig.macroOf(bass), nullptr);
    ASSERT_EQ(rig.portsOn(bass).inlets, 0);

    const int serialBefore = rig.mc->getUndoManager().getEditSerial();
    ASSERT_TRUE(rig.addSendThroughMenu(bass));
    EXPECT_EQ(rig.mc->getUndoManager().getEditSerial(), serialBefore + 1) << "send + ports are one undo step";

    EXPECT_EQ(rig.portsOn(kick).outlets, 1) << "the send leaves the kick's macro through an outlet";
    EXPECT_EQ(rig.portsOn(bass).inlets, 1) << "and enters the bass's macro through ONE inlet for both legs";

    // Left leg: kick send L -> Macro Out -> Macro In -> bass In L. Right leg lands on bass kRightBase.
    const auto outL = rig.next(kick, ChannelStripModule::sendLeftChannel(0));
    ASSERT_TRUE(rig.isA<MacroOutletModule>(outL.nodeID));
    const auto inL = rig.next(outL.nodeID, outL.channelIndex);
    ASSERT_TRUE(rig.isA<MacroInletModule>(inL.nodeID));
    EXPECT_EQ(rig.next(inL.nodeID, inL.channelIndex), (juce::AudioProcessorGraph::NodeAndChannel{bass, 0}));
    const auto outR = rig.next(kick, ChannelStripModule::sendRightChannel(0));
    EXPECT_EQ(outR.nodeID, outL.nodeID) << "both legs share the one outlet";
    const auto inR = rig.next(outR.nodeID, outR.channelIndex);
    EXPECT_EQ(inR.nodeID, inL.nodeID) << "both legs share the one inlet";
    auto* inlet = dynamic_cast<MacroInletModule*>(rig.graph().getNodeForId(inL.nodeID)->getProcessor());
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Stereo) << "the strip's split L/R jacks get one two-jack port";
    EXPECT_EQ(rig.next(inR.nodeID, inR.channelIndex),
              (juce::AudioProcessorGraph::NodeAndChannel{bass, ChannelStripModule::kRightBase}));

    EXPECT_EQ(synth::findSendTarget(rig.graph(), kick, 0), bass) << "the mixer still sees the bus behind the ports";
    auto* knob = rig.sends().getKnobForTest(0);
    ASSERT_NE(knob, nullptr);
    EXPECT_EQ(knob->getTitle(), "Send to " + synth::sendTargetName(rig.graph(), &rig.editor().getMacros(), bass));

    ASSERT_TRUE(rig.mc->getUndoManager().undo());
    rig.refresh();
    EXPECT_EQ(rig.sends().getRowCountForTest(), 0) << "undo removes the send";
    EXPECT_EQ(rig.portsOn(rig.strip(0)).outlets, 0) << "and the outlet it created";
    EXPECT_EQ(rig.portsOn(rig.strip(1)).inlets, 0) << "and the inlet it created";
}

TEST(MixerSendListMacroPortTests, WithThePreferenceOffASendWiresStraightIntoTheStrip) {
    MacroSendRig rig;
    rig.editor().setAutoCreateMacroPortsOnDragEnabled(false);
    const auto kick = rig.strip(0);
    const auto bass = rig.strip(1);

    ASSERT_TRUE(rig.addSendThroughMenu(bass));
    EXPECT_TRUE(rig.graph().isConnected({{kick, ChannelStripModule::sendLeftChannel(0)}, {bass, 0}}));
    EXPECT_TRUE(rig.graph().isConnected(
        {{kick, ChannelStripModule::sendRightChannel(0)}, {bass, ChannelStripModule::kRightBase}}));
    EXPECT_EQ(rig.portsOn(kick).outlets, 0);
    EXPECT_EQ(rig.portsOn(bass).inlets, 0);
}

TEST(MixerSendListMacroPortTests, RemovingASendRemovesThePortsItMadeAndUndoRestoresThem) {
    MacroSendRig rig;
    ASSERT_TRUE(rig.addSendThroughMenu(rig.strip(1)));
    ASSERT_EQ(rig.portsOn(rig.strip(1)).inlets, 1);

    const int serialBefore = rig.mc->getUndoManager().getEditSerial();
    rig.sends().removeRow(0);
    rig.refresh();
    EXPECT_EQ(rig.mc->getUndoManager().getEditSerial(), serialBefore + 1);
    EXPECT_EQ(rig.sends().getRowCountForTest(), 0);
    EXPECT_EQ(rig.portsOn(rig.strip(0)).outlets, 0) << "nothing else used the outlet";
    EXPECT_EQ(rig.portsOn(rig.strip(1)).inlets, 0) << "nothing else used the inlet";

    ASSERT_TRUE(rig.mc->getUndoManager().undo());
    rig.refresh();
    EXPECT_EQ(rig.sends().getRowCountForTest(), 1);
    EXPECT_EQ(rig.portsOn(rig.strip(0)).outlets, 1);
    EXPECT_EQ(rig.portsOn(rig.strip(1)).inlets, 1);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), rig.strip(0), 0), rig.strip(1));
}

TEST(MixerSendListMacroPortTests, RetargetingASendMovesItsInletToTheNewTarget) {
    MacroSendRig rig(3);
    ASSERT_TRUE(rig.addSendThroughMenu(rig.strip(1)));

    rig.sends().retargetRow(0, rig.strip(2));
    rig.refresh();
    EXPECT_EQ(synth::findSendTarget(rig.graph(), rig.strip(0), 0), rig.strip(2));
    EXPECT_EQ(rig.portsOn(rig.strip(1)).inlets, 0) << "the old target's inlet carried only this send";
    EXPECT_EQ(rig.portsOn(rig.strip(2)).inlets, 1) << "the new target gets its own inlet";
    EXPECT_EQ(rig.portsOn(rig.strip(0)).outlets, 1) << "the source keeps exactly one outlet for the send";
}

TEST(MixerSendListMacroPortTests, ReorderingSendsNeverMintsAPort) {
    MacroSendRig rig(3);
    ASSERT_TRUE(rig.addSendThroughMenu(rig.strip(1)));
    ASSERT_TRUE(rig.addSendThroughMenu(rig.strip(2)));
    ASSERT_EQ(rig.portsOn(rig.strip(0)).outlets, 2);

    rig.sends().moveRow(0, 1);
    rig.refresh();
    EXPECT_EQ(synth::findSendTarget(rig.graph(), rig.strip(0), 0), rig.strip(2));
    EXPECT_EQ(synth::findSendTarget(rig.graph(), rig.strip(0), 1), rig.strip(1));
    EXPECT_EQ(rig.portsOn(rig.strip(0)).outlets, 2);
    EXPECT_EQ(rig.portsOn(rig.strip(1)).inlets, 1);
    EXPECT_EQ(rig.portsOn(rig.strip(2)).inlets, 1);
}
