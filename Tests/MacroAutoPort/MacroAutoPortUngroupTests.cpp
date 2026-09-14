// MacroAutoPortUngroupTests.cpp
// Ungrouping removes a macro's auto-created ports and splices cables back (founder-review fix G7,
// docs/macros_implementation.md §7); the presentation-count/tooltip rules that exclude auto-created ports; and
// the tri-state auto-create-ports preference's modal-firing conditions plus its
// remember/skip-the-modal paths. Shared test modules/helpers live in MacroAutoPortTestHelpers.h.
//
// Creation tests live in MacroAutoPortCreationTests.cpp; the T148/T154 auto-delete suite lives in
// MacroAutoPortDeleteTests.cpp.

#include "MacroAutoPortTestHelpers.h"

#include "../../Source/AppUndoManager.h"
#include "../../Source/Modules/AttenuverterModule.h"
#include "../../Source/Modules/FX/DelayModule.h"
#include "../../Source/Modules/FX/ReverbModule.h"
#include "../../Source/Modules/FilterModule.h"
#include "../../Source/Modules/MacroInletModule.h"
#include "../../Source/Modules/MacroMidiInletModule.h"
#include "../../Source/Modules/MacroMidiOutletModule.h"
#include "../../Source/Modules/MacroOutletModule.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Settings/PreferencesSettingsTab.h"

// ============================================================================
// Ungroup removes the macro's ports and splices the cable back (founder-review fix G7,
// docs/macros_implementation.md §7): "ungroup leaves the macro input/output in place (They should be removed)".
// Group then Ungroup must be a true round trip — every port node gone, every boundary cable it
// proxied reconnected external<->internal directly, on the original raw channels.
// ============================================================================

TEST(MacroUngroupPorts, RemovesTheAutoCreatedPortsAndSplicesTheCablesBack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2);
    EXPECT_FALSE(hasConnection(engine, cin, 0, a, 0));
    EXPECT_FALSE(hasConnection(engine, b, 0, cout, 0));

    editor.setSelectedNodes({a, b});
    editor.ungroupSelection();

    EXPECT_TRUE(editor.getMacros().empty()) << "the macro record itself is gone";
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore)
        << "both port nodes are removed — ungroup no longer leaves them behind";
    EXPECT_TRUE(hasConnection(engine, cin, 0, a, 0))
        << "the inlet's boundary cable is spliced straight back, on the original channels";
    EXPECT_TRUE(hasConnection(engine, b, 0, cout, 0)) << "same for the outlet's";
}

TEST(MacroUngroupPorts, UndoRoundTripRestoresTheActualLiveSignalPath) {
    // Node/cable counts matching is not enough — this renders a real block through the chain
    // before and after each step, per the task's own "assert the round trip BEHAVIOURALLY"
    // instruction.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    constexpr float kSourceValue = 0.42f;
    auto source = addModuleAt(editor, engine, std::make_unique<TestConstantModule>(kSourceValue), "Source", 100, 100);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto probe = addModuleAt(editor, engine, std::make_unique<TestCvProbeModule>(), "Probe", 700, 100);
    // A second, unconnected member: groupSelectionIntoMacro's min-2 rule, without giving `a` a
    // second crossing of its own to confuse the two edges under test.
    auto spare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Spare", 400, 400);

    engine.getGraph().addConnection({{source, 0}, {a, 0}}); // will cross -> an inlet port
    engine.getGraph().addConnection({{a, 0}, {probe, 0}});  // will cross -> an outlet port

    auto renderAndReadProbe = [&]() -> float {
        constexpr double kSampleRate = 44100.0;
        constexpr int kBlockSize = 64;
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 0, kSampleRate, kBlockSize);
        graph.prepareToPlay(kSampleRate, kBlockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < 4; ++i) {
            juce::AudioBuffer<float> buf(1, kBlockSize);
            buf.clear();
            graph.processBlock(buf, midi);
        }
        graph.releaseResources();
        auto* p = dynamic_cast<TestCvProbeModule*>(engine.getGraph().getNodeForId(probe)->getProcessor());
        return p != nullptr ? p->lastSample() : -1.0f;
    };

    editor.setSelectedNodes({a, spare});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u);
    EXPECT_NEAR(renderAndReadProbe(), kSourceValue, 0.001f)
        << "signal flows through the auto-spliced inlet/outlet ports before ungroup";

    editor.setSelectedNodes({a});
    editor.ungroupSelection();
    ASSERT_TRUE(editor.getMacros().empty());
    EXPECT_NEAR(renderAndReadProbe(), kSourceValue, 0.001f)
        << "the direct cable ungroup spliced back still carries the signal";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    ASSERT_EQ(editor.getMacros().size(), 1u) << "undo restores the macro AND its ports together, one step";
    EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 2u);
    EXPECT_NEAR(renderAndReadProbe(), kSourceValue, 0.001f)
        << "the original signal path is actually LIVE again through the re-spliced ports after "
           "undo, not just node/cable counts matching";
}

