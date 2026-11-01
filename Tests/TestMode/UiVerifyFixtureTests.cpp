// Tools/ui-verify/fixtures/macros.agsproj: the project the UI-verification examples open. One test proves the
// committed fixture still loads and still holds an open, a collapsed and a nested macro; a disabled one regenerates it
// through the app's real save path:
//   build-ci-local/Tests/Tests --gtest_also_run_disabled_tests
//   --gtest_filter=UiVerifyFixture.DISABLED_WritesMacrosProject

#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Modules/ChannelStripModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace {

juce::File fixtureBundle() { return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("macros.agsproj"); }

using NodeSet = std::set<juce::AudioProcessorGraph::NodeID>;

NodeSet nodeIds(AudioEngine& engine) {
    NodeSet ids;
    for (auto* n : engine.getGraph().getNodes())
        ids.insert(n->nodeID);
    return ids;
}

// Adds a module through the canvas path (undo recorded, position snapped) and returns its node id.
juce::AudioProcessorGraph::NodeID addModule(MainComponent& mc, const char* type, juce::Point<int> pos) {
    const auto before = nodeIds(mc.getAudioEngine());
    mc.getGraphEditor().addModuleAtCanvasPosition(type, pos, {});
    for (auto* n : mc.getAudioEngine().getGraph().getNodes())
        if (before.count(n->nodeID) == 0)
            return n->nodeID;
    return {};
}

juce::String group(MainComponent& mc, std::vector<juce::AudioProcessorGraph::NodeID> nodes, const char* name,
                   bool collapsed) {
    auto& editor = mc.getGraphEditor();
    editor.setSelectedNodes(nodes);
    const auto id = editor.getMacroController().groupSelectionIntoMacro();
    if (id.isNotEmpty()) {
        editor.getMacros().find(id)->name = name;
        editor.getMacroController().setMacroCollapsed(id, collapsed);
    }
    return id;
}

} // namespace

class UiVerifyFixture : public ChannelFlowTest {};

TEST_F(UiVerifyFixture, DISABLED_WritesMacrosProject) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    addModule(mc, "Oscillator", {100, 300});

    const auto lfo1 = addModule(mc, "LFO", {400, 200});
    const auto env1 = addModule(mc, "ADSR", {700, 200});
    ASSERT_FALSE(group(mc, {lfo1, env1}, "Open macro", false).isEmpty());

    const auto lfo2 = addModule(mc, "LFO", {400, 600});
    const auto env2 = addModule(mc, "ADSR", {700, 600});
    ASSERT_FALSE(group(mc, {lfo2, env2}, "Collapsed macro", true).isEmpty());

    const auto lfo3 = addModule(mc, "LFO", {1100, 200});
    const auto env3 = addModule(mc, "ADSR", {1400, 200});
    const auto osc3 = addModule(mc, "Oscillator", {1250, 500});
    const auto outer = group(mc, {lfo3, env3, osc3}, "Outer macro", false);
    ASSERT_FALSE(outer.isEmpty());
    const auto inner = group(mc, {lfo3, env3}, "Nested macro", false);
    ASSERT_FALSE(inner.isEmpty());
    ASSERT_EQ(mc.getGraphEditor().getMacros().parentOf(inner), outer);

    const auto bundle = fixtureBundle();
    bundle.deleteRecursively();
    bundle.getParentDirectory().createDirectory();
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
}

TEST_F(UiVerifyFixture, TheCommittedMacrosProjectHoldsAnOpenACollapsedAndANestedMacro) {
    const auto bundle = fixtureBundle();
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();

    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    const auto result = synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                                   editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(result.ok) << result.message;

    std::map<juce::String, const synth::Macro*> byName;
    for (const auto& m : editor.getMacros().getAll())
        byName[m.name] = &m;
    ASSERT_TRUE(byName.count("Open macro") && byName.count("Collapsed macro") && byName.count("Nested macro") &&
                byName.count("Outer macro"));
    EXPECT_FALSE(byName["Open macro"]->collapsed);
    EXPECT_TRUE(byName["Collapsed macro"]->collapsed);
    EXPECT_EQ(byName["Nested macro"]->parentId, byName["Outer macro"]->id);

    std::set<juce::String> types;
    const auto json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    for (const auto& n : *json.getProperty("nodes", {}).getArray())
        types.insert(n["type"].toString());
    EXPECT_TRUE(types.count("LFO") && types.count("ADSR") && types.count("Oscillator"));
}

