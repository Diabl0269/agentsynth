#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"

// Topic: T148 — auto-creating a macro port when a dragged cable crosses an EXPANDED macro's
// boundary (jack-to-jack, both endpoints real ModuleComponents), including the mod-CV
// attenuverter-wrapping case and the auto-create-on-drag preference toggle.

// ============================================================================
// T148 (docs/macros/auto-ports.md#ports-on-a-cable-drag): auto-create a macro port when a dragged cable crosses an
// EXPANDED macro's boundary — the counterpart to the collapsed-card drop convenience above, which
// only fires when there is no jack under the cursor. All jack-to-jack, so both endpoints are real,
// visible ModuleComponents this time (no MacroCardComponent involved).
// ============================================================================

TEST(MacroPortFlow, DraggingFromAnExpandedMacroMemberToAnExternalModuleAutoCreatesAnOutletAndWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId,
                                                  false); // expand: members become real, visible ModuleComponents

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    ASSERT_TRUE(memberComp->isVisible());

    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    // Oscillator's audio OUTPUT (jack 0, the member) -> the external Filter's audio INPUT (jack 0).
    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1) << "exactly one new outlet port node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, portId, 0)) << "interior leg: member -> port";
    EXPECT_TRUE(hasConnection(engine, portId, 0, extFilter, 0)) << "exterior leg: port -> external";
    EXPECT_FALSE(hasConnection(engine, oscMember, 0, extFilter, 0)) << "the direct cable must not also exist";
}

TEST(MacroPortFlow, DraggingFromAnExternalModuleToAnExpandedMacroMemberAutoCreatesAnInletAndWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, filterMember);
    ASSERT_NE(memberComp, nullptr);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* extComp = compFor(editor, extOsc);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    // External Oscillator's OUTPUT (jack 0) -> the member Filter's audio INPUT (jack 0).
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(memberComp->getBounds().getPosition() + memberComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1) << "exactly one new inlet port node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);

    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, portId, 0)) << "exterior leg: external -> port";
    EXPECT_TRUE(hasConnection(engine, portId, 0, filterMember, 0)) << "interior leg: port -> member";
}

TEST(MacroPortFlow, DraggingBetweenMembersOfTwoDifferentMacrosCreatesAPortOnEachWithAPortToPortConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Macro X: an Oscillator (the drag source) plus a filler member (min-2 rule).
    auto oscX = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto fillerX = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscX, fillerX});
    const auto macroXId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroXId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroXId, false);

    // Macro Y: a Filter (the drag destination) plus a filler member.
    auto filterY = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto fillerY = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 400);
    editor.setSelectedNodes({filterY, fillerY});
    const auto macroYId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroYId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroYId, false);

    auto* oscXComp = compFor(editor, oscX);
    auto* filterYComp = compFor(editor, filterY);
    ASSERT_NE(oscXComp, nullptr);
    ASSERT_NE(filterYComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(oscXComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(filterYComp->getBounds().getPosition() + filterYComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "one outlet on X, one inlet on Y";

    auto* macroX = editor.getMacros().find(macroXId);
    auto* macroY = editor.getMacros().find(macroYId);
    ASSERT_NE(macroX, nullptr);
    ASSERT_NE(macroY, nullptr);
    ASSERT_EQ(macroX->ports.size(), 1u);
    ASSERT_EQ(macroY->ports.size(), 1u);
    EXPECT_FALSE(macroX->ports[0].isInput) << "X gets an outlet";
    EXPECT_TRUE(macroY->ports[0].isInput) << "Y gets an inlet";

    const auto outletId = nodeIdForUuid(engine, macroX->ports[0].nodeUuid);
    const auto inletId = nodeIdForUuid(engine, macroY->ports[0].nodeUuid);
    EXPECT_TRUE(hasConnection(engine, oscX, 0, outletId, 0)) << "X's interior leg";
    EXPECT_TRUE(hasConnection(engine, outletId, 0, inletId, 0)) << "the port-to-port boundary leg";
    EXPECT_TRUE(hasConnection(engine, inletId, 0, filterY, 0)) << "Y's interior leg";
}

TEST(MacroPortFlow, DraggingBetweenTwoMembersOfTheSameMacroCreatesAPlainDirectConnectionRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* oscComp = compFor(editor, oscMember);
    auto* filterComp = compFor(editor, filterMember);
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(oscComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "no port minted for a same-macro connection";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, filterMember, 0)) << "plain direct connection instead";
}

TEST(MacroPortFlow, DraggingToAnExistingPortDirectlyDoesNotMintASecondPortRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto macroId = makeTwoMemberMacro(editor, engine);
    editor.getMacroController().setMacroCollapsed(macroId, false);

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    auto* portComp = compFor(editor, portId);
    ASSERT_NE(portComp, nullptr);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* extComp = compFor(editor, extOsc);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    // Drag straight onto the existing inlet port's own jack (isInput on the port side).
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(portComp->getBounds().getPosition() + portComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "no second port minted";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "still just the one hand-added port";
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, portId, 0)) << "wired straight into the existing port";
}

