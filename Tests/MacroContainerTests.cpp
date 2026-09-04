// MacroContainerTests.cpp
// GraphEditor-level tests for Macros — P8-12's named, coloured, collapsible container that groups
// selected modules on the canvas as metadata layered over the existing multi-select system.
//
//   • wrap/unwrap   — Cmd+G/Cmd+Shift+G round trip, min-selection and no-nesting refusals
//   • persistence   — membership + name/colour survive a project bundle save/load
//   • snippets      — a grouped selection round-trips through extract/insert with fresh uuids
//   • delete        — member delete shrinks/dissolves a macro; macro delete removes modules,
//                     ungroup keeps them
//   • collapse      — hides/shows member ModuleComponents, gives the card real bounds
//   • undo          — a macro-only change (no graph delta) refreshes the canvas on undo/redo
//   • cables        — a collapsed macro drops internal cables and re-anchors boundary ones
//   • trust         — an untrusted patch cannot smuggle a "macros" key
//   • recolour      — P8-14: the shared ColourPickerPopup previews live (no undo) and commits as
//                     one undo step spanning original -> final colour
//   • card dbl-click — P8-14: the collapsed card's title row renames in place; elsewhere expands

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PatchDocument.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include "../Source/UI/ModuleComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

/** Adds a module, lays out its ModuleComponent at a known position, and gives it a real
 *  persistent uuid up front — macro membership is keyed by uuid, and the plain graph.addNode()
 *  a bare test would use leaves it empty until something like graphToJSON assigns one lazily. */
NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

ModuleComponent* findComponent(GraphEditor& editor, NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}

} // namespace

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
// Collapse
// ============================================================================

TEST(MacroCollapse, CollapsingHidesMembersAndExpandingShowsThemAgain) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // grouping collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    EXPECT_FALSE(compA->isVisible());
    EXPECT_FALSE(compB->isVisible());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_GT(macro->bounds.getWidth(), 0);
    EXPECT_GT(macro->bounds.getHeight(), 0);

    editor.setMacroCollapsed(macroId, false);
    EXPECT_TRUE(compA->isVisible());
    EXPECT_TRUE(compB->isVisible());

    editor.setMacroCollapsed(macroId, true);
    EXPECT_FALSE(compA->isVisible());
    EXPECT_FALSE(compB->isVisible());
}

TEST(MacroCollapse, ToggleSelectionMacrosCollapsedRoundTripsExpandedAndCollapsed) {
    // Regression test: once expanded, a macro's card (the only UI that offered "Collapse") no
    // longer exists on screen — toggleSelectionMacrosCollapsed() is the actual reachable path
    // back (ModuleComponent's right-click menu and the Cmd+Alt+G shortcut both call it). Unlike
    // the old collapse-only command, it must ALSO expand an already-collapsed macro, and a
    // round trip (toggle, toggle) must return to the original state.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // grouping collapses by default
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    // Collapsed + selection -> expands.
    editor.setSelectedNodes({a});
    editor.toggleSelectionMacrosCollapsed();
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed);

    // Expanded + selection -> collapses (only one member selected — the toggle must still find
    // and act on the whole macro, matching ungroupSelection's "touches at least one selected
    // node" semantics).
    editor.selectModule(a, false);
    editor.toggleSelectionMacrosCollapsed();
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().find(macroId)->collapsed) << "round trip must return to collapsed";
}

TEST(MacroCollapse, ToggleSelectionMacrosCollapsedWithMixedSelectionCollapsesBoth) {
    // A selection spanning one collapsed and one expanded macro must collapse BOTH (the
    // documented "if any touched macro is expanded, collapse them all" rule) rather than acting
    // per-macro or refusing.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto d = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1300, 100);

    editor.setSelectedNodes({a, b});
    auto macroOne = editor.groupSelectionIntoMacro(); // collapsed by default
    ASSERT_FALSE(macroOne.isEmpty());

    editor.setSelectedNodes({c, d});
    auto macroTwo = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroTwo.isEmpty());
    editor.setMacroCollapsed(macroTwo, false); // now expanded

    ASSERT_TRUE(editor.getMacros().find(macroOne)->collapsed);
    ASSERT_FALSE(editor.getMacros().find(macroTwo)->collapsed);

    editor.setSelectedNodes({a, c}); // one member from each macro
    editor.toggleSelectionMacrosCollapsed();

    EXPECT_TRUE(editor.getMacros().find(macroOne)->collapsed) << "already-collapsed macro stays collapsed";
    EXPECT_TRUE(editor.getMacros().find(macroTwo)->collapsed) << "expanded macro must collapse too";
}

TEST(MacroCollapse, ToggleSelectionMacrosCollapsedIsANoOpWithStatusMessageWhenSelectionTouchesNoMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    juce::String lastMessage;
    editor.onStatusMessage = [&](const juce::String& msg) { lastMessage = msg; };

    // Nothing selected at all -> refused.
    editor.toggleSelectionMacrosCollapsed();
    EXPECT_FALSE(lastMessage.isEmpty());

    // A plain, non-macro module selected -> refused (the selection touches no macro).
    lastMessage.clear();
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    editor.selectModule(a, false);
    editor.toggleSelectionMacrosCollapsed();
    EXPECT_FALSE(lastMessage.isEmpty());
    EXPECT_TRUE(editor.getMacros().empty()) << "nothing should have been created or changed";
}

