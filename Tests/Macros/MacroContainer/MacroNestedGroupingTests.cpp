// MacroNestedGroupingTests.cpp
// Making nested macros (docs/macros/menu-and-membership.md#grouping-rules): the selection-to-units
// resolver, Create Macro from the real canvas and module right-click menus, the Cmd+G matrix, refusals,
// ports across a new parent, undo/redo, and a ProjectBundle save/load round trip.

#include "MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/MacroGroupController/MacroSelectionUnits.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {

// Two flat top-level macros A = {a1, a2} and B = {b1, b2}, both collapsed, plus a loose module.
struct TwoMacros {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a1, a2, b1, b2, loose;
    juce::String macroA, macroB;
    juce::String lastMessage;

    explicit TwoMacros(bool autoPortsForA = false, const std::function<void(TwoMacros&)>& beforeGrouping = {}) {
        undo.setGraphEditor(&editor);
        editor.setSize(2000, 1600);
        editor.onStatusMessage = [this](const juce::String& msg) { lastMessage = msg; };
        a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
        b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 700);
        b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 700);
        loose = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 1000, 400);
        if (beforeGrouping)
            beforeGrouping(*this);
        editor.setSelectedNodes({a1, a2});
        macroA = ctl().groupSelectionIntoMacro(autoPortsForA);
        editor.setSelectedNodes({b1, b2});
        macroB = ctl().groupSelectionIntoMacro();
        editor.clearSelection();
        undo.clearUndoHistory();
        lastMessage.clear();
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    const synth::MacroSet& macros() { return editor.getMacros(); }
    juce::String uuid(NodeID id) { return uuidOf(engine, id); }
    juce::String ownerOf(NodeID id) {
        const auto* m = macros().findByMember(uuid(id));
        return m != nullptr ? m->id : juce::String();
    }
    // The macro that is neither A nor B (the one a nest just made), or empty.
    juce::String newMacro() {
        for (const auto& m : macros().getAll())
            if (m.id != macroA && m.id != macroB)
                return m.id;
        return {};
    }
};

juce::MouseEvent rightClickAt(juce::Component& comp, juce::Point<int> pos) {
    const auto p = pos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

// Runs the menu item whose text starts with `prefix`; false when there is none.
bool runItemStartingWith(juce::PopupMenu& menu, const juce::String& prefix) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text.startsWith(prefix) && it.getItem().action != nullptr) {
            it.getItem().action();
            return true;
        }
    return false;
}

// Right-clicks the editor's empty top-right corner (the canvas menu) and runs "Create Macro from ...".
bool createMacroFromCanvasMenu(GraphEditor& editor) {
    juce::PopupMenu captured;
    bool shown = false;
    editor.setShowCanvasContextMenuHookForTest([&](juce::PopupMenu& m) {
        captured = m;
        shown = true;
    });
    const juce::Point<int> emptyCorner(editor.getWidth() - 20, 20);
    editor.mouseDown(rightClickAt(editor, emptyCorner));
    editor.setShowCanvasContextMenuHookForTest(nullptr);
    return shown && runItemStartingWith(captured, "Create Macro from");
}

// Right-clicks `id`'s module card and runs "Create Macro from ..." off its own menu.
bool createMacroFromModuleMenu(GraphEditor& editor, NodeID id) {
    auto* comp = findComponent(editor, id);
    if (comp == nullptr)
        return false;
    juce::PopupMenu captured;
    comp->setShowContextMenuHookForTest([&captured](juce::PopupMenu& m) { captured = m; });
    const juce::Point<int> body(comp->getWidth() / 2, comp->getHeight() - 10);
    comp->mouseDown(rightClickAt(*comp, body));
    comp->setShowContextMenuHookForTest(nullptr);
    return runItemStartingWith(captured, "Create Macro from");
}

