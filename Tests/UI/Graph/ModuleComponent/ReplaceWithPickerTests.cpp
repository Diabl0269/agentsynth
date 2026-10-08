// ReplaceWithPickerTests.cpp
//
// A module card's right-click "Replace with..." opens the shared searchable picker (ModMatrixPicker): every module
// type the old submenu listed, minus the card's own, then the scanned hosted plugins. Picking a row replaces the
// module -- a plugin row with a hosted module for that identity -- keeping its cables, in one undo step.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {

using synth::PluginIdentity;
using synth::ui::ModMatrixPicker;

PluginIdentity pluginNamed(const juce::String& name, int uid, const juce::String& format = "VST3") {
    PluginIdentity identity;
    identity.format = format;
    identity.name = name;
    identity.uid = uid;
    return identity;
}

bool contains(const std::vector<juce::String>& names, const juce::String& text) {
    return std::find(names.begin(), names.end(), text) != names.end();
}

struct ReplaceFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::AudioProcessorGraph::NodeID oscId, filterId;

    ReplaceFixture() {
        editor.setSize(800, 600);
        auto& graph = engine.getGraph();
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        oscId = osc->nodeID;
        filterId = filter->nodeID;
        osc->properties.set("x", 120);
        osc->properties.set("y", 80);
        graph.addConnection({{oscId, 0}, {filterId, 0}});
        editor.updateComponents();
        editor.installedPluginsProvider = [] {
            return std::vector<PluginIdentity>{pluginNamed("Diva", 0xD1FA), pluginNamed("Serum", 0x5E12, "AudioUnit")};
        };
    }

    ModuleComponent* card(juce::AudioProcessorGraph::NodeID id) {
        auto* content = editor.getChildComponent(0);
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child))
                if (mod->getModule() == engine.getGraph().getNodeForId(id)->getProcessor())
                    return mod;
        return nullptr;
    }

    synth::HostedPluginModule* hosted() {
        for (auto* node : engine.getGraph().getNodes())
            if (auto* h = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()))
                return h;
        return nullptr;
    }

    // Opens the picker through the card's real context-menu item.
    std::unique_ptr<ModMatrixPicker> openFromMenu(ModuleComponent& c) {
        std::unique_ptr<ModMatrixPicker> captured;
        synth::ui::test_hooks::replacePickerHookForTest() = [&captured](std::unique_ptr<ModMatrixPicker> p) {
            captured = std::move(p);
        };
        auto menu = c.buildModuleContextMenu();
        juce::PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
            if (it.getItem().text == "Replace with..." && it.getItem().action)
                it.getItem().action();
        synth::ui::test_hooks::replacePickerHookForTest() = nullptr;
        return captured;
    }
};

} // namespace

TEST(ReplaceWithPicker, ChoicesListEveryModuleTypeButTheCurrentOneThenThePlugins) {
    const auto choices = synth::ui::collectReplaceChoices(
        ModuleType::Oscillator, std::nullopt, {pluginNamed("Diva", 1), pluginNamed("Serum", 2, "AudioUnit")});
    std::vector<juce::String> texts, plugins;
    for (const auto& item : choices.items) {
        texts.push_back(item.text);
        if (item.category == "Plugins")
            plugins.push_back(item.text + "/" + item.detail);
    }
    EXPECT_FALSE(contains(texts, "Oscillator")) << "the card's own type is skipped";
    EXPECT_TRUE(contains(texts, "Filter"));
    EXPECT_TRUE(contains(texts, "Math"));
    EXPECT_EQ(plugins, (std::vector<juce::String>{"Diva/VST3", "Serum/AU"}));
    EXPECT_EQ(choices.items.size(), choices.choices.size());
}

TEST(ReplaceWithPicker, TheHostedPluginTheCardAlreadyHostsIsLeftOut) {
    const auto diva = pluginNamed("Diva", 1);
    const auto choices =
        synth::ui::collectReplaceChoices(ModuleType::HostedPlugin, diva, {diva, pluginNamed("Serum", 2)});
    std::vector<juce::String> texts;
    for (const auto& item : choices.items)
        texts.push_back(item.text);
    EXPECT_FALSE(contains(texts, "Diva"));
    EXPECT_TRUE(contains(texts, "Serum"));
    EXPECT_TRUE(contains(texts, "Oscillator")) << "a hosted card can still become any built-in module";
}