TEST(MacroCollapse, MacroForNodeFindsTheOwningMacroOnlyWhileGrouped) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 0);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 600, 0);

    EXPECT_EQ(editor.macroForNode(a), nullptr);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto* found = editor.macroForNode(a);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, macroId);
    EXPECT_EQ(editor.macroForNode(c), nullptr);

    editor.ungroupSelection();
    EXPECT_EQ(editor.macroForNode(a), nullptr);
}

// ============================================================================
// Group-or-toggle dispatch (P8-14 — Cmd+G, GraphEditor::groupOrToggleSelectionMacros)
// ============================================================================

TEST(MacroGroupOrToggle, SelectionTouchingNoMacroGroups) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.getMacros().empty());

    editor.groupOrToggleSelectionMacros();

    ASSERT_EQ(editor.getMacros().size(), 1) << "no macro touched -> Cmd+G groups";
    EXPECT_NE(editor.macroForNode(a), nullptr);
    EXPECT_NE(editor.macroForNode(b), nullptr);
}

TEST(MacroGroupOrToggle, SelectionWhollyInsideCollapsedMacroExpandsAndCreatesNoNewMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapsed by default
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    editor.setSelectedNodes({a, b}); // wholly inside the one macro
    editor.groupOrToggleSelectionMacros();

    ASSERT_EQ(editor.getMacros().size(), 1) << "must toggle the existing macro, not create a new one";
    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed) << "collapsed selection -> expands";
}

TEST(MacroGroupOrToggle, SelectionWhollyInsideExpandedMacroCollapsesAndCreatesNoNewMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // now expanded

    editor.setSelectedNodes({a, b}); // wholly inside the one macro
    editor.groupOrToggleSelectionMacros();

    ASSERT_EQ(editor.getMacros().size(), 1) << "must toggle the existing macro, not create a new one";
    EXPECT_TRUE(editor.getMacros().find(macroId)->collapsed) << "expanded selection -> collapses";
}

TEST(MacroGroupOrToggle, MixedSelectionTogglesTheMacroAndLeavesLooseModulesAlone) {
    // The mixed-selection rule: one node already in a macro plus one loose node must toggle the
    // touched macro and ignore the loose module — NOT group (the flat model has no nested
    // macros), and NOT refuse (a no-op here reads as broken).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto loose = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapsed by default
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    juce::String lastMessage;
    editor.onStatusMessage = [&](const juce::String& msg) { lastMessage = msg; };

    editor.setSelectedNodes({a, loose}); // a is grouped, loose is not
    editor.groupOrToggleSelectionMacros();

    EXPECT_EQ(editor.getMacros().size(), 1) << "must not create a second macro";
    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed) << "the touched macro must still toggle";
    EXPECT_EQ(editor.macroForNode(loose), nullptr) << "the loose module must not be pulled into the macro";
    EXPECT_FALSE(lastMessage.isEmpty()) << "the mixed-selection outcome must be explained";
}

TEST(MacroGroupOrToggle, SingleLooseModuleStillRefusesViaGroupSelectionIntoMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    juce::String lastMessage;
    editor.onStatusMessage = [&](const juce::String& msg) { lastMessage = msg; };

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 0, 0);
    editor.selectModule(a, false);

    editor.groupOrToggleSelectionMacros();

    EXPECT_TRUE(editor.getMacros().empty()) << "fewer than two modules must still refuse to group";
    EXPECT_FALSE(lastMessage.isEmpty());
}

// ============================================================================
// Hull (Fix 2/4 — click-to-select and right-click-menu inside an expanded macro)
// ============================================================================

TEST(MacroHull, HullBoundsIsEmptyWhileCollapsedAndTheMemberUnionWhileExpanded) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    EXPECT_TRUE(editor.macroHullBounds(macroId).isEmpty()) << "a collapsed macro has no hull";
    EXPECT_TRUE(editor.macroHullBounds("no-such-macro-id").isEmpty());

    editor.setMacroCollapsed(macroId, false);
    const auto hull = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    EXPECT_TRUE(hull.contains(compA->getBounds())) << "the hull must cover every member's live bounds";
    EXPECT_TRUE(hull.contains(compB->getBounds()));
}

TEST(MacroHull, HullAtHitsInsideAndMissesOutsideAndWhileCollapsed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // Collapsed: no hull to hit, anywhere.
    EXPECT_TRUE(editor.macroHullAt({150, 150}).isEmpty());

    editor.setMacroCollapsed(macroId, false);
    const auto hull = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());

    EXPECT_EQ(editor.macroHullAt(hull.getCentre()), macroId);
    EXPECT_TRUE(editor.macroHullAt(juce::Point<int>(hull.getX() - 500, hull.getY() - 500)).isEmpty());
}

// ============================================================================
// Chip (P8-14 — the expanded hull's name-chip drag handle)
// ============================================================================