TEST(MacroUngroupPorts, SplicesTheFullCrossProductForFanInAndFanOut) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Fan-in: two external sources into ONE internal jack share one inlet port
    // (buildMacroPortCrossingPlan's own "share ONE port, not N" dedup rule).
    auto src1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Src1", 100, 100);
    auto src2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Src2", 100, 300);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 200);
    engine.getGraph().addConnection({{src1, 0}, {a, 0}});
    engine.getGraph().addConnection({{src2, 0}, {a, 0}});

    // Fan-out (the mirror): ONE internal source out to two external destinations shares one
    // outlet port.
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 500);
    auto dst1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Dst1", 700, 400);
    auto dst2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Dst2", 700, 600);
    engine.getGraph().addConnection({{b, 0}, {dst1, 0}});
    engine.getGraph().addConnection({{b, 0}, {dst2, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u);

    editor.setSelectedNodes({a, b});
    editor.ungroupSelection();

    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_TRUE(hasConnection(engine, src1, 0, a, 0)) << "fan-in: BOTH original sources reconnect";
    EXPECT_TRUE(hasConnection(engine, src2, 0, a, 0));
    EXPECT_TRUE(hasConnection(engine, b, 0, dst1, 0)) << "fan-out: BOTH original destinations reconnect";
    EXPECT_TRUE(hasConnection(engine, b, 0, dst2, 0));
}

TEST(MacroUngroupPorts, RemovesAHandAddedPortTooNoProvenanceDistinctionNeeded) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(); // no crossing -> no auto-created ports
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());

    const auto uuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                          MacroPortShape::Mono, 1, "Hand Added");
    ASSERT_FALSE(uuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, uuid);
    ASSERT_TRUE(portId.uid != 0);

    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}});
    engine.getGraph().addConnection({{portId, 0}, {b, 0}});

    editor.setSelectedNodes({a});
    editor.ungroupSelection();

    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr)
        << "a hand-added port (via Configure I/O) is removed on ungroup exactly like an "
           "auto-created one — no provenance field, no different treatment";
    EXPECT_TRUE(hasConnection(engine, ext, 0, b, 0)) << "its cable is spliced back the same way too";
}

TEST(MacroUngroupPorts, AMacroWithNoPortsUngroupsExactlyAsBeforeRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true); // no crossing cable -> no ports
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore);

    editor.setSelectedNodes({a, b});
    editor.ungroupSelection();

    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "nothing was ever spliced, nothing to remove";
    EXPECT_NE(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_NE(engine.getGraph().getNodeForId(b), nullptr);
}

// The CRITICAL trap this fix's own task spec calls out by name: ungroup used to be metadata-only
// and plausibly never reached the graph-change notification path. Now it removes nodes and
// rewires connections, so it MUST reach the same reconcile/notification seam every other graph
// mutation does (GraphEditor::onGraphStructureChanged -> MainComponent's
// reconcileTimelineBindingsOnly), or a timeline binding/automation lane keyed to a now-deleted
// port node's uuid survives stale into the next audio-thread render pass.
TEST(MacroUngroupPorts, ReachesTheGraphStructureChangedNotificationHook) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);

    int fireCount = 0;
    editor.onGraphStructureChanged = [&] { ++fireCount; };

    editor.setSelectedNodes({a});
    editor.ungroupSelection();

    EXPECT_GE(fireCount, 1) << "ungrouping a macro with ports removes nodes and rewires connections -- it MUST fire "
                               "onGraphStructureChanged (mirroring deleteSelection's own updateComponents() call), or "
                               "MainComponent never runs its post-graph-change reconcile pass for this path";
}

