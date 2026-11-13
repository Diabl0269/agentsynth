// MainComponentModuleKeysTests.cpp -- the Rename Module and Replace Module With... commands, dispatched through the
// application command manager the way F2 / Cmd+Alt+R reach them.
#include "../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "../../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/AudioInputModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

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

// Option+R is the second Rename key: with a card selected and nothing else owning the name it opens the card's title
// editor through the same command F2 does.
TEST_F(MainComponentTest, OptionROpensTheSelectedCardsTitleEditorLikeF2) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto& editor = mc.getGraphEditor();
    const auto oscId = editor.getAudioEngine().getGraph().addNode(std::make_unique<OscillatorModule>())->nodeID;
    editor.updateComponents();
    editor.selectModule(oscId, false);

    EXPECT_TRUE(mc.keyPressed(juce::KeyPress('r', juce::ModifierKeys::altModifier, juce::juce_wchar(0xAE))));
    synth::test::flushMessageQueue();

    ModuleComponent* osc = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == oscId)
            osc = c;
    ASSERT_NE(osc, nullptr);
    EXPECT_TRUE(osc->isRenamingTitle());
}

namespace {

const juce::KeyPress kOptionR(juce::KeyPress('r', juce::ModifierKeys::altModifier, juce::juce_wchar(0xAE)));
const juce::KeyPress kF2Key(juce::KeyPress::F2Key, juce::ModifierKeys::noModifiers, 0);

// A canvas with a two-module macro (oscillator + filter) and one loose module, the macro folded or open.
struct MacroRenameRig {
    MainComponent mc{std::make_unique<MockProvider>()};
    GraphEditor& editor = mc.getGraphEditor();
    AudioEngine& engine = editor.getAudioEngine();
    NodeID a, b, loose;
    juce::String macroId;
    juce::String promptedId;

    MacroRenameRig() {
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 100);
        loose = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 900);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro(false);
        editor.promptRenameMacroForTest = [this](const juce::String& id) { promptedId = id; };
    }
    void fold(bool folded) {
        editor.getMacroController().setMacroCollapsed(macroId, folded);
        editor.finishCardGlideForTest();
    }
    MacroCardComponent* card() { return editor.getMacroController().getMacroCardForTest(macroId); }
};

} // namespace

TEST_F(MainComponentTest, OptionRRenamesTheSelectedFoldedMacroOnItsCard) {
    MacroRenameRig r;
    r.fold(true);
    r.editor.getMacroController().selectMacro(r.macroId, false);
    ASSERT_TRUE(isActive(r.mc, AppCommands::renameSelectedModule));
    EXPECT_TRUE(r.mc.keyPressed(kOptionR));
    synth::test::flushMessageQueue();
    ASSERT_NE(r.card(), nullptr);
    EXPECT_TRUE(r.card()->isRenamingTitle());
    EXPECT_TRUE(r.promptedId.isEmpty()) << "a folded macro renames inline, not in the dialog";
}

TEST_F(MainComponentTest, F2RenamesTheSelectedOpenMacroThroughItsPrompt) {
    MacroRenameRig r;
    r.fold(false);
    r.editor.getMacroController().selectMacro(r.macroId, false);
    ASSERT_TRUE(isActive(r.mc, AppCommands::renameSelectedModule));
    EXPECT_TRUE(r.mc.keyPressed(kF2Key));
    synth::test::flushMessageQueue();
    EXPECT_EQ(r.promptedId, r.macroId);
    EXPECT_TRUE(r.mc.keyPressed(kOptionR));
    synth::test::flushMessageQueue();
}

TEST_F(MainComponentTest, RenameStillRenamesTheModuleWhenOneModuleInsideAnOpenMacroIsSelected) {
    MacroRenameRig r;
    r.fold(false);
    r.editor.selectModule(r.a, false);
    EXPECT_TRUE(r.mc.keyPressed(kOptionR));
    synth::test::flushMessageQueue();
    EXPECT_TRUE(r.promptedId.isEmpty());
    auto* card = findComponent(r.editor, r.a);
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->isRenamingTitle());
}

TEST_F(MainComponentTest, RenameIsOffWithNothingOrAPartialMacroSelected) {
    MacroRenameRig r;
    r.fold(false);
    r.editor.clearSelection();
    EXPECT_FALSE(isActive(r.mc, AppCommands::renameSelectedModule));
    r.editor.selectModule(r.a, false);
    r.editor.selectModule(r.loose, true);
    EXPECT_FALSE(isActive(r.mc, AppCommands::renameSelectedModule)) << "a module plus a loose one is no macro";
}

// Guard (sibling gap): every nameable thing the canvas draws is reached by the Rename command. Enumerated from the
// canvas itself: each card the editor holds (module cards, macro cards) in each fold state. Tracks and mixer strips
// answer the key from their own focus (RenameKeyTests.cpp).
TEST_F(MainComponentTest, RenameCommandReachesEveryNameableCanvasThing) {
    for (const bool folded : {true, false}) {
        MacroRenameRig r;
        r.fold(folded);
        int reached = 0;
        for (auto* c : r.editor.getModuleComponents()) {
            if (c == nullptr || !c->isVisible() || !c->canRename())
                continue;
            r.editor.selectModule(c->getNodeId(), false);
            ASSERT_TRUE(isActive(r.mc, AppCommands::renameSelectedModule)) << "module card, folded=" << folded;
            EXPECT_TRUE(r.mc.keyPressed(kOptionR));
            synth::test::flushMessageQueue();
            EXPECT_TRUE(c->isRenamingTitle());
            c->finishTitleRename(false);
            ++reached;
        }
        EXPECT_GE(reached, folded ? 1 : 3) << "visible module cards, folded=" << folded;
        for (const auto& macro : r.editor.getMacros().getAll()) {
            r.promptedId.clear();
            r.editor.getMacroController().selectMacro(macro.id, false);
            ASSERT_TRUE(isActive(r.mc, AppCommands::renameSelectedModule)) << "macro, folded=" << folded;
            EXPECT_TRUE(r.mc.keyPressed(kOptionR));
            synth::test::flushMessageQueue();
            auto* card = r.editor.getMacroController().getMacroCardForTest(macro.id);
            const bool inline_ = card != nullptr && card->isRenamingTitle();
            EXPECT_TRUE(folded ? inline_ : r.promptedId == macro.id) << "macro, folded=" << folded;
        }
    }
}