TEST(MacroChip, ChipBoundsIsEmptyWhileCollapsedAndSitsOnTheHullsTopEdgeWhileExpanded) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    EXPECT_TRUE(editor.macroChipBounds(macroId).isEmpty()) << "a collapsed macro has no chip";
    EXPECT_TRUE(editor.macroChipBounds("no-such-macro-id").isEmpty());

    editor.setMacroCollapsed(macroId, false);
    const auto hull = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());

    const auto chip = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chip.isEmpty());
    EXPECT_EQ(chip.getY(), hull.getY()) << "the chip sits on the hull's top edge";
    EXPECT_GE(chip.getX(), hull.getX()) << "the chip stays within the hull's horizontal span";
    EXPECT_LE(chip.getRight(), hull.getRight());
}

TEST(MacroChip, ChipAtHitsInsideAndMissesJustOutsideAndWhileCollapsed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // Collapsed: no chip to hit, anywhere.
    EXPECT_TRUE(editor.macroChipAt({150, 150}).isEmpty());

    editor.setMacroCollapsed(macroId, false);
    const auto chip = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chip.isEmpty());

    EXPECT_EQ(editor.macroChipAt(chip.getCentre()), macroId);
    // Just past the chip's right/bottom edge — still comfortably inside the hull, so a miss here
    // proves the hit-test is scoped to the chip itself, not the whole hull.
    EXPECT_TRUE(editor.macroChipAt(juce::Point<int>(chip.getRight() + 5, chip.getBottom() + 5)).isEmpty());
}

// ============================================================================
// Chip drag (P8-14 — dragging the chip moves the whole macro as a rigid body)
// ============================================================================

TEST(MacroChipDrag, DraggingMovesEveryMemberByTheDragDeltaAsARigidBody) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    const auto startA = compA->getPosition();
    const auto startB = compB->getPosition();
    const auto startOffset = startB - startA;

    // Synthesizing real mouse events into GraphEditor is not reliable headless (no real OS event
    // loop/window to deliver them through); drive the same primitives GraphEditor::mouseDown/
    // mouseDrag/mouseUp use for a chip drag directly instead.
    editor.selectMacro(macroId, false);
    editor.beginSelectionDrag();
    const juce::Point<int> delta(120, 40);
    editor.dragSelectionBy(delta, nullptr);

    EXPECT_EQ(compA->getPosition(), startA + delta);
    EXPECT_EQ(compB->getPosition(), startB + delta);
    EXPECT_EQ(compB->getPosition() - compA->getPosition(), startOffset) << "the group must move as a single rigid body";

    editor.finalizeSelectionDrag();
    EXPECT_EQ(compB->getPosition() - compA->getPosition(), startOffset)
        << "finalize applies one uniform snap/de-overlap offset to the whole group, preserving "
           "relative member positions";
}

TEST(MacroChipDrag, ChipDragIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    const auto startA = compA->getPosition();
    const auto startB = compB->getPosition();

    // Mirrors GraphEditor::mouseDown/mouseDrag/mouseUp's exact sequence for a chip drag
    // (captureBeforeState before the drag starts, pushSnapshotFromCapture once it finalizes) —
    // see the comment on the previous test for why this drives the primitives directly rather
    // than synthesizing mouse events.
    editor.selectMacro(macroId, false);
    undo.captureBeforeState(engine.getGraph());
    editor.beginSelectionDrag();
    editor.dragSelectionBy({120, 40}, nullptr);
    editor.finalizeSelectionDrag();
    undo.pushSnapshotFromCapture(engine.getGraph());

    EXPECT_NE(compA->getPosition(), startA) << "sanity: the drag actually moved something";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(compA->getPosition(), startA) << "a single undo restores every member's original position";
    EXPECT_EQ(compB->getPosition(), startB);
}

// ============================================================================
// Rename dialog (Fix 5 — the hull menu's AlertWindow affordance)
// ============================================================================

TEST(MacroRename, RenameMacroHasNoEmptyInputGuardOfItsOwnTheDialogCallbackProvidesIt) {
    // promptRenameMacro() pops a real juce::AlertWindow and enters a real modal state -- calling
    // it here would hang a headless test run the same way this repo's other AlertWindow-driven
    // dialogs avoid doing (see PianoRollTests/AutosaveTests' own comments on this). Its
    // "empty/whitespace-only input cancels without renaming" contract lives entirely in the
    // dialog's own ModalCallbackFunction (a `typed.isEmpty()` early-return BEFORE ever calling
    // renameMacro -- see GraphEditor::promptRenameMacro), so it is exercised here at the layer
    // that IS testable headless: renameMacro() itself takes whatever string it is given, which is
    // exactly why the guard has to live in the callback rather than in renameMacro.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(editor.getMacros().find(macroId)->name, juce::String("Macro"));

    // What the dialog's callback does on non-empty trimmed input.
    editor.renameMacro(macroId, "Filter Chain");
    EXPECT_EQ(editor.getMacros().find(macroId)->name, juce::String("Filter Chain"));

    // renameMacro alone has no guard against an empty name -- proving the dialog's own
    // `typed.isEmpty()` check (never reached in this test) is load-bearing, not redundant.
    editor.renameMacro(macroId, "");
    EXPECT_TRUE(editor.getMacros().find(macroId)->name.isEmpty())
        << "renameMacro itself sets whatever it's given; promptRenameMacro's callback is what "
           "keeps an empty/whitespace-only typed value from ever reaching it";
}

