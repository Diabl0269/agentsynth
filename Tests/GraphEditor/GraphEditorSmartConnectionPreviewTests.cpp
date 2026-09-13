// GraphEditor smart-connection tests for the drop preview, Ctrl gesture plumbing (both press
// orderings, selection integrity), and the round-5 regressions (library Ctrl, live downgrade,
// jack alignment, dual fan).
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "../../Source/AI/AIStateMapper.h"
#include "../../Source/AppUndoManager.h"
#include "../../Source/Modules/FX/ChorusModule.h"
#include "../../Source/Modules/FX/DelayModule.h"
#include "../../Source/Modules/FX/ReverbModule.h"
#include "../../Source/Modules/FilterModule.h"
#include "../../Source/Modules/ModuleBase.h"
#include "../../Source/Modules/OscillatorModule.h"
#include "../../Source/Modules/SequencerModule.h"

// ---- Preview must show exactly what the drop wires -------------------------

TEST_F(GraphEditorTest, SmartConnectionParallelAddPreviewCoversBothOutputLegs) {
    // One suggestion is not one cable. A collapsed ghost jack fans across the sink's whole raw pair,
    // and the sink fronts those raws as two SEPARATE visible jacks (no ModuleBase to group them), so
    // the drop wires two cables. The preview used to draw a single wire to the left leg only.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);
    editor.setDefaultDualIOForNewModules(false); // collapsed ghost: one jack owning both raw legs

    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);

    std::set<int> previewedSinkJacks;
    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_FALSE(s.isInsert);
        ASSERT_FALSE(s.mainPreviewLegs.empty()) << "the preview must enumerate its resolved legs";
        for (const auto& leg : s.mainPreviewLegs) {
            previewedSinkJacks.insert(leg.toJack);
            EXPECT_NE(leg.p1, leg.p2) << "every previewed leg needs real endpoints";
        }
    }
    EXPECT_EQ(previewedSinkJacks, (std::set<int>{0, 1}))
        << "preview drew " << previewedSinkJacks.size() << " sink leg(s); the drop fans both";

    // And the drop really does wire both, so the preview above is the truth and not just a guess.
    editor.itemDropped(details);
    const auto chorusId = findNodeIdByName(engine.getGraph(), "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 1}, {f.outId, 1}}));
}