// ============================================================================
// Presentation (founder-review fix G6, docs/macros_implementation.md §7 item 4 note): the module-count
// indicator, the tooltip's member list and the content preview must count/list MODULES, never
// the port nodes a crossing cable spliced in — members.size() itself stays untouched.
// ============================================================================

TEST(MacroAutoPort, PresentationCountExcludesAutoCreatedPortsFounderScenario) {
    // The founder's exact reproduction: "the number of modules indicator seems to show more then
    // there are - 4 when i grouped the delay and reverb in the default patch - should have shown
    // 2." Delay -> Reverb, each with a crossing stereo connection on its outward-facing side (both
    // default to Dual I/O OFF, so each crossing collapses into ONE port, same as the founder's
    // screenshot) -> 2 real modules + 2 auto-created port nodes = 4 members, but 2 modules.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto delay = addModuleAt(editor, engine, std::make_unique<DelayModule>(), "Delay", 400, 100);
    auto reverb = addModuleAt(editor, engine, std::make_unique<ReverbModule>(), "Reverb", 400, 300);
    auto cinL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CinL", 100, 60);
    auto cinR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CinR", 100, 160);
    auto coutL = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CoutL", 700, 260);
    auto coutR = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "CoutR", 700, 360);
    engine.getGraph().addConnection({{cinL, 0}, {delay, 0}});
    engine.getGraph().addConnection({{cinR, 0}, {delay, 1}});
    engine.getGraph().addConnection({{delay, 0}, {reverb, 0}});
    engine.getGraph().addConnection({{delay, 1}, {reverb, 1}});
    engine.getGraph().addConnection({{reverb, 0}, {coutL, 0}});
    engine.getGraph().addConnection({{reverb, 1}, {coutR, 0}});

    editor.setSelectedNodes({delay, reverb});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 2u) << "one collapsed-stereo inlet feeding Delay, one outlet after Reverb";
    ASSERT_EQ((int)macro->members.size(), 4) << "the founder's literal '4' — members.size() itself is correct";

    // The fix: the user-facing MODULE count must read 2, not members.size()'s 4.
    EXPECT_EQ(macro->moduleMemberCount(), 2);

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr) << "groupSelectionIntoMacro leaves the macro collapsed by default";
    EXPECT_EQ(card->getModuleCountText(), "2 modules, 2 ports")
        << "honest about both quantities, never a bare '2' that hides the ports entirely";

    // Guard (do not remove): a port node must STILL be a real member — this is the invariant the
    // whole feature (bounds/group-drag/bypass-mute/undo/serialization) depends on. A future "fix"
    // that removes ports from `members` to make the count line up is the wrong fix.
    for (const auto& p : macro->ports)
        EXPECT_TRUE(macro->hasMember(p.nodeUuid)) << "port node " << p.nodeUuid << " must remain a member";
}

TEST(MacroAutoPort, PresentationTooltipListsModulesNotPortNodes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Delay", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Reverb", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u);

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto tooltip = card->getTooltip();
    EXPECT_TRUE(tooltip.contains("Delay"));
    EXPECT_TRUE(tooltip.contains("Reverb"));
    EXPECT_FALSE(tooltip.contains("Macro In")) << "a port node's own module name must not appear in the member list";
    EXPECT_FALSE(tooltip.contains("Macro Out"));
}

TEST(MacroAutoPort, PresentationCountIsUnchangedForAPlainMacroWithNoPorts) {
    // Regression guard: the plain P8-12 grouping case (no crossing cable, no ports at all) —
    // the most common case in the existing suite — must read exactly as it always did.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true); // no crossing cable -> no ports
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_EQ(macro->moduleMemberCount(), 2);

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getModuleCountText(), "2 modules");
}

