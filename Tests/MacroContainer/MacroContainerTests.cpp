// MacroContainerTests.cpp
// Lifecycle tests for Macros — P8-12's named, coloured, collapsible container that groups
// selected modules on the canvas as metadata layered over the existing multi-select system.
// Shared helpers live in MacroContainerTestHelpers.h.
//
//   • wrap/unwrap   — Cmd+G/Cmd+Shift+G round trip, min-selection and no-nesting refusals
//   • persistence   — membership + name/colour survive a project bundle save/load
//   • snippets      — a grouped selection round-trips through extract/insert with fresh uuids
//   • delete        — member delete shrinks/dissolves a macro; macro delete removes modules,
//                     ungroup keeps them
//   • membership    — add/remove selection to/from a macro, including port-splice on a newly
//                     crossing cable
//
// Geometry (collapse, hull/chip, undo, cables, trust, recolour) lives in
// MacroContainerGeometryTests.cpp; chrome/interaction (collapse button, card double-click,
// context menus) lives in MacroContainerInteractionTests.cpp.

#include "MacroContainerTestHelpers.h"

#include "../../Source/AI/AIStateMapper/AIStateMapper.h"
#include "../../Source/AppUndoManager.h"
#include "../../Source/Modules/FilterModule.h"
#include "../../Source/Modules/OscillatorModule.h"
#include "../../Source/Modules/VCAModule.h"
#include "../../Source/PatchDocument.h"
#include "../../Source/ProjectBundle.h"
#include "../../Source/Timeline/TimelineDoc.h"
#include "UI/Macros/MacroCardComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// Wrap / unwrap
// ============================================================================

TEST(MacroWrap, WrapAndUnwrapRoundTrip) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto uuidA = uuidOf(engine, a);
    auto uuidB = uuidOf(engine, b);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    EXPECT_EQ(editor.getMacros().size(), 1);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u);
    EXPECT_TRUE(macro->hasMember(uuidA));
    EXPECT_TRUE(macro->hasMember(uuidB));

    editor.ungroupSelection();
    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_NE(engine.getGraph().getNodeForId(a), nullptr) << "ungroup keeps the modules";
    EXPECT_NE(engine.getGraph().getNodeForId(b), nullptr);
}

TEST(MacroWrap, WrapsFreshlyDroppedModulesWithNoUuidYet) {
    // Reproduces the real "drag two modules onto the canvas and immediately Cmd+G them" path:
    // GraphEditor::itemDropped never stamps a "uuid" property (only graphToJSON does, lazily, on
    // first save), so grouping must assign one on the spot rather than silently dropping the
    // module from membership and reporting "select at least two modules".
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    a->properties.set("x", 100);
    a->properties.set("y", 100);
    auto b = engine.getGraph().addNode(std::make_unique<FilterModule>());
    b->properties.set("x", 500);
    b->properties.set("y", 100);
    editor.updateComponents();

    ASSERT_TRUE(a->properties["uuid"].toString().isEmpty());
    ASSERT_TRUE(b->properties["uuid"].toString().isEmpty());

    editor.setSelectedNodes({a->nodeID, b->nodeID});
    EXPECT_EQ(editor.getSelectionCount(), 2);

    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u);
    EXPECT_FALSE(a->properties["uuid"].toString().isEmpty());
    EXPECT_FALSE(b->properties["uuid"].toString().isEmpty());
}

TEST(MacroWrap, RefusesFewerThanTwoSelected) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    juce::String lastMessage;
    editor.onStatusMessage = [&](const juce::String& msg) { lastMessage = msg; };

    // Nothing selected.
    EXPECT_TRUE(editor.groupSelectionIntoMacro().isEmpty());
    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_FALSE(lastMessage.isEmpty());

    lastMessage.clear();
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    editor.selectModule(a, false);
    EXPECT_TRUE(editor.groupSelectionIntoMacro().isEmpty());
    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_FALSE(lastMessage.isEmpty());
}

TEST(MacroWrap, RefusesNestingAnAlreadyGroupedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.setSelectedNodes({a, b});
    auto firstMacroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(firstMacroId.isEmpty());

    juce::String lastMessage;
    editor.onStatusMessage = [&](const juce::String& msg) { lastMessage = msg; };

    editor.setSelectedNodes({b, c}); // b is already in a macro
    EXPECT_TRUE(editor.groupSelectionIntoMacro().isEmpty());
    EXPECT_FALSE(lastMessage.isEmpty());

    ASSERT_EQ(editor.getMacros().size(), 1) << "nesting must be refused, not silently create a second macro";
    auto* macro = editor.getMacros().find(firstMacroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u) << "the original macro must be untouched";
}