TEST_F(GraphEditorTest, SmartConnectionProbeHonoursTheDualIODefault) {
    // The library-drop ghost is an AIStateMapper probe, and it decides both the preview and the plan
    // that gets applied. It never used to receive applyDefaultDualIOForNewModule, so with the
    // default set to dual the plan was computed for a COLLAPSED ghost and then applied to a module
    // that spawned dual — wiring only the left legs.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);
    editor.setDefaultDualIOForNewModules(true); // ghost must front Left/Right, same as the real drop

    auto f = makeWiredSink(engine, editor);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);

    // A dual ghost has one output jack per leg, so the plan must name BOTH of them.
    std::set<int> plannedGhostJacks;
    for (const auto& s : editor.getSmartSuggestions())
        plannedGhostJacks.insert(s.ghostJack);
    EXPECT_EQ(plannedGhostJacks, (std::set<int>{0, 1}))
        << "the probe still looks collapsed — plan and spawned module disagree";

    editor.itemDropped(details);
    const auto chorusId = findNodeIdByName(engine.getGraph(), "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    auto* chorusMb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(chorusId)->getProcessor());
    ASSERT_NE(chorusMb, nullptr);
    EXPECT_TRUE(chorusMb->isDualIO()) << "the spawned module is dual, which is what the probe must match";
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 0}, {f.outId, 0}}));
    EXPECT_TRUE(engine.getGraph().isConnected({{chorusId, 1}, {f.outId, 1}}))
        << "a dual ghost must wire its RIGHT leg too";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlInsertWiresBothLegsOfADualGhostBetweenDualNeighbours) {
    // The exact user repro: Delay L+R → Reverb L+R, ctrl-drag a Chorus between them. Every node dual.
    // Expect four new cables (Delay L→Chorus L, Delay R→Chorus R, Chorus L→Reverb L, Chorus R→Reverb
    // R), no direct Delay→Reverb left, and nothing dangling on any Right jack.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1400, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);
    editor.setDefaultDualIOForNewModules(true); // the dropped Chorus spawns dual, like the user's

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 40);
    delayNode->properties.set("y", 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 760);
    reverbNode->properties.set("y", 100);
    setDualIOParam(*delayNode->getProcessor(), true);
    setDualIOParam(*reverbNode->getProcessor(), true);
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto delayId = delayNode->nodeID;
    const auto reverbId = reverbNode->nodeID;
    editor.connectPorts(delayId, 0, reverbId, 0, false, false); // L → L
    editor.connectPorts(delayId, 1, reverbId, 1, false, false); // R → R
    ASSERT_TRUE(graph.isConnected({{delayId, 0}, {reverbId, 0}}));
    ASSERT_TRUE(graph.isConnected({{delayId, 1}, {reverbId, 1}}));
    ASSERT_EQ(countAudioConnectionsBetween(graph, delayId, reverbId), 2);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamId, delayId);
        EXPECT_EQ(s.doomedLinks.size(), 2u) << "both original cables are doomed";
        EXPECT_EQ(s.upstreamCables.size(), 2u) << "a dual ghost takes one cable per leg, not one total";
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);

    EXPECT_EQ(countAudioConnectionsBetween(graph, delayId, reverbId), 0) << "no direct cable may survive";
    EXPECT_TRUE(graph.isConnected({{delayId, 0}, {chorusId, 0}})) << "Delay L → Chorus L";
    EXPECT_TRUE(graph.isConnected({{delayId, 1}, {chorusId, 1}})) << "Delay R → Chorus R (was dangling)";
    EXPECT_TRUE(graph.isConnected({{chorusId, 0}, {reverbId, 0}})) << "Chorus L → Reverb L";
    EXPECT_TRUE(graph.isConnected({{chorusId, 1}, {reverbId, 1}})) << "Chorus R → Reverb R (was dangling)";
    // No cross-wiring: a leg must not be duplicated across both of the far end's inputs.
    EXPECT_FALSE(graph.isConnected({{delayId, 0}, {chorusId, 1}}));
    EXPECT_FALSE(graph.isConnected({{chorusId, 0}, {reverbId, 1}}));

    // Nothing dangling: every leg that was carrying signal before is carrying signal after.
    EXPECT_FALSE(editor.isOutputJackFreeForTests(delayId, 1)) << "Delay Right OUT must not be left dangling";
    EXPECT_FALSE(editor.isInputJackFreeForTests(reverbId, 1)) << "Reverb Right IN must not be left dangling";
    EXPECT_FALSE(editor.isInputJackFreeForTests(chorusId, 1)) << "Chorus Right IN must be fed";
    EXPECT_FALSE(editor.isOutputJackFreeForTests(chorusId, 1)) << "Chorus Right OUT must be used";

    // Still one undo step, restoring both original cables exactly.
    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    const auto restoredDelay = findNodeIdByName(graph, "Delay");
    const auto restoredReverb = findNodeIdByName(graph, "Reverb");
    ASSERT_NE(restoredDelay.uid, 0u);
    ASSERT_NE(restoredReverb.uid, 0u);
    EXPECT_TRUE(graph.isConnected({{restoredDelay, 0}, {restoredReverb, 0}}));
    EXPECT_TRUE(graph.isConnected({{restoredDelay, 1}, {restoredReverb, 1}}));
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredDelay, restoredReverb), 2);
}

// ---- Ctrl gesture plumbing: both press orderings, and selection integrity ---
//
// Two SEPARATE mechanisms both keyed to Ctrl, and the tests below drive each on its own terms:
//   * press-time classification reads the MouseEvent's own mods (ModuleComponent::mouseDown), so
//     these tests hand it a real Ctrl-flagged event;
//   * the live per-tick sample reads the keyboard (GraphEditor::isInsertModifierDown), which a
//     headless test cannot press, so it uses the override.

