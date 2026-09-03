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
