#pragma once

// A canvas with an LFO, two Filters and a spare Oscillator plus its Mod Matrix panel, shared by the
// matrix tests that need a real GraphEditor (macro routing, module titles, the searchable picker).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "../../Macros/MacroAutoPort/MacroAutoPortTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/ModMatrixComponent.h"

namespace {

constexpr int kCutoff = 1; // FilterModule's Cutoff CV channel (mono layout)

// Which pair is boxed into a macro decides the scenario: `DestInside` boxes a Filter, so a row
// pointing at it crosses inward; `SourceInside` boxes the LFO, so a row reading from it crosses outward.
enum class Boxed { DestInside, SourceInside };

struct MatrixCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID lfo, filterIn, filterOut, spare;
    juce::String macroId;

    explicit MatrixCanvas(Boxed boxed) {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
        editor.getModMatrix().setBounds(0, 0, 600, 400);
        lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), "LFO", 100, 100);
        filterIn = addModuleAt(editor, engine, std::make_unique<FilterModule>(), "FilterIn", 500, 100);
        filterOut = addModuleAt(editor, engine, std::make_unique<FilterModule>(), "FilterOut", 900, 100);
        spare = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), "Spare", 500, 400);
        editor.setSelectedNodes({boxed == Boxed::DestInside ? filterIn : lfo, spare});
        macroId = editor.getMacroController().groupSelectionIntoMacro(false);
    }

    ModMatrixComponent& matrix() { return editor.getModMatrix(); }
    synth::Macro& macro() { return *editor.getMacros().find(macroId); }

    // The panel's own "Add" button, so a test covers the undo step around it too.
    NodeID addRow() {
        static_cast<juce::Button*>(matrix().findChildWithID("addModulation"))->onClick();
        matrix().updateRowsFromGraph();
        const auto ids = nodesOf<AttenuverterModule>();
        return ids.empty() ? NodeID{} : ids.back();
    }

    void pick(juce::ComboBox* combo, NodeID node, int channel) {
        ASSERT_NE(combo, nullptr);
        combo->setSelectedId(ModMatrixComponent::encodeComboId(node, channel), juce::sendNotificationSync);
        matrix().updateRowsFromGraph(); // what the 10 Hz tick does
    }
    void pickSource(NodeID node, int channel = 0) { pick(matrix().getRowSourceComboForTest(0), node, channel); }
    void pickDest(NodeID node, int channel = kCutoff) { pick(matrix().getRowDestComboForTest(0), node, channel); }

    template <typename T>
    std::vector<NodeID> nodesOf() {
        std::vector<NodeID> ids;
        for (auto* node : engine.getGraph().getNodes())
            if (dynamic_cast<T*>(node->getProcessor()) != nullptr)
                ids.push_back(node->nodeID);
        return ids;
    }

    bool edge(NodeID a, NodeID b, int dstCh = 0) { return hasConnection(engine, a, 0, b, dstCh); }
    juce::String nameOf(NodeID id) { return engine.getGraph().getNodeForId(id)->getProcessor()->getName(); }
};

} // namespace
