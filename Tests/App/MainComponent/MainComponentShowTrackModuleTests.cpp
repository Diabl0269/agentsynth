// Concern: "Show module" end to end on a real MainComponent -- Ctrl+E through a track header's keyPressed (and the
// header's button) selects the track's module on the canvas, and for a hosted plugin opens its editor window and
// closes it on the next press. The plugin is the stub backend's, so the window is the real (headless) one.
// Also the drum machine's way to a Drum Kit track's kit through the same unit: its Show all toggle and its row menu's
// Hide pad / Show pad reach the card's shared undo helpers, one undo step each, the toggle hides exactly the pads it
// brought back, and hiding a drum with hits asks the card's question.
#include "../../StubPluginInstance.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

#include "../../TestWait.h"
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
    synth::test::waitUntil([&] { return (selected().size()) == (1u); });

    ASSERT_EQ(selected().size(), 1u);
    EXPECT_NE(dynamic_cast<OscillatorModule*>(graph().getNodeForId(selected()[0])->getProcessor()), nullptr);
}

TEST_F(ShowTrackModuleTest, TheWindowChordOnABuiltInInstrumentTrackOpensNothing) {
    const auto id = addTrack(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    const auto toggleKey = mc->getShortcutManager().getBinding("timelineToggleFocusedTrackPluginWindow");

    ASSERT_TRUE(headerOf(id).keyPressed(toggleKey));

    EXPECT_EQ(mc->getPluginWindowManager().getOpenWindowCountForTest(), 0) << "a built-in card has no window";
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

TEST_F(ShowTrackModuleTest, CtrlEOnAHostedPluginTrackFocusesItsModuleAndCtrlCmdEToggleTheWindow) {
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

    // Ctrl+E (and the row's button) only focus the module: the plugin's window stays shut.
    ASSERT_TRUE(header.keyPressed(kCtrlE));
    EXPECT_FALSE(windows.hasWindowForTest(node->nodeID)) << "Ctrl+E no longer opens the window";
    ASSERT_EQ(selected().size(), 1u);
    EXPECT_EQ(selected()[0], node->nodeID) << "it shows the plugin, not the Track In";
    header.getShowModuleButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // triggerClick posts its click
    EXPECT_FALSE(windows.hasWindowForTest(node->nodeID)) << "nor does the row's button";

    // The window has its own rebindable chord (Ctrl+Cmd+E on the Mac, Ctrl+Alt+E elsewhere).
    const auto toggleKey = mc->getShortcutManager().getBinding("timelineToggleFocusedTrackPluginWindow");
    ASSERT_TRUE(header.keyPressed(toggleKey));
    EXPECT_TRUE(windows.hasWindowForTest(node->nodeID)) << "the chord opens the instrument's window";
    ASSERT_TRUE(header.keyPressed(toggleKey));
    EXPECT_FALSE(windows.hasWindowForTest(node->nodeID)) << "and the next press closes it";
    windows.closeAll();
}

using synth::ui::DrumMachineComponent;
using synth::ui::TimelinePanelComponent;

namespace {

class DrumKitShowAllAppTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->getTimelinePanel().applyAddTrackMenuChoice(TimelinePanelComponent::kAddInstrumentDrumKitMenuId);
        track = mc->getTimelineDoc().getTracks().back().id;
        for (auto* node : mc->getAudioEngine().getGraph().getNodes())
            if (auto* k = dynamic_cast<DrumKitModule*>(node->getProcessor()))
                kit = k;
        ASSERT_NE(kit, nullptr);
        ASSERT_TRUE(mc->getTimelinePanel().openDrumMachineOnTrack(track));
        synth::ui::test_hooks::drumRowMenuHookForTest() = [this](int, juce::PopupMenu m, std::function<void(int)> c) {
            menu = std::move(m);
            choose = std::move(c);
        };
    }
    void TearDown() override {
        synth::ui::test_hooks::drumRowMenuHookForTest() = nullptr;
        synth::ui::test_hooks::hideDrumConfirmHookForTest() = nullptr;
        mc.reset();
        MainComponentTest::TearDown();
    }

    DrumMachineComponent& grid() { return mc->getTimelinePanel().getDrumMachine(); }
    bool undo() {
        const bool ok = mc->getUndoManager().undo();
        grid().refreshKitRows();
        return ok;
    }
    // Opens the row menu of `pitch` and picks the item titled `text` (in a submenu too); false when it is not there.
    bool pickRowItem(int pitch, const juce::String& text) {
        grid().showRowMenu(pitch, std::nullopt);
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
            if (it.getItem().text == text && it.getItem().subMenu == nullptr) {
                choose(it.getItem().itemID);
                return true;
            }
        return false;
    }
    std::vector<int> hiddenPads() const {
        std::vector<int> out;
        for (int i = 0; i < kit->padCount(); ++i)
            if (!kit->padShown(i))
                out.push_back(i);
        return out;
    }

    std::unique_ptr<MainComponent> mc;
    synth::TrackId track;
    DrumKitModule* kit = nullptr;
    juce::PopupMenu menu;
    std::function<void(int)> choose;
};

} // namespace

