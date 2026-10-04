// Concern: "Show module" end to end on a real MainComponent -- Ctrl+E through a track header's keyPressed (and the
// header's button) selects the track's module on the canvas, and for a hosted plugin opens its editor window and
// closes it on the next press. The plugin is the stub backend's, so the window is the real (headless) one.
#include "../../StubPluginInstance.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

#include <chrono>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

const juce::KeyPress kCtrlE('e', juce::ModifierKeys::ctrlModifier, 0);

NodeID nodeWithUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (uuid.isNotEmpty() && node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

} // namespace

class ShowTrackModuleTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
    }
    void TearDown() override {
        mc.reset();
        MainComponentTest::TearDown();
    }

    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    synth::TimelineDoc& doc() { return mc->getTimelineDoc(); }

    synth::TrackId addTrack(int menuId) {
        mc->getTimelinePanel().applyAddTrackMenuChoice(menuId);
        return doc().getTracks().back().id;
    }

    synth::ui::TimelineTrackHeaderComponent& headerOf(synth::TrackId id) {
        for (int i = 0; i < static_cast<int>(doc().getTracks().size()); ++i)
            if (auto* header = mc->getTimelinePanel().getTrackHeaderAt(i);
                header != nullptr && header->getTrackId() == id)
                return *header;
        ADD_FAILURE() << "no header for the track";
        return *mc->getTimelinePanel().getTrackHeaderAt(0);
    }

    std::vector<NodeID> selected() { return mc->getGraphEditor().getSelectedNodes(); }

    std::unique_ptr<MainComponent> mc;
};

TEST_F(ShowTrackModuleTest, CtrlEOnAnInstrumentTrackSelectsItsInstrumentNotItsTrackIn) {
    const auto id = addTrack(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    const auto trackIn = nodeWithUuid(graph(), doc().getTrack(id)->bindingUuid);
    ASSERT_NE(trackIn.uid, 0u);

    ASSERT_TRUE(headerOf(id).keyPressed(kCtrlE));

    const auto sel = selected();
    ASSERT_EQ(sel.size(), 1u);
    EXPECT_NE(sel[0], trackIn);
    EXPECT_NE(dynamic_cast<OscillatorModule*>(graph().getNodeForId(sel[0])->getProcessor()), nullptr);
    EXPECT_EQ(mc->getPluginWindowManager().getOpenWindowCountForTest(), 0) << "a built-in card has no window";
}

TEST_F(ShowTrackModuleTest, TheButtonDoesWhatTheKeyDoes) {
    const auto id = addTrack(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);

    headerOf(id).getShowModuleButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // triggerClick posts its click

    ASSERT_EQ(selected().size(), 1u);
    EXPECT_NE(dynamic_cast<OscillatorModule*>(graph().getNodeForId(selected()[0])->getProcessor()), nullptr);
}

TEST_F(ShowTrackModuleTest, CtrlEOnAnInstrumentTrackCentresItsCollapsedMacroCard) {
    const auto id = addTrack(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    auto& editor = mc->getGraphEditor();
    const auto* macro = editor.getMacros().findByMember(doc().getTrack(id)->bindingUuid);
    ASSERT_NE(macro, nullptr);
    ASSERT_TRUE(macro->collapsed) << "a new instrument track is boxed in a collapsed macro";
    editor.centreViewOn({5000.0f, 5000.0f}); // somewhere else entirely

    ASSERT_TRUE(headerOf(id).keyPressed(kCtrlE));

    const auto card = macro->bounds.toFloat().getCentre();
    const auto view = editor.getVisibleCanvasRect().getCentre();
    EXPECT_NEAR(card.x, view.x, 2.0f) << "the card, not its hidden members, is what gets centred";
    EXPECT_NEAR(card.y, view.y, 2.0f);
}

TEST_F(ShowTrackModuleTest, CtrlEOnAnAudioTrackSelectsItsOwnNodeAndOpensNothing) {
    const auto id = addTrack(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    const auto bound = nodeWithUuid(graph(), doc().getTrack(id)->bindingUuid);
    ASSERT_NE(bound.uid, 0u);

    ASSERT_TRUE(headerOf(id).keyPressed(kCtrlE));

    ASSERT_EQ(selected().size(), 1u);
    EXPECT_EQ(selected()[0], bound);
    EXPECT_EQ(mc->getPluginWindowManager().getOpenWindowCountForTest(), 0);
}

TEST_F(ShowTrackModuleTest, CtrlEOnAnUnboundTrackDoesNothing) {
    const auto id = doc().addTrack(synth::TrackKind::Midi, "Loose");
    ASSERT_TRUE(doc().getTrack(id)->bindingUuid.isEmpty());
    mc->getGraphEditor().selectModule(graph().getNodes().getFirst()->nodeID, false);
    const auto before = selected();

    EXPECT_TRUE(headerOf(id).keyPressed(kCtrlE));

    EXPECT_EQ(selected(), before);
    EXPECT_EQ(mc->getPluginWindowManager().getOpenWindowCountForTest(), 0);
}

TEST_F(ShowTrackModuleTest, CtrlEOnAHostedPluginTrackOpensItsWindowThenClosesIt) {
    synth::test::StubBackend backend;
    const auto id = addTrack(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    const auto trackIn = nodeWithUuid(graph(), doc().getTrack(id)->bindingUuid);
    ASSERT_NE(trackIn.uid, 0u);
    // Make the hosted plugin the one thing this Track In plays.
    for (const auto& c : graph().getConnections())
        if (c.source.nodeID == trackIn && c.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
            graph().removeConnection(c);

    auto module = std::make_unique<synth::HostedPluginModule>();
    auto* plugin = module.get();
    auto* node = graph().addNode(std::move(module)).get();
    synth::AIStateMapper::ensureNodeUuid(node);
    plugin->prepareToPlay(48000.0, 64);
    juce::PluginDescription description;
    description.name = "Stub Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = description.deprecatedUid = 0x5754424;
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    plugin->loadPlugin(description, backend);
    ASSERT_TRUE(pumpUntil([&] { return plugin->hasInstance(); }));
    ASSERT_TRUE(graph().addConnection({{trackIn, juce::AudioProcessorGraph::midiChannelIndex},
                                       {node->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    auto& windows = mc->getPluginWindowManager();
    auto& header = headerOf(id);

    ASSERT_TRUE(header.keyPressed(kCtrlE));
    EXPECT_TRUE(windows.hasWindowForTest(node->nodeID)) << "the first press opens the instrument's window";
    ASSERT_EQ(selected().size(), 1u);
    EXPECT_EQ(selected()[0], node->nodeID) << "and shows the plugin, not the Track In";

    ASSERT_TRUE(header.keyPressed(kCtrlE));
    EXPECT_FALSE(windows.hasWindowForTest(node->nodeID)) << "the second press closes it";

    windows.closeAll();
    header.getShowModuleButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // triggerClick posts its click
    EXPECT_TRUE(windows.hasWindowForTest(node->nodeID)) << "the button opens it too";
    windows.closeAll();
}
