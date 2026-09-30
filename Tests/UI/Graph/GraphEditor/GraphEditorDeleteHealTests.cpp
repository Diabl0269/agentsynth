// "Reconnect the chain": deleting a module that has exactly one incoming and one outgoing
// audio cable splices its surviving neighbours together. GraphEditor::captureHealSplices()/
// healDeletedChain() (GraphEditorDeleteHeal.cpp) are exercised only indirectly here, through the
// real user-facing delete entry points (deleteSelection/requestDeleteModule) — same discipline as
// MacroAutoPortDeleteTests.cpp. Shared GraphEditorTest fixture/helpers live in
// GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MacroPortShape.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

// A plain mono module: 1 audio in, 1 audio out. ModuleBase's default mapInputChannel/
// mapOutputChannel leaves an unclassified channel at PortRole::Other -- exactly what a simple
// module's one unlabeled audio jack is (GraphEditorInternal.h's collectSmartAudioLegs treats
// PortRole::Other as audio too, for the same reason) -- so this needs no override to read as one
// audio leg in, one audio leg out.
class HealTestMonoModule : public ModuleBase {
public:
    explicit HealTestMonoModule(const juce::String& name)
        : ModuleBase(name, 1, 1) {
        setModuleName(name);
    }
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
};

// Two audio inputs, kept as two SEPARATE visible jacks (ModuleBase's default vis == total input
// channel count, with no stereo-pair collapsing declared) -- a module with more audio legs
// deletes unhealed: this must never be heal-eligible.
class HealTestTwoAudioInModule : public ModuleBase {
public:
    explicit HealTestTwoAudioInModule(const juce::String& name)
        : ModuleBase(name, 2, 1) {
        setModuleName(name);
    }
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
};

// One audio input (ch0, PortRole::Other) plus one ModCV input (ch1) -- so a real modulation cable
// can land on this module WITHOUT it losing its "exactly one audio leg" eligibility. Verifies the
// mod cable is simply dropped alongside the module rather than blocking the heal.
class HealTestMonoWithCvModule : public ModuleBase {
public:
    explicit HealTestMonoWithCvModule(const juce::String& name)
        : ModuleBase(name, 2, 1) {
        setModuleName(name);
    }
    void prepareToPlay(double, int) override {}
    void processModuleBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Math; }
    int getVisibleInputPortCount() const override { return 2; }
    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        p.isPolyGroupHead = true;
        p.polyVoiceSpan = 1;
        if (raw == 0) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Other;
        } else if (raw == 1) {
            p.visibleJackIndex = 1;
            p.role = PortRole::ModCV;
        }
        return p;
    }
};

NodeID addHealNode(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

int monoConnectionCount(AudioEngine& engine, NodeID a, NodeID b) {
    return countAudioConnectionsBetween(engine.getGraph(), a, b);
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

// Undo/redo restores a removed node through AIStateMapper's graph-snapshot apply, which can hand
// it back a fresh NodeID even when it preserves the underlying node (its "uuid" property, set by
// addHealNode, is the one identity that survives the round-trip) -- same idiom as
// MacroAutoPortTestHelpers.h's nodeIdForUuid.
NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

} // namespace

// ============================================================================
// The headline case: Osc -> Filter -> Distortion -> Output, delete the middle module.
// Real FX modules (2 audio in/2 audio out, collapsed to one stereo jack by default) stand in for
// Filter/Distortion so the healed cable is a genuine stereo pair covering both raw legs, not a
// mono stand-in -- "a stereo pair L+R between the same two jacks counts as one cable".
// ============================================================================

TEST_F(GraphEditorTest, DeletingTheMiddleModuleHealsTheStereoChain) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto osc = addHealNode(editor, engine, std::make_unique<ChorusModule>(), 0, 0);         // upstream
    auto filter = addHealNode(editor, engine, std::make_unique<ReverbModule>(), 400, 0);    // deleted
    auto distortion = addHealNode(editor, engine, std::make_unique<DelayModule>(), 800, 0); // downstream
    sizeModuleComponents(editor);

    editor.connectPorts(osc, 0, filter, 0, false, false);
    editor.connectPorts(filter, 0, distortion, 0, false, false);
    ASSERT_EQ(monoConnectionCount(engine, osc, filter), 2) << "collapsed stereo pair, both raw legs";
    ASSERT_EQ(monoConnectionCount(engine, filter, distortion), 2);
    ASSERT_EQ(monoConnectionCount(engine, osc, distortion), 0);

    editor.requestDeleteModule(filter);

    EXPECT_EQ(engine.getGraph().getNodeForId(filter), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, osc, distortion), 2) << "L->L and R->R, healed as one stereo cable";
}

