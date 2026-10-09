// MacroReplaceMemberTests.cpp
// "Replace with..." on a module inside a macro: the new module takes the old one's place in the macro, built-in
// or hosted plugin alike, and one undo puts the old module back inside the macro.

#include "MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "AudioEngine/GraphSnapshotCache.h"
#include "MidiRemote/RemoteModel.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include <gtest/gtest.h>

namespace {

synth::PluginIdentity pluginNamed(const juce::String& name, int uid) {
    synth::PluginIdentity identity;
    identity.format = "VST3";
    identity.name = name;
    identity.uid = uid;
    return identity;
}

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::MidiRemoteProjectDoc midiRemoteDoc;
    NodeID osc, filter;
    juce::String macroId;

    // `withMidiRemote` wires the MIDI Remote doc the way the app does, so the replace records graph, macros and
    // that doc in one undo step; without it the replace takes the headless graph + macros path.
    explicit Canvas(bool withMidiRemote = false) {
        undo.setGraphEditor(&editor);
        if (withMidiRemote)
            editor.setMidiRemoteProjectDocForUndo(&midiRemoteDoc);
        editor.setSize(2400, 1600);
        osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 300);
        filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);
        editor.setSelectedNodes({osc, filter});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
    }

    // The node whose processor passes `match`, or an invalid id.
    template <typename Pred>
    NodeID nodeWhere(Pred match) {
        for (auto* node : engine.getGraph().getNodes())
            if (match(node->getProcessor()))
                return node->nodeID;
        return {};
    }

    NodeID hostedNamed(const juce::String& name) {
        return nodeWhere([&](juce::AudioProcessor* p) {
            auto* h = dynamic_cast<synth::HostedPluginModule*>(p);
            return h != nullptr && h->getIdentity().name == name;
        });
    }

    bool inMacro(NodeID id) {
        const auto* macro = editor.getMacros().find(macroId);
        return id.uid != 0 && macro != nullptr && macro->hasMember(uuidOf(engine, id));
    }

    void replace(NodeID id, const synth::ui::ReplaceChoice& choice) {
        synth::ui::applyReplaceChoice(editor, findComponent(editor, id), choice);
    }
};

} // namespace

TEST(MacroReplaceMember, ABuiltInModuleReplacedInsideAMacroStaysInItAndUndoKeepsTheOldOneThere) {
    Canvas c;
    ASSERT_FALSE(c.macroId.isEmpty());
    const auto oscUuid = uuidOf(c.engine, c.osc);

    c.replace(c.osc, {"LFO", std::nullopt});
    const auto lfo = c.nodeWhere([](juce::AudioProcessor* p) { return p->getName() == "LFO"; });
    EXPECT_TRUE(c.inMacro(lfo));
    EXPECT_TRUE(c.inMacro(c.filter));

    ASSERT_TRUE(c.undo.undo());
    EXPECT_TRUE(c.editor.getMacros().find(c.macroId)->hasMember(oscUuid));
    EXPECT_TRUE(c.inMacro(nodeIdForUuid(c.engine, oscUuid)));

    ASSERT_TRUE(c.undo.redo());
    EXPECT_TRUE(c.inMacro(c.nodeWhere([](juce::AudioProcessor* p) { return p->getName() == "LFO"; })));
}

// Regression test for FRO651: replacing a hosted plugin inside a macro with another plugin put the new one outside
// the macro, and undo did not put the old plugin back in.
TEST(MacroReplaceMember, AHostedPluginReplacedByAnotherInsideAMacroStaysInItAndUndoRestoresIt) {
    Canvas c;
    ASSERT_FALSE(c.macroId.isEmpty());
    c.replace(c.osc, {{}, pluginNamed("Diva", 0xD1FA)});
    const auto diva = c.hostedNamed("Diva");
    ASSERT_TRUE(c.inMacro(diva));
    const auto divaUuid = uuidOf(c.engine, diva);

    c.replace(diva, {{}, pluginNamed("Serum", 0x5E12)});
    const auto serum = c.hostedNamed("Serum");
    EXPECT_TRUE(c.inMacro(serum));
    EXPECT_EQ(c.hostedNamed("Diva").uid, 0u);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.hostedNamed("Serum").uid, 0u);
    EXPECT_TRUE(c.inMacro(c.hostedNamed("Diva")));
    EXPECT_EQ(uuidOf(c.engine, c.hostedNamed("Diva")), divaUuid);
}

TEST(MacroReplaceMember, WithMidiRemoteWiredLikeTheAppUndoAndRedoKeepMembership) {
    Canvas c(/*withMidiRemote*/ true);
    ASSERT_FALSE(c.macroId.isEmpty());
    c.replace(c.osc, {{}, pluginNamed("Diva", 0xD1FA)});
    const auto diva = c.hostedNamed("Diva");
    ASSERT_TRUE(c.inMacro(diva));
    c.replace(diva, {{}, pluginNamed("Serum", 0x5E12)});
    EXPECT_TRUE(c.inMacro(c.hostedNamed("Serum")));

    ASSERT_TRUE(c.undo.undo());
    EXPECT_TRUE(c.inMacro(c.hostedNamed("Diva")));
    ASSERT_TRUE(c.undo.redo());
    EXPECT_TRUE(c.inMacro(c.hostedNamed("Serum")));
    EXPECT_EQ(c.hostedNamed("Diva").uid, 0u);
}

TEST(MacroReplaceMember, AModuleOutsideAnyMacroStaysOutside) {
    Canvas c;
    const auto loose = addModuleAt(c.editor, c.engine, std::make_unique<OscillatorModule>(), 400, 900);
    const auto before = c.editor.getMacros().toVar();
    c.replace(loose, {"LFO", std::nullopt});
    EXPECT_TRUE(synth::sameJson(before, c.editor.getMacros().toVar()));
}
