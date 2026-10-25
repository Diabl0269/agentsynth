// GraphEditor smart-connection tests for occupied audio destinations: the default parallel-add at a
// terminal sink, and Ctrl insert-in-series (including stereo fan correctness) at any module.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"

#include <array>
#include <tuple>

// --- Occupied audio destinations: parallel add vs. Ctrl insert-in-series ------
// Default (no modifier): the terminal audio sink accepts an ADDITIVE parallel cable, because it is
// wired in essentially every real patch and summing into the mix bus is what a hand-dragged cable
// there already does. Every other occupied destination stays a hard stop.
// Ctrl held: INSERT IN SERIES at ANY module — the upstream cabling is rerouted through the ghost.
// Ctrl is sampled live per drag tick, never latched at mouse-down (see isInsertModifierDown).

// ---- Default: parallel add at the terminal sink -----------------------------

TEST_F(GraphEditorTest, SmartConnectionAddsParallelCableAtOccupiedAudioOutput) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setSideBySideMode(SmartConnectionEngine::SideBySideMode::OnlyWithCtrl);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false); // no Ctrl

    auto& graph = engine.getGraph();
    auto f = makeWiredSink(engine, editor);
    ASSERT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", f.ghostTopLeft));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "an occupied sink must still offer a parallel cable";
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        EXPECT_FALSE(s.isInsert) << "without Ctrl nothing is ever rerouted";
        EXPECT_TRUE(s.doomedLinks.empty());
        EXPECT_EQ(s.neighborId, f.outId);
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);

    // Added alongside, not instead of: the pre-existing cable is untouched.
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2)
        << "the existing cable must survive a parallel add";
    EXPECT_TRUE(graph.isConnected({{f.reverbId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{f.reverbId, 1}, {f.outId, 1}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {f.outId, 1}}));
    EXPECT_FALSE(graph.isConnected({{chorusId, 0}, {f.outId, 1}}))
        << "a collapsed jack already fans both legs — a second cable would sum Left into Right";

    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredReverb = findNodeIdByName(graph, "Reverb");
    const auto restoredOut = findNodeIdByName(graph, "Audio Output");
    ASSERT_NE(restoredReverb.uid, 0u);
    ASSERT_NE(restoredOut.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredReverb, restoredOut), 2);
}

TEST_F(GraphEditorTest, SmartConnectionAddsParallelCableForPureSourceAtOccupiedAudioOutput) {
    // A pure source has no audio input, so it can never be inserted — but summing it into the mix
    // bus is a perfectly ordinary thing to want, and is exactly what wiring it by hand would do.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setSideBySideMode(SmartConnectionEngine::SideBySideMode::OnlyWithCtrl);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", f.ghostTopLeft));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "a source at an occupied sink gets a parallel cable";
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions())
        EXPECT_FALSE(s.isInsert);
    editor.itemDropped(details);

    const auto oscId = findNodeIdByName(graph, "Oscillator");
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, oscId, f.outId), 0);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2) << "existing cable untouched";
}

// ---- Ctrl: insert-in-series, at any module -----------------------------------

