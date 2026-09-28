// MacroAutoPortNameTests.cpp
// Undoing (or redoing, or ungrouping+undoing) a macro mutation must never rename a
// module. AudioEngine::updateModuleNames() (AudioEngineModRouting.cpp) auto-numbers modules
// sharing a base type "wholesale on every graph change" (docs/layout/module-card.md), and it runs
// incidentally whenever the mod matrix notices the graph's node count changed
// (ModMatrixComponent::updateRowsFromGraph) — including a macro's own inlet/outlet port
// splice/unsplice on group/ungroup/undo/redo, which is not a module the user added or removed.
// Before the fix, that incidental run gave a LONE module (no sibling of its base type) a spurious
// "1" suffix ("Oscillator" -> "Oscillator 1") the first time it ever fired, and it stuck from then
// on. The fix (AudioEngineModRouting.cpp) makes the pass skip the number entirely when a base type
// has only one instance; these tests pin the macro-path symptom end to end, through the real
// grouping entry point (MacroGroupController::groupSelectionIntoMacro), not by calling
// updateModuleNames() directly (see ModMatrixTests.cpp for that unit-level coverage). Shared test
// modules/helpers live in MacroAutoPortTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "MacroAutoPortTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/AudioInputModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SequencerModule.h"
#include <juce_events/juce_events.h>
#include <map>

namespace {

// Forces any pending juce::MessageManager::callAsync (GraphEditor::updateComponents()'s own
// mod-matrix refresh, the actual carrier of the rename) and the 10 Hz ModMatrixComponent
// timer to actually run — without this, the bug is invisible in a headless test that never pumps
// the message loop (the async callback simply never fires).
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

// A small patch shaped like the ticket's repro: several modules that are each the only one of
// their type (so each is a "lone singleton" the auto-rename targets), one of them additionally carrying a
// user-set custom card title ("displayName" — a totally different mechanism from the processor's
// own auto-numbered name, see docs/layout/module-card.md, and must survive untouched too).
struct NamedPatch {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID input, osc, distortion, sequencer;

    NamedPatch() {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
        auto& graph = engine.getGraph();

        auto add = [&](std::unique_ptr<juce::AudioProcessor> p, int x, int y) {
            auto node = graph.addNode(std::move(p));
            node->properties.set("x", x);
            node->properties.set("y", y);
            node->properties.set("uuid", juce::Uuid().toDashedString());
            return node->nodeID;
        };
        input = add(std::make_unique<AudioInputModule>(), 100, 100);
        osc = add(std::make_unique<OscillatorModule>(), 300, 100);
        distortion = add(std::make_unique<DistortionModule>(), 500, 100);
        sequencer = add(std::make_unique<SequencerModule>(), 300, 400);
        graph.addConnection({{input, 0}, {osc, 0}});
        graph.addConnection({{osc, 0}, {distortion, 0}});

        editor.setModuleDisplayName(distortion, "Fuzz Bus"); // a custom title, untouched by numbering

        editor.updateComponents();
        // Deliberately NOT pumped here: GraphEditor::updateComponents() just scheduled a mod-matrix
        // refresh via callAsync (ModMatrixComponent::updateRowsFromGraph -> AudioEngine::
        // updateModuleNames() the first time it ever runs — see ChannelFlowPluginInstrumentTests.cpp's
        // identical comment). Leaving it unpumped means every module's name here is still the bare,
        // never-yet-renumbered one createModule() constructed it with, so `before` below captures the
        // TRUE starting names rather than whatever that first settle would already have turned them
        // into — a real running app pumps its event loop continuously, so this settle would already
        // have happened before the user could act, but that would make it impossible for a headless
        // test to observe the rename transition at all; not pumping here is what lets the group
        // action below be the thing that finally triggers it.
    }

    std::map<juce::String, juce::String> snapshotNames() {
        std::map<juce::String, juce::String> names;
        for (auto id : {input, osc, distortion, sequencer})
            if (auto* node = engine.getGraph().getNodeForId(id))
                names[id.uid == input.uid        ? "input"
                      : id.uid == osc.uid        ? "osc"
                      : id.uid == distortion.uid ? "distortion"
                                                 : "sequencer"] = node->getProcessor()->getName();
        return names;
    }
};

} // namespace

TEST(MacroUndoNamePreservation, GroupThenUndoLeavesEveryModuleNameByteIdentical) {
    NamedPatch patch;
    const auto before = patch.snapshotNames();
    const auto customTitleBefore = patch.editor.getModuleDisplayName(patch.distortion);
    ASSERT_EQ(customTitleBefore, "Fuzz Bus");

    patch.editor.setSelectedNodes({patch.input, patch.osc});
    const auto macroId =
        patch.editor.getMacroController().groupSelectionIntoMacro(true); // real entry point, auto ports
    ASSERT_FALSE(macroId.isEmpty());
    pump();

    ASSERT_TRUE(patch.undo.canUndo());
    patch.undo.undo();
    pump();

    EXPECT_EQ(patch.snapshotNames(), before) << "undoing the group must not rename anything";
    EXPECT_EQ(patch.editor.getModuleDisplayName(patch.distortion), customTitleBefore)
        << "a custom card title must survive too";
    EXPECT_TRUE(patch.editor.getMacros().empty());
}

TEST(MacroUndoNamePreservation, GroupUndoRedoLeavesEveryModuleNameByteIdentical) {
    NamedPatch patch;
    const auto before = patch.snapshotNames();

    patch.editor.setSelectedNodes({patch.input, patch.osc});
    const auto macroId = patch.editor.getMacroController().groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    pump();
    const auto afterGroup = patch.snapshotNames();

    patch.undo.undo();
    pump();
    EXPECT_EQ(patch.snapshotNames(), before) << "undo";

    patch.undo.redo();
    pump();
    EXPECT_EQ(patch.snapshotNames(), afterGroup) << "redo restores exactly what grouping produced, byte-identical";
    EXPECT_FALSE(patch.editor.getMacros().empty());
}

TEST(MacroUndoNamePreservation, UngroupThenUndoLeavesEveryModuleNameByteIdentical) {
    NamedPatch patch;

    patch.editor.setSelectedNodes({patch.input, patch.osc});
    const auto macroId = patch.editor.getMacroController().groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    pump();
    const auto afterGroup = patch.snapshotNames();

    patch.editor.setSelectedNodes({patch.input, patch.osc});
    patch.editor.getMacroController().ungroupSelection();
    pump();

    ASSERT_TRUE(patch.undo.canUndo());
    patch.undo.undo();
    pump();

    EXPECT_EQ(patch.snapshotNames(), afterGroup)
        << "undoing the ungroup must restore the macro without renaming anything";
    EXPECT_FALSE(patch.editor.getMacros().empty());
}