// ============================================================================
// Undo
// ============================================================================

TEST(MacroUndo, UndoOfGroupRefreshesTheCanvasAndRedoReCollapses) {
    // Regression test: undo/redo of a macro-only change (group/ungroup/rename/recolour/collapse,
    // no graph delta) restores MacroSet state correctly but used to leave member ModuleComponents
    // stuck at their pre-undo visibility because nothing called GraphEditor::updateComponents().
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    ASSERT_TRUE(compA->isVisible());
    ASSERT_TRUE(compB->isVisible());

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    EXPECT_FALSE(compA->isVisible());
    EXPECT_FALSE(compB->isVisible());

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_TRUE(editor.getMacros().empty());
    EXPECT_TRUE(compA->isVisible()) << "undo of a macro-only change must refresh the canvas";
    EXPECT_TRUE(compB->isVisible());

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(editor.getMacros().size(), 1);
    EXPECT_FALSE(compA->isVisible());
    EXPECT_FALSE(compB->isVisible());
}

TEST(MacroUndo, UndoOfRenameAndRecolourRefreshesTheCanvas) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.renameMacro(macroId, "Renamed");
    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    auto* macroAfterUndo = editor.getMacros().find(macroId);
    ASSERT_NE(macroAfterUndo, nullptr);
    EXPECT_EQ(macroAfterUndo->name, juce::String("Macro")) << "rename must be undoable";

    ASSERT_TRUE(undo.canRedo());
    undo.redo();
    auto* macroAfterRedo = editor.getMacros().find(macroId);
    ASSERT_NE(macroAfterRedo, nullptr);
    EXPECT_EQ(macroAfterRedo->name, juce::String("Renamed"));

    editor.setMacroColour(macroId, juce::Colour(0xff112233));
    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    auto* macroColourUndo = editor.getMacros().find(macroId);
    ASSERT_NE(macroColourUndo, nullptr);
    EXPECT_NE(macroColourUndo->colour, juce::Colour(0xff112233));

    ASSERT_TRUE(undo.canRedo());
    undo.redo();
    auto* macroColourRedo = editor.getMacros().find(macroId);
    ASSERT_NE(macroColourRedo, nullptr);
    EXPECT_EQ(macroColourRedo->colour, juce::Colour(0xff112233));
}

// ============================================================================
// Cables
// ============================================================================

TEST(MacroCable, CollapsedMacroHidesInternalCablesAndReanchorsBoundaryCrossingOnes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400, 100);
    auto vca = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);

    ASSERT_TRUE(engine.getGraph().addConnection({{osc, 0}, {filter, 0}}));
    ASSERT_TRUE(engine.getGraph().addConnection({{filter, 0}, {vca, 0}}));

    // Positive control: both cables must actually be enumerated BEFORE collapsing, or the
    // assertions below (especially "the internal cable is absent") would pass vacuously in a
    // headless setup where buildVisibleCables() sees nothing at all.
    ASSERT_EQ(editor.getVisibleCableCount(), 2);

    editor.setSelectedNodes({osc, filter});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_TRUE(macro->collapsed);

    const auto& cables = editor.buildVisibleCables();

    bool sawInternal = false;
    bool sawBoundary = false;
    for (const auto& cable : cables) {
        if (cable.id.srcUid == osc.uid && cable.id.dstUid == filter.uid)
            sawInternal = true;
        if (cable.id.srcUid == filter.uid && cable.id.dstUid == vca.uid) {
            sawBoundary = true;
            // Anchored to the point where the ray from the card's centre toward the other
            // endpoint (vca, to the right) exits the card's rectangle — not floating at a fixed
            // point unrelated to the rest of the wire. vca sits to the right of the macro card,
            // so the landing point should be on the boundary and on/right of centre.
            const auto bounds = macro->bounds.toFloat();
            const bool onVerticalEdge = juce::approximatelyEqual(cable.p1.x, bounds.getX()) ||
                                        juce::approximatelyEqual(cable.p1.x, bounds.getRight());
            const bool onHorizontalEdge = juce::approximatelyEqual(cable.p1.y, bounds.getY()) ||
                                          juce::approximatelyEqual(cable.p1.y, bounds.getBottom());
            EXPECT_TRUE(onVerticalEdge || onHorizontalEdge)
                << "the hidden endpoint must land on the macro card's edge, not float at an arbitrary point";
            EXPECT_GE(cable.p1.x, bounds.getCentreX())
                << "the endpoint should face the direction of the module it connects to";
        }
    }
    EXPECT_FALSE(sawInternal) << "a cable wholly inside a collapsed macro must not be drawn";
    EXPECT_TRUE(sawBoundary) << "a cable crossing the macro boundary must still be drawn";
}