// ============================================================================
// A mod/CV cable on the deleted module is simply dropped; the audio heal still happens.
// ============================================================================

TEST_F(GraphEditorTest, ModCableOnDeletedModuleIsDroppedAndAudioStillHeals) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto a = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("A"), 0, 0);
    auto b = addHealNode(editor, engine, std::make_unique<HealTestMonoWithCvModule>("B"), 400, 0);
    auto c = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("C"), 800, 0);
    auto modSource = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Mod"), 400, 400);
    sizeModuleComponents(editor);

    editor.connectPorts(a, 0, b, 0, false, false);
    editor.connectPorts(b, 0, c, 0, false, false);
    engine.getGraph().addConnection({{modSource, 0}, {b, 1}}); // B's ModCV jack, not audio
    ASSERT_TRUE(engine.getGraph().isConnected({{modSource, 0}, {b, 1}}));

    editor.requestDeleteModule(b);

    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
    EXPECT_FALSE(engine.getGraph().isConnected({{modSource, 0}, {b, 1}})) << "dropped along with B";
    EXPECT_EQ(monoConnectionCount(engine, a, c), 1) << "the audio chain still heals";
}

// ============================================================================
// More than one audio leg on a side (a splitter/mixer-shaped module) never heals.
// ============================================================================

TEST_F(GraphEditorTest, TwoAudioInputsNeverHeals) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto a = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("A"), 0, 0);
    auto other = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Other"), 0, 400);
    auto mixer = addHealNode(editor, engine, std::make_unique<HealTestTwoAudioInModule>("Mixer"), 400, 200);
    auto c = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("C"), 800, 200);
    sizeModuleComponents(editor);

    editor.connectPorts(a, 0, mixer, 0, false, false);
    editor.connectPorts(other, 0, mixer, 1, false, false);
    editor.connectPorts(mixer, 0, c, 0, false, false);

    editor.requestDeleteModule(mixer);

    EXPECT_EQ(engine.getGraph().getNodeForId(mixer), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, a, c), 0) << "two audio inputs -- deletes as today, no heal";
    EXPECT_EQ(monoConnectionCount(engine, other, c), 0);
}

// ============================================================================
// The preference gate.
// ============================================================================

TEST_F(GraphEditorTest, PreferenceOffLeavesTheChainBroken) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setReconnectChainOnDeleteEnabled(false);

    auto a = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("A"), 0, 0);
    auto b = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("B"), 400, 0);
    auto c = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("C"), 800, 0);
    sizeModuleComponents(editor);

    editor.connectPorts(a, 0, b, 0, false, false);
    editor.connectPorts(b, 0, c, 0, false, false);

    editor.requestDeleteModule(b);

    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, a, c), 0) << "preference off -- the chain stays broken";
}

// ============================================================================
// One undo restores the module and its original cables and removes the heal edge; redo re-applies.
// ============================================================================

TEST_F(GraphEditorTest, OneUndoRestoresTheModuleAndRemovesTheHealEdgeAndRedoReapplies) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 900);

    // Real, factory-registered module types: undo/redo restores a removed node through
    // AIStateMapper's graph-snapshot apply, which reconstructs it by its registered type name --
    // an unregistered test-only ModuleBase subclass (used by the other tests in this file, which
    // never cross an undo/redo boundary) cannot come back that way.
    auto a = addHealNode(editor, engine, std::make_unique<ChorusModule>(), 0, 0);
    auto b = addHealNode(editor, engine, std::make_unique<ReverbModule>(), 400, 0);
    auto c = addHealNode(editor, engine, std::make_unique<DelayModule>(), 800, 0);
    sizeModuleComponents(editor);
    const auto aUuid = uuidOf(engine, a);
    const auto bUuid = uuidOf(engine, b);
    const auto cUuid = uuidOf(engine, c);

    editor.connectPorts(a, 0, b, 0, false, false);
    editor.connectPorts(b, 0, c, 0, false, false);
    const int nodesBefore = engine.getGraph().getNumNodes();

    editor.requestDeleteModule(b);
    ASSERT_EQ(engine.getGraph().getNumNodes(), nodesBefore - 1);
    ASSERT_EQ(monoConnectionCount(engine, a, c), 2);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore) << "B is back";
    auto restoredA = nodeIdForUuid(engine, aUuid);
    auto restoredB = nodeIdForUuid(engine, bUuid);
    auto restoredC = nodeIdForUuid(engine, cUuid);
    ASSERT_TRUE(restoredB.uid != 0);
    EXPECT_EQ(monoConnectionCount(engine, restoredA, restoredB), 2) << "the original A->B cable is back";
    EXPECT_EQ(monoConnectionCount(engine, restoredB, restoredC), 2) << "the original B->C cable is back";
    EXPECT_EQ(monoConnectionCount(engine, restoredA, restoredC), 0) << "the heal edge is gone with the undo";

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore - 1) << "B is deleted again";
    auto redoneA = nodeIdForUuid(engine, aUuid);
    auto redoneC = nodeIdForUuid(engine, cUuid);
    EXPECT_EQ(monoConnectionCount(engine, redoneA, redoneC), 2) << "the heal is re-applied";
}