// ============================================================================
// Persistence
// ============================================================================

TEST(MacroPersistence, MembershipAndPresentationSurviveProjectBundleSaveAndLoad) {
    auto root =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-macrocontainer-tests");
    root.deleteRecursively();
    root.createDirectory();
    auto dir = root.getChildFile(juce::String("Macro") + synth::ProjectBundle::kBundleExtension);

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto uuidA = uuidOf(engine, a);
    auto uuidB = uuidOf(engine, b);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.renameMacro(macroId, "MyGroup");
    editor.setMacroColour(macroId, juce::Colour(0xffabcdef));

    synth::TimelineDoc timeline;
    synth::PatchDocument patchDocument;
    auto saveResult = synth::ProjectBundle::save(dir, engine.getGraph(), timeline, patchDocument, editor.getMacros());
    ASSERT_TRUE(saveResult.ok) << saveResult.message;

    juce::AudioProcessorGraph freshGraph;
    synth::TimelineDoc freshTimeline;
    synth::PatchDocument freshPatchDocument;
    synth::MacroSet freshMacros;
    auto loadResult = synth::ProjectBundle::load(dir, freshGraph, freshTimeline, freshPatchDocument, freshMacros);
    ASSERT_TRUE(loadResult.ok) << loadResult.message;

    ASSERT_EQ(freshMacros.size(), 1);
    const auto& loaded = freshMacros.getAll()[0];
    EXPECT_EQ(loaded.name, juce::String("MyGroup"));
    EXPECT_EQ(loaded.colour, juce::Colour(0xffabcdef));
    ASSERT_EQ(loaded.members.size(), 2u);
    EXPECT_TRUE(loaded.hasMember(uuidA)) << "a trusted load honours uuids exactly";
    EXPECT_TRUE(loaded.hasMember(uuidB));

    root.deleteRecursively();
}

// ============================================================================
// Snippets
// ============================================================================

TEST(MacroSnippet, ExtractAndInsertSucceedsAndCreatesANewMacroWithFreshUuids) {
    // Regression test: a macro-carrying snippet used to fail AIStateMapper::validatePatch's
    // untrusted-path "macros" refusal, and insertSnippetAt silently returned false.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* originalMacro = editor.getMacros().find(macroId);
    ASSERT_NE(originalMacro, nullptr);
    const auto originalMembers = originalMacro->members; // copy before extraction/insert mutate macros

    editor.setSelectedNodes({a, b}); // grouping already left these selected; be explicit
    ASSERT_EQ(editor.getSelectionCount(), 2) << "a collapsed macro's members must still be selectable by id";
    auto snippet = editor.extractSelectionSnippet("Grouped");

    ASSERT_TRUE(editor.insertSnippetAt(snippet, {1400, 900})) << "must succeed even though the selection was grouped";

    EXPECT_EQ(editor.getMacros().size(), 2);
    const synth::Macro* pasted = nullptr;
    for (const auto& m : editor.getMacros().getAll())
        if (m.id != macroId)
            pasted = &m;
    ASSERT_NE(pasted, nullptr);
    ASSERT_EQ(pasted->members.size(), 2u);
    for (const auto& uuid : pasted->members)
        EXPECT_EQ(std::find(originalMembers.begin(), originalMembers.end(), uuid), originalMembers.end())
            << "the pasted copy must get fresh uuids, not reuse the originals";
}

