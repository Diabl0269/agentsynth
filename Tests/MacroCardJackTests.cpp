// GraphEditor-level tests for collapsed-card port jacks (P8-15c, T141, docs/macros.md §7 item 4):
// one jack per configured MacroPort, and buildVisibleCables() anchoring a boundary cable to the
// specific jack it passes through rather than the card's generic edge-projection.
//
// The hit-test (macroCardPortForPoint, read from inside GraphEditor::endConnectionDrag) is
// exercised through the REAL mouse path — synthesised juce::MouseEvents driven into the actual
// ModuleComponent::mouseDown/mouseUp, the same widget methods a real drag-and-drop gesture calls —
// not by calling GraphEditor's internals directly. Per memory/test-the-real-mouse-path-for-ui-
// gestures: testing the layer beneath mouseDown cannot catch a broken hit-test on the card jacks,
// which is exactly the risk this file exists to cover (buildVisibleCables() is the function P8-12c
// last reworked, and adversarial review caught 3 real integration bugs there).

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

ModuleComponent* findComponent(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule).
 *  Returns {macroId, oscillatorNodeId, filterNodeId}. */
struct TwoMemberMacro {
    juce::String macroId;
    NodeID a, b;
};
TwoMemberMacro makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    TwoMemberMacro m;
    m.a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    m.b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({m.a, m.b});
    m.macroId = editor.groupSelectionIntoMacro();
    return m;
}

juce::MouseEvent makeMouseEvent(juce::Component& comp, juce::Point<float> position, bool mouseWasDragged,
                                juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            mouseWasDragged);
}

} // namespace

// ============================================================================
// Layout geometry — the ONE definition paint(), the jack hit-test and buildVisibleCables()'s
// anchoring all share (macroCardPortLayout).
// ============================================================================

TEST(MacroCardJack, LayoutIsEmptyForAMacroWithNoConfiguredPorts) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto m = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(m.macroId.isEmpty());

    EXPECT_TRUE(editor.macroCardPortLayout(m.macroId).empty())
        << "a macro with no ports yet draws no jacks - just the plain P8-12 card";
}

TEST(MacroCardJack, InputsLayOutDownTheLeftEdgeAndOutputsDownTheRightEdgeInOrderOrder) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto m = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(m.macroId.isEmpty());

    const auto in0 = editor.addMacroPort(m.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                         MacroPortShape::Mono, 1, "In A");
    const auto in1 = editor.addMacroPort(m.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                         MacroPortShape::Mono, 1, "In B");
    const auto out0 = editor.addMacroPort(m.macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV,
                                          MacroPortShape::Mono, 1, "Out A");
    ASSERT_FALSE(in0.isEmpty());
    ASSERT_FALSE(in1.isEmpty());
    ASSERT_FALSE(out0.isEmpty());

    const auto layout = editor.macroCardPortLayout(m.macroId);
    ASSERT_EQ(layout.size(), 3u);

    std::vector<GraphEditor::MacroCardPort> inputs, outputs;
    for (const auto& p : layout)
        (p.isInput ? inputs : outputs).push_back(p);

    ASSERT_EQ(inputs.size(), 2u);
    ASSERT_EQ(outputs.size(), 1u);

    // Every input jack sits at the SAME x (the left edge), every output at the SAME x (the right
    // edge), and the two never coincide - the visual "left side / right side" split a real module
    // card uses.
    EXPECT_EQ(inputs[0].jackPos.x, inputs[1].jackPos.x);
    EXPECT_NE(inputs[0].jackPos.x, outputs[0].jackPos.x);
    EXPECT_LT(inputs[0].jackPos.x, outputs[0].jackPos.x) << "inputs on the left, outputs on the right";

    // In-order: the port added first ("In A") sits above the one added second ("In B") - order
    // 0 vs 1 (nextMacroPortOrder appends).
    EXPECT_EQ(inputs[0].nodeUuid, in0);
    EXPECT_EQ(inputs[1].nodeUuid, in1);
    EXPECT_LT(inputs[0].jackPos.y, inputs[1].jackPos.y);

    // Every jack lands inside the card's fixed footprint - never off the card entirely.
    const auto* card = editor.getMacroCardForTest(m.macroId);
    ASSERT_NE(card, nullptr);
    for (const auto& p : layout)
        EXPECT_TRUE(card->getLocalBounds().contains(p.jackPos)) << p.nodeUuid;
}

// ============================================================================
// Jack hit-test through the REAL mouse path (ModuleComponent::mouseDown/mouseUp)
// ============================================================================

