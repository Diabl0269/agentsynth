#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"

// Topic: shape change — the load-bearing case (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed):
// delete-node + create-node + rewire as ONE undo step, cable survival/drop by raw channel, MIDI no-op.

// ============================================================================
// Shape change — the load-bearing case
// ============================================================================

TEST(MacroPortFlow, ChangeShapeIsOneUndoStepAndPreservesCablesOnStillActiveChannels) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto memberIds = macro->members; // Oscillator, Filter uuids
    const auto filterId = nodeIdForUuid(engine, memberIds[1]);

    const auto oldUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(oldUuid.isEmpty());
    const auto oldNodeId = nodeIdForUuid(engine, oldUuid);

    // An external node (not a macro member) feeding the port's input, and the port's output
    // feeding an internal member — both on raw channel 0, which stays active under every shape.
    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    engine.getGraph().addConnection({{extOsc, 0}, {oldNodeId, 0}});
    engine.getGraph().addConnection({{oldNodeId, 0}, {filterId, 0}});
    ASSERT_TRUE(hasConnection(engine, extOsc, 0, oldNodeId, 0));
    ASSERT_TRUE(hasConnection(engine, oldNodeId, 0, filterId, 0));

    const int nodesBefore = engine.getGraph().getNodes().size();

    const auto newUuid = editor.getMacroController().changeMacroPortShape(macroId, oldUuid, MacroPortShape::Stereo, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    EXPECT_NE(newUuid, oldUuid);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore); // delete + create: net zero nodes
    EXPECT_TRUE(nodeIdForUuid(engine, oldUuid).uid == 0) << "old node is gone";

    auto* macroAfter = editor.getMacros().find(macroId);
    ASSERT_EQ(macroAfter->ports.size(), 1u);
    EXPECT_EQ(macroAfter->ports[0].nodeUuid, newUuid);
    EXPECT_EQ(macroAfter->ports[0].name, "Pitch In"); // identity preserved across the shape change
    EXPECT_TRUE(macroAfter->ports[0].isInput);
    EXPECT_TRUE(macroAfter->hasMember(newUuid));
    EXPECT_FALSE(macroAfter->hasMember(oldUuid));

    const auto newNodeId = nodeIdForUuid(engine, newUuid);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, newNodeId, 0)) << "external cable replayed on ch0";
    EXPECT_TRUE(hasConnection(engine, newNodeId, 0, filterId, 0)) << "internal cable replayed on ch0";

    // ---- The load-bearing assertion: ONE undo restores BOTH the graph and the macro together. ----
    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_TRUE(nodeIdForUuid(engine, newUuid).uid == 0) << "the shape-changed node is gone after undo";
    const auto restoredNodeId = nodeIdForUuid(engine, oldUuid);
    EXPECT_FALSE(restoredNodeId.uid == 0) << "the original node uuid is back";

    auto* macroRestored = editor.getMacros().find(macroId);
    ASSERT_EQ(macroRestored->ports.size(), 1u);
    EXPECT_EQ(macroRestored->ports[0].nodeUuid, oldUuid);
    EXPECT_EQ(macroRestored->ports[0].name, "Pitch In");

    const auto restoredFilterId = nodeIdForUuid(engine, memberIds[1]);
    const auto restoredExtOsc = extOsc; // graph I/O node ids for pre-existing nodes are unaffected by undo here
    EXPECT_TRUE(hasConnection(engine, restoredExtOsc, 0, restoredNodeId, 0)) << "external cable restored";
    EXPECT_TRUE(hasConnection(engine, restoredNodeId, 0, restoredFilterId, 0)) << "internal cable restored";
}

TEST(MacroPortFlow, ChangeShapeDropsACableOnARawChannelTheNewShapeNoLongerExposes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Stereo, 1, "Stereo In");
    const auto nodeId = nodeIdForUuid(engine, uuid);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    // Wired onto the Stereo shape's RIGHT leg (kRightBase), which Mono does not expose.
    engine.getGraph().addConnection({{extOsc, 0}, {nodeId, MacroInletModule::kRightBase}});
    ASSERT_TRUE(hasConnection(engine, extOsc, 0, nodeId, MacroInletModule::kRightBase));

    const auto newUuid = editor.getMacroController().changeMacroPortShape(macroId, uuid, MacroPortShape::Mono, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    const auto newNodeId = nodeIdForUuid(engine, newUuid);

    for (const auto& c : engine.getGraph().getConnections())
        EXPECT_FALSE(c.destination.nodeID == newNodeId && c.destination.channelIndex == MacroInletModule::kRightBase)
            << "a raw channel Mono no longer exposes must not carry a replayed cable";
}