TEST(MacroAutoPort, PresentationCountUpdatesWhenAPortIsDeleted) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto cin = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cin", 100, 100);
    auto cout = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Cout", 700, 300);
    engine.getGraph().addConnection({{cin, 0}, {a, 0}});
    engine.getGraph().addConnection({{b, 0}, {cout, 0}});

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 2u);
    ASSERT_EQ(macro->moduleMemberCount(), 2);

    const juce::String outletUuid = macro->ports[0].isInput ? macro->ports[1].nodeUuid : macro->ports[0].nodeUuid;
    editor.removeMacroPort(macroId, outletUuid);

    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u);
    EXPECT_EQ(macro->moduleMemberCount(), 2) << "deleting a port must never touch the MODULE count";

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getModuleCountText(), "2 modules, 1 port");
}

TEST(MacroAutoPort, PresentationCountExcludesAHandAddedPortViaConfigureIO) {
    // There is no provenance distinction between an auto-created port and one added by hand
    // through Configure I/O — both must be excluded from the module count the same way.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(); // no auto-ports
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());

    const auto portUuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                              MacroPortShape::Mono, 1, "Extra In");
    ASSERT_FALSE(portUuid.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u);
    EXPECT_EQ((int)macro->members.size(), 3);
    EXPECT_EQ(macro->moduleMemberCount(), 2) << "a hand-added port must not inflate the module count";

    const auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->getModuleCountText(), "2 modules, 1 port");
}

// ============================================================================
// requestGroupSelectionIntoMacro(): the modal fires only when Unset AND there is a crossing cable
// ============================================================================

TEST(MacroAutoPort, ModalDoesNotFireWhenTheSelectionHasNoCrossingCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);

    ASSERT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset);
    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 1) << "grouping proceeds immediately when there's nothing to decide";
}

TEST(MacroAutoPort, ModalFiresWhenUnsetAndTheSelectionHasACrossingCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    ASSERT_TRUE(editor.selectionHasCrossingMacroCable() == false) << "nothing selected yet";
    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.selectionHasCrossingMacroCable());

    bool modalShown = false;
    std::function<void(bool, bool)> capturedRespond;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)> respond) {
        modalShown = true;
        capturedRespond = respond;
    };

    editor.requestGroupSelectionIntoMacro();

    EXPECT_TRUE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 0) << "grouping is deferred until the user answers";

    ASSERT_TRUE((bool)capturedRespond);
    capturedRespond(true, false); // "Create Ports", don't remember
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 1u);
    EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::Unset)
        << "remember=false must not persist the choice";
}

// A module fresh on the canvas has no "uuid" property yet (only lazily assigned on first save, or
// by groupSelectionIntoMacro() itself once it decides to proceed) — this is the single most common
// real path: drop two never-saved modules, wire one to an existing module, group immediately. The
// crossing-cable gate must not silently under-detect just because nothing has a uuid yet.
TEST(MacroAutoPort, ModalFiresEvenWhenTheSelectedModulesHaveNoUuidYet) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Deliberately not addModuleAt() (which stamps a uuid) -- nodes added exactly the way a fresh
    // drop onto the canvas does, with no "uuid" property at all.
    auto addBareModule = [&](std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        editor.updateComponents();
        return node->nodeID;
    };
    auto a = addBareModule(std::make_unique<TestMonoModule>(), 400, 100);
    auto b = addBareModule(std::make_unique<TestMonoModule>(), 400, 300);
    auto ext = addBareModule(std::make_unique<TestMonoModule>(), 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.selectionHasCrossingMacroCable()) << "must detect the crossing cable without any uuid";

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };
    editor.requestGroupSelectionIntoMacro();

    EXPECT_TRUE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 0) << "grouping is still deferred until the user answers";
}

// A selection that already touches an existing macro is refused outright by
// groupSelectionIntoMacro() (see its own guard) -- nothing to decide, so the modal must mirror that
// refusal rather than asking a question whose answer can never be applied.
TEST(MacroAutoPort, ModalDoesNotFireWhenASelectedModuleIsAlreadyInAMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto x = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "X", 400, 100);
    auto y = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Y", 400, 300);
    editor.setSelectedNodes({x, y});
    const auto firstMacroId = editor.groupSelectionIntoMacro(false);
    ASSERT_FALSE(firstMacroId.isEmpty());

    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 700, 100);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 300);
    engine.getGraph().addConnection({{ext, 0}, {b, 0}});

    editor.setSelectedNodes({x, b}); // x is already a macro member
    EXPECT_FALSE(editor.selectionHasCrossingMacroCable());

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    EXPECT_EQ(editor.getMacros().size(), 1) << "the refused grouping must not have created a second macro";
}