static juce::ModifierKeys ctrlLeftClick() {
    return juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::leftButtonModifier);
}

/** Reverb upstream (40/600) → Delay target (760/100) already cabled, plus a Chorus at (40/300) to
 *  drag. Real modules throughout, so the ghost is the actual processor (canvas-move path). */
struct CtrlDragFixture {
    juce::AudioProcessorGraph::NodeID upstreamId, targetId, ghostId;
    ModuleComponent* ghostComp = nullptr;
};
static CtrlDragFixture makeCtrlDragFixture(AudioEngine& engine, GraphEditor& editor) {
    CtrlDragFixture f;
    auto& graph = engine.getGraph();
    auto target = graph.addNode(std::make_unique<DelayModule>());
    target->properties.set("x", 760);
    target->properties.set("y", 100);
    auto upstream = graph.addNode(std::make_unique<ReverbModule>());
    upstream->properties.set("x", 40);
    upstream->properties.set("y", 600);
    auto ghost = graph.addNode(std::make_unique<ChorusModule>());
    ghost->properties.set("x", 40);
    ghost->properties.set("y", 300);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.targetId = target->nodeID;
    f.upstreamId = upstream->nodeID;
    f.ghostId = ghost->nodeID;
    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    f.ghostComp = findModuleComp(editor, ghost->getProcessor());
    return f;
}