TEST(MacroPortFlow, ChangeShapeIsANoOpForAMidiPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::Midi,
                                                               MacroPortShape::Mono, 1, "");

    const auto result = editor.getMacroController().changeMacroPortShape(macroId, uuid, MacroPortShape::Stereo, 1);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_FALSE(nodeIdForUuid(engine, uuid).uid == 0) << "the original MIDI node is untouched";
}

// ============================================================================
// Changing an audio port from Mono to Stereo (or
// StereoCollapsed) must auto-wire the NEW raw channel, reusing the same rule the Dual I/O toggle
// applies when an ordinary module grows a right leg -- pair with the peer's own right leg when it
// has one, else sum into the same mono jack it already exposes.
// ============================================================================

// Mono -> Stereo (two SEPARATE jacks), peer has its own real right leg: Oscillator and Filter are
// both DECLARED split-block stereo pairs (StereoAudio::Declared -- see their own ctor comments)
// and default to Dual I/O ON when constructed directly like this, exactly like a real canvas drop
// with the app's own default toggle left alone. The new right leg must pair L->L / R->R with each
// peer's own kRightBase, not sum into channel 0.
TEST(MacroPortFlow, ChangeShapeMonoToStereoPairsTheRightLegWithThePeersOwnRightLeg) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto filterId = nodeIdForUuid(engine, macro->members[1]);

    const auto oldUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto oldNodeId = nodeIdForUuid(engine, oldUuid);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    engine.getGraph().addConnection({{extOsc, 0}, {oldNodeId, 0}});
    engine.getGraph().addConnection({{oldNodeId, 0}, {filterId, 0}});

    const auto newUuid = editor.getMacroController().changeMacroPortShape(macroId, oldUuid, MacroPortShape::Stereo, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    const auto newNodeId = nodeIdForUuid(engine, newUuid);

    EXPECT_TRUE(hasConnection(engine, extOsc, 0, newNodeId, 0)) << "left leg replayed as before";
    EXPECT_TRUE(hasConnection(engine, newNodeId, 0, filterId, 0));
    EXPECT_TRUE(hasConnection(engine, extOsc, OscillatorModule::kRightBase, newNodeId, MacroInletModule::kRightBase))
        << "the new right leg must be auto-wired, paired with the peer's own real right leg";
    EXPECT_TRUE(hasConnection(engine, newNodeId, MacroInletModule::kRightBase, filterId, FilterModule::kRightBase))
        << "same rule on the outgoing side";
}

// A local stand-in for a peer with NO second audio channel at all (unlike Oscillator/Filter above,
// which are declared split pairs) -- the case reachablePeerRightAudioLeg's fallback exists for.
namespace {
class TestGenuinelyMonoModule : public ModuleBase {
public:
    TestGenuinelyMonoModule()
        : ModuleBase("TestGenuinelyMono", 1, 1) {}
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
};
} // namespace

// Mono -> Stereo, peer has NO second audio channel of its own: the new right leg must be summed
// into the SAME channel the left leg already uses on that peer, rather than left dangling.
TEST(MacroPortFlow, ChangeShapeMonoToStereoSumsTheRightLegIntoAGenuinelyMonoPeersOwnChannel) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto filterId = nodeIdForUuid(engine, macro->members[1]);

    const auto oldUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto oldNodeId = nodeIdForUuid(engine, oldUuid);

    auto extMono = addModuleAt(editor, engine, std::make_unique<TestGenuinelyMonoModule>(), 900, 900);
    engine.getGraph().addConnection({{extMono, 0}, {oldNodeId, 0}});
    engine.getGraph().addConnection({{oldNodeId, 0}, {filterId, 0}});

    const auto newUuid = editor.getMacroController().changeMacroPortShape(macroId, oldUuid, MacroPortShape::Stereo, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    const auto newNodeId = nodeIdForUuid(engine, newUuid);

    EXPECT_TRUE(hasConnection(engine, extMono, 0, newNodeId, 0)) << "left leg replayed as before";
    EXPECT_TRUE(hasConnection(engine, extMono, 0, newNodeId, MacroInletModule::kRightBase))
        << "the new right leg must be auto-wired, not left dangling -- summed into extMono's own "
           "ch0 since it has no separate right leg of its own";
}