// Tools/ui-verify/fixtures/lfo-two-macros.agsproj (the arrange-lfo-two-macros scenario): two tracks, each an open
// macro, and an LFO cabled into the second and modulating the first.
TEST_F(UiVerifyFixture, TheCommittedLfoTwoMacrosProjectHoldsTwoTrackMacrosAndAnLfoFeedingBoth) {
    const auto bundle = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("lfo-two-macros.agsproj");
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();

    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    const auto result = synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                                   editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(result.ok) << result.message;

    int topLevelMacros = 0;
    for (const auto& m : editor.getMacros().getAll())
        topLevelMacros += m.parentId.isEmpty() ? 1 : 0;
    EXPECT_EQ(topLevelMacros, 2);
    const auto json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    int lfos = 0;
    for (const auto& n : *json.getProperty("nodes", {}).getArray())
        lfos += n["type"].toString() == "LFO" ? 1 : 0;
    EXPECT_EQ(lfos, 1);
}

// Tools/ui-verify/fixtures/channel-strip-macro.agsproj: an Oscillator track with its channel (the Channel Strip sits in
// the open channel macro, cabled to the channel's own output port) and, beside the strip, a collapsed macro with no
// ports. Regenerate with --gtest_filter=UiVerifyFixture.DISABLED_WritesChannelStripMacroProject.
TEST_F(UiVerifyFixture, DISABLED_WritesChannelStripMacroProject) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& editor = mc.getGraphEditor();

    const auto osc = addModule(mc, "Oscillator", {100, 300});
    juce::AudioProcessorGraph::NodeID output;
    for (auto* n : mc.getAudioEngine().getGraph().getNodes())
        if (synth::AIStateMapper::getFactoryTypeName(n->getProcessor()) == "Audio Output")
            output = n->nodeID;
    ASSERT_NE(output.uid, 0u);
    editor.connectPorts(osc, 0, output, 0, false, false);
    ASSERT_TRUE(editor.makeChannelFromNode(osc, "Lead"));

    for (const auto& m : editor.getMacros().getAll())
        editor.getMacroController().setMacroCollapsed(m.id, false);
    for (auto* n : mc.getAudioEngine().getGraph().getNodes())
        if (dynamic_cast<ChannelStripModule*>(n->getProcessor()) != nullptr) {
            n->properties.set("x", 300);
            n->properties.set("y", 300);
        }
    editor.updateComponents();

    const auto f1 = addModule(mc, "Filter", {900, 300});
    const auto f2 = addModule(mc, "Filter", {900, 700});
    ASSERT_FALSE(group(mc, {f1, f2}, "Empty macro", true).isEmpty());

    const auto bundle = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("channel-strip-macro.agsproj");
    bundle.deleteRecursively();
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
}

TEST_F(UiVerifyFixture, TheCommittedChannelStripMacroProjectHoldsAStripInAnOpenChannelAndAFoldedEmptyMacro) {
    const auto bundle = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("channel-strip-macro.agsproj");
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();

    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    const auto result = synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                                   editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(result.ok) << result.message;

    bool hasStrip = false;
    for (auto* n : engine.getGraph().getNodes())
        hasStrip = hasStrip || dynamic_cast<ChannelStripModule*>(n->getProcessor()) != nullptr;
    EXPECT_TRUE(hasStrip);
    int folded = 0, open = 0;
    for (const auto& m : editor.getMacros().getAll()) {
        (m.collapsed ? folded : open)++;
        if (m.collapsed)
            EXPECT_TRUE(m.ports.empty());
    }
    EXPECT_EQ(folded, 1);
    EXPECT_EQ(open, 1);
}