TEST_F(GraphEditorTest, SmartConnectionWithoutCtrlNeverInsertsIntoOccupiedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setSideBySideMode(SmartConnectionEngine::SideBySideMode::OnlyWithCtrl);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto f = makeWiredChain(engine, editor, /*wireIt=*/false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));

    // Positive control, so the zero below is the modifier rule and not a geometry accident.
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "geometry check: Chorus → free Delay input is in range";
    editor.getDragDropController().endDragPreview();

    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);

    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // The group is fully occupied and a valid insert in every other respect — the ONLY thing
    // missing is the modifier. A surprise reroute mid-patch is exactly what this prevents.
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.getDragDropController().endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertsIntoOccupiedOrdinaryModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true); // Ctrl held

    auto f = makeWiredChain(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "Ctrl must offer an insert at an ordinary module";
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_TRUE(s.ghostIsSource);
        EXPECT_EQ(s.neighborId, f.targetId) << "insert is no longer limited to the terminal sink";
        EXPECT_EQ(s.upstreamId, f.upstreamId);
        ASSERT_FALSE(s.doomedLinks.empty());
        ASSERT_FALSE(s.upstreamCables.empty());
        for (const auto& d : s.doomedLinks) {
            EXPECT_NE(d.p1, juce::Point<float>());
            EXPECT_NE(d.p2, juce::Point<float>());
        }
        for (const auto& c : s.upstreamCables) {
            EXPECT_NE(c.p1, juce::Point<float>());
            EXPECT_NE(c.p2, juce::Point<float>());
        }
    }
    editor.getDragDropController().endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertAtOccupiedModuleIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto f = makeWiredChain(engine, editor);
    ASSERT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0)
        << "the direct cable is rerouted, not kept";
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, chorusId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, chorusId, f.targetId), 0);

    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredUpstream = findNodeIdByName(graph, "Reverb");
    const auto restoredTarget = findNodeIdByName(graph, "Delay");
    ASSERT_NE(restoredUpstream.uid, 0u);
    ASSERT_NE(restoredTarget.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, restoredUpstream, restoredTarget), 0)
        << "one undo must put the rerouted cable back";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlDoesNotInsertPureSourceIntoOccupiedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true); // Ctrl held, and still refused

    auto f = makeWiredChain(engine, editor, /*wireIt=*/false);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {440, 100}));

    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "geometry check: Osc → free Delay input is in range";
    editor.getDragDropController().endDragPreview();

    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);

    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    // Even with Ctrl: an Oscillator has no audio input, so there is nothing to put in series. And
    // outside the terminal sink a parallel sum is not offered either.
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.getDragDropController().endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertIsBothOrNeitherAcrossStereoInputLegs) {
    // Both-or-neither. A Filter fronts TWO audio input legs (Left/Right). With only one wired the
    // group is half occupied and rerouting it would silently change what sums where — refused even
    // with Ctrl. Wire the rest and the very same drag becomes a valid insert, which is what makes
    // the refusal above a rule rather than an accident of geometry.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 760);
    filterNode->properties.set("y", 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 600);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Discover the Filter's audio input legs by label rather than assuming indices — the split-block
    // voice modules put Audio R on its own block, so the second leg is not necessarily jack 1.
    auto* filterMb = dynamic_cast<ModuleBase*>(filterNode->getProcessor());
    ASSERT_NE(filterMb, nullptr);
    std::vector<int> audioLegs;
    for (int j = 0; j < filterMb->getVisibleInputPortCount(); ++j) {
        const auto label = filterMb->getInputPortLabel(j).trim().toLowerCase();
        if (label == "left" || label == "right" || label == "audio l" || label == "audio r" || label == "audio")
            audioLegs.push_back(j);
    }
    ASSERT_GE(audioLegs.size(), 2u) << "this test needs a destination with a multi-leg audio input group";

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));

    // Phase 1: only the first leg wired → half-occupied group, no insert.
    // Raw edges, not connectPorts: a collapsed Reverb dragged onto the Filter's Left jack now wires
    // BOTH legs (Left to Left, Right to Audio R), which is the fully occupied case below.
    ASSERT_TRUE(graph.addConnection({{reverbNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions())
        EXPECT_FALSE(s.isInsert) << "a half-wired stereo input must not be rerouted";
    editor.getDragDropController().endDragPreview();

    // Phase 2: wire the rest → fully occupied group, and now the same drag inserts.
    ASSERT_TRUE(graph.addConnection({{reverbNode->nodeID, 1}, {filterNode->nodeID, FilterModule::kRightBase}}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << "a fully occupied group is a valid Ctrl insert";
        EXPECT_EQ(s.upstreamId, reverbNode->nodeID);
    }
    editor.getDragDropController().endDragPreview();
}

// ---- Ctrl insert: stereo fan correctness ------------------------------------

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertRemovesEveryDoomedLegOfADualIOUpstream) {
    // Regression: a Dual I/O upstream feeds the sink through TWO distinct cables (jack0→raw0,
    // jack1→raw1). A collapsed ghost's output fans across both raw legs, so the fan dedupe keeps
    // only one jack pair — and the doomed links used to hang off the surviving pair, so the second
    // cable was never removed and kept summing into the sink's right leg beside the ghost's output.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto outNode = addAudioOutputNode(graph, 760, 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 100);
    setDualIOParam(*reverbNode->getProcessor(), true); // split Left/Right BEFORE wiring
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto reverbId = reverbNode->nodeID;
    const auto outId = outNode->nodeID;
    editor.connectPorts(reverbId, 0, outId, 0, false, false); // Left  -> sink raw0
    editor.connectPorts(reverbId, 1, outId, 1, false, false); // Right -> sink raw1
    ASSERT_TRUE(graph.isConnected({{reverbId, 0}, {outId, 0}}));
    ASSERT_TRUE(graph.isConnected({{reverbId, 1}, {outId, 1}}));
    ASSERT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    // Both legs must be marked for removal even though only one jack pair survives the dedupe.
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        ASSERT_TRUE(s.isInsert);
        EXPECT_EQ(s.doomedLinks.size(), 2u) << "one doomed cable per occupied sink leg";
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);

    // The whole point: NO direct upstream->sink edge survives on EITHER raw channel.
    EXPECT_FALSE(graph.isConnected({{reverbId, 0}, {outId, 0}}));
    EXPECT_FALSE(graph.isConnected({{reverbId, 1}, {outId, 1}}));
    EXPECT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 0)
        << "a doomed leg left behind would sum into the sink alongside the ghost's output";

    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {outId, 1}}));
    EXPECT_GT(countAudioConnectionsBetween(graph, reverbId, chorusId), 0) << "the upstream now feeds the ghost";

    // Still one undo step, and it restores both original cables exactly.
    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredReverb = findNodeIdByName(graph, "Reverb");
    const auto restoredOut = findNodeIdByName(graph, "Audio Output");
    ASSERT_NE(restoredReverb.uid, 0u);
    ASSERT_NE(restoredOut.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{restoredReverb, 0}, {restoredOut, 0}}));
    EXPECT_TRUE(graph.isConnected({{restoredReverb, 1}, {restoredOut, 1}}));
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredReverb, restoredOut), 2);
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertDoesNotDuplicateOneUpstreamLegOntoADualIOGhost) {
    // The mirror image of the test above: a COLLAPSED upstream reaches the sink through one visible
    // cable that owns both raw legs, while a Dual I/O ghost has two separate input jacks. Wiring
    // that one upstream jack into each of them would fan its LEFT leg over both ghost legs, summing
    // on the right. Only one upstream->ghost cable is correct; it already carries L->L and R->R.
    //
    // Uses the MOVE path deliberately: the library-drop ghost is an AIStateMapper probe that never
    // has the Dual I/O default applied, so a dropped module's real jack layout can differ from the
    // one the preview measured. Dragging a module already on the canvas makes the ghost the real
    // processor, which is what this shape needs.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto outNode = addAudioOutputNode(graph, 760, 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>()); // Dual I/O off: collapsed
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 100);
    auto chorusNode = graph.addNode(std::make_unique<ChorusModule>());
    chorusNode->properties.set("x", 40);
    chorusNode->properties.set("y", 600);
    setDualIOParam(*chorusNode->getProcessor(), true); // ghost splits Left/Right
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto reverbId = reverbNode->nodeID;
    const auto outId = outNode->nodeID;
    const auto chorusId = chorusNode->nodeID;
    editor.connectPorts(reverbId, 0, outId, 0, false, false); // one cable, both raw legs
    ASSERT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 2);

    ModuleComponent* chorusComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c->getNodeId() == chorusId)
            chorusComp = c;
    }
    ASSERT_NE(chorusComp, nullptr);

    editor.getDragDropController().beginDragPreview(chorusComp->getWidth(), chorusComp->getHeight(), chorusId);
    editor.getDragDropController().updateDragPreview({440, 100});
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        ASSERT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamCables.size(), 1u) << "one collapsed upstream jack needs exactly one cable";
    }
    editor.finalizeModuleDrag(chorusComp);
    editor.getDragDropController().endDragPreview();

    EXPECT_EQ(countAudioConnectionsBetween(graph, reverbId, outId), 0);
    EXPECT_TRUE(graph.isConnected({{reverbId, 0}, {chorusId, 0}}));
    EXPECT_TRUE(graph.isConnected({{reverbId, 1}, {chorusId, 1}}));
    EXPECT_FALSE(graph.isConnected({{reverbId, 0}, {chorusId, 1}}))
        << "the upstream's LEFT leg must not also land on the ghost's RIGHT input";
    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {outId, 0}}));
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {outId, 1}}));
}