TEST(MacroSnippet, ConfiguredPortSurvivesDuplicateAndSaveAsSnippet) {
    // T117: a macro's configured I/O (P8-15) used to be dropped by SnippetManager::extractSnippet/
    // insertSnippet — the boundary jack's underlying MacroInlet node travelled through as an
    // ordinary member (nothing excludes it), but the MacroPort entry naming it as a jack did not,
    // so a duplicated or snippet-round-tripped macro silently lost its port and fell back to
    // looking like a plain, unconfigured group. Exercises the real user paths (Duplicate and Save
    // as Snippet), not SnippetManager's functions directly.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto portUuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono,
                                        1, "Pitch In");
    ASSERT_FALSE(portUuid.isEmpty());

    auto* original = editor.getMacros().find(macroId);
    ASSERT_NE(original, nullptr);
    ASSERT_EQ(original->ports.size(), 1u);

    // ---- Duplicate ----
    editor.selectMacro(macroId, false);
    ASSERT_EQ(editor.getSelectionCount(), 3) << "the macro now has 3 members: a, b, and the port's inlet node";
    ASSERT_TRUE(editor.duplicateSelection());

    const synth::Macro* duplicated = nullptr;
    for (const auto& m : editor.getMacros().getAll())
        if (m.id != macroId)
            duplicated = &m;
    ASSERT_NE(duplicated, nullptr);
    ASSERT_EQ(duplicated->ports.size(), 1u) << "duplicate must keep the configured port, not just the raw members";
    EXPECT_EQ(duplicated->ports[0].name, "Pitch In");
    EXPECT_TRUE(duplicated->ports[0].isInput);
    EXPECT_NE(duplicated->ports[0].nodeUuid, portUuid) << "must front the copy's own node";
    const juce::String duplicatedId = duplicated->id; // copy before the next mutation may reallocate MacroSet

    // ---- Save as Snippet / insert ----
    editor.selectMacro(macroId, false);
    auto snippet = editor.extractSelectionSnippet("PortedMacro");
    ASSERT_TRUE(editor.insertSnippetAt(snippet, {1400, 900}));

    const synth::Macro* pasted = nullptr;
    for (const auto& m : editor.getMacros().getAll())
        if (m.id != macroId && m.id != duplicatedId)
            pasted = &m;
    ASSERT_NE(pasted, nullptr);
    ASSERT_EQ(pasted->ports.size(), 1u) << "Save as Snippet must keep the configured port too";
    EXPECT_EQ(pasted->ports[0].name, "Pitch In");
}

// ============================================================================
// Delete
// ============================================================================

TEST(MacroDelete, DeletingOneMemberShrinksTheMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidA = uuidOf(engine, a);

    editor.setSelectedNodes({a, b, c});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.setSelectedNodes({a});
    editor.deleteSelection();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "a 3-member macro must survive losing one member";
    EXPECT_EQ(macro->members.size(), 2u);
    EXPECT_FALSE(macro->hasMember(uuidA));
}

// MacroSet::retainOnly (Source/MacroSet.h) only dissolves a macro that drops to ZERO members, not
// one — the doc comment says "a macro left with zero members ... is dissolved outright", so a
// 2-member macro losing one member is expected to survive with a single member.
TEST(MacroDelete, DeletingDownToOneMemberDoesNotDissolveButDeletingTheLastDoes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.setSelectedNodes({a});
    editor.deleteSelection();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "retainOnly only dissolves a macro at zero members, not one";
    EXPECT_EQ(macro->members.size(), 1u);

    editor.setSelectedNodes({b});
    editor.deleteSelection();

    EXPECT_EQ(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().empty());
}

TEST(MacroDelete, DeleteMacroAndMembersRemovesBothTheMacroAndItsNodes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.deleteMacroAndMembers(macroId);

    EXPECT_EQ(editor.getMacros().find(macroId), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
}

TEST(MacroDelete, UngroupSelectionKeepsTheModulesInTheGraph) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.ungroupSelection();

    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_NE(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_NE(engine.getGraph().getNodeForId(b), nullptr);
}

// ============================================================================
// Membership (T138): Add Selection to Macro / Remove from Macro -- the incremental
// counterparts to groupSelectionIntoMacro()/ungroupSelection() for an EXISTING macro.
// ============================================================================

TEST(MacroMembership, AddSelectionToMacroAddsALooseModuleAsANewOrdinaryMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidC = uuidOf(engine, c);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.addSelectionToMacro(macroId, {uuidC});

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 3u);
    EXPECT_TRUE(macro->hasMember(uuidC));
}

TEST(MacroMembership, AddSelectionToMacroRefusesWhenAUuidIsAlreadyInAnotherMacroAndAddsNothing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroA = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroA.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto d = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1300, 100);
    editor.setSelectedNodes({c, d});
    auto macroB = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroB.isEmpty());

    auto uuidC = uuidOf(engine, c); // already a member of macroB
    auto loose = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 1700, 100);
    auto uuidLoose = uuidOf(engine, loose);

    juce::String lastMessage;
    editor.onStatusMessage = [&](const juce::String& msg) { lastMessage = msg; };

    editor.addSelectionToMacro(macroA, {uuidLoose, uuidC});

    EXPECT_FALSE(lastMessage.isEmpty()) << "must refuse with a status message, not a silent no-op";
    auto* macro = editor.getMacros().find(macroA);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u) << "the whole call must be refused -- the loose module must "
                                            "not be added either, matching groupSelectionIntoMacro's own "
                                            "all-or-nothing refusal";
    EXPECT_FALSE(macro->hasMember(uuidLoose));

    auto* otherMacro = editor.getMacros().find(macroB);
    ASSERT_NE(otherMacro, nullptr);
    EXPECT_EQ(otherMacro->members.size(), 2u) << "macroB must be untouched";
}