TEST(MacroCable, BoundaryCableTracksTheLiveCardBoundsBeforeFinalizeMacroCardDrag) {
    // Fix 3 (P8-12 follow-up): rebuildVisibleCables() used to anchor on the PERSISTED
    // macro.bounds, which finalizeMacroCardDrag only writes on drop -- so a boundary cable stayed
    // pointed at the card's pre-drag position for the whole gesture. Moving the live
    // MacroCardComponent directly (never calling finalizeMacroCardDrag) reproduces "mid-drag"
    // without needing a real mouse gesture.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 400, 100);
    auto vca = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 700, 100);

    ASSERT_TRUE(engine.getGraph().addConnection({{osc, 0}, {filter, 0}}));
    ASSERT_TRUE(engine.getGraph().addConnection({{filter, 0}, {vca, 0}}));

    editor.setSelectedNodes({osc, filter});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const auto movedTopLeft = card->getPosition() + juce::Point<int>(0, 400);
    card->setTopLeftPosition(movedTopLeft); // NEVER calls finalizeMacroCardDrag

    ASSERT_NE(editor.getMacros().find(macroId)->bounds.getPosition(), movedTopLeft)
        << "the persisted macro.bounds must NOT have moved yet -- that staleness is exactly what "
           "Fix 3 has to see past";

    const auto& cables = editor.buildVisibleCables();
    bool sawBoundary = false;
    for (const auto& cable : cables) {
        if (cable.id.srcUid == filter.uid && cable.id.dstUid == vca.uid) {
            sawBoundary = true;
            const auto liveBounds = card->getBounds().toFloat();
            const bool onVerticalEdge = juce::approximatelyEqual(cable.p1.x, liveBounds.getX()) ||
                                        juce::approximatelyEqual(cable.p1.x, liveBounds.getRight());
            const bool onHorizontalEdge = juce::approximatelyEqual(cable.p1.y, liveBounds.getY()) ||
                                          juce::approximatelyEqual(cable.p1.y, liveBounds.getBottom());
            EXPECT_TRUE(onVerticalEdge || onHorizontalEdge)
                << "the endpoint must track the card's LIVE (moved) bounds, not the stale persisted "
                   "macro.bounds";
        }
    }
    ASSERT_TRUE(sawBoundary);
}

// ============================================================================
// Trust boundary
// ============================================================================

TEST(MacroTrust, UntrustedPatchWithMacrosKeyIsRefused) {
    juce::AudioProcessorGraph graph;

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", juce::var(juce::Array<juce::var>()));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    root->setProperty("macros", juce::var(juce::Array<juce::var>()));
    juce::var json(root.get());

    auto untrustedResult = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/false, /*trusted=*/false);
    EXPECT_FALSE(untrustedResult.ok);
    EXPECT_EQ(untrustedResult.error, synth::PatchValidationError::MacrosNotAllowed);

    auto trustedResult = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/false, /*trusted=*/true);
    EXPECT_TRUE(trustedResult.ok) << "the trusted path does not carry this refusal";
}

TEST(MacroUndo, TogglingASelectionSpanningTwoMacrosIsOneUndoStep) {
    // One gesture, one undo entry. toggleSelectionMacrosCollapsed used to call setMacroCollapsed
    // in a loop, and setMacroCollapsed records its own recordGraphAndMacroChange — so a single
    // Cmd+Alt+G over a selection spanning two macros pushed TWO undo entries and needed two
    // Cmd+Z to reverse. The undo history should mirror the gesture the user made, not the number
    // of macros it happened to reach; hence applyMacroCollapsed (the unrecorded mutation) inside
    // one recorded change.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto d = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1300, 100);

    editor.setSelectedNodes({a, b});
    auto macroOne = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroOne.isEmpty());
    editor.setSelectedNodes({c, d});
    auto macroTwo = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroTwo.isEmpty());

    // Both start collapsed (groupSelectionIntoMacro collapses by default), so one toggle expands
    // both -- a single gesture that touches two macros.
    ASSERT_TRUE(editor.getMacros().find(macroOne)->collapsed);
    ASSERT_TRUE(editor.getMacros().find(macroTwo)->collapsed);

    editor.setSelectedNodes({a, c}); // one member from each macro
    editor.toggleSelectionMacrosCollapsed();
    ASSERT_FALSE(editor.getMacros().find(macroOne)->collapsed);
    ASSERT_FALSE(editor.getMacros().find(macroTwo)->collapsed);

    // Exactly ONE undo must put both macros back, not one macro per undo.
    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_TRUE(editor.getMacros().find(macroOne)->collapsed) << "a single undo must reverse the whole toggle gesture";
    EXPECT_TRUE(editor.getMacros().find(macroTwo)->collapsed)
        << "the second macro must be restored by the SAME undo step, not a later one";
}

// ============================================================================
// Chip drag through the REAL mouse path
// ============================================================================
//
// The chip-drag tests above drive selectMacro/beginSelectionDrag/dragSelectionBy directly, which
// exercises the API layer BENEATH GraphEditor::mouseDown/mouseDrag/mouseUp. That is exactly the
// layer a broken hit-test cannot fail in, so those tests stayed green while the gesture was dead
// on the canvas. These drive synthesised mouse events into GraphEditor itself, the same way
// GraphEditorTests.cpp and MinimapComponentTests.cpp already do.

namespace {

juce::MouseEvent makeCanvasMouseEvent(juce::Component& comp, juce::Point<int> position, int clicks = 1) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), clicks,
                            false);
}

} // namespace