// ---- Insert and delete between a collapsed output and a split input -----------------------------
// The cable between a one-jack stereo output and a split-block stereo input (Channel Strip: Left on
// raw 0, Right on kRightBase), through the real insert (SmartConnectionEngine) and the real delete heal.

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

struct StripChain {
    NodeID compressorId, stripId;
};

StripChain makeCompressorIntoStrip(AudioEngine& engine, GraphEditor& editor) {
    auto& graph = engine.getGraph();
    auto strip = std::make_unique<ChannelStripModule>();
    strip->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(strip));
    stripNode->properties.set("x", 760);
    stripNode->properties.set("y", 100);
    auto compNode = graph.addNode(std::make_unique<CompressorModule>());
    compNode->properties.set("x", 40);
    compNode->properties.set("y", 600);
    editor.updateComponents();
    sizeModuleComponents(editor);
    return {compNode->nodeID, stripNode->nodeID};
}

bool wired(juce::AudioProcessorGraph& graph, NodeID a, int ca, NodeID b, int cb) {
    return graph.isConnected({{a, ca}, {b, cb}});
}

int audioEdgesInto(juce::AudioProcessorGraph& graph, NodeID dest) {
    int n = 0;
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == dest && !c.destination.isMIDI())
            ++n;
    return n;
}