TEST(MacroPortFlow,
     DraggingAModulationConnectionAcrossAMacroBoundaryAutoCreatesAPortAndWrapsTheModTargetLegInAnAttenuverter) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // LFO is the macro member (the drag source); a Filter fills the min-2 rule.
    auto lfoMember = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto fillerMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({lfoMember, fillerMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* lfoComp = compFor(editor, lfoMember);
    ASSERT_NE(lfoComp, nullptr);

    // External Wavetable Oscillator: its "Position" slider is a real modulation target, the same
    // Serum-style knob-drop DroppingACableOnAKnobCreatesAModRouting (GraphEditor/GraphEditorTests.cpp) exercises.
    auto extWt = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 100);
    auto* extComp = compFor(editor, extWt);
    ASSERT_NE(extComp, nullptr);

    juce::Slider* position = nullptr;
    for (auto* child : extComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);

    const auto knobPoint = extComp->getBounds().getPosition() + position->getBounds().getCentre();
    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(lfoComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(knobPoint);

    // T155: a mod-routed drag now mints a port too, exactly like a plain audio drag — TWO new
    // nodes, not one: the auto-created MacroOutletModule AND the hidden AttenuverterModule that
    // connectPorts()'s own CV detection wraps the port->realDestination leg in.
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "one port node, one attenuverter node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "the port, and only the port, is a macro member";
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, lfoMember, 0, portId, 0)) << "interior leg: member -> port, plain and direct";

    // The exterior leg (port -> the real modulation-target destination) is the one that must be
    // wrapped: NOT a direct connection, but a hidden attenuverter chain, reported by the mod
    // matrix as one active routing whose source is the port (not the LFO, and not the Attenuverter
    // node itself — mirrors MacroAutoPortTests.cpp's
    // AttenuverterAdjacentCrossingIsSplicedForAGenuineExternalCrossing assertion style).
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u) << "exactly one mod routing, wrapping the port->destination leg";
    EXPECT_EQ(active[0].sourceNodeID, portId) << "the routing's source is the port, not the LFO directly";
    EXPECT_EQ(active[0].destNodeID, extWt) << "the routing's destination is the real modulation target";
    const auto attenId = active[0].attenuverterNodeID;
    ASSERT_TRUE(attenId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, portId, 0, attenId, 0)) << "port -> attenuverter";
    EXPECT_TRUE(hasConnection(engine, attenId, 0, extWt, active[0].destChannelIndex))
        << "attenuverter -> real destination";
    EXPECT_FALSE(hasConnection(engine, portId, 0, extWt, active[0].destChannelIndex))
        << "no leftover direct port->destination connection";

    // Neither the attenuverter nor the port's own uuid appears anywhere but the one macro entry —
    // the attenuverter can never itself be a macro member
    // (docs/macros/macros.md#macro-ports-are-proxy-nodes-on-a-flat-graph,
    // docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter).
    EXPECT_FALSE(macro->hasMember(uuidOf(engine, attenId)));
}

TEST(MacroPortFlow, TheWholeModCVAutoCreateSequenceIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto lfoMember = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto fillerMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({lfoMember, fillerMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* lfoComp = compFor(editor, lfoMember);
    ASSERT_NE(lfoComp, nullptr);
    auto extWt = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 100);
    auto* extComp = compFor(editor, extWt);
    ASSERT_NE(extComp, nullptr);

    juce::Slider* position = nullptr;
    for (auto* child : extComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);
    const auto knobPoint = extComp->getBounds().getPosition() + position->getBounds().getCentre();

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(lfoComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(knobPoint);

    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "port node + attenuverter node";
    ASSERT_EQ(engine.getActiveModRoutings().size(), 1u);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore)
        << "a single Cmd+Z removed the port AND the attenuverter";
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_TRUE(engine.getActiveModRoutings().empty()) << "the mod routing itself is undone too";

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2);
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, editor.getMacros().find(macroId)->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, lfoMember, 0, portId, 0));
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].sourceNodeID, portId);
    EXPECT_EQ(active[0].destNodeID, extWt);
}

