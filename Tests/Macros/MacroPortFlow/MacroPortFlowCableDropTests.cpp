#include "MacroPortFlowTestHelpers.h"

// Topic: dragging a cable onto a collapsed macro card creates a port and wires it, with no
// Configure I/O dialog involved (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed convenience path).

// ============================================================================
// Cable-drop convenience (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed)
// ============================================================================

TEST(MacroPortFlow, DroppingACableOnACollapsedCardCreatesAMonoInputAndWiresIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    ModuleComponent* extComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == extOsc)
            extComp = c;
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->isVisible());
    const auto dropPoint = card->getBounds().getCentre();

    // Drag from the external Oscillator's audio OUTPUT (jack 0) and release over the collapsed
    // card — no jack under the cursor (T141 hasn't drawn any yet), so this exercises the
    // convenience path: an output source means the macro should gain an INPUT.
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(dropPoint);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    const auto newNodeId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, newNodeId, 0)) << "the boundary connection was wired";
}

TEST(MacroPortFlow, DroppingAMidiCableOnACollapsedCardCreatesAMidiOutput) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    auto extMidi = addModuleAt(editor, engine, std::make_unique<MidiKeyboardModule>(), 900, 900);
    ModuleComponent* extComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == extMidi)
            extComp = c;
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto dropPoint = card->getBounds().getCentre();

    // Dragging FROM an input jack (isInput=true) looking for a source -> the macro should offer
    // an OUTPUT. MidiKeyboardModule has no input jack, so this simulates the gesture directly via
    // the same primitive endConnectionDrag itself drives from a real jack drag.
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/true, /*isMidi=*/true, {0, 0});
    editor.endConnectionDrag(dropPoint);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::Midi);
}