TEST(MacroChipDrag, ChipRectNeverOverlapsAMemberModule) {
    // The chip is painted, not a component, so it has no z-order of its own: wherever it overlaps
    // a member's ModuleComponent, that component wins the click and drags ITSELF instead of the
    // macro. The chip must therefore sit entirely clear of every member's bounds.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // expanded: the hull and chip exist

    const auto chip = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chip.isEmpty());

    for (auto id : {a, b}) {
        auto* comp = findComponent(editor, id);
        ASSERT_NE(comp, nullptr);
        EXPECT_FALSE(chip.intersects(comp->getBounds()))
            << "chip " << chip.toString() << " overlaps member " << comp->getBounds().toString()
            << " - the member's component will swallow clicks in the overlap";
    }
}

TEST(MacroChipDrag, PressingAndDraggingTheChipMovesTheMacroThroughTheRealMousePath) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    // Canvas coordinates equal GraphEditor-local coordinates only at the identity transform, which
    // is the freshly constructed editor's state. Assert it rather than assume it - a future default
    // pan or zoom would otherwise turn this into a silently mis-aimed click that still passes.
    const auto visible = editor.getVisibleCanvasRect();
    ASSERT_FLOAT_EQ(visible.getX(), 0.0f);
    ASSERT_FLOAT_EQ(visible.getY(), 0.0f);
    ASSERT_FLOAT_EQ(visible.getWidth(), (float)editor.getWidth());

    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);
    const auto startA = compA->getPosition();
    const auto startB = compB->getPosition();

    const auto chipCentre = editor.macroChipBounds(macroId).getCentre();
    const juce::Point<int> delta(120, 80);

    editor.mouseDown(makeCanvasMouseEvent(editor, chipCentre));
    editor.mouseDrag(makeCanvasMouseEvent(editor, chipCentre + delta));
    editor.mouseUp(makeCanvasMouseEvent(editor, chipCentre + delta));

    EXPECT_NE(compA->getPosition(), startA) << "pressing the chip and dragging must move the macro";
    EXPECT_NE(compB->getPosition(), startB);

    // Rigid body: both members keep their relative offset (finalizeSelectionDrag snaps the group
    // as a whole, so the absolute delta may be nudged, but the offset between members must not be).
    EXPECT_EQ(compB->getPosition() - compA->getPosition(), startB - startA);
}

// ============================================================================
// Recolour (P8-14): "Change Colour..." opens the shared synth::ui::ColourPickerPopup, with a
// live preview (no undo) and exactly one undo step spanning original -> final colour on commit.
// ============================================================================
//
// promptRecolourMacro launches a real juce::CallOutBox, which never runs in the headless test
// process. createMacroColourPickerForTest() is the seam that hands back the SAME popup, wired
// with the SAME onPreview/onCommit callbacks buildMacroColourPicker builds for the real path
// (mirrors TimelineRulerComponent::createMarkerColourPickerForTest()) -- the tests below drive
// its preview/commit through ColourPickerPopup's own test seams (setCurrentColourForTest /
// commitForTest) rather than duplicating the recolour logic here.

TEST(MacroRecolour, PreviewThenCommitToADifferentColourIsOneUndoStepThatRestoresTheOriginal) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto originalColour = editor.getMacros().find(macroId)->colour;
    const juce::Colour intermediateColour(0xffaa11bbu);
    const juce::Colour finalColour(0xff33cc44u);
    ASSERT_NE(intermediateColour, originalColour);
    ASSERT_NE(finalColour, originalColour);

    auto popup = editor.createMacroColourPickerForTest(macroId);
    ASSERT_NE(popup, nullptr);

    const int serialBeforeRecolour = undo.getEditSerial();

    popup->setCurrentColourForTest(intermediateColour); // simulates the user mid-drag
    EXPECT_EQ(editor.getMacros().find(macroId)->colour, intermediateColour)
        << "a preview must write straight to the macro";
    EXPECT_EQ(undo.getEditSerial(), serialBeforeRecolour) << "a preview must not push an undo step";

    popup->setCurrentColourForTest(finalColour); // the drag settles here
    popup->commitForTest();

    EXPECT_EQ(editor.getMacros().find(macroId)->colour, finalColour);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeRecolour + 1)
        << "commit to a different colour must be exactly ONE undo step";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(editor.getMacros().find(macroId)->colour, originalColour)
        << "the single undo step must restore the ORIGINAL colour, not the intermediate preview value";
}

TEST(MacroRecolour, PreviewThenCommitBackToTheOriginalColourPushesNoUndoEntry) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto originalColour = editor.getMacros().find(macroId)->colour;
    const juce::Colour driftedColour(0xffaa11bbu);
    ASSERT_NE(driftedColour, originalColour);

    auto popup = editor.createMacroColourPickerForTest(macroId);
    ASSERT_NE(popup, nullptr);

    const int serialBeforeRecolour = undo.getEditSerial();

    popup->setCurrentColourForTest(driftedColour);  // a preview nudges the colour away
    popup->setCurrentColourForTest(originalColour); // ... and the drag settles back on the original
    popup->commitForTest();

    EXPECT_EQ(editor.getMacros().find(macroId)->colour, originalColour);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeRecolour) << "a no-net-change commit must push NO undo entry";
}