TEST_F(GraphEditorTest, SmartConnectionCtrlHeldBeforePressStillArmsAnInsertDrag) {
    // Ordering (b): Ctrl down FIRST, then press and drag. Two things used to swallow this press
    // before it could arm a drag, and this test guards both:
    //   1. the additive-selection branch returned early on Ctrl;
    //   2. on macOS isPopupMenu() is (rightButton | ctrl), so Ctrl+LEFT-click opened the module
    //      context menu and returned.
    // Either one leaves dragPreviewActive false, which makes updateDragPreview a no-op and yields
    // zero suggestions — so a non-zero suggestion count here proves the press armed the drag.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl physically held

    auto f = makeCtrlDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> bodyPoint{f.ghostComp->getWidth() / 2, f.ghostComp->getHeight() / 2};
    ASSERT_FALSE(f.ghostComp->getPortForPoint(bodyPoint).has_value()) << "the press must land on the card BODY";

    f.ghostComp->mouseDown(makeModuleClickWithMods(*f.ghostComp, bodyPoint, ctrlLeftClick()));
    editor.updateDragPreview({440, 100}); // drag it between upstream and target

    ASSERT_GT(editor.getSmartSuggestionCount(), 0)
        << "Ctrl-held press never armed the drag (selection early-return, or a context menu)";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.neighborId, f.targetId);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionCtrlPressedMidDragTurnsTheSuggestionIntoAnInsert) {
    // Ordering (a): start an ordinary drag, THEN press Ctrl. The modifier is sampled per tick, so
    // the very same ghost position flips from "nothing on offer" to an insert.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false); // no modifier yet

    auto f = makeCtrlDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> bodyPoint{f.ghostComp->getWidth() / 2, f.ghostComp->getHeight() / 2};
    f.ghostComp->mouseDown(makeModuleClickWithMods(*f.ghostComp, bodyPoint, plainLeftClick()));
    editor.updateDragPreview({440, 100});
    EXPECT_EQ(editor.getSmartSuggestionCount(), 0)
        << "the target's input is occupied and it is not the sink, so an unmodified drag gets nothing";

    editor.setInsertModifierOverrideForTests(true); // user presses Ctrl mid-drag
    editor.updateDragPreview({440, 100});

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "pressing Ctrl mid-drag must offer the insert";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, CtrlClickTogglesSelectionButCtrlDragDoesNot) {
    // The deferred classification, from the selection's point of view. A Ctrl+CLICK must behave as a
    // pure additive toggle and leave the rest of the selection alone; a Ctrl+DRAG must move the card
    // and NOT leave a stray toggle behind.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto a = graph.addNode(std::make_unique<OscillatorModule>());
    a->properties.set("x", 40);
    a->properties.set("y", 40);
    auto b = graph.addNode(std::make_unique<FilterModule>());
    b->properties.set("x", 400);
    b->properties.set("y", 40);
    auto c = graph.addNode(std::make_unique<DelayModule>());
    c->properties.set("x", 40);
    c->properties.set("y", 500);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* compC = findModuleComp(editor, c->getProcessor());
    ASSERT_NE(compC, nullptr);
    const juce::Point<int> bodyPoint{compC->getWidth() / 2, compC->getHeight() / 2};

    // A + B selected; Ctrl+click C ADDS it and keeps A and B.
    editor.setSelectedNodes({a->nodeID, b->nodeID});
    compC->mouseDown(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    compC->mouseUp(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    EXPECT_EQ(editor.getSelectionCount(), 3);
    EXPECT_TRUE(editor.isNodeSelected(a->nodeID)) << "a Ctrl+click must not discard the rest of the selection";
    EXPECT_TRUE(editor.isNodeSelected(b->nodeID));
    EXPECT_TRUE(editor.isNodeSelected(c->nodeID));

    // Ctrl+click C again REMOVES it, still keeping A and B.
    compC->mouseDown(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    compC->mouseUp(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    EXPECT_EQ(editor.getSelectionCount(), 2);
    EXPECT_FALSE(editor.isNodeSelected(c->nodeID)) << "a second Ctrl+click must toggle it back off";
    EXPECT_TRUE(editor.isNodeSelected(a->nodeID));
    EXPECT_TRUE(editor.isNodeSelected(b->nodeID));

    // Now a Ctrl+DRAG: the press collapses onto C so the move is single-module (a group drag would
    // suppress smart connections), it actually moves, and mouse-up must NOT run the toggle.
    editor.setSelectedNodes({a->nodeID, b->nodeID});
    compC->mouseDown(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));
    EXPECT_EQ(editor.getSelectionCount(), 1) << "the press collapses onto the dragged card";
    EXPECT_TRUE(editor.isNodeSelected(c->nodeID));
    compC->setTopLeftPosition(compC->getPosition() + juce::Point<int>(120, 0)); // the drag moved it
    compC->mouseUp(makeModuleClickWithMods(*compC, bodyPoint, ctrlLeftClick()));

    EXPECT_TRUE(editor.isNodeSelected(c->nodeID)) << "a Ctrl+drag must not toggle the dragged card away";
    EXPECT_EQ(editor.getSelectionCount(), 1) << "and must not resurrect the pre-press selection either";
    editor.endDragPreview();
}

// ---- Round 5 regressions: library Ctrl, live downgrade, jack alignment, dual fan ----

TEST_F(GraphEditorTest, GhostPortEstimateMatchesTheRealJackCentre) {
    // The drag ghost's jack positions come from GraphEditor::estimatePortCenter while a real card's
    // come from ModuleComponent::getPortCenter. They carried separate header literals (30 vs 38), so
    // every preview cable terminated 8px ABOVE the jack dot it claimed to land on.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 700);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<DelayModule>());
    node->properties.set("x", 200);
    node->properties.set("y", 120);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);
    const auto bounds = comp->getBounds();

    for (bool isInput : {true, false}) {
        for (int jack = 0; jack < 2; ++jack) {
            const auto real = bounds.getPosition() + comp->getPortCenter(jack, isInput);
            const auto ghost = GraphEditor::estimatePortCenter(node->getProcessor(), bounds, jack, isInput, false);
            EXPECT_EQ(ghost, real) << "ghost estimate drifted from the real jack centre for "
                                   << (isInput ? "input" : "output") << " jack " << jack;
        }
    }
}

TEST_F(GraphEditorTest, MidiCableAnchorsOnTheDrawnJackNotTheAudioPortStack) {
    // T149: buildVisibleCables() anchored a MIDI wire's source at portPos(comp, 0, false) — audio
    // output jack 0, which paint() offsets DOWN by kPortStep to dodge the MIDI Out dot — instead of
    // the fixed MIDI Out dot itself, so the cable left from below the jack it claimed to leave from.
    // The destination leg carried a second, independent drift: a stale y=30 literal instead of
    // ModuleComponent::kPortGutterHeaderHeight (the same 30-vs-38 drift GhostPortEstimateMatches...
    // already guards for ghost previews).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 700);

    auto& graph = engine.getGraph();
    auto seqNode = graph.addNode(std::make_unique<SequencerModule>());
    seqNode->properties.set("x", 100);
    seqNode->properties.set("y", 100);
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 500);
    oscNode->properties.set("y", 100);

    ASSERT_TRUE(graph.addConnection({{seqNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {oscNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* seqComp = findModuleComp(editor, seqNode->getProcessor());
    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(seqComp, nullptr);
    ASSERT_NE(oscComp, nullptr);

    const auto& cables = editor.buildVisibleCables();
    const GraphEditor::VisibleCable* midiCable = nullptr;
    for (const auto& c : cables) {
        if (c.signal == synth::ui::CableSignal::Midi) {
            midiCable = &c;
            break;
        }
    }
    ASSERT_NE(midiCable, nullptr) << "the Sequencer->Oscillator MIDI connection did not produce a visible cable";

    const auto expectedSrc = (seqComp->getBounds().getPosition() + seqComp->getMidiPortCenter(true)).toFloat();
    const auto expectedDst = (oscComp->getBounds().getPosition() + oscComp->getMidiPortCenter(false)).toFloat();
    EXPECT_EQ(midiCable->p1, expectedSrc) << "MIDI cable source did not land on the drawn MIDI Out dot";
    EXPECT_EQ(midiCable->p2, expectedDst) << "MIDI cable destination did not land on the drawn MIDI In dot";
}

TEST_F(GraphEditorTest, SmartConnectionPreviewLegsLandOnTheRealDestinationJack) {
    // End-to-end version of the above: the previewed leg's destination endpoint must sit on the
    // destination card's actual jack dot, not floating over its label row.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto dest = graph.addNode(std::make_unique<DelayModule>());
    dest->properties.set("x", 760);
    dest->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* destComp = findModuleComp(editor, dest->getProcessor());
    ASSERT_NE(destComp, nullptr);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);

    for (const auto& s : editor.getSmartSuggestions()) {
        ASSERT_FALSE(s.mainPreviewLegs.empty());
        for (const auto& leg : s.mainPreviewLegs) {
            const auto expected =
                (destComp->getBounds().getPosition() + destComp->getPortCenter(leg.toJack, true)).toFloat();
            EXPECT_LT(leg.p2.getDistanceFrom(expected), 1.0f)
                << "preview leg ends at " << leg.p2.y << " but the jack dot is at " << expected.y;
        }
    }
    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionReleasingCtrlMidDragDowngradesTheInsert) {
    // Pressing or releasing the modifier is not a mouse move. Suggestions used to be recomputed only
    // from updateDragPreview, so letting Ctrl go without moving left the insert preview on screen.
    // The drag tick re-samples and must flip it back, and forward again, with no mouse movement.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto f = makeCtrlDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> bodyPoint{f.ghostComp->getWidth() / 2, f.ghostComp->getHeight() / 2};
    f.ghostComp->mouseDown(makeModuleClickWithMods(*f.ghostComp, bodyPoint, ctrlLeftClick()));
    editor.updateDragPreview({440, 100});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions())
        ASSERT_TRUE(s.isInsert);

    // Ctrl released, mouse perfectly still: the tick must downgrade the preview.
    editor.setInsertModifierOverrideForTests(false);
    editor.pumpDragModifierTickForTests();
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_FALSE(s.isInsert) << "releasing Ctrl left a stale insert preview on screen";

    // And pressing it again, still without moving, must bring the insert back.
    editor.setInsertModifierOverrideForTests(true);
    editor.pumpDragModifierTickForTests();
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_TRUE(s.isInsert) << "re-pressing Ctrl without moving must re-offer the insert";

    editor.endDragPreview();
}

TEST_F(GraphEditorTest, SmartConnectionDualOutputWiresBothLegsIntoACollapsedInput) {
    // A dual Reverb dropped beside a collapsed Chorus wired only Left: the planner treated the
    // Left jack as MONO and let connectPorts duplicate it onto both destination raw legs (stride 0),
    // which also made the Right jack's pair redundant and dropped it. Left must reach raw0 and Right
    // must reach raw1.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto chorusNode = graph.addNode(std::make_unique<ChorusModule>()); // Dual I/O off: collapsed input
    chorusNode->properties.set("x", 760);
    chorusNode->properties.set("y", 100);
    auto reverbNode = graph.addNode(std::make_unique<ReverbModule>());
    reverbNode->properties.set("x", 40);
    reverbNode->properties.set("y", 100);
    setDualIOParam(*reverbNode->getProcessor(), true); // dual OUTPUT: separate Left/Right jacks
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto reverbId = reverbNode->nodeID;
    const auto chorusId = chorusNode->nodeID;

    auto* reverbComp = findModuleComp(editor, reverbNode->getProcessor());
    ASSERT_NE(reverbComp, nullptr);
    editor.beginDragPreview(reverbComp->getWidth(), reverbComp->getHeight(), reverbId);
    editor.updateDragPreview({440, 100});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    editor.finalizeModuleDrag(reverbComp);
    editor.endDragPreview();

    EXPECT_TRUE(graph.isConnected({{reverbId, 0}, {chorusId, 0}})) << "Left must reach the collapsed jack's raw0";
    EXPECT_TRUE(graph.isConnected({{reverbId, 1}, {chorusId, 1}})) << "Right must reach the collapsed jack's raw1";
    EXPECT_FALSE(graph.isConnected({{reverbId, 0}, {chorusId, 1}}))
        << "Left must not be duplicated onto the right leg as well";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlLibraryDropInsertsIntoAnOccupiedModule) {
    // Item 1's canvas half: a LIBRARY drag with Ctrl held must offer the insert, not just a
    // canvas-move drag. The library row's own press guard is covered by
    // ModuleLibraryRowPress.CtrlLeftClickDoesNotSuppressTheDrag.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held for the whole library drag

    auto& graph = engine.getGraph();
    auto f = makeWiredChain(engine, editor);
    ASSERT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);

    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &dummySource,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "a Ctrl-held library drag must offer the insert";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, chorusId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, chorusId, f.targetId), 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkPairsADualSourceOntoACollapsedDestination) {
    // The pure-function half of the fix, and the mirror direction that already worked.
    ReverbModule dualSource;
    setDualIOParam(dualSource, true);
    ChorusModule collapsedDest; // Dual I/O off

    const auto fromDual = GraphEditor::resolvePolyLink(&dualSource, 0, &collapsedDest, 0);
    EXPECT_EQ(fromDual.voiceCount, 2) << "a dual source's Left jack must carry the whole pair";
    EXPECT_EQ(fromDual.sourceRawChannel, 0);
    EXPECT_EQ(fromDual.destRawChannel, 0);
    EXPECT_EQ(fromDual.sourceStride, 1) << "stride must step to the module's own right leg, not 0";

    // Mirror: collapsed source into a dual destination still fans L->L / R->R.
    ReverbModule collapsedSource;
    ChorusModule dualDest;
    setDualIOParam(dualDest, true);
    const auto toDual = GraphEditor::resolvePolyLink(&collapsedSource, 0, &dualDest, 0);
    EXPECT_EQ(toDual.voiceCount, 2);
    EXPECT_EQ(toDual.sourceStride, 1);
}