// Mono -> StereoCollapsed (ONE jack, two raw legs): the same external cable must fan onto BOTH raw
// legs, exactly like a collapsed FX jack's own dual-raw-leg fan -- never a second, separately
// wired jack.
TEST(MacroPortFlow, ChangeShapeMonoToStereoCollapsedFansTheSameCableOntoBothRawLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto filterId = nodeIdForUuid(engine, macro->members[1]);

    const auto oldUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out");
    const auto oldNodeId = nodeIdForUuid(engine, oldUuid);

    auto extVca = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    engine.getGraph().addConnection({{filterId, 0}, {oldNodeId, 0}});
    engine.getGraph().addConnection({{oldNodeId, 0}, {extVca, 0}});

    const auto newUuid =
        editor.getMacroController().changeMacroPortShape(macroId, oldUuid, MacroPortShape::StereoCollapsed, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    const auto newNodeId = nodeIdForUuid(engine, newUuid);

    EXPECT_TRUE(hasConnection(engine, filterId, 0, newNodeId, 0)) << "raw ch0 replayed as before";
    EXPECT_TRUE(hasConnection(engine, newNodeId, 0, extVca, 0));
    EXPECT_TRUE(hasConnection(engine, filterId, 0, newNodeId, 1))
        << "the SAME cable must also fan onto raw ch1 -- one jack, two raw legs, not a second jack";
    EXPECT_TRUE(hasConnection(engine, newNodeId, 1, extVca, 0));
}

// Undo: growing the right leg is part of the SAME one-undo-step shape change, not a second
// mutation that could survive a single Cmd+Z.
TEST(MacroPortFlow, ChangeShapeMonoToStereoAutoWireIsPartOfTheSameOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto filterId = nodeIdForUuid(engine, macro->members[1]);

    const auto oldUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto oldNodeId = nodeIdForUuid(engine, oldUuid);
    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    engine.getGraph().addConnection({{extOsc, 0}, {oldNodeId, 0}});
    engine.getGraph().addConnection({{oldNodeId, 0}, {filterId, 0}});
    undo.clearUndoHistory();

    const auto newUuid = editor.getMacroController().changeMacroPortShape(macroId, oldUuid, MacroPortShape::Stereo, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    const auto newNodeId = nodeIdForUuid(engine, newUuid);
    ASSERT_TRUE(hasConnection(engine, extOsc, OscillatorModule::kRightBase, newNodeId, MacroInletModule::kRightBase))
        << "sanity: the auto-wire really happened";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_TRUE(nodeIdForUuid(engine, newUuid).uid == 0) << "the stereo node, right-leg wiring included, is gone";
    const auto restoredNodeId = nodeIdForUuid(engine, oldUuid);
    ASSERT_FALSE(restoredNodeId.uid == 0);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, restoredNodeId, 0)) << "the original Mono wiring is back";
    for (const auto& c : engine.getGraph().getConnections())
        EXPECT_FALSE(c.destination.nodeID == restoredNodeId &&
                     c.destination.channelIndex == MacroInletModule::kRightBase)
            << "undo must not leave a right-leg cable dangling on the restored Mono node";

    undo.redo();
    const auto redoneNodeId = nodeIdForUuid(engine, newUuid);
    ASSERT_FALSE(redoneNodeId.uid == 0);
    EXPECT_TRUE(hasConnection(engine, extOsc, OscillatorModule::kRightBase, redoneNodeId, MacroInletModule::kRightBase))
        << "redo restores the auto-wired right leg together with the rest of the shape change";
}