TEST(MacroMembership, AddSelectionToMacroIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    undoManager.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidC = uuidOf(engine, c);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    undoManager.clearUndoHistory();

    editor.addSelectionToMacro(macroId, {uuidC});
    ASSERT_TRUE(editor.getMacros().find(macroId)->hasMember(uuidC));

    ASSERT_TRUE(undoManager.canUndo());
    undoManager.undo();
    EXPECT_FALSE(editor.getMacros().find(macroId)->hasMember(uuidC)) << "a single undo must fully revert the add";

    undoManager.redo();
    EXPECT_TRUE(editor.getMacros().find(macroId)->hasMember(uuidC));
}

TEST(MacroMembership, RemoveSelectionFromMacroShrinksTheMacroWithoutDissolvingIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidA = uuidOf(engine, a);

    editor.setSelectedNodes({a, b, c});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.removeSelectionFromMacro(macroId, {uuidA});

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u);
    EXPECT_FALSE(macro->hasMember(uuidA));
    EXPECT_NE(engine.getGraph().getNodeForId(a), nullptr) << "remove-from-macro never deletes the module itself";
}

TEST(MacroMembership, RemoveSelectionFromMacroDissolvesOnTheLastMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto uuidA = uuidOf(engine, a);
    auto uuidB = uuidOf(engine, b);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.removeSelectionFromMacro(macroId, {uuidA, uuidB});

    EXPECT_EQ(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_NE(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_NE(engine.getGraph().getNodeForId(b), nullptr);
}

TEST(MacroMembership, RemoveSelectionFromMacroSkipsAPortUuidRatherThanDesyncingMacroPorts) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    // Stands in for a real MacroInlet/MacroOutlet node -- updateComponents() reconciles macro
    // membership against LIVE graph node uuids (macros.retainOnly()), so a synthetic uuid with no
    // backing node would just get silently pruned as an orphan the moment doRemove() below calls
    // it, independent of removeSelectionFromMacro's own skip-a-port logic under test here.
    auto portNode = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto portUuid = uuidOf(engine, portNode);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // Hand-crafts a MacroPort entry fronting that real node, mirroring the one invariant
    // removeSelectionFromMacro relies on (every port's nodeUuid is also a `members` entry) --
    // exercising the skip rule needs only MacroSet's own data, not a real spliced MacroInlet node.
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    macro->members.push_back(portUuid);
    synth::MacroPort port;
    port.nodeUuid = portUuid;
    port.isInput = true;
    port.name = "Test In";
    macro->ports.push_back(port);

    const auto uuidB = uuidOf(engine, b);
    editor.removeSelectionFromMacro(macroId, {portUuid, uuidB});

    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(portUuid)) << "a port must never be pulled out by this generic path -- "
                                               "it has its own delete affordance";
    EXPECT_FALSE(macro->hasMember(uuidB)) << "the ordinary member must still be removed";
}

// ----------------------------------------------------------------------------
// T138 second live-testing round (2026-09-10): auto-create/delete ports on Add/Remove Selection
// to/from Macro, matching what groupSelectionIntoMacro(true)/ungroupSelection() already do at
// whole-macro creation/dissolution time. Reported live as "I remove a module from a macro, but its
// connection still remains, and the output of that module still goes into the macro connection" --
// the cable itself was always fine (an un-ported boundary crossing is a supported state), but it
// never got a proper port the way a from-scratch grouping would have given it.
// ----------------------------------------------------------------------------

TEST(MacroMembership, RemoveSelectionFromMacroCreatesAPortForTheNewlyCrossingCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    ASSERT_TRUE(engine.getGraph().addConnection({{a, 0}, {b, 0}})); // wholly interior while both are members
    auto uuidA = uuidOf(engine, a);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // no auto-ports at creation (default false)
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());

    editor.removeSelectionFromMacro(macroId, {uuidA});

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(uuidA)) << "a removed member never stays a member";
    ASSERT_EQ(macro->ports.size(), 1u) << "b's now-external cable to the departed a must get a real "
                                          "boundary port, not just render as an un-ported crossing";
    const auto& port = macro->ports.front();
    EXPECT_TRUE(port.isInput) << "signal flows a -> b, so b's boundary jack is an inlet";

    NodeID portId;
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == port.nodeUuid)
            portId = node->nodeID;
    ASSERT_NE(portId.uid, 0u);

    auto hasConn = [&](NodeID src, int srcCh, NodeID dst, int dstCh) {
        for (const auto& c : engine.getGraph().getConnections())
            if (c.source.nodeID == src && c.source.channelIndex == srcCh && c.destination.nodeID == dst &&
                c.destination.channelIndex == dstCh)
                return true;
        return false;
    };
    EXPECT_TRUE(hasConn(a, 0, portId, 0)) << "a (now external) feeds the new port";
    EXPECT_TRUE(hasConn(portId, 0, b, 0)) << "the port feeds b (still a member), preserving the signal path";
    EXPECT_FALSE(hasConn(a, 0, b, 0)) << "the original direct cable was spliced, not left dangling alongside the port";
}

