// MacroLibraryDropTests.cpp
// A module dragged from the library onto an open macro lands INSIDE it: a member of that macro (the innermost open one
// under the pointer), placed within its hull, in one undo step, with no modifier. A collapsed macro is not a target
// and empty canvas stays top-level. Drives the real DragAndDropTarget path (itemDragEnter/Move/Dropped) with the
// payload the library sends (the entry's text). See docs/macros/menu-and-membership.md and
// docs/layout/module-library.md.

#include "MacroContainer/MacroDragTestHelpers.h"

#include "AppUndoManager.h"
#include <gtest/gtest.h>
#include <set>

namespace {

struct LibraryDropCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::Component source;
    juce::String highlightedDuringMove;

    LibraryDropCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        // No modifier is read and the canvas-drag preference off: a library drop has nothing to disambiguate.
        editor.setMacroDragWithoutCmdEnabled(false);
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }

    juce::String openMacro(std::vector<NodeID> ids) {
        editor.setSelectedNodes(ids);
        const auto id = ctl().groupSelectionIntoMacro();
        ctl().setMacroCollapsed(id, false);
        editor.clearSelection();
        return id;
    }

    /** The full gesture: enter, move, drop with the cursor at canvas point `p`. Returns the new node's uuid. */
    juce::String dropLfoAt(juce::Point<int> p) {
        std::set<uint32_t> before;
        for (auto* n : engine.getGraph().getNodes())
            before.insert(n->nodeID.uid);
        const juce::DragAndDropTarget::SourceDetails details(juce::var("LFO"), &source, p);
        EXPECT_TRUE(editor.isInterestedInDragSource(details));
        editor.itemDragEnter(details);
        editor.itemDragMove(details);
        highlightedDuringMove = editor.getMacroDragJoinId();
        editor.itemDropped(details);
        for (auto* n : engine.getGraph().getNodes())
            if (before.count(n->nodeID.uid) == 0)
                return n->properties["uuid"].toString();
        return {};
    }

    bool isMember(const juce::String& macroId, const juce::String& uuid) {
        const auto* m = editor.getMacros().find(macroId);
        return m != nullptr && m->hasMember(uuid);
    }

    juce::Rectangle<int> boundsOf(const juce::String& uuid) {
        auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
        return comp != nullptr ? comp->getBounds() : juce::Rectangle<int>();
    }
};

} // namespace

TEST(MacroLibraryDrop, DropOnAnOpenMacroBecomesAMemberPlacedInsideIt) {
    LibraryDropCanvas c;
    const auto macroId = c.openMacro({c.osc(300, 300), c.osc(800, 300)});
    const auto hull = c.ctl().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());

    const auto uuid = c.dropLfoAt(hull.getCentre());

    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_EQ(c.highlightedDuringMove, macroId) << "the hull is emphasised while hovering";
    EXPECT_TRUE(c.isMember(macroId, uuid));
    EXPECT_TRUE(c.editor.getMacroDragJoinId().isEmpty()) << "highlight cleared on drop";
    EXPECT_TRUE(c.ctl().macroHullBounds(macroId).contains(c.boundsOf(uuid))) << "placed within the open body";
}

TEST(MacroLibraryDrop, DropIsOneUndoStepAddingNodeAndMembership) {
    LibraryDropCanvas c;
    const auto macroId = c.openMacro({c.osc(300, 300), c.osc(800, 300)});
    const int nodesBefore = c.engine.getGraph().getNodes().size();
    const int serialBefore = c.undo.getEditSerial();

    const auto uuid = c.dropLfoAt(c.ctl().macroHullBounds(macroId).getCentre());
    ASSERT_TRUE(c.isMember(macroId, uuid));
    EXPECT_EQ(c.undo.getEditSerial(), serialBefore + 1);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.engine.getGraph().getNodes().size(), nodesBefore);
    EXPECT_EQ(c.editor.getMacros().find(macroId)->members.size(), 2u);
}

TEST(MacroLibraryDrop, DropIntoANestedMacroJoinsTheInnermostOpenOne) {
    LibraryDropCanvas c;
    const auto childId = c.openMacro({c.osc(400, 300), c.osc(900, 300)});
    const auto parentId = c.openMacro({c.osc(400, 1100), c.osc(900, 1100)});
    ASSERT_TRUE(c.editor.getMacros().setParent(childId, parentId));
    c.ctl().setMacroCollapsed(childId, false);
    c.ctl().setMacroCollapsed(parentId, false);
    c.editor.updateComponents();
    const auto childHull = c.ctl().macroHullBounds(childId);
    ASSERT_TRUE(c.ctl().macroHullBounds(parentId).contains(childHull));
    const auto parentMembersBefore = c.editor.getMacros().find(parentId)->members.size();
    const auto childMembersBefore = c.editor.getMacros().find(childId)->members.size();

    const auto uuid = c.dropLfoAt(childHull.getCentre());

    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_EQ(c.highlightedDuringMove, childId);
    EXPECT_TRUE(c.isMember(childId, uuid)) << "innermost open macro under the pointer wins";
    EXPECT_FALSE(c.isMember(parentId, uuid));
    EXPECT_EQ(c.editor.getMacros().find(childId)->members.size(), childMembersBefore + 1);
    EXPECT_EQ(c.editor.getMacros().find(parentId)->members.size(), parentMembersBefore);
}

TEST(MacroLibraryDrop, DropOnEmptyCanvasStaysTopLevel) {
    LibraryDropCanvas c;
    const auto macroId = c.openMacro({c.osc(300, 300), c.osc(800, 300)});
    const auto uuid = c.dropLfoAt({2400, 1800});

    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_TRUE(c.highlightedDuringMove.isEmpty());
    EXPECT_EQ(c.editor.getMacros().findByMember(uuid), nullptr);
    EXPECT_EQ(c.editor.getMacros().find(macroId)->members.size(), 2u);
}

TEST(MacroLibraryDrop, CollapsedMacroIsNotAnInteriorDropTarget) {
    LibraryDropCanvas c;
    const auto macroId = c.openMacro({c.osc(300, 300), c.osc(800, 300)});
    const auto hull = c.ctl().macroHullBounds(macroId);
    c.ctl().setMacroCollapsed(macroId, true);
    c.editor.updateComponents();

    const auto uuid = c.dropLfoAt(hull.getCentre());

    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_FALSE(c.isMember(macroId, uuid));
    EXPECT_EQ(c.editor.getMacros().find(macroId)->members.size(), 2u);
}