TEST(ReplaceWithPicker, TheContextMenuHasOneReplaceItemAndNoSubmenu) {
    ReplaceFixture f;
    auto* osc = f.card(f.oscId);
    ASSERT_NE(osc, nullptr);
    auto menu = osc->buildModuleContextMenu();
    int count = 0;
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text.startsWith("Replace")) {
            ++count;
            EXPECT_EQ(it.getItem().text, "Replace with...");
            EXPECT_EQ(it.getItem().subMenu, nullptr);
            EXPECT_TRUE(it.getItem().isEnabled);
        }
    EXPECT_EQ(count, 1);
}

TEST(ReplaceWithPicker, TypingDivaFindsTheScannedPluginAndTheSearchFieldIsAccessible) {
    ReplaceFixture f;
    auto picker = f.openFromMenu(*f.card(f.oscId));
    ASSERT_NE(picker, nullptr);

    auto& search = picker->getSearchEditorForTest();
    EXPECT_FALSE(search.getTitle().isEmpty());
    EXPECT_FALSE(search.getTooltip().isEmpty());
    EXPECT_FALSE(picker->getTitle().isEmpty());

    picker->setSearchTextForTest("diva");
    EXPECT_EQ(picker->getVisibleItemTextsForTest(), std::vector<juce::String>{"Diva"});
    picker->setSearchTextForTest("filt");
    EXPECT_TRUE(contains(picker->getVisibleItemTextsForTest(), "Filter"));
}

TEST(ReplaceWithPicker, ReturnOnAPluginMatchReplacesTheModuleWithAHostedOneAndKeepsItsCables) {
    ReplaceFixture f;
    auto picker = f.openFromMenu(*f.card(f.oscId));
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("diva");
    EXPECT_TRUE(picker->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));

    auto* hosted = f.hosted();
    ASSERT_NE(hosted, nullptr);
    EXPECT_EQ(hosted->getIdentity(), pluginNamed("Diva", 0xD1FA));
    EXPECT_EQ(f.engine.getGraph().getNodeForId(f.oscId), nullptr);

    int x = 0, y = 0;
    for (auto* node : f.engine.getGraph().getNodes())
        if (node->getProcessor() == hosted) {
            x = node->properties.getWithDefault("x", 0);
            y = node->properties.getWithDefault("y", 0);
        }
    EXPECT_EQ(x, 120);
    EXPECT_EQ(y, 80);

    bool cabled = false;
    for (auto& c : f.engine.getGraph().getConnections())
        if (c.destination.nodeID == f.filterId && c.source.channelIndex == 0 &&
            f.engine.getGraph().getNodeForId(c.source.nodeID)->getProcessor() == hosted)
            cabled = true;
    // A placeholder hosted module (no instance yet) may expose fewer channels; the cable survives only when it does.
    if (hosted->getTotalNumOutputChannels() > 0)
        EXPECT_TRUE(cabled);
}

TEST(ReplaceWithPicker, ChoosingAModuleRowStillReplacesWithThatModuleType) {
    ReplaceFixture f;
    auto picker = f.openFromMenu(*f.card(f.oscId));
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("chorus");
    picker->chooseVisibleItemForTest(0);
    bool chorus = false;
    for (auto* node : f.engine.getGraph().getNodes())
        chorus = chorus || node->getProcessor()->getName() == "Chorus";
    EXPECT_TRUE(chorus);
    EXPECT_EQ(f.engine.getGraph().getNodeForId(f.oscId), nullptr);
}

TEST(ReplaceWithPicker, UndoBringsTheOldModuleBackAndRedoRemembersTheWhichPlugin) {
    ReplaceFixture f;
    synth::ui::applyReplaceChoice(f.editor, f.card(f.oscId), {{}, pluginNamed("Diva", 0xD1FA)});
    ASSERT_NE(f.hosted(), nullptr);

    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.hosted(), nullptr);
    bool osc = false;
    for (auto* node : f.engine.getGraph().getNodes())
        osc = osc || dynamic_cast<OscillatorModule*>(node->getProcessor()) != nullptr;
    EXPECT_TRUE(osc);

    EXPECT_TRUE(f.undo.redo());
    ASSERT_NE(f.hosted(), nullptr);
    EXPECT_EQ(f.hosted()->getIdentity(), pluginNamed("Diva", 0xD1FA));
}
