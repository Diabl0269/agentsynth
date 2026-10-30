// AutomationLanesReplaceModuleTests.cpp -- the Replace Module With... shortcut (Cmd/Ctrl+Alt+R) with an automation
// lane header or a modulator row focused: it replaces the lane's target module / the row's source module, through the
// real key path (the focused component's keyPressed, then MainComponent::keyPressed, then the command table).

#include "AutomationLanesModulatorFixture.h"

#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include "UI/Layout/ReducedMotion.h"
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

// Presses the Replace Module binding the way the key reaches the app: the focused component offers it first, then it
// bubbles to MainComponent, whose dispatch runs on the next message-loop pump. Returns the picker that opened.
std::unique_ptr<synth::ui::ModMatrixPicker> pressReplace(MainComponent& mc, juce::Component& focused) {
    mc.setFocusedComponentOverrideForTest(&focused);
    const auto key = mc.getShortcutManager().getBinding("replaceSelectedModule");
    std::unique_ptr<synth::ui::ModMatrixPicker> captured;
    synth::ui::test_hooks::replacePickerHookForTest() = [&captured](auto p) { captured = std::move(p); };
    EXPECT_FALSE(focused.keyPressed(key)) << "the focused lane header / row must not swallow the chord";
    EXPECT_TRUE(mc.keyPressed(key));
    // Wait for the dispatch rather than a fixed slice: a loaded machine can take longer than 50 ms to pump it.
    for (int slice = 0; slice < 40 && captured == nullptr; ++slice)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    synth::ui::test_hooks::replacePickerHookForTest() = nullptr;
    mc.setFocusedComponentOverrideForTest(nullptr);
    return captured;
}

// Folds `uuids` into one collapsed macro, as an instrument track's modules are by default.
const synth::Macro* collapseIntoMacro(Scene& s, const std::vector<juce::String>& uuids) {
    synth::Macro macro;
    macro.name = "Oscillator 1";
    macro.collapsed = true;
    macro.bounds = {900, 700, 400, 300};
    macro.members.assign(uuids.begin(), uuids.end());
    s.mc.getGraphEditor().getMacros().add(macro);
    s.mc.getGraphEditor().updateComponents();
    return s.mc.getGraphEditor().getMacros().findByMember(uuids.front());
}

} // namespace

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutOnAFocusedLaneHeaderReplacesTheLanesTargetModule) {
    Scene s;
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    s.mc.getGraphEditor().clearSelection();
    ASSERT_FALSE(isActive(s.mc, AppCommands::replaceSelectedModule)) << "nothing on the canvas is selected";

    s.mc.setFocusedComponentOverrideForTest(header);
    EXPECT_EQ(s.mc.resolveEditSurface(), MainComponent::EditSurface::AutomationLane);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule)) << "the lane's module is the target";
    s.mc.setFocusedComponentOverrideForTest(nullptr);

    auto picker = pressReplace(s.mc, *header);
    ASSERT_NE(picker, nullptr) << "the replace picker opened";
    const auto selected = s.mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), s.target->nodeID) << "the card is selected on the canvas so the picker sits on it";

    picker->setSearchTextForTest("Chorus");
    EXPECT_TRUE(picker->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(s.graph().getNodeForId(s.target->nodeID), nullptr) << "the lane's module was replaced";
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutOnAFocusedModulatorRowReplacesTheSourceModule) {
    Scene s;
    s.addLfoFromLaneMenu();
    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    const auto lfoUuid = row->getInfo().sourceUuid;
    auto* lfo = s.byUuid(lfoUuid);
    ASSERT_NE(lfo, nullptr);
    s.mc.getGraphEditor().clearSelection();

    s.mc.setFocusedComponentOverrideForTest(row);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule));
    s.mc.setFocusedComponentOverrideForTest(nullptr);

    const auto lfoId = lfo->nodeID;
    auto picker = pressReplace(s.mc, *row);
    ASSERT_NE(picker, nullptr);
    const auto selected = s.mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), lfoId) << "the row's source (the LFO), not the lane's target, is replaced";
    EXPECT_NE(selected.front(), s.target->nodeID);
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutKeepsTheCanvasBehaviourWhenNoLaneOrRowHasFocus) {
    Scene s;
    s.mc.getGraphEditor().clearSelection();
    EXPECT_FALSE(isActive(s.mc, AppCommands::replaceSelectedModule));
    s.mc.getGraphEditor().selectModule(s.target->nodeID, false);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule));
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutUnfoldsTheCollapsedMacroHoldingTheLanesTargetModule) {
    Scene s;
    const auto* macro = collapseIntoMacro(s, {s.targetUuid});
    ASSERT_NE(macro, nullptr);
    const auto macroId = macro->id;
    ASSERT_TRUE(macro->collapsed);
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    s.mc.getGraphEditor().clearSelection();

    s.mc.setFocusedComponentOverrideForTest(header);
    EXPECT_TRUE(isActive(s.mc, AppCommands::replaceSelectedModule)) << "a hidden card is no reason to grey it out";
    s.mc.setFocusedComponentOverrideForTest(nullptr);

    auto picker = pressReplace(s.mc, *header);
    ASSERT_NE(picker, nullptr) << "the picker opened over the revealed card";
    EXPECT_FALSE(s.mc.getGraphEditor().getMacros().find(macroId)->collapsed) << "the macro was unfolded";
    const auto selected = s.mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), s.target->nodeID);
}

TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutUnfoldsTheCollapsedMacroHoldingTheModulatorSource) {
    Scene s;
    s.addLfoFromLaneMenu();
    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    const auto lfoUuid = row->getInfo().sourceUuid;
    auto* lfo = s.byUuid(lfoUuid);
    ASSERT_NE(lfo, nullptr);
    const auto lfoId = lfo->nodeID;
    const auto* macro = collapseIntoMacro(s, {lfoUuid});
    ASSERT_NE(macro, nullptr);
    const auto macroId = macro->id;
    s.mc.getGraphEditor().clearSelection();

    auto picker = pressReplace(s.mc, *row);
    ASSERT_NE(picker, nullptr);
    EXPECT_FALSE(s.mc.getGraphEditor().getMacros().find(macroId)->collapsed);
    const auto selected = s.mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), lfoId);
}

// Regression test: the unfold hides the module's card until its glide lands, and a hidden card cannot take the
// picker, so the first press opened nothing.
TEST_F(TimelinePanelIntegrationTest, ReplaceShortcutOpensThePickerOnlyOnceTheUnfoldGlideHasLanded) {
    synth::ui::setReducedMotionForTest(false);
    Scene s;
    const auto* macro = collapseIntoMacro(s, {s.targetUuid});
    ASSERT_NE(macro, nullptr);
    auto& editor = s.mc.getGraphEditor();
    editor.getCardGlideForTest().setForceAnimateForTest(true);
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    editor.clearSelection();

    std::unique_ptr<synth::ui::ModMatrixPicker> captured;
    synth::ui::test_hooks::replacePickerHookForTest() = [&captured](auto p) { captured = std::move(p); };
    s.mc.setFocusedComponentOverrideForTest(header);
    EXPECT_TRUE(s.mc.keyPressed(s.mc.getShortcutManager().getBinding("replaceSelectedModule")));
    // The chord dispatches on a later message-loop turn (the first in a process can take well over a slice), so wait
    // for the unfold to start rather than a fixed slice, and judge the picker at that moment, not after the glide.
    bool gliding = false;
    for (int slice = 0; slice < 200 && !gliding && captured == nullptr; ++slice) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        gliding = editor.getCardGlide().fold().isLive();
    }
    const bool pickerOpenedEarly = captured != nullptr;
    s.mc.setFocusedComponentOverrideForTest(nullptr);

    EXPECT_TRUE(gliding) << "the unfold is gliding";
    EXPECT_FALSE(pickerOpenedEarly) << "the card is still held back, so no picker yet";

    editor.finishCardGlideForTest();
    synth::ui::test_hooks::replacePickerHookForTest() = nullptr;
    synth::ui::setReducedMotionForTest(std::nullopt);
    ASSERT_NE(captured, nullptr) << "the picker opens when the glide lands";
    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), s.target->nodeID);
}