void selectMacros(TwoMacros& t, std::initializer_list<juce::String> ids, std::initializer_list<NodeID> extra = {}) {
    t.editor.clearSelection();
    for (const auto& id : ids)
        t.ctl().selectMacro(id, /*additive=*/true);
    auto sel = t.editor.getSelectedNodes();
    for (auto id : extra)
        sel.push_back(id);
    t.editor.setSelectedNodes(sel);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The resolver
// ---------------------------------------------------------------------------------------------

TEST(MacroSelectionUnits, ResolvesOutermostWholeMacrosAndTagsLooseNodesWithTheirContainer) {
    synth::MacroSet set;
    synth::Macro outer;
    outer.id = "outer";
    outer.members = {"o1", "portO"};
    outer.ports = {{"portO", true, "In"}};
    set.add(outer);
    synth::Macro inner;
    inner.id = "inner";
    inner.members = {"i1", "i2"};
    inner.parentId = "outer";
    set.add(inner);

    auto units = [&](std::vector<juce::String> uuids) {
        std::vector<macro_units::SelectedNode> sel;
        juce::uint32 next = 1;
        for (const auto& u : uuids)
            sel.push_back({NodeID(next++), u});
        return macro_units::resolveUnits(set, sel);
    };

    // Every module of outer (its port left out) is one unit: outer itself, at top level.
    auto all = units({"o1", "i1", "i2", "top"});
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].macroId, "outer") << "the outermost whole macro wins, and port nodes never decide wholeness";
    EXPECT_EQ(all[1].uuid, "top");
    EXPECT_TRUE(macro_units::commonContainer(all).has_value());

    // The inner macro whole plus outer's own module: two units inside outer.
    auto nested = units({"i1", "i2", "o1x"});
    ASSERT_EQ(nested.size(), 2u);
    EXPECT_EQ(nested[0].macroId, "inner");
    EXPECT_EQ(nested[0].container, "outer");
    EXPECT_EQ(nested[1].container, "") << "an unknown uuid is a loose top-level node";
    EXPECT_FALSE(macro_units::commonContainer(nested).has_value());

    // One inner module plus outer's module: two loose units in different containers.
    auto spread = units({"i1", "o1"});
    ASSERT_EQ(spread.size(), 2u);
    EXPECT_EQ(spread[0].container, "inner");
    EXPECT_EQ(spread[1].container, "outer");
    EXPECT_FALSE(macro_units::commonContainer(spread).has_value());

    // A port node of a macro that is not whole is dropped.
    auto port = units({"portO", "i1"});
    ASSERT_EQ(port.size(), 1u);
    EXPECT_EQ(port[0].uuid, "i1");

    EXPECT_TRUE(macro_units::allWholeMacros(units({"i1", "i2"})));
    EXPECT_FALSE(macro_units::allWholeMacros(units({"i1"})));
}

// ---------------------------------------------------------------------------------------------
// Create Macro
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedGrouping, CreateMacroFromTheCanvasMenuNestsTwoWholeMacros) {
    TwoMacros t;
    selectMacros(t, {t.macroA, t.macroB});
    ASSERT_TRUE(createMacroFromCanvasMenu(t.editor)) << "the real canvas right-click offers Create Macro";

    const auto parentId = t.newMacro();
    ASSERT_FALSE(parentId.isEmpty()) << t.lastMessage;
    const auto* parent = t.macros().find(parentId);
    EXPECT_TRUE(parent->parentId.isEmpty());
    EXPECT_TRUE(parent->members.empty()) << "a parent of two whole macros holds only children";
    EXPECT_TRUE(parent->collapsed);
    EXPECT_EQ(t.macros().parentOf(t.macroA), parentId);
    EXPECT_EQ(t.macros().parentOf(t.macroB), parentId);
    EXPECT_EQ(t.ownerOf(t.a1), t.macroA) << "the children keep their own members";
    EXPECT_TRUE(t.ctl().isMacroSelected(parentId)) << "the new parent is selected";
}

TEST(MacroNestedGrouping, CreateMacroFromAModuleMenuGroupsLooseModulesInsideAnOpenMacro) {
    TwoMacros t;
    t.editor.setSelectedNodes({t.loose});
    t.ctl().addSelectionToMacro(t.macroA, {t.uuid(t.loose)}); // A = {a1, a2, loose}
    t.ctl().setMacroCollapsed(t.macroA, false);
    t.editor.updateComponents();

    t.editor.setSelectedNodes({t.a1, t.a2});
    ASSERT_TRUE(createMacroFromModuleMenu(t.editor, t.a1));

    const auto childId = t.newMacro();
    ASSERT_FALSE(childId.isEmpty()) << t.lastMessage;
    EXPECT_EQ(t.macros().parentOf(childId), t.macroA) << "the new macro nests in the open macro";
    EXPECT_EQ(t.ownerOf(t.a1), childId);
    EXPECT_EQ(t.ownerOf(t.a2), childId);
    const auto* a = t.macros().find(t.macroA);
    EXPECT_FALSE(a->hasMember(t.uuid(t.a1))) << "a module leaves its old container's direct members";
    EXPECT_TRUE(a->hasMember(t.uuid(t.loose)));
    EXPECT_EQ(t.macros().descendantMembers(t.macroA).count(t.uuid(t.a1)), 1u);
}

TEST(MacroNestedGrouping, GroupingAcrossContainersIsRefusedWithAStatusMessage) {
    TwoMacros t;
    t.editor.setSelectedNodes({t.a1, t.b1}); // one module of A, one of B
    const auto before = t.macros().toVar();

    EXPECT_TRUE(t.ctl().groupSelectionIntoMacro().isEmpty());
    EXPECT_TRUE(t.lastMessage.contains("different macros")) << t.lastMessage;
    EXPECT_EQ(juce::JSON::toString(t.macros().toVar()), juce::JSON::toString(before)) << "nothing changes";
}