// ---- The same two paths across every collapsed / split pairing --------------------------------

enum class Kind { FxCollapsed, FxSplit, Filter, Strip, Master, MasterCollapsed, MasterDirect };

const char* kindName(Kind k) {
    switch (k) {
    case Kind::FxCollapsed:
        return "FxCollapsed";
    case Kind::FxSplit:
        return "FxSplit";
    case Kind::Filter:
        return "Filter";
    case Kind::Strip:
        return "Strip";
    case Kind::Master:
        return "Master";
    case Kind::MasterCollapsed:
        return "MasterCollapsed";
    case Kind::MasterDirect:
        return "MasterDirect";
    }
    return "";
}

std::unique_ptr<juce::AudioProcessor> makeKind(Kind k) {
    switch (k) {
    case Kind::FxCollapsed:
        return std::make_unique<CompressorModule>();
    case Kind::FxSplit: {
        auto m = std::make_unique<CompressorModule>();
        setDualIOParam(*m, true);
        return m;
    }
    case Kind::Filter:
        return std::make_unique<FilterModule>();
    case Kind::Strip: {
        auto m = std::make_unique<ChannelStripModule>();
        m->setShape(ChannelStripModule::Shape::Stereo);
        return m;
    }
    case Kind::Master:
    case Kind::MasterDirect:
        return std::make_unique<MasterModule>();
    case Kind::MasterCollapsed: {
        auto m = std::make_unique<MasterModule>();
        setDualIOParam(*m, false);
        return m;
    }
    }
    return nullptr;
}

// The raw channels carrying Left and Right when the module is the source / destination of a stereo cable.
std::array<int, 2> outLegs(Kind k) {
    if (k == Kind::Filter)
        return {0, FilterModule::kRightBase};
    if (k == Kind::Strip)
        return {0, ChannelStripModule::kRightBase};
    return {0, 1};
}
std::array<int, 2> inLegs(Kind k) { return k == Kind::MasterDirect ? std::array<int, 2>{2, 3} : outLegs(k); }

void expectPair(juce::AudioProcessorGraph& graph, NodeID a, std::array<int, 2> aLegs, NodeID b,
                std::array<int, 2> bLegs, const char* what) {
    for (int i = 0; i < 2; ++i)
        EXPECT_TRUE(wired(graph, a, aLegs[(size_t)i], b, bLegs[(size_t)i])) << what << " leg " << i;
    int n = 0;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == a && c.destination.nodeID == b && !c.source.isMIDI())
            ++n;
    EXPECT_EQ(n, 2) << what << ": exactly the two legs, nothing summed in";
}

void wirePair(juce::AudioProcessorGraph& graph, NodeID a, std::array<int, 2> aLegs, NodeID b,
              std::array<int, 2> bLegs) {
    for (int i = 0; i < 2; ++i)
        ASSERT_TRUE(graph.addConnection({{a, aLegs[(size_t)i]}, {b, bLegs[(size_t)i]}}));
}