TEST(MacroPortFlow,
     DraggingAModCrossingBetweenMembersOfTwoDifferentMacrosCreatesAPortOnEachWithTheAttenuverterOnTheFinalLeg) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Macro X: the LFO (the drag source) plus a filler member (min-2 rule).
    auto lfoX = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto fillerX = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({lfoX, fillerX});
    const auto macroXId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroXId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroXId, false);

    // Macro Y: a Wavetable Oscillator (its "Position" slider is the drag destination, a real
    // modulation target) plus a filler member.
    auto wtY = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 100);
    auto fillerY = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 400);
    editor.setSelectedNodes({wtY, fillerY});
    const auto macroYId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroYId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroYId, false);

    auto* lfoXComp = compFor(editor, lfoX);
    auto* wtYComp = compFor(editor, wtY);
    ASSERT_NE(lfoXComp, nullptr);
    ASSERT_NE(wtYComp, nullptr);

    juce::Slider* position = nullptr;
    for (auto* child : wtYComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);
    const auto knobPoint = wtYComp->getBounds().getPosition() + position->getBounds().getCentre();

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(lfoXComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(knobPoint);

    // One outlet on X, one inlet on Y, and the hidden attenuverter — three new nodes total.
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 3) << "outlet + inlet + attenuverter";

    auto* macroX = editor.getMacros().find(macroXId);
    auto* macroY = editor.getMacros().find(macroYId);
    ASSERT_NE(macroX, nullptr);
    ASSERT_NE(macroY, nullptr);
    ASSERT_EQ(macroX->ports.size(), 1u);
    ASSERT_EQ(macroY->ports.size(), 1u);
    EXPECT_FALSE(macroX->ports[0].isInput) << "X gets an outlet";
    EXPECT_TRUE(macroY->ports[0].isInput) << "Y gets an inlet";

    const auto outletId = nodeIdForUuid(engine, macroX->ports[0].nodeUuid);
    const auto inletId = nodeIdForUuid(engine, macroY->ports[0].nodeUuid);
    ASSERT_TRUE(outletId.uid != 0);
    ASSERT_TRUE(inletId.uid != 0);

    // X's interior leg and the port-to-port boundary leg are both plain, direct connections — the
    // outlet is not itself a modulation target, so neither leg attracts the wrap.
    EXPECT_TRUE(hasConnection(engine, lfoX, 0, outletId, 0)) << "X's interior leg: plain";
    EXPECT_TRUE(hasConnection(engine, outletId, 0, inletId, 0)) << "the port-to-port boundary leg: plain";

    // Only Y's interior leg (inlet -> the real modulation-target destination) is wrapped — it's
    // the last leg wired, and the only one whose real endpoint is a genuine modulation target.
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u) << "exactly one mod routing, on Y's interior leg only";
    EXPECT_EQ(active[0].sourceNodeID, inletId);
    EXPECT_EQ(active[0].destNodeID, wtY);
    const auto attenId = active[0].attenuverterNodeID;
    ASSERT_TRUE(attenId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, inletId, 0, attenId, 0)) << "inlet -> attenuverter";
    EXPECT_TRUE(hasConnection(engine, attenId, 0, wtY, active[0].destChannelIndex))
        << "attenuverter -> real destination";
    EXPECT_FALSE(hasConnection(engine, inletId, 0, wtY, active[0].destChannelIndex))
        << "no leftover direct inlet->destination connection";
}

TEST(MacroPortFlow, TheWholeAutoCreateSequenceIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "a single Cmd+Z removed the port node too";
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_FALSE(hasConnection(engine, oscMember, 0, extFilter, 0)) << "no leftover cable from the mint-and-wire";

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, editor.getMacros().find(macroId)->ports[0].nodeUuid);
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, portId, 0));
    EXPECT_TRUE(hasConnection(engine, portId, 0, extFilter, 0));
}

TEST(MacroPortFlow, AutoCreateOnDragDisabledPreferenceLeavesTheOriginalBehaviourRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setAutoCreateMacroPortsOnDragEnabled(false);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "the toggle off means no port is minted";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, extFilter, 0)) << "plain direct connection instead";
}