TEST(MacroAutoPort, ModalRememberPersistsThePreference) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    std::function<void(bool, bool)> capturedRespond;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)> respond) { capturedRespond = respond; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();
    ASSERT_TRUE((bool)capturedRespond);
    capturedRespond(false, true); // "Leave Cables As Is", remember it

    EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_TRUE(editor.getMacros().getAll()[0].ports.empty());
    EXPECT_TRUE(hasConnection(engine, ext, 0, a, 0));
}

// T147: the actual bug — the modal's "Remember my choice" wrote to the properties file but a fresh
// launch never read it back, so the modal re-asked every session. This exercises the complete
// round trip the way a relaunch does: the modal's write path persists the choice through the
// editor's properties file, then a SECOND, freshly-constructed editor gets the restored preference
// exactly the way MainComponent's constructor does (loadMacroAutoPortPreference), and the modal
// must no longer fire on a crossing grouping. Covers both sides without a real relaunch.
TEST(MacroAutoPort, RememberedChoiceSurvivesAReload) {
    // Isolated storage so the round trip never touches the developer's real settings; the same
    // PropertiesFile the editor persists through is the one the loader reads back from.
    juce::ApplicationProperties appProperties;
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "MacroAutoPortReloadTest";
        options.filenameSuffix = "reload";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
    }

    // --- Session 1: the modal's "auto-create + remember it" write path persists the choice. ---
    {
        AudioEngine engine;
        GraphEditor editor(engine);
        editor.setSize(1600, 1200);
        editor.setPropertiesFile(appProperties.getUserSettings());
        auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
        auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
        auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
        engine.getGraph().addConnection({{ext, 0}, {a, 0}});

        std::function<void(bool, bool)> capturedRespond;
        editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)> respond) { capturedRespond = respond; };
        editor.setSelectedNodes({a, b});
        editor.requestGroupSelectionIntoMacro();
        ASSERT_TRUE((bool)capturedRespond);
        capturedRespond(true, true); // "Create Ports" + "Remember my choice"

        EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::AutoCreatePorts);
        // The persistence itself: the write path must have actually stored the value under its key.
        EXPECT_EQ(appProperties.getUserSettings()->getValue("macroAutoCreatePorts"), "auto");
    }

    // --- Session 2: a fresh editor restored the way MainComponent's constructor restores it. ---
    {
        AudioEngine engine;
        GraphEditor editor(engine);
        editor.setSize(1600, 1200);
        // Exactly the call MainComponent makes on launch: load the tri-state and apply it.
        editor.setMacroAutoPortPreference(PreferencesSettingsTab::loadMacroAutoPortPreference(appProperties));
        EXPECT_EQ(editor.getMacroAutoPortPreference(), GraphEditor::MacroAutoPortPreference::AutoCreatePorts)
            << "a fresh session must have restored the remembered choice";

        auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
        auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
        auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
        engine.getGraph().addConnection({{ext, 0}, {a, 0}});

        bool modalShown = false;
        editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };
        editor.setSelectedNodes({a, b});
        editor.requestGroupSelectionIntoMacro();

        EXPECT_FALSE(modalShown) << "the remembered choice must suppress the modal on relaunch (T147)";
        ASSERT_EQ(editor.getMacros().size(), 1);
        EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 1u);
    }
}

TEST(MacroAutoPort, PreferenceAutoCreatePortsSkipsTheModal) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::AutoCreatePorts);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_EQ(editor.getMacros().getAll()[0].ports.size(), 1u);
}

TEST(MacroAutoPort, PreferenceLeaveCablesAsIsSkipsTheModal) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs);
    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {a, 0}});

    bool modalShown = false;
    editor.macroAutoPortModalForTest = [&](std::function<void(bool, bool)>) { modalShown = true; };

    editor.setSelectedNodes({a, b});
    editor.requestGroupSelectionIntoMacro();

    EXPECT_FALSE(modalShown);
    ASSERT_EQ(editor.getMacros().size(), 1);
    EXPECT_TRUE(editor.getMacros().getAll()[0].ports.empty());
    EXPECT_TRUE(hasConnection(engine, ext, 0, a, 0));
}