TEST(MacroNestedGrouping, OneWholeMacroAloneIsRefused) {
    TwoMacros t;
    selectMacros(t, {t.macroA});
    EXPECT_TRUE(t.ctl().groupSelectionIntoMacro().isEmpty());
    EXPECT_FALSE(t.lastMessage.isEmpty());
    EXPECT_EQ(t.macros().size(), 2);
}

TEST(MacroNestedGrouping, NestingSplicesAParentPortForACableLeavingAChildPort) {
    NodeID outside;
    TwoMacros t(/*autoPortsForA=*/true, [&](TwoMacros& fx) {
        outside = addModuleAt(fx.editor, fx.engine, std::make_unique<VCAModule>(), 1500, 100);
        ASSERT_TRUE(fx.engine.getGraph().addConnection({{fx.a2, 0}, {outside, 0}}));
    });
    const auto* a = t.macros().find(t.macroA);
    ASSERT_EQ(a->ports.size(), 1u) << "grouping A spliced an outlet port onto a2 -> outside";
    const auto childPort = nodeIdForUuid(t.engine, a->ports.front().nodeUuid);

    selectMacros(t, {t.macroA}, {t.loose});
    const auto parentId = t.ctl().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    ASSERT_FALSE(parentId.isEmpty()) << t.lastMessage;

    const auto* parent = t.macros().find(parentId);
    ASSERT_EQ(parent->ports.size(), 1u) << "the cable leaving the child's port crosses the new parent too";
    const auto parentPort = nodeIdForUuid(t.engine, parent->ports.front().nodeUuid);
    bool childToParent = false, parentToOutside = false, childToOutside = false;
    for (const auto& c : t.engine.getGraph().getConnections()) {
        childToParent |= c.source.nodeID == childPort && c.destination.nodeID == parentPort;
        parentToOutside |= c.source.nodeID == parentPort && c.destination.nodeID == outside;
        childToOutside |= c.source.nodeID == childPort && c.destination.nodeID == outside;
    }
    EXPECT_TRUE(childToParent);
    EXPECT_TRUE(parentToOutside);
    EXPECT_FALSE(childToOutside) << "the direct edge is rerouted through the parent's port";
}

// ---------------------------------------------------------------------------------------------
// Cmd+G
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedGrouping, CmdGOnAWholeMacroAndALooseSiblingNests) {
    TwoMacros t;
    selectMacros(t, {t.macroA}, {t.loose});
    t.ctl().groupOrToggleSelectionMacros();

    const auto parentId = t.newMacro();
    ASSERT_FALSE(parentId.isEmpty()) << t.lastMessage;
    EXPECT_EQ(t.macros().parentOf(t.macroA), parentId);
    EXPECT_EQ(t.ownerOf(t.loose), parentId);
    EXPECT_TRUE(t.macros().find(t.macroA)->collapsed) << "the child is nested, not toggled";
}

TEST(MacroNestedGrouping, CmdGOnOnlyWholeMacrosStillToggles) {
    TwoMacros t;
    selectMacros(t, {t.macroA, t.macroB});
    t.ctl().groupOrToggleSelectionMacros();

    EXPECT_EQ(t.macros().size(), 2) << "no new macro";
    EXPECT_FALSE(t.macros().find(t.macroA)->collapsed) << "both collapsed macros expand";
    EXPECT_FALSE(t.macros().find(t.macroB)->collapsed);
}

TEST(MacroNestedGrouping, CmdGOnTwoLooseModulesInsideAnOpenMacroGroupsThem) {
    TwoMacros t;
    t.ctl().addSelectionToMacro(t.macroA, {t.uuid(t.loose)});
    t.ctl().setMacroCollapsed(t.macroA, false);

    t.editor.setSelectedNodes({t.a1, t.a2});
    t.ctl().groupOrToggleSelectionMacros();

    const auto childId = t.newMacro();
    ASSERT_FALSE(childId.isEmpty()) << "two of an open macro's three modules group, they no longer collapse it";
    EXPECT_EQ(t.macros().parentOf(childId), t.macroA);
    EXPECT_FALSE(t.macros().find(t.macroA)->collapsed);
}

TEST(MacroNestedGrouping, CmdGOnOneModuleInsideAnOpenMacroStillCollapsesIt) {
    TwoMacros t;
    t.ctl().setMacroCollapsed(t.macroA, false);
    t.editor.setSelectedNodes({t.a1});
    t.ctl().groupOrToggleSelectionMacros();

    EXPECT_EQ(t.macros().size(), 2);
    EXPECT_TRUE(t.macros().find(t.macroA)->collapsed);
}