// ============================================================================
// A heal that would create an invalid connection (a cycle) is skipped.
// ============================================================================

TEST_F(GraphEditorTest, HealSkippedWhenItWouldCreateACycle) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto x = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("X"), 0, 0);
    auto b = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("B"), 400, 0);
    auto y = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Y"), 800, 0);
    sizeModuleComponents(editor);

    editor.connectPorts(x, 0, b, 0, false, false);
    editor.connectPorts(b, 0, y, 0, false, false);
    // An existing, unrelated feedback edge the other way: healing X->Y directly would close a
    // cycle (X->Y->X), which AudioProcessorGraph::isAnInputTo refuses -- same check a manual
    // cable drag is subject to.
    engine.getGraph().addConnection({{y, 0}, {x, 0}});
    ASSERT_TRUE(engine.getGraph().isConnected({{y, 0}, {x, 0}}));

    editor.requestDeleteModule(b);

    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
    EXPECT_FALSE(engine.getGraph().isConnected({{x, 0}, {y, 0}})) << "the would-be cycle is never wired";
    EXPECT_TRUE(engine.getGraph().isConnected({{y, 0}, {x, 0}})) << "the pre-existing feedback edge is untouched";
}

// ============================================================================
// A run of several deleted modules in one selection heals from end to end.
// ============================================================================

TEST_F(GraphEditorTest, DeletingTheMiddleTwoOfAFourChainHealsAcrossTheWholeRun) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto w = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("W"), 0, 0);
    auto x = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("X"), 400, 0);
    auto y = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Y"), 800, 0);
    auto z = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Z"), 1200, 0);
    sizeModuleComponents(editor);

    editor.connectPorts(w, 0, x, 0, false, false);
    editor.connectPorts(x, 0, y, 0, false, false);
    editor.connectPorts(y, 0, z, 0, false, false);

    editor.setSelectedNodes({x, y});
    editor.deleteSelection();

    EXPECT_EQ(engine.getGraph().getNodeForId(x), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(y), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, w, z), 1) << "the whole deleted run heals from W straight to Z";
}

// ============================================================================
// Composes with macro ports: a surviving macro port on one side just gets the healed cable.
// ============================================================================

TEST_F(GraphEditorTest, HealsAcrossASurvivingMacroPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto member = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Member"), 400, 100);
    // groupSelectionIntoMacro() refuses a single-module selection, so a second, otherwise
    // unrelated member joins the macro purely to satisfy that -- it plays no part in the chain.
    auto filler = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Filler"), 400, 300);
    editor.setSelectedNodes({member, filler});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    NodeID portId;
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == portUuid)
            portId = node->nodeID;
    ASSERT_TRUE(portId.uid != 0);

    auto b = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("B"), 800, 100);
    auto c = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("C"), 1200, 100);
    sizeModuleComponents(editor);

    editor.connectPorts(portId, 0, b, 0, false, false);
    editor.connectPorts(b, 0, c, 0, false, false);

    editor.requestDeleteModule(b);

    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, portId, c), 1) << "the port survives and gets the healed cable directly";
}