// ============================================================================
// Collapsed-card double-click (P8-14): title row renames in place (like ModuleComponent's own
// title), anywhere else on the card still expands.
// ============================================================================

namespace {
// Matches MacroCardComponent::getTitleRowBounds() exactly (280x90 card: getLocalBounds().
// reduced(10, 6), top 20 px, minus the right-hand 28 px chevron reservation) -- a point safely
// inside x:[10,242) y:[6,26). Kept here rather than exposing the private helper to tests: paint()
// and mouseDoubleClick() sharing the ONE method is what this whole change is guarding.
constexpr juce::Point<int> kTitleRowPoint(40, 16);
constexpr juce::Point<int> kBodyPoint(140, 60); // below the title row, still inside the card
} // namespace

TEST(MacroCardDoubleClick, TitleRowStartsInlineRenameAndDoesNotExpand) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_FALSE(card->isRenamingTitle());

    card->mouseDoubleClick(makeCanvasMouseEvent(*card, kTitleRowPoint, 2));

    EXPECT_TRUE(card->isRenamingTitle());
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().find(macroId)->collapsed) << "a title-row double-click must not expand the macro";

    card->finishRename(false); // tidy up the open editor before the test ends
}

TEST(MacroCardDoubleClick, OutsideTitleRowExpandsAndDoesNotRename) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    card->mouseDoubleClick(makeCanvasMouseEvent(*card, kBodyPoint, 2));

    EXPECT_FALSE(card->isRenamingTitle());
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed)
        << "a double-click outside the title row must still expand, as before";
}

TEST(MacroCardDoubleClick, TitleRowRenameCancelsAnyArmedCardDragSoMouseUpIsANoOp) {
    // Regression guard: mouseDown already arms a card drag (dragStartPosition/bodyDragActive/
    // dragger.startDraggingComponent/owner.beginMacroCardDrag) before a double-click's second
    // press resolves. If opening the rename editor didn't cancel that drag, this mouseUp would
    // resolve it as a real (zero-delta) drag -- or worse, leave it armed for whatever gesture
    // comes next.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);

    const auto cardStart = card->getPosition();
    const auto startA = compA->getPosition();
    const auto startB = compB->getPosition();
    const int serialBeforeGesture = undo.getEditSerial();

    card->mouseDown(makeCanvasMouseEvent(*card, kTitleRowPoint)); // arms a card drag
    card->mouseDoubleClick(makeCanvasMouseEvent(*card, kTitleRowPoint, 2));
    ASSERT_TRUE(card->isRenamingTitle());
    card->mouseUp(makeCanvasMouseEvent(*card, kTitleRowPoint));

    EXPECT_EQ(card->getPosition(), cardStart) << "no drag should have resolved after the rename opened";
    EXPECT_EQ(compA->getPosition(), startA);
    EXPECT_EQ(compB->getPosition(), startB);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeGesture)
        << "a stuck drag resolving on mouseUp would push a finalize-drag undo step";

    card->finishRename(false); // tidy up the open editor before the test ends
}

// ============================================================================
// Right-click on a macro MEMBER module also offers the macro (founder-review item 4)
// ============================================================================
//
// GraphEditor::mouseDown already offers the macro's own menu when a right-click lands on EMPTY
// canvas inside an expanded macro's hull (macroHullAt() -> buildMacroMenu(), with
// selectMacro(hullMacroId, false) called first because buildMacroMenu()'s "Ungroup"/"Save as
// Snippet..." act on the CURRENT SELECTION, not the macro id passed in). This section covers the
// companion path: right-clicking a MEMBER MODULE itself
// (ModuleComponent::buildModuleContextMenu()), which appends the same buildMacroMenu() items as a
// "Macro: <name>" submenu when -- and only when -- the clicked module resolves to a macro via
// MacroSet::findByMember(). Because a member module's own right-click menu must NOT disturb the
// module selection its OWN items (Copy/Duplicate/Delete Module...) act on, buildMacroMenu() itself
// was changed to select its macro immediately before running "Ungroup"/"Save as Snippet...",
// rather than relying on a pre-select from the call site the way the hull path does.

namespace {

juce::MouseEvent makeModuleRightClick(ModuleComponent& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

/** A point on the card body that is neither a jack nor the header's double-click-to-rename band --
 *  the ordinary "right-click the body" target every test below uses. */
juce::Point<int> bodyClickPoint(const ModuleComponent& comp) { return {comp.getWidth() / 2, comp.getHeight() - 10}; }

/** Finds an item anywhere in `menu`, including inside submenus, by its exact text -- the item's
 *  `.action` is what a real click on it would invoke. Returns nullptr if not found. */
const juce::PopupMenu::Item* findMenuItemByText(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next()) {
        if (it.getItem().text == text)
            return &it.getItem();
    }
    return nullptr;
}

bool anyMenuItemStartsWith(const juce::PopupMenu& menu, const juce::String& prefix) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text.startsWith(prefix))
            return true;
    return false;
}

} // namespace

