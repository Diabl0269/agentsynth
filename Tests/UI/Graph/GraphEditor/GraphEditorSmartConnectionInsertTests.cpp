// GraphEditor smart-connection tests for occupied audio destinations: the default parallel-add at a
// terminal sink, and Ctrl insert-in-series (including stereo fan correctness) at any module.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"

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
    editor.getSmartConnections().setInsertModifierOverrideForTests(false); // no Ctrl

    auto& graph = engine.getGraph();
    auto f = makeWiredSink(engine, editor);
    ASSERT_EQ(countAudioConnectionsBetween(graph, f.reverbId, f.outId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
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
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Oscillator"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Oscillator", {440, 100}));
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
    editor.connectPorts(reverbNode->nodeID, 0, filterNode->nodeID, audioLegs[0], false, false);
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions())
        EXPECT_FALSE(s.isInsert) << "a half-wired stereo input must not be rerouted";
    editor.getDragDropController().endDragPreview();

    // Phase 2: wire the rest → fully occupied group, and now the same drag inserts.
    for (size_t i = 1; i < audioLegs.size(); ++i)
        editor.connectPorts(reverbNode->nodeID, 0, filterNode->nodeID, audioLegs[i], false, false);
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