// Deleting the macro port NODE ITSELF, not a neighbour of it, is NOT this heal's call: that is
// the macro-port splice's call, with its own preference (spliceCableOnMacroPortDelete, default
// OFF -- see MacroPortContextMenu.DeleteFromTheMenuDropsTheCableByDefault,
// Tests/Macros/MacroPortWidgetTests.cpp). With this heal ON (the default) and the splice
// preference at its own default OFF, deleting the port must still drop the cable, not splice it --
// a port anywhere in a deleted run is excluded from this heal at every hop.
TEST_F(GraphEditorTest, ReconnectChainNeverHealsThroughADeletedMacroPortNodeItself) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    ASSERT_TRUE(editor.getReconnectChainOnDeleteEnabled()) << "sanity: this heal defaults on";
    ASSERT_FALSE(editor.getSpliceCableOnMacroPortDeleteEnabled()) << "sanity: FRO235's own splice defaults off";

    auto member = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Member"), 400, 100);
    auto filler = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Filler"), 400, 300);
    editor.setSelectedNodes({member, filler});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);

    auto ext = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Ext"), 0, 100);
    auto b = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("B"), 800, 100);
    sizeModuleComponents(editor);

    editor.connectPorts(ext, 0, portId, 0, false, false);
    editor.connectPorts(portId, 0, b, 0, false, false);

    editor.requestDeleteModule(portId);

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, ext, b), 0) << "the port's own deletion drops the cable, never heals it";
}

// ============================================================================
// The ticket's literal illustration, with real modules: Osc -> Filter -> Distortion -> a BARE
// Audio Output node. Osc and Filter are split-block modules (Audio R lives on its own far block,
// docs/modules/fx-modules.md#the-toggle-is-inherited-not-registered) -- collapsed (Dual I/O off,
// as here) their one "Audio" jack is genuinely MONO, unlike an auto-derived FX shape's collapsed
// jack (Chorus/Reverb/Delay above), which owns both raw legs. So Osc->Filter and Filter->Distortion
// are one raw connection each here, not two -- still exactly one audio leg in, one out, which is
// all the heal rule requires. Exercises the split-block LogicalPort mapping (mapAudioLeg's
// mono-only collapsed branch) neither the earlier FX-module tests nor the synthetic HealTest*
// modules touch.
// ============================================================================

namespace {
struct OscFilterDistortionOutFixture {
    NodeID osc, filter, distortion, out;
};

OscFilterDistortionOutFixture makeOscFilterDistortionOutFixture(GraphEditor& editor, AudioEngine& engine) {
    auto& graph = engine.getGraph();
    OscFilterDistortionOutFixture f;

    // Output first: AudioGraphIOProcessor snapshots the channel layout in setParentGraph, so
    // addAudioOutputNode's setPlayConfigDetails must run before it is added (see its own doc
    // comment) -- same ordering makeWiredSink uses.
    auto outNode = addAudioOutputNode(graph, 1200, 0);
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto distortionNode = graph.addNode(std::make_unique<DistortionModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    oscNode->properties.set("x", 0);
    oscNode->properties.set("y", 0);
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 0);
    distortionNode->properties.set("x", 800);
    distortionNode->properties.set("y", 0);
    editor.updateComponents();
    sizeModuleComponents(editor);

    f.osc = oscNode->nodeID;
    f.filter = filterNode->nodeID;
    f.distortion = distortionNode->nodeID;
    f.out = outNode->nodeID;

    editor.connectPorts(f.osc, 0, f.filter, 0, false, false);
    editor.connectPorts(f.filter, 0, f.distortion, 0, false, false);
    editor.connectPorts(f.distortion, 0, f.out, 0, false, false);
    return f;
}
} // namespace

TEST_F(GraphEditorTest, DeletingFilterInARealOscFilterDistortionOutputChainHeals) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    const auto f = makeOscFilterDistortionOutFixture(editor, engine);
    ASSERT_EQ(monoConnectionCount(engine, f.osc, f.filter), 1) << "collapsed split-block modules: mono, not stereo";
    // Distortion is an auto-derived FX shape: collapsed (Dual I/O off), its input jack 0 is a real
    // stereo PAIR (DistortionModule::mapInputChannel -> mapStereoPairInput(raw, 2)), not mono like
    // Filter's own collapsed jack. Filter's mono output landing on that one visible jack broadcasts
    // onto both raw legs (resolvePolyLink's mono-into-collapsed-stereo-pair branch -- the same thing
    // a manual cable drag would do), hence 2 raw connections for what is still ONE logical/visible
    // cable -- exactly the "count at the visible-jack level" the heal's eligibility rule uses.
    ASSERT_EQ(monoConnectionCount(engine, f.filter, f.distortion), 2)
        << "mono Filter broadcast onto Distortion's collapsed stereo input pair";

    editor.requestDeleteModule(f.filter);

    EXPECT_EQ(engine.getGraph().getNodeForId(f.filter), nullptr);
    // Osc heals straight to Distortion with the identical broadcast semantics Filter's own cable
    // had -- still one logical cable, still 2 raw legs.
    EXPECT_EQ(monoConnectionCount(engine, f.osc, f.distortion), 2) << "Osc heals straight to Distortion";
}