TEST(MacroMemberContextMenu, ModuleInNoMacroMenuIsUnchanged) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = findComponent(editor, a);
    ASSERT_NE(comp, nullptr);
    ASSERT_EQ(editor.macroForNode(a), nullptr) << "precondition: this module is in no macro";

    comp->mouseDown(makeModuleRightClick(*comp, bodyClickPoint(*comp)));
    EXPECT_TRUE(editor.isNodeSelected(a)) << "the ordinary right-click retarget must still run";

    const auto menu = comp->buildModuleContextMenu();
    EXPECT_FALSE(anyMenuItemStartsWith(menu, "Macro: "))
        << "a module in no macro must see no macro submenu at all -- no change from before this fix";
    EXPECT_NE(findMenuItemByText(menu, "Delete Module"), nullptr) << "the module's own items are still there";
}

TEST(MacroMemberContextMenu, RightClickingAMacroMemberOffersTheMacroSubmenu) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // expand: members are what get right-clicked

    // Select something OTHER than `a` first, so the retarget below is actually observable.
    editor.setSelectedNodes({b});
    ASSERT_FALSE(editor.isNodeSelected(a));

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);
    compA->mouseDown(makeModuleRightClick(*compA, bodyClickPoint(*compA)));

    // Went through the real gesture entry point: mouseDown's own hit-test/retarget logic moved
    // the selection onto the clicked module, collapsing it off `b`.
    EXPECT_TRUE(editor.isNodeSelected(a));
    EXPECT_FALSE(editor.isNodeSelected(b));

    const auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);

    const auto menu = compA->buildModuleContextMenu();
    const auto* macroSubmenu = findMenuItemByText(menu, "Macro: " + macro->name);
    ASSERT_NE(macroSubmenu, nullptr) << "a member module's own menu must offer its macro's options";
    ASSERT_NE(macroSubmenu->subMenu, nullptr);

    // Spot-check a few of buildMacroMenu()'s own items made it into the submenu, so this can't
    // silently pass against an empty or unrelated submenu.
    EXPECT_NE(findMenuItemByText(menu, "Ungroup"), nullptr);
    EXPECT_NE(findMenuItemByText(menu, "Configure I/O..."), nullptr);
    EXPECT_NE(findMenuItemByText(menu, "Delete Macro && Modules"), nullptr);
}

TEST(MacroMemberContextMenu, UngroupFromTheSubmenuDissolvesTheRightMacroDespiteAMixedSelection) {
    // The load-bearing trap: buildMacroMenu()'s "Ungroup" acts on the CURRENT SELECTION
    // (ungroupSelection()), not on the macro id it was built for. A member module's right-click
    // does NOT retarget selection when the clicked module is already part of a multi-selection
    // (mouseDown's "if (!owner.isNodeSelected(nodeId))" guard skips), so a selection spanning two
    // macros can genuinely reach the submenu's "Ungroup" unchanged. This must still dissolve only
    // the ONE macro whose submenu was opened.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 100);
    editor.setSelectedNodes({a1, a2});
    auto macroA = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroA.isEmpty());
    editor.setMacroCollapsed(macroA, false);

    auto b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 700, 100);
    auto b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    editor.setSelectedNodes({b1, b2});
    auto macroB = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroB.isEmpty());
    editor.setMacroCollapsed(macroB, false);

    // A mixed selection spanning BOTH macros, with b1 (about to be right-clicked) already part of
    // it -- mouseDown's retarget guard therefore does NOT fire, and the selection at click time
    // genuinely stays {a1, b1} right through to whichever submenu item gets invoked.
    editor.setSelectedNodes({a1, b1});
    ASSERT_TRUE(editor.isNodeSelected(a1));
    ASSERT_TRUE(editor.isNodeSelected(b1));

    auto* compB1 = findComponent(editor, b1);
    ASSERT_NE(compB1, nullptr);
    compB1->mouseDown(makeModuleRightClick(*compB1, bodyClickPoint(*compB1)));

    // Confirms the retarget guard really did skip: selection is exactly as set up above, a
    // "different module was selected beforehand" (a1, in the OTHER macro).
    EXPECT_TRUE(editor.isNodeSelected(a1));
    EXPECT_TRUE(editor.isNodeSelected(b1));

    const auto menu = compB1->buildModuleContextMenu();
    const auto* ungroupItem = findMenuItemByText(menu, "Ungroup");
    ASSERT_NE(ungroupItem, nullptr);
    ASSERT_TRUE(static_cast<bool>(ungroupItem->action));

    ungroupItem->action();

    EXPECT_EQ(editor.getMacros().find(macroB), nullptr)
        << "the RIGHT macro (B -- the one whose submenu was actually opened) must dissolve";
    ASSERT_NE(editor.getMacros().find(macroA), nullptr)
        << "macro A must survive -- a naive graft (no select-before-act) would have ungrouped it "
           "too, since a1 was still part of the selection at click time";
    EXPECT_EQ(editor.getMacros().find(macroA)->members.size(), 2u);

    // Ungrouping never deletes member nodes -- all four modules must still be real graph nodes.
    for (auto id : {a1, a2, b1, b2})
        EXPECT_NE(engine.getGraph().getNodeForId(id), nullptr);
}