TEST(MacroMembership, AddSelectionToMacroCreatesAPortForTheNewCrossingCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);    // about to join
    auto ext = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 1300, 100); // stays outside
    ASSERT_TRUE(engine.getGraph().addConnection({{c, 0}, {ext, 0}}));
    auto uuidC = uuidOf(engine, c);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->ports.empty());

    editor.addSelectionToMacro(macroId, {uuidC});

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuidC));
    ASSERT_EQ(macro->ports.size(), 1u) << "c's pre-existing cable to ext is now a real boundary crossing "
                                          "and must get a port, not just render un-ported";
    const auto& port = macro->ports.front();
    EXPECT_FALSE(port.isInput) << "signal flows c -> ext, so c's boundary jack is an outlet";

    NodeID portId;
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == port.nodeUuid)
            portId = node->nodeID;
    ASSERT_NE(portId.uid, 0u);

    auto hasConn = [&](NodeID src, int srcCh, NodeID dst, int dstCh) {
        for (const auto& c2 : engine.getGraph().getConnections())
            if (c2.source.nodeID == src && c2.source.channelIndex == srcCh && c2.destination.nodeID == dst &&
                c2.destination.channelIndex == dstCh)
                return true;
        return false;
    };
    EXPECT_TRUE(hasConn(c, 0, portId, 0)) << "the newly-interior c feeds the new port";
    EXPECT_TRUE(hasConn(portId, 0, ext, 0)) << "the port feeds the still-external ext";
    EXPECT_FALSE(hasConn(c, 0, ext, 0))
        << "the original direct cable was spliced, not left dangling alongside the port";
}

TEST(MacroMembership, AddSelectionToMacroSplicesOutAnExistingPortTheJoiningMemberMakesInterior) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // b starts alone in the macro, wired to `outside` -- groupSelectionIntoMacro(true) gives that
    // crossing a real port immediately.
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto outside = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100); // about to join too
    ASSERT_TRUE(engine.getGraph().addConnection({{outside, 0}, {b, 0}}));
    auto uuidOutside = uuidOf(engine, outside);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "the crossing to `outside` got a real port";
    const auto portUuidBefore = editor.getMacros().find(macroId)->ports.front().nodeUuid;

    // Now `outside` itself joins the SAME macro -- its own connection to b's port is no longer a
    // real external boundary at all (both ends interior), so that port must be spliced back out
    // into a plain direct b<->outside connection, not left as a redundant double-port.
    editor.addSelectionToMacro(macroId, {uuidOutside});

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuidOutside));
    EXPECT_TRUE(macro->ports.empty()) << "the now-redundant port must be spliced out, not left as a "
                                         "no-op double-port in front of a now-interior connection";
    EXPECT_EQ(nodeIdForUuid(engine, portUuidBefore).uid, 0u) << "the old port node itself must be gone";

    bool sawDirect = false;
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == outside && c.source.channelIndex == 0 && c.destination.nodeID == b &&
            c.destination.channelIndex == 0)
            sawDirect = true;
    EXPECT_TRUE(sawDirect) << "outside and b must end up connected directly, with the port's two "
                              "edges cross-connected the way spliceOutMacroPort always does";
}

TEST(MacroMembership, RemoveSelectionFromMacroWithASplicedPortIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    undoManager.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    ASSERT_TRUE(engine.getGraph().addConnection({{a, 0}, {b, 0}}));
    auto uuidA = uuidOf(engine, a);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    undoManager.clearUndoHistory();

    editor.removeSelectionFromMacro(macroId, {uuidA});
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "sanity: this call really did splice a port";

    ASSERT_TRUE(undoManager.canUndo());
    undoManager.undo();
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuidA)) << "one undo must restore both the membership AND the splice";
    EXPECT_TRUE(macro->ports.empty()) << "one undo must also remove the port the removal had created";

    bool sawDirect = false;
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == a && c.source.channelIndex == 0 && c.destination.nodeID == b &&
            c.destination.channelIndex == 0)
            sawDirect = true;
    EXPECT_TRUE(sawDirect) << "the original direct a->b cable must be back too";

    undoManager.redo();
    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(uuidA));
    EXPECT_EQ(macro->ports.size(), 1u);
}