NodeID addKind(AudioEngine& engine, Kind k, int x, int y) {
    auto node = engine.getGraph().addNode(makeKind(k));
    node->properties.set("x", x);
    node->properties.set("y", y);
    return node->nodeID;
}

using HealParam = std::tuple<Kind, Kind, Kind>;
class StripWiringHealMatrix : public ::testing::TestWithParam<HealParam> {};

} // namespace

// Regression test for FRO900: a module inserted between a one-jack Compressor and a split-input
// Channel Strip rewired only the strip's Left leg, and deleting it left the Left input empty.
TEST_F(GraphEditorTest, InsertAndDeleteBetweenCollapsedOutputAndSplitStripKeepsBothLegs) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    const auto f = makeCompressorIntoStrip(engine, editor);
    constexpr int kR = ChannelStripModule::kRightBase;
    ASSERT_TRUE(graph.addConnection({{f.compressorId, 0}, {f.stripId, 0}}));
    ASSERT_TRUE(graph.addConnection({{f.compressorId, 1}, {f.stripId, kR}}));

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_FALSE(wired(graph, f.compressorId, 0, f.stripId, 0)) << "the direct Left cable is rerouted";
    EXPECT_FALSE(wired(graph, f.compressorId, 1, f.stripId, kR)) << "the direct Right cable is rerouted";
    EXPECT_TRUE(wired(graph, f.compressorId, 0, chorusId, 0));
    EXPECT_TRUE(wired(graph, f.compressorId, 1, chorusId, 1));
    EXPECT_TRUE(wired(graph, chorusId, 0, f.stripId, 0)) << "new module -> strip Left";
    EXPECT_TRUE(wired(graph, chorusId, 1, f.stripId, kR)) << "new module -> strip Right";
    EXPECT_EQ(audioEdgesInto(graph, f.stripId), 2) << "nothing else may land on the strip";

    editor.requestDeleteModule(chorusId);
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    EXPECT_TRUE(wired(graph, f.compressorId, 0, f.stripId, 0)) << "healed Left";
    EXPECT_TRUE(wired(graph, f.compressorId, 1, f.stripId, kR)) << "healed Right";
    EXPECT_EQ(audioEdgesInto(graph, f.stripId), 2);
}

// Regression test for FRO900: the delete heal restored one leg (or none) whenever the deleted module
// and a survivor presented the stereo cable differently (one jack vs. two, an adjacent right leg vs.
// a dedicated block).
TEST_P(StripWiringHealMatrix, DeletingTheMiddleModuleRestoresBothLegs) {
    const auto [upKind, midKind, downKind] = GetParam();
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    auto& graph = engine.getGraph();

    const auto up = addKind(engine, upKind, 0, 0);
    const auto mid = addKind(engine, midKind, 400, 0);
    const auto down = addKind(engine, downKind, 800, 0);
    editor.updateComponents();
    wirePair(graph, up, outLegs(upKind), mid, inLegs(midKind));
    wirePair(graph, mid, outLegs(midKind), down, inLegs(downKind));

    editor.requestDeleteModule(mid);

    EXPECT_EQ(graph.getNodeForId(mid), nullptr);
    expectPair(graph, up, outLegs(upKind), down, inLegs(downKind), "healed cable");
}

INSTANTIATE_TEST_SUITE_P(CollapsedAndSplit, StripWiringHealMatrix,
                         ::testing::Combine(::testing::Values(Kind::FxCollapsed, Kind::FxSplit, Kind::Filter),
                                            ::testing::Values(Kind::FxCollapsed, Kind::FxSplit, Kind::Filter),
                                            ::testing::Values(Kind::FxCollapsed, Kind::FxSplit, Kind::Filter,
                                                              Kind::Strip, Kind::Master, Kind::MasterCollapsed,
                                                              Kind::MasterDirect)),
                         [](const ::testing::TestParamInfo<HealParam>& info) {
                             return std::string(kindName(std::get<0>(info.param))) + "_" +
                                    kindName(std::get<1>(info.param)) + "_" + kindName(std::get<2>(info.param));
                         });