// Distortion's own connection into the bare Audio Output node LOOKS like two raw legs, but they
// land on Output's matching channel pair (raw 0 and 1) off the SAME source jack -- still one
// logical cable: the "delete the last effect before Audio Output" case, which must not lose sound
// (a bare I/O node has no LogicalPort collapsing of its own, ModuleComponentLayout.cpp's
// getContentTopY reads its RAW getTotalNumOutputChannels(), so its jack-painting/hit-testing
// genuinely treats each raw channel as its own visible cable -- GraphEditorCables.cpp's
// rebuildVisibleCables falls back to `dstJack = connection.destination.channelIndex` -- but this
// heal's OWN eligibility count pairs raw 0/1 off one source jack back into a single leg,
// mergeBareIoChannelPairs in GraphEditorDeleteHeal.cpp). Deleting Distortion therefore DOES heal:
// Filter -> Audio Output directly. Filter's own collapsed jack is genuinely mono (span 1, confirmed
// by osc->filter above), so the healed cable lands on Output's Left alone -- one leg, exactly what
// a manual mono cable into that jack would draw. No mono-broadcast second leg is added; render-time
// L/Mono normalling covers it instead: Right borrows Left for as long as Right stays unpatched, so
// the heal is audible in both ears without a second EDGE (see Tests/Engine/NormallingTests.cpp for
// the audible proof through a real AudioEngine render).
TEST_F(GraphEditorTest, DeletingTheLastEffectBeforeAudioOutputHealsToASingleLeftLeg) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    const auto f = makeOscFilterDistortionOutFixture(editor, engine);
    ASSERT_EQ(monoConnectionCount(engine, f.distortion, f.out), 2)
        << "two raw legs off Distortion's one collapsed output jack, into Output's matching pair";

    editor.requestDeleteModule(f.distortion);

    EXPECT_EQ(engine.getGraph().getNodeForId(f.distortion), nullptr);
    EXPECT_EQ(monoConnectionCount(engine, f.filter, f.out), 1)
        << "Filter heals straight to Audio Output on Left alone -- normalling, not a broadcast edge, fills Right";
}

// Two DIFFERENT destination nodes, even sitting on the same matching raw-channel numbers, are NOT
// one leg: the pairing above only merges legs that share the SAME peer node. A splitter/mixer-style
// module (more than one distinct downstream) still deletes unhealed, like any module with more
// audio legs (mixer, splitters).
TEST_F(GraphEditorTest, TwoDifferentDestinationNodesOnMatchingChannelsStillCountAsTwoLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto source = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Source"), 0, 100);
    auto middle = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("Middle"), 400, 100);
    auto destA = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("DestA"), 800, 0);
    auto destB = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("DestB"), 800, 300);
    sizeModuleComponents(editor);

    editor.connectPorts(source, 0, middle, 0, false, false);
    editor.connectPorts(middle, 0, destA, 0, false, false);
    editor.connectPorts(middle, 0, destB, 0, false, false);
    ASSERT_EQ(monoConnectionCount(engine, middle, destA), 1);
    ASSERT_EQ(monoConnectionCount(engine, middle, destB), 1);

    editor.requestDeleteModule(middle);

    EXPECT_EQ(engine.getGraph().getNodeForId(middle), nullptr) << "deletes as today -- just without a heal";
    EXPECT_EQ(monoConnectionCount(engine, source, destA), 0) << "two distinct downstreams: not heal-eligible";
    EXPECT_EQ(monoConnectionCount(engine, source, destB), 0);
}

// ============================================================================
// A module with Dual I/O split into two separate jacks per side counts as two audio legs on that
// side -- same "more audio legs... deletes as today" rule TwoAudioInputsNeverHeals exercises with
// a synthetic module, here with a real split-block module's actual Left/Right jacks.
// ============================================================================

