// MixerSendListKeyTargetTests.cpp -- FRO318 (docs/mixer/sends-and-buses.md#sending-to-a-key-input): the
// send menu's "Key: ..." entries, driven through a real off-screen MainComponent with two audio
// tracks (each "Add audio track" builds its own Gate -> EQ -> Compressor -> strip channel).
//
//   * menu       -- "+ Send" (clicked through the real mouse path) lists, after the bus/strip
//                   targets, "Key: <the bass Compressor> on <bass channel>" and never the kick's own
//                   Compressor or Gate (keying those from the kick would be a render cycle)
//   * one step   -- choosing the item wires the Key cables as ONE undo step, and undo removes them
//   * row        -- the new row and its knob read "Send to Key: ..."

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/CompressorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Same minimal mock as every other headless MainComponent test in this suite -- a unique name to
// avoid an ODR clash across test translation units.
class MockProviderMSLKT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMSLKT"; }
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

struct MenuItem {
    juce::String text;
    std::function<void()> action;
};

/** Two audio tracks: column 0 is the kick, column 1 the bass. */
struct KeyMenuRig {
    std::unique_ptr<MainComponent> mc;

    KeyMenuRig() {
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderMSLKT>());
        mc->setSize(1400, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();
        mc->simulateAddAudioTrackClick();
        mc->simulateAddAudioTrackClick();
        panel().rebuild();
        panel().setSize(1400, 300);
        panel().resized();
    }

    synth::ui::MixerPanelComponent& panel() { return mc->getBottomDock().getMixerPanel(); }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    synth::ui::MixerColumnComponent* column(int index) { return panel().getStripColumnForTest(index); }

    /** The Compressor on the channel whose strip is `strip`. */
    NodeID compressorOn(NodeID strip) {
        for (auto* node : graph().getNodes())
            if (dynamic_cast<CompressorModule*>(node->getProcessor()) != nullptr &&
                synth::findKeyTargetChannel(graph(), node->nodeID) == strip)
                return node->nodeID;
        return {};
    }

    /** Clicks "+ Send" on the kick's list and returns the menu it built. */
    std::vector<MenuItem> openAddMenu() {
        auto& list = column(0)->getSendListForTest();
        std::vector<MenuItem> items;
        list.setShowMenuHookForTest([&](juce::PopupMenu& menu) {
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                if (!it.getItem().isSeparator)
                    items.push_back({it.getItem().text, it.getItem().action});
        });
        const juce::Point<float> pos(5.0f, 5.0f + (float)(list.getRowCountForTest() * 20));
        const auto now = juce::Time::getCurrentTime();
        const juce::ModifierKeys mods(juce::ModifierKeys::leftButtonModifier);
        list.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, &list, &list, now, pos, now, 1, false));
        list.setShowMenuHookForTest(nullptr);
        return items;
    }
};

} // namespace

TEST(MixerSendListKeyTargetTests, TheSendMenuOffersTheOtherChannelsKeyAndChoosingItIsOneUndoStep) {
    KeyMenuRig rig;
    ASSERT_NE(rig.column(0), nullptr);
    ASSERT_NE(rig.column(1), nullptr);
    const auto kick = rig.column(0)->getNodeId();
    const auto bass = rig.column(1)->getNodeId();
    const auto bassComp = rig.compressorOn(bass);
    const auto kickComp = rig.compressorOn(kick);
    ASSERT_NE(bassComp, NodeID{});
    ASSERT_NE(kickComp, NodeID{});

    auto& macros = rig.mc->getGraphEditor().getMacros();
    const auto bassChannel = synth::sendTargetName(rig.graph(), &macros, bass);
    const auto bassKeyLabel = synth::sendTargetName(rig.graph(), &macros, synth::SendTarget{bassComp, true});
    const auto kickKeyLabel = synth::sendTargetName(rig.graph(), &macros, synth::SendTarget{kickComp, true});
    EXPECT_EQ(bassChannel, "Audio 2") << "the key label names the track, not its inner channel macro";
    EXPECT_TRUE(bassKeyLabel.startsWith("Key: Compressor")) << bassKeyLabel;
    EXPECT_TRUE(bassKeyLabel.endsWith(" on " + bassChannel)) << bassKeyLabel;

    const auto items = rig.openAddMenu();
    ASSERT_FALSE(items.empty()) << "clicking + Send must build the menu";
    int bassKeyIndex = -1, firstKeyIndex = -1, lastStripIndex = -1;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (items[(size_t)i].text.startsWith("Key: ")) {
            firstKeyIndex = firstKeyIndex < 0 ? i : firstKeyIndex;
            EXPECT_NE(items[(size_t)i].text, kickKeyLabel) << "the kick's own Compressor would be a cycle";
        } else {
            lastStripIndex = i;
        }
        if (items[(size_t)i].text == bassKeyLabel)
            bassKeyIndex = i;
    }
    ASSERT_GE(bassKeyIndex, 0) << "the bass Compressor's Key is offered";
    EXPECT_GT(firstKeyIndex, lastStripIndex) << "Key entries follow every bus/strip target";

    const int serialBefore = rig.mc->getUndoManager().getEditSerial();
    items[(size_t)bassKeyIndex].action();
    rig.panel().rebuild();
    EXPECT_EQ(rig.mc->getUndoManager().getEditSerial(), serialBefore + 1) << "exactly one undo step";
    EXPECT_EQ(synth::resolveSendTarget(rig.graph(), kick, 0), (synth::SendTarget{bassComp, true}));
    EXPECT_TRUE(rig.graph().isConnected(
        {{kick, ChannelStripModule::sendLeftChannel(0)}, {bassComp, CompressorModule::kKeyBase}}));

    auto* knob = rig.column(0)->getSendListForTest().getKnobForTest(0);
    ASSERT_NE(knob, nullptr);
    EXPECT_TRUE(knob->getTitle().startsWith("Send to Key: Compressor")) << knob->getTitle();

    // Nothing later re-routes the cable (e.g. through a macro port on the bass channel's hull):
    // after the message loop runs, the send still resolves to the same Key.
    for (int i = 0; i < 5; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_EQ(synth::resolveSendTarget(rig.graph(), rig.column(0)->getNodeId(), 0),
              (synth::SendTarget{bassComp, true}));

    ASSERT_TRUE(rig.mc->getUndoManager().undo());
    rig.panel().rebuild();
    const auto kickAfterUndo = rig.column(0)->getNodeId();
    EXPECT_FALSE(synth::resolveSendTarget(rig.graph(), kickAfterUndo, 0).isValid()) << "undo removes the Key send";
    EXPECT_EQ(rig.column(0)->getSendListForTest().getRowCountForTest(), 0);
}
