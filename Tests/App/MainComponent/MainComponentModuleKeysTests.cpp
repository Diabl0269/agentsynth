// MainComponentModuleKeysTests.cpp -- the Rename Module and Replace Module With... commands, dispatched through the
// application command manager the way F2 / Cmd+Alt+R reach them.
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/AudioInputModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"

namespace {

bool isActive(MainComponent& mc, juce::CommandID id) {
    juce::ApplicationCommandInfo info(id);
    if (mc.getCommandManager().getTargetForCommand(id, info) == nullptr)
        return false;
    return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
}

} // namespace

TEST_F(MainComponentTest, RenameAndReplaceCommandsActOnTheOneSelectedModule) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto& editor = mc.getGraphEditor();
    auto& graph = editor.getAudioEngine().getGraph();
    const auto oscId = graph.addNode(std::make_unique<OscillatorModule>())->nodeID;
    const auto filterId = graph.addNode(std::make_unique<FilterModule>())->nodeID;
    const auto inputId = graph.addNode(std::make_unique<AudioInputModule>())->nodeID;
    editor.updateComponents();
    auto cardFor = [&](juce::AudioProcessorGraph::NodeID id) -> ModuleComponent* {
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->getNodeId() == id)
                return c;
        return nullptr;
    };

    editor.clearSelection();
    EXPECT_FALSE(isActive(mc, AppCommands::renameSelectedModule));
    EXPECT_FALSE(isActive(mc, AppCommands::replaceSelectedModule));

    editor.selectModule(oscId, false);
    editor.selectModule(filterId, true);
    EXPECT_FALSE(isActive(mc, AppCommands::renameSelectedModule));
    EXPECT_FALSE(isActive(mc, AppCommands::replaceSelectedModule));

    editor.selectModule(inputId, false);
    EXPECT_TRUE(isActive(mc, AppCommands::renameSelectedModule));
    EXPECT_FALSE(isActive(mc, AppCommands::replaceSelectedModule)) << "Audio Input is a singleton I/O node";

    editor.selectModule(oscId, false);
    ASSERT_TRUE(isActive(mc, AppCommands::renameSelectedModule));
    ASSERT_TRUE(isActive(mc, AppCommands::replaceSelectedModule));

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::renameSelectedModule, false));
    auto* osc = cardFor(oscId);
    ASSERT_NE(osc, nullptr);
    ASSERT_TRUE(osc->isRenamingTitle());
    auto* ed = dynamic_cast<juce::TextEditor*>(osc->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    ed->setText("Bass Osc", juce::dontSendNotification);
    osc->finishTitleRename(true);
    EXPECT_EQ(osc->cardTitle(), "Bass Osc");

    std::unique_ptr<synth::ui::ModMatrixPicker> captured;
    synth::ui::test_hooks::replacePickerHookForTest() = [&captured](std::unique_ptr<synth::ui::ModMatrixPicker> p) {
        captured = std::move(p);
    };
    const bool invoked = mc.getCommandManager().invokeDirectly(AppCommands::replaceSelectedModule, false);
    synth::ui::test_hooks::replacePickerHookForTest() = nullptr;
    ASSERT_TRUE(invoked);
    ASSERT_NE(captured, nullptr);
    captured->setSearchTextForTest("Chorus");
    EXPECT_TRUE(captured->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(graph.getNodeForId(oscId), nullptr);
}
