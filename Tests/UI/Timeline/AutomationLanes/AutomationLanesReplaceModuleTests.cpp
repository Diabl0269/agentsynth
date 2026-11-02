// AutomationLanesReplaceModuleTests.cpp -- the Replace Module With... shortcut (Cmd/Ctrl+Alt+R) with an automation
// lane header or a modulator row focused: on a mod row it opens the row's Change source picker, on a lane header the
// lane's Change parameter picker, both in the timeline; with a canvas card selected it opens the card's Replace with...
// picker as before. Through the real key path (the focused component's keyPressed, then MainComponent::keyPressed, then
// the command table).

#include "../../../TestWait.h"
#include "AutomationLanesModulatorFixture.h"

#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace modulator_test;
using namespace automation_lanes_test;

namespace {

bool isActive(MainComponent& mc, juce::CommandID id) {
    juce::ApplicationCommandInfo info(id);
    if (mc.getCommandManager().getTargetForCommand(id, info) == nullptr)
        return false;
    return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
}

// Which picker a press opened: the three hooks are captured at once so a wrong one shows up as the wrong slot.
struct Pickers {
    std::unique_ptr<synth::ui::ModMatrixPicker> source;    // a mod row's Change source
    std::unique_ptr<synth::ui::ModMatrixPicker> parameter; // a lane header's Change parameter
    std::unique_ptr<synth::ui::ModMatrixPicker> replace;   // a canvas card's Replace with...
    Pickers() {
        synth::ui::test_hooks::changeSourcePickerHookForTest() = [this](auto p) { source = std::move(p); };
        synth::ui::test_hooks::laneParameterPickerHookForTest() = [this](auto p) { parameter = std::move(p); };
        synth::ui::test_hooks::replacePickerHookForTest() = [this](auto p) { replace = std::move(p); };
    }
    ~Pickers() {
        synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr;
        synth::ui::test_hooks::laneParameterPickerHookForTest() = nullptr;
        synth::ui::test_hooks::replacePickerHookForTest() = nullptr;
    }
    bool any() const { return source != nullptr || parameter != nullptr || replace != nullptr; }
};

// Presses the Replace Module binding the way the key reaches the app: the focused component offers it first, then it
// bubbles to MainComponent, whose dispatch runs on the next message-loop pump.
void pressReplace(MainComponent& mc, juce::Component& focused, Pickers& pickers) {
    mc.setFocusedComponentOverrideForTest(&focused);
    const auto key = mc.getShortcutManager().getBinding("replaceSelectedModule");
    EXPECT_FALSE(focused.keyPressed(key)) << "the focused lane header / row must not swallow the chord";
    EXPECT_TRUE(mc.keyPressed(key));
    // Wait for the dispatch rather than a fixed slice: a loaded machine can take longer than 50 ms to pump it.
    for (int slice = 0; slice < 40 && !pickers.any(); ++slice)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc.setFocusedComponentOverrideForTest(nullptr);
}

// The track must play the Filter (Track In -> Oscillator -> Filter) for the lane's parameter picker to offer the
// Filter's other parameters.
void playTheFilter(Scene& s) {
    auto osc = addNodeWithUuid(s.mc, "Oscillator");
    for (auto* node : s.graph().getNodes())
        if (node->properties["uuid"].toString() == s.doc().getTracks().front().bindingUuid) {
            EXPECT_TRUE(s.graph().addConnection({{node->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                                 {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
            break;
        }
    EXPECT_TRUE(s.graph().addConnection({{osc->nodeID, 0}, {s.target->nodeID, 0}}));
    s.mc.getGraphEditor().updateComponents();
}

} // namespace

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutOnAFocusedLaneHeaderOpensTheChangeParameterPickerInTheTimeline) {
    Scene s;
    playTheFilter(s);
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    s.mc.getGraphEditor().clearSelection();
    ASSERT_FALSE(isActive(s.mc, AppCommands::replaceSelectedModule)) << "nothing on the canvas is selected";

    s.mc.setFocusedComponentOverrideForTest(header);
    EXPECT_EQ(s.mc.resolveEditSurface(), MainComponent::EditSurface::AutomationLane);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule)) << "the lane's parameter picker can open";
    s.mc.setFocusedComponentOverrideForTest(nullptr);

    Pickers pickers;
    pressReplace(s.mc, *header, pickers);
    ASSERT_NE(pickers.parameter, nullptr) << "the lane's Change parameter picker opened";
    EXPECT_EQ(pickers.replace, nullptr) << "not the canvas card's picker";
    EXPECT_TRUE(s.mc.getGraphEditor().getSelectedNodes().empty()) << "the canvas was not touched";
    EXPECT_EQ(s.panel().getAutomationLanes().headerFor(s.lane), header);
    EXPECT_NE(s.graph().getNodeForId(s.target->nodeID), nullptr) << "the lane's module is still there";
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutOnALaneHeaderRepointsTheLaneAndUndoPutsItBack) {
    Scene s;
    playTheFilter(s);
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    const auto before = s.doc().getLane(s.lane)->paramId;

    Pickers pickers;
    pressReplace(s.mc, *header, pickers);
    ASSERT_NE(pickers.parameter, nullptr);
    const auto texts = pickers.parameter->getVisibleItemTextsForTest();
    ASSERT_FALSE(texts.empty()) << "other parameters to pick";
    pickers.parameter->chooseVisibleItemForTest(0);
    synth::test::waitUntil([&] { return s.doc().getLane(s.lane)->paramId != before; });
    EXPECT_NE(s.doc().getLane(s.lane)->paramId, before) << "the lane points at the picked parameter";

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, before);
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutOnAFocusedModulatorRowOpensItsChangeSourcePicker) {
    Scene s;
    s.addLfoFromLaneMenu();
    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    const auto lfoUuid = row->getInfo().sourceUuid;
    s.mc.getGraphEditor().clearSelection();

    s.mc.setFocusedComponentOverrideForTest(row);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule));
    s.mc.setFocusedComponentOverrideForTest(nullptr);

    Pickers pickers;
    pressReplace(s.mc, *row, pickers);
    ASSERT_NE(pickers.source, nullptr) << "the row's Change source picker opened";
    EXPECT_EQ(pickers.replace, nullptr) << "not the canvas card's picker";
    EXPECT_EQ(pickers.parameter, nullptr) << "not the lane's parameter picker";
    EXPECT_TRUE(s.mc.getGraphEditor().getSelectedNodes().empty()) << "the canvas was not touched";

    // Picking "New envelope" swaps the row's source in place, as the name button's own picker does.
    const auto texts = pickers.source->getVisibleItemTextsForTest();
    int envelope = -1;
    for (int i = 0; i < (int)texts.size(); ++i)
        if (texts[(size_t)i].containsIgnoreCase("New envelope"))
            envelope = i;
    ASSERT_GE(envelope, 0);
    pickers.source->chooseVisibleItemForTest(envelope);
    synth::test::waitUntil([&] { return s.row() != nullptr && s.row()->getInfo().sourceUuid != lfoUuid; });
    ASSERT_NE(s.row(), nullptr);
    EXPECT_NE(s.row()->getInfo().sourceUuid, lfoUuid);
    EXPECT_TRUE(s.row()->getInfo().isEnvelope);

    ASSERT_TRUE(s.undo().undo());
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, lfoUuid) << "one undo puts the LFO back";
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutIsGreyedOnARowWhoseSourceCannotBeSwapped) {
    Scene s;
    s.addLfoFromLaneMenu();
    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    // A hand-patched cable has no Attenuverter, the case the Change source item is hidden for.
    auto info = row->getInfo();
    info.attenuverterUuid = {};
    ASSERT_FALSE(s.mc.canChangeModulatorSourceForTest(info));
    row->setRouting(info, "Cutoff");
    ASSERT_FALSE(row->canOpenChangeSourcePicker());

    s.mc.getGraphEditor().clearSelection();
    s.mc.setFocusedComponentOverrideForTest(row);
    EXPECT_FALSE(isActive(s.mc, AppCommands::replaceSelectedModule));
    s.mc.setFocusedComponentOverrideForTest(nullptr);
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutKeepsTheCanvasBehaviourWhenNoLaneOrRowHasFocus) {
    Scene s;
    s.mc.getGraphEditor().clearSelection();
    EXPECT_FALSE(isActive(s.mc, AppCommands::replaceSelectedModule));
    s.mc.getGraphEditor().selectModule(s.target->nodeID, false);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule));

    Pickers pickers;
    s.mc.getGraphEditor().grabKeyboardFocus();
    EXPECT_TRUE(s.mc.getGraphEditor().getCardKeyboard().replaceSelectedModule());
    EXPECT_NE(pickers.replace, nullptr) << "a selected card still opens Replace with...";
    EXPECT_EQ(pickers.source, nullptr);
    EXPECT_EQ(pickers.parameter, nullptr);
}