TEST(MacroNestedGrouping, CmdGAcrossContainersTogglesAndNamesTheLooseModules) {
    TwoMacros t;
    t.ctl().setMacroCollapsed(t.macroA, false);
    t.editor.setSelectedNodes({t.a1, t.loose}); // part of A, plus a top-level module
    t.ctl().groupOrToggleSelectionMacros();

    EXPECT_EQ(t.macros().size(), 2) << "units in different containers never group";
    EXPECT_TRUE(t.macros().find(t.macroA)->collapsed) << "the touched macro toggles, as before";
    EXPECT_TRUE(t.ownerOf(t.loose).isEmpty());
    EXPECT_TRUE(t.lastMessage.contains("left alone")) << t.lastMessage;
}

// ---------------------------------------------------------------------------------------------
// Add Selection to Macro
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedGrouping, AddingAWholeSiblingMacroNestsIt) {
    TwoMacros t;
    std::vector<juce::String> uuids;
    for (const auto& u : t.macros().descendantMembers(t.macroB))
        uuids.push_back(u);
    t.ctl().addSelectionToMacro(t.macroA, uuids);

    EXPECT_EQ(t.macros().parentOf(t.macroB), t.macroA);
    EXPECT_EQ(t.ownerOf(t.b1), t.macroB) << "B keeps its own members";
}

TEST(MacroNestedGrouping, AddingAModuleFromAnotherLevelIsRefused) {
    TwoMacros t;
    selectMacros(t, {t.macroA}, {t.loose});
    const auto parentId = t.ctl().groupSelectionIntoMacro(); // P = {loose, A}
    ASSERT_FALSE(parentId.isEmpty());

    t.ctl().addSelectionToMacro(t.macroB, {t.uuid(t.loose)}); // loose sits in P, B at top level
    EXPECT_FALSE(t.lastMessage.isEmpty());
    EXPECT_EQ(t.ownerOf(t.loose), parentId) << "the whole add is refused";
}

// ---------------------------------------------------------------------------------------------
// Undo, redo, save and load
// ---------------------------------------------------------------------------------------------

TEST(MacroNestedGrouping, NestIsOneUndoStepAndRedoes) {
    TwoMacros t;
    const auto flat = juce::JSON::toString(t.macros().toVar());
    selectMacros(t, {t.macroA}, {t.loose});
    const auto parentId = t.ctl().groupSelectionIntoMacro();
    ASSERT_FALSE(parentId.isEmpty());
    const auto nested = juce::JSON::toString(t.macros().toVar());

    ASSERT_TRUE(t.undo.undo());
    EXPECT_EQ(juce::JSON::toString(t.macros().toVar()), flat);
    EXPECT_TRUE(t.macros().parentOf(t.macroA).isEmpty());
    ASSERT_TRUE(t.undo.redo());
    EXPECT_EQ(juce::JSON::toString(t.macros().toVar()), nested);
    EXPECT_EQ(t.macros().parentOf(t.macroA), parentId);
}

TEST(MacroNestedGrouping, ANestedProjectSurvivesProjectBundleSaveAndLoad) {
    auto root = synth::userSettingsRootDirectory().getChildFile("agentsynth-macronestedgrouping-tests");
    root.deleteRecursively();
    root.createDirectory();
    auto dir = root.getChildFile(juce::String("Nested") + synth::ProjectBundle::kBundleExtension);

    TwoMacros t;
    selectMacros(t, {t.macroA, t.macroB});
    const auto parentId = t.ctl().groupSelectionIntoMacro(); // container-only parent
    ASSERT_FALSE(parentId.isEmpty());

    synth::TimelineDoc timeline;
    synth::PatchDocument patchDocument;
    synth::MidiRemoteProjectDoc midiRemote;
    auto saved =
        synth::ProjectBundle::save(dir, t.engine.getGraph(), timeline, patchDocument, t.editor.getMacros(), midiRemote);
    ASSERT_TRUE(saved.ok) << saved.message;

    juce::AudioProcessorGraph freshGraph;
    synth::TimelineDoc freshTimeline;
    synth::PatchDocument freshPatchDocument;
    synth::MacroSet freshMacros;
    synth::MidiRemoteProjectDoc freshMidiRemote;
    auto loaded =
        synth::ProjectBundle::load(dir, freshGraph, freshTimeline, freshPatchDocument, freshMacros, freshMidiRemote);
    ASSERT_TRUE(loaded.ok) << loaded.message;

    ASSERT_EQ(freshMacros.size(), 3) << "the container-only parent loads with its children";
    EXPECT_EQ(freshMacros.parentOf(t.macroA), parentId);
    EXPECT_EQ(freshMacros.parentOf(t.macroB), parentId);
    EXPECT_TRUE(freshMacros.find(parentId)->members.empty());
    EXPECT_EQ(freshMacros.descendantMembers(parentId).size(), 4u);

    root.deleteRecursively();
}