TEST(MacroCardJack, DroppingACableExactlyOnAnExistingInputJackWiresToThatPortNotANewOne) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto m = makeTwoMemberMacro(editor, engine);
    ASSERT_TRUE(editor.getMacros().find(m.macroId)->collapsed);

    const auto portUuid = editor.addMacroPort(m.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                              MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portNodeId = nodeIdForUuid(engine, portUuid);

    auto extOscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* extComp = findComponent(editor, extOscId);
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroCardForTest(m.macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->isVisible());

    const auto layout = editor.macroCardPortLayout(m.macroId);
    ASSERT_EQ(layout.size(), 1u);
    ASSERT_TRUE(layout[0].isInput);
    ASSERT_EQ(layout[0].nodeUuid, portUuid);

    // The screen point a real click would land on, computed from the LIVE card component (not a
    // hand-rolled duplicate of GraphEditor's own math) - card->localPointToGlobal is the exact
    // inverse of the getLocalPoint(nullptr, ...) call endConnectionDrag itself uses.
    const auto targetScreen = card->localPointToGlobal(layout[0].jackPos.toFloat());

    // Real press on the external Oscillator's real audio OUTPUT jack (index 0) - drives
    // ModuleComponent::mouseDown's own getPortForPoint hit-test, exactly as a real drag start
    // would, then a real release at the jack's screen position - drives ModuleComponent::mouseUp,
    // which calls owner.endConnectionDrag(e.getScreenPosition()) exactly as a real drop would.
    const auto pressPos = extComp->getPortCenter(0, /*isInput=*/false).toFloat();
    extComp->mouseDown(makeMouseEvent(*extComp, pressPos, /*mouseWasDragged=*/false, pressPos));

    const auto releaseLocal = extComp->getLocalPoint(nullptr, targetScreen);
    extComp->mouseUp(makeMouseEvent(*extComp, releaseLocal, /*mouseWasDragged=*/true, pressPos));

    auto* macro = editor.getMacros().find(m.macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u) << "landing exactly on the existing jack must not mint a second port";
    EXPECT_TRUE(hasConnection(engine, extOscId, 0, portNodeId, 0))
        << "the external module must be wired straight into the EXISTING port's node";
}

TEST(MacroCardJack, DroppingOnAJackWithTheWrongDirectionIsRefusedAndCreatesNoConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto m = makeTwoMemberMacro(editor, engine);

    // An INPUT port on the card...
    const auto portUuid = editor.addMacroPort(m.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                              MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portNodeId = nodeIdForUuid(engine, portUuid);

    // ...but the drag starts at an external module's INPUT jack, looking for a SOURCE - the macro
    // would need to offer an OUTPUT, and this card has none. The one jack under the cursor is the
    // wrong direction and must be refused outright (§5.3: a mismatched connection is refused, not
    // silently adapted), not fall through to minting a fresh port either.
    auto extFilterId = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 900);
    auto* extComp = findComponent(editor, extFilterId);
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroCardForTest(m.macroId);
    ASSERT_NE(card, nullptr);
    const auto layout = editor.macroCardPortLayout(m.macroId);
    ASSERT_EQ(layout.size(), 1u);
    const auto targetScreen = card->localPointToGlobal(layout[0].jackPos.toFloat());

    const auto pressPos = extComp->getPortCenter(0, /*isInput=*/true).toFloat();
    extComp->mouseDown(makeMouseEvent(*extComp, pressPos, false, pressPos));
    const auto releaseLocal = extComp->getLocalPoint(nullptr, targetScreen);
    extComp->mouseUp(makeMouseEvent(*extComp, releaseLocal, true, pressPos));

    auto* macro = editor.getMacros().find(m.macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u) << "a mismatched jack must not mint a fallback port either";
    EXPECT_FALSE(hasConnection(engine, portNodeId, 0, extFilterId, 0));
    EXPECT_FALSE(hasConnection(engine, extFilterId, 0, portNodeId, 0));
}

// ============================================================================
// buildVisibleCables() anchoring (§5.4): a cable through a PORT anchors at that port's own jack;
// a cable straight to an ordinary interior member (no port involved) keeps the pre-P8-15
// projectToRectEdge treatment, unchanged.
// ============================================================================