TEST_F(GraphEditorTest, DualIOSplitInputLegsCountAsTwoAndNeverHeals) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);

    auto& graph = engine.getGraph();
    auto leftSrc = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("LeftSrc"), 0, 0);
    auto rightSrc = addHealNode(editor, engine, std::make_unique<HealTestMonoModule>("RightSrc"), 0, 300);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>()); // Dual I/O ON by default: Left/Right split
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 150);
    editor.updateComponents();
    sizeModuleComponents(editor);
    const auto filter = filterNode->nodeID;
    const int nodesBefore = graph.getNumNodes();

    editor.connectPorts(leftSrc, 0, filter, 0, false, false);  // Filter's Left In jack
    editor.connectPorts(rightSrc, 0, filter, 1, false, false); // Filter's Right In jack -- a second, distinct leg

    editor.requestDeleteModule(filter);

    EXPECT_EQ(graph.getNodeForId(filter), nullptr);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore - 1);
    EXPECT_EQ(monoConnectionCount(engine, leftSrc, rightSrc), 0) << "two incoming legs -- never heals";
}

// ============================================================================
// The orchestrator's follow-up: the DEFAULT PATCH's own shape, reproduced by loading it for real
// (synth::PresetManager::loadDefaultPreset) rather than approximating it -- Oscillator -> Filter ->
// VCA, all three shipping Dual I/O ON (their constructor default for a Declared stereo shape, never
// collapsed by anything a bare preset load runs), so Filter has TWO incoming legs (Osc L->Filter L,
// Osc R->Filter R) and TWO outgoing legs (Filter L->VCA L, Filter R->VCA R) rather than one of each.
// mergeDualIoStereoPairLegs (GraphEditorDeleteHeal.cpp) is what turns each matching L/R pair back
// into one logical leg so the 1-in/1-out rule still applies and the chain heals: Osc L->VCA L,
// Osc R->VCA R, healed as two explicit jack pairs since resolvePolyLink has no single-jack notion
// of "two separate jacks" the way a collapsed stereo jack's own multi-voice fan does.
// ============================================================================

namespace {
struct DefaultPatchOscFilterVca {
    NodeID osc, filter, vca;
};

// Bundled presets carry only an "id" in their JSON, never a "uuid" -- AIStateMapper's
// adoptUuidIfTrusted only adopts one when the node object actually has one, so a preset-loaded
// node's "uuid" property stays empty and this file's own uuidOf/nodeIdForUuid helpers (built for
// addHealNode's synthetic nodes, which DO set one) cannot track identity across this fixture's
// undo/redo. Re-scanning by module type after the fact works just as well here: Osc/Filter/VCA are
// each singletons in the default patch, and a plain requestDeleteModule/deleteSelection (no
// undo/redo involved yet) never reassigns a SURVIVING node's own NodeID anyway.
DefaultPatchOscFilterVca findOscFilterVca(juce::AudioProcessorGraph& graph) {
    DefaultPatchOscFilterVca f;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()) != nullptr)
            f.osc = node->nodeID;
        else if (dynamic_cast<FilterModule*>(node->getProcessor()) != nullptr)
            f.filter = node->nodeID;
        else if (dynamic_cast<VCAModule*>(node->getProcessor()) != nullptr)
            f.vca = node->nodeID;
    }
    return f;
}

DefaultPatchOscFilterVca loadDefaultPatchOscFilterVca(GraphEditor& editor, AudioEngine& engine) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::PresetManager::loadDefaultPreset(graph);
    editor.updateComponents();
    sizeModuleComponents(editor);
    return findOscFilterVca(graph);
}

