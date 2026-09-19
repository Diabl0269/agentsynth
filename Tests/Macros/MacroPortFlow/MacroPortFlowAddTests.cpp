#include "MacroPortFlowTestHelpers.h"

// Topic: adding a port (Mono/Stereo/Poly-N/MIDI), one undo step, plus the four macro node
// types staying out of the module library with a pinned size estimate.

TEST(MacroPortFlow, AddMonoInputCreatesAMacroInletMemberAndPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());

    const auto uuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                          MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(uuid.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuid));
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_EQ(macro->ports[0].nodeUuid, uuid);
    EXPECT_TRUE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].name, "Pitch In");
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    auto nodeId = nodeIdForUuid(engine, uuid);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(nodeId)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Mono);
}

TEST(MacroPortFlow, AddOutputMidiCreatesAMacroMidiOutletMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto uuid =
        editor.addMacroPort(macroId, /*isInput=*/false, synth::MacroPortKind::Midi, MacroPortShape::Mono, 1, "");
    ASSERT_FALSE(uuid.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::Midi);
    EXPECT_EQ(macro->ports[0].name, "MIDI Out"); // blank name falls back to the direction/kind default

    auto nodeId = nodeIdForUuid(engine, uuid);
    auto* proc = engine.getGraph().getNodeForId(nodeId)->getProcessor();
    ASSERT_NE(dynamic_cast<MacroMidiOutletModule*>(proc), nullptr);
    // Pass-through, like MacroMidiInletModule: both true (an internal member feeds it MIDI in,
    // it hands MIDI out to the world) — the outlet/inlet distinction is which SIDE of the macro
    // boundary the node sits on, not an asymmetry in acceptsMidi()/producesMidi().
    EXPECT_TRUE(proc->producesMidi());
    EXPECT_TRUE(proc->acceptsMidi());
}

TEST(MacroPortFlow, AddStereoAndPolySetTheChosenShapeOnTheNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto stereoUuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1, "Stereo In");
    auto* stereoInlet = dynamic_cast<MacroInletModule*>(
        engine.getGraph().getNodeForId(nodeIdForUuid(engine, stereoUuid))->getProcessor());
    ASSERT_NE(stereoInlet, nullptr);
    EXPECT_EQ(stereoInlet->getPortShape(), MacroPortShape::Stereo);
    EXPECT_EQ(stereoInlet->getVisibleInputPortCount(), 2);

    const auto polyUuid =
        editor.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Poly, 5, "Poly Out");
    auto* polyOutlet = dynamic_cast<MacroOutletModule*>(
        engine.getGraph().getNodeForId(nodeIdForUuid(engine, polyUuid))->getProcessor());
    ASSERT_NE(polyOutlet, nullptr);
    EXPECT_EQ(polyOutlet->getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(polyOutlet->getVoiceCount(), 5);
    EXPECT_EQ(polyOutlet->getVisibleOutputPortCount(), 1); // one fanned jack, not five
}

TEST(MacroPortFlow, AddIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const int nodesBefore = engine.getGraph().getNodes().size();

    const auto uuid = editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore);
    EXPECT_TRUE(nodeIdForUuid(engine, uuid).uid == 0);
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
}

// ============================================================================
// Library absence + pinned size estimate (docs/macros/configure-io.md#adding-a-port's own requirement)
// ============================================================================

TEST(MacroPortFlow, AllFourTypesAreAbsentFromTheLibraryWithAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    for (const juce::String& typeName : {"Macro In", "Macro Out", "Macro MIDI In", "Macro MIDI Out"}) {
        EXPECT_FALSE(library.getDraggableModuleNames().contains(typeName))
            << typeName << " is internal-only and must stay out of the module library";

        auto processor = synth::AIStateMapper::createModule(typeName);
        ASSERT_NE(processor, nullptr) << typeName;

        AudioEngine engine;
        GraphEditor editor(engine);
        ModuleComponent comp(processor.get(), NodeID(1), editor);

        const auto estimate = GraphEditor::estimateModuleSize(typeName);
        EXPECT_EQ(estimate.x, comp.getWidth()) << typeName;
        EXPECT_EQ(estimate.y, comp.getHeight()) << typeName;
    }
}