TEST(MacroCardJack, BoundaryCableThroughAPortAnchorsAtItsJackWhileAnInteriorMemberCableKeepsTheEdgeProjection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto m = makeTwoMemberMacro(editor, engine); // a = Oscillator, b = Filter, both interior members
    ASSERT_TRUE(editor.getMacros().find(m.macroId)->collapsed);

    // Cable 1: wired straight to the interior Filter's audio input (member 'b'), bypassing any
    // port entirely - the case §5.4 says must keep working exactly as it did before P8-15.
    auto extOscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    editor.connectPorts(extOscId, 0, m.b, 0, /*isMidi=*/false, /*recordUndo=*/false);

    // Cable 2: a real port, connected to a second external module.
    const auto outUuid = editor.addMacroPort(m.macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV,
                                             MacroPortShape::Mono, 1, "Out");
    ASSERT_FALSE(outUuid.isEmpty());
    const auto outNodeId = nodeIdForUuid(engine, outUuid);
    auto extSinkId = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 1100);
    editor.connectPorts(outNodeId, 0, extSinkId, 0, /*isMidi=*/false, /*recordUndo=*/false);

    auto* card = editor.getMacroCardForTest(m.macroId);
    ASSERT_NE(card, nullptr);
    const auto cardBounds = card->getBounds();
    const auto layout = editor.macroCardPortLayout(m.macroId);
    ASSERT_EQ(layout.size(), 1u);
    const auto expectedJackCanvas = (cardBounds.getPosition() + layout[0].jackPos).toFloat();

    const auto& cables = editor.buildVisibleCables();

    bool foundInteriorCable = false, foundPortCable = false;
    for (const auto& cable : cables) {
        if (cable.id.srcUid == extOscId.uid && cable.id.dstUid == m.b.uid) {
            foundInteriorCable = true;
            // projectToRectEdge always lands exactly on the card's rectangle perimeter; a port
            // jack never does (it is inset kMacroCardJackInsetX px from the edge). Distinguish the
            // two treatments by that property rather than duplicating projectToRectEdge's formula.
            const bool onEdge = juce::approximatelyEqual(cable.p2.x, (float)cardBounds.getX()) ||
                                juce::approximatelyEqual(cable.p2.x, (float)cardBounds.getRight()) ||
                                juce::approximatelyEqual(cable.p2.y, (float)cardBounds.getY()) ||
                                juce::approximatelyEqual(cable.p2.y, (float)cardBounds.getBottom());
            EXPECT_TRUE(onEdge) << "an interior-member cable must keep landing exactly on the card's edge, "
                                   "not a port jack: "
                                << cable.p2.toString();
        }
        if (cable.id.srcUid == outNodeId.uid && cable.id.dstUid == extSinkId.uid) {
            foundPortCable = true;
            EXPECT_EQ(cable.p1, expectedJackCanvas)
                << "a cable passing through a port must anchor exactly at that port's own jack";
        }
    }
    EXPECT_TRUE(foundInteriorCable);
    EXPECT_TRUE(foundPortCable);
}

// ============================================================================
// Ungrouping a macro whose port has an external cable landing on it (docs/macros.md §5.4 doc
// duty for T141): confirms the actual behaviour rather than assuming it, per the task spec.
// ============================================================================

TEST(MacroCardJack, UngroupingPreservesAnExternalCableIntoAPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto m = makeTwoMemberMacro(editor, engine);
    ASSERT_TRUE(editor.getMacros().find(m.macroId)->collapsed);

    const auto inUuid =
        editor.addMacroPort(m.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(inUuid.isEmpty());
    const auto portNodeId = nodeIdForUuid(engine, inUuid);

    auto extOscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    editor.connectPorts(extOscId, 0, portNodeId, 0, /*isMidi=*/false, /*recordUndo=*/false);
    ASSERT_TRUE(hasConnection(engine, extOscId, 0, portNodeId, 0));

    // Ungroup by selecting a member and calling the same entry point Cmd+Shift+G drives
    // (GraphEditor::ungroupSelection) — MacroSet::remove only erases the macro RECORD
    // (Source/MacroSet.cpp): it deletes no graph node and no graph connection, so the port's
    // fronting node (portNodeId) stays exactly where it was, still wired.
    editor.setSelectedNodes({m.a});
    editor.ungroupSelection();

    ASSERT_EQ(editor.getMacros().find(m.macroId), nullptr) << "the macro record itself is gone";
    EXPECT_NE(editor.getAudioEngine().getGraph().getNodeForId(portNodeId), nullptr)
        << "the port's own node (MacroInlet) is an ordinary member, not deleted by ungroup";
    EXPECT_TRUE(hasConnection(engine, extOscId, 0, portNodeId, 0))
        << "ungrouping is a presentation-only change (docs/macros.md section 1) - the boundary "
           "cable, a real graph edge, is untouched and is NOT dropped, contrary to a naive "
           "assumption";

    // Now rendered as an ordinary cable to the (no-longer-collapsed, no-longer-hidden) port
    // node's own jack - not the special collapsed-card anchoring, since there is no macro left.
    auto* portComp = findComponent(editor, portNodeId);
    ASSERT_NE(portComp, nullptr);
    EXPECT_TRUE(portComp->isVisible()) << "no macro left to hide it";
}