// Asserts the default patch really does ship the shape this whole test class relies on: Osc and
// VCA both genuinely Dual I/O split, and Filter wired to both with a full L/R pair on each side --
// the premise "Filter has 2 incoming audio legs... and 2 outgoing" the fix is built to still heal.
void assertDefaultPatchIsFullyDualIOWired(AudioEngine& engine, const DefaultPatchOscFilterVca& f) {
    auto& graph = engine.getGraph();
    ASSERT_TRUE(f.osc.uid != 0 && f.filter.uid != 0 && f.vca.uid != 0) << "default patch missing an expected module";
    auto* oscMb = dynamic_cast<ModuleBase*>(graph.getNodeForId(f.osc)->getProcessor());
    auto* filterMb = dynamic_cast<ModuleBase*>(graph.getNodeForId(f.filter)->getProcessor());
    auto* vcaMb = dynamic_cast<ModuleBase*>(graph.getNodeForId(f.vca)->getProcessor());
    ASSERT_TRUE(oscMb->isDualIO() && filterMb->isDualIO() && vcaMb->isDualIO())
        << "sanity: the default patch ships every stereo module Dual I/O ON";
    ASSERT_TRUE(graph.isConnected({{f.osc, 0}, {f.filter, 0}})) << "Osc L -> Filter L";
    ASSERT_TRUE(
        graph.isConnected({{f.osc, oscMb->rightAudioLegChannel()}, {f.filter, filterMb->rightAudioLegChannel()}}))
        << "Osc R -> Filter R";
    ASSERT_TRUE(graph.isConnected({{f.filter, 0}, {f.vca, 0}})) << "Filter L -> VCA L";
    ASSERT_TRUE(
        graph.isConnected({{f.filter, filterMb->rightAudioLegChannel()}, {f.vca, vcaMb->rightAudioLegChannel()}}))
        << "Filter R -> VCA R";
}
} // namespace

TEST_F(GraphEditorTest, DeletingFilterInTheDefaultPatchHealsOscDirectlyIntoVcaViaRequestDeleteModule) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 900);

    const auto f = loadDefaultPatchOscFilterVca(editor, engine);
    assertDefaultPatchIsFullyDualIOWired(engine, f);
    auto* oscMb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(f.osc)->getProcessor());
    auto* vcaMb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(f.vca)->getProcessor());
    const int nodesBefore = engine.getGraph().getNumNodes();

    editor.requestDeleteModule(f.filter);

    EXPECT_EQ(engine.getGraph().getNodeForId(f.filter), nullptr);
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore - 1);
    // f.osc/f.vca are still the right NodeIDs: deleting one node via requestDeleteModule is a
    // surgical graph.removeNode(), not a full snapshot restore, so a surviving node's own NodeID
    // never changes underneath it.
    EXPECT_TRUE(engine.getGraph().isConnected({{f.osc, 0}, {f.vca, 0}})) << "Osc L healed straight into VCA L";
    EXPECT_TRUE(
        engine.getGraph().isConnected({{f.osc, oscMb->rightAudioLegChannel()}, {f.vca, vcaMb->rightAudioLegChannel()}}))
        << "Osc R healed straight into VCA R";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    // Undo restores through a full graph-snapshot apply, which CAN hand every node a fresh
    // NodeID -- re-find Osc/VCA by type rather than trusting f.osc/f.vca still resolve.
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore) << "one undo restores Filter";
    const auto restored = findOscFilterVca(engine.getGraph());
    EXPECT_FALSE(engine.getGraph().isConnected({{restored.osc, 0}, {restored.vca, 0}})) << "the heal edge is gone";
}

TEST_F(GraphEditorTest, DeletingFilterInTheDefaultPatchHealsOscDirectlyIntoVcaViaDeleteSelection) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 900);

    const auto f = loadDefaultPatchOscFilterVca(editor, engine);
    assertDefaultPatchIsFullyDualIOWired(engine, f);
    auto* oscMb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(f.osc)->getProcessor());
    auto* vcaMb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(f.vca)->getProcessor());
    const int nodesBefore = engine.getGraph().getNumNodes();

    editor.setSelectedNodes({f.filter});
    editor.deleteSelection();

    EXPECT_EQ(engine.getGraph().getNodeForId(f.filter), nullptr);
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore - 1);
    // f.osc/f.vca are still the right NodeIDs -- see the sibling requestDeleteModule test's own
    // comment for why a single-node delete never touches a survivor's NodeID.
    EXPECT_TRUE(engine.getGraph().isConnected({{f.osc, 0}, {f.vca, 0}})) << "Osc L healed straight into VCA L";
    EXPECT_TRUE(
        engine.getGraph().isConnected({{f.osc, oscMb->rightAudioLegChannel()}, {f.vca, vcaMb->rightAudioLegChannel()}}))
        << "Osc R healed straight into VCA R";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore) << "one undo restores Filter";
    const auto restored = findOscFilterVca(engine.getGraph());
    EXPECT_FALSE(engine.getGraph().isConnected({{restored.osc, 0}, {restored.vca, 0}})) << "the heal edge is gone";
}