// Regression test for FRO1394: the clip editor could neither hide a pad nor hide the pads Show all brought back.
TEST_F(DrumKitShowAllAppTest, TheRowMenuHidesAndShowsAPadAsOneUndoStepEach) {
    ASSERT_TRUE(pickRowItem(kit->pad(2).note, "Hide pad"));
    EXPECT_EQ(hiddenPads(), std::vector<int>{2});
    EXPECT_LT(grid().rowOfPitch(kit->pad(2).note), 0);
    ASSERT_TRUE(undo());
    EXPECT_TRUE(hiddenPads().empty()) << "Cmd+Z brings the pad back in one step";
    EXPECT_GE(grid().rowOfPitch(kit->pad(2).note), 0);

    ASSERT_TRUE(pickRowItem(kit->pad(2).note, "Hide pad"));
    ASSERT_TRUE(pickRowItem(kit->pad(0).note, kit->pad(2).name)) << "Show pad lists it";
    EXPECT_TRUE(hiddenPads().empty());
    ASSERT_TRUE(undo());
    EXPECT_EQ(hiddenPads(), std::vector<int>{2}) << "undoing the show hides it again";
}

TEST_F(DrumKitShowAllAppTest, ShowAllTogglesTheSamePadsAndEachPressIsOneUndoStep) {
    for (int pad : {1, 4, 6})
        ASSERT_TRUE(pickRowItem(kit->pad(pad).note, "Hide pad"));
    ASSERT_EQ(hiddenPads(), (std::vector<int>{1, 4, 6}));
    auto& button = grid().getShowAllButton();
    ASSERT_TRUE(button.isEnabled());

    button.onClick();
    EXPECT_TRUE(hiddenPads().empty()) << "they return";
    EXPECT_TRUE(button.getToggleState());
    button.onClick();
    EXPECT_EQ(hiddenPads(), (std::vector<int>{1, 4, 6})) << "the same 3 hide";
    EXPECT_FALSE(button.getToggleState());

    ASSERT_TRUE(undo());
    EXPECT_TRUE(hiddenPads().empty()) << "one undo brings the hide-again back";
    EXPECT_TRUE(button.getToggleState()) << "lit again: the remembered pads came back with it";
    ASSERT_TRUE(undo());
    EXPECT_EQ(hiddenPads(), (std::vector<int>{1, 4, 6})) << "one more undoes Show all";
    EXPECT_FALSE(button.getToggleState());
}

TEST_F(DrumKitShowAllAppTest, HidingADrumWithHitsFromTheRowMenuAsksTheCardsQuestion) {
    const int clap = kit->pad(2).note;
    synth::MidiNote hit;
    hit.pitch = clap;
    hit.lengthBeats = 0.25;
    mc->getTimelineDoc().addNote(grid().getClipId(), hit);
    std::vector<juce::String> asked;
    std::function<void(bool, bool)> answer;
    synth::ui::test_hooks::hideDrumConfirmHookForTest() = [&](const synth::ui::HideDrumConfirmText& text,
                                                              std::function<void(bool, bool)> done) {
        asked.push_back(text.title);
        answer = std::move(done);
    };
    ASSERT_TRUE(pickRowItem(clap, "Hide pad"));
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0], "Clap has 1 hit; hide it anyway?");
    EXPECT_TRUE(kit->padShown(2)) << "nothing changes until the person says yes";
    answer(true, false);
    EXPECT_FALSE(kit->padShown(2));

    asked.clear();
    grid().getShowAllButton().onClick();
    grid().getShowAllButton().onClick();
    EXPECT_FALSE(kit->padShown(2)) << "Show all's second press hid it again";
    EXPECT_TRUE(asked.empty()) << "without asking: it restores the person's own choice";
}
