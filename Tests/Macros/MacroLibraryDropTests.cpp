// MacroLibraryDropTests.cpp
// A module dragged from the library onto an open macro lands INSIDE it: a member of that macro (the innermost open one
// under the pointer), placed within its hull, in one undo step, with no modifier. A collapsed macro is not a target
// and empty canvas stays top-level. Drives the real DragAndDropTarget path (itemDragEnter/Move/Dropped) with the
// payload the library sends (the entry's text). See docs/macros/menu-and-membership.md and
// docs/layout/module-library.md.

#include "MacroContainer/MacroDragTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Layout/ReducedMotion.h"
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

// An open macro's body is mostly its member cards, so the pointer is usually OVER a card when a library module is
// dropped. That must add the module and join the macro just like a drop on the gaps between cards.
TEST(MacroLibraryDrop, DropOverAMemberCardInsideAnOpenMacroJoinsInOneUndoStep) {
    LibraryDropCanvas c;
    const auto m1 = c.osc(300, 300);
    const auto macroId = c.openMacro({m1, c.osc(800, 300)});
    const auto cardCentre = findComponent(c.editor, m1)->getBounds().getCentre();
    ASSERT_EQ(c.ctl().macroHullAt(cardCentre), macroId) << "premise: the card sits inside the open hull";
    const int nodesBefore = c.engine.getGraph().getNodes().size();
    const int serialBefore = c.undo.getEditSerial();

    const auto uuid = c.dropLfoAt(cardCentre);

    ASSERT_FALSE(uuid.isEmpty()) << "a drop over a card still adds the module";
    EXPECT_EQ(c.highlightedDuringMove, macroId);
    EXPECT_TRUE(c.isMember(macroId, uuid));
    EXPECT_FALSE(c.boundsOf(uuid).intersects(findComponent(c.editor, m1)->getBounds())) << "lands on free space";
    EXPECT_TRUE(c.ctl().macroHullBounds(macroId).contains(c.boundsOf(uuid))) << "and the hull grows to hold it";
    EXPECT_EQ(c.undo.getEditSerial(), serialBefore + 1);
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.engine.getGraph().getNodes().size(), nodesBefore);
    EXPECT_EQ(c.editor.getMacros().find(macroId)->members.size(), 2u);
}

TEST(MacroLibraryDrop, DropOverATopLevelCardAddsTopLevelNearThePointer) {
    LibraryDropCanvas c;
    c.openMacro({c.osc(300, 300), c.osc(800, 300)});
    const auto loose = c.osc(2000, 1500);
    const auto looseBounds = findComponent(c.editor, loose)->getBounds();
    const auto pointer = looseBounds.getCentre();

    const auto uuid = c.dropLfoAt(pointer);

    ASSERT_FALSE(uuid.isEmpty()) << "a drop over a card still adds the module";
    EXPECT_EQ(c.editor.getMacros().findByMember(uuid), nullptr);
    const auto landed = c.boundsOf(uuid);
    EXPECT_FALSE(landed.intersects(looseBounds));
    EXPECT_LT(landed.getCentre().getDistanceFrom(pointer), 700) << "nearest free spot, not thrown across the canvas";
}

// The shape of the real demo: LFO cards inside the open macro, one wired to a module outside it, and an LFO dragged
// from the library onto the body of one of the member LFOs.
TEST(MacroLibraryDrop, DropOverAWiredMemberLfoJoinsTheMacro) {
    LibraryDropCanvas c;
    const auto lfo1 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 300, 300);
    const auto lfo2 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 800, 300);
    const auto target = c.osc(2000, 300);
    const auto macroId = c.openMacro({lfo1, lfo2});
    c.engine.getGraph().addConnection({{lfo2, 0}, {target, 0}});
    c.editor.updateComponents();
    const auto pointer = findComponent(c.editor, lfo2)->getBounds().getCentre();
    ASSERT_EQ(c.ctl().macroHullAt(pointer), macroId);

    const auto uuid = c.dropLfoAt(pointer);

    ASSERT_FALSE(uuid.isEmpty()) << "the drop adds a module";
    EXPECT_TRUE(c.isMember(macroId, uuid));
}

namespace {
/** Canvas-space centre of the first visible rotary knob on `card`. */
juce::Point<int> firstKnobCentre(ModuleComponent& card) {
    std::function<synth::ui::CardKnobSlider*(juce::Component*)> find = [&](juce::Component* comp) {
        for (auto* child : comp->getChildren()) {
            if (auto* knob = dynamic_cast<synth::ui::CardKnobSlider*>(child); knob != nullptr && knob->isVisible())
                return knob;
            if (auto* inner = find(child))
                return inner;
        }
        return static_cast<synth::ui::CardKnobSlider*>(nullptr);
    };
    auto* knob = find(&card);
    return knob != nullptr ? card.getPosition() + card.getLocalPoint(knob, knob->getLocalBounds().getCentre())
                           : juce::Point<int>();
}
} // namespace

// Over a knob, not just the card body: the knob is the widget a pointer is most likely to be over.
TEST(MacroLibraryDrop, DropOverAKnobOfAMemberCardJoinsTheMacro) {
    LibraryDropCanvas c;
    const auto lfo1 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 300, 300);
    const auto lfo2 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 800, 300);
    const auto macroId = c.openMacro({lfo1, lfo2});
    const auto pointer = firstKnobCentre(*findComponent(c.editor, lfo1));
    ASSERT_NE(pointer, juce::Point<int>()) << "premise: the card has a visible knob";
    ASSERT_EQ(c.ctl().macroHullAt(pointer), macroId);
    const int nodesBefore = c.engine.getGraph().getNodes().size();
    const int serialBefore = c.undo.getEditSerial();

    const auto uuid = c.dropLfoAt(pointer);

    ASSERT_EQ(c.engine.getGraph().getNodes().size(), nodesBefore + 1) << "the drop adds a module";
    EXPECT_TRUE(c.isMember(macroId, uuid));
    EXPECT_EQ(c.undo.getEditSerial(), serialBefore + 1);
}

// A second library drop into a macro that already took one this session must add a second member.
// Reduce Motion is a system setting (on in CI's macOS runner): pin it so both answers are covered on every machine.
namespace {
struct ReducedMotionPin {
    explicit ReducedMotionPin(bool on) { synth::ui::setReducedMotionForTest(on); }
    ~ReducedMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

static void twoConsecutiveDropsBothJoin() {
    LibraryDropCanvas c;
    const auto lfo1 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 300, 300);
    const auto lfo2 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 800, 300);
    const auto macroId = c.openMacro({lfo1, lfo2});
    const auto knob = firstKnobCentre(*findComponent(c.editor, lfo1));

    const auto first = c.dropLfoAt(knob);
    ASSERT_FALSE(first.isEmpty());
    ASSERT_TRUE(c.isMember(macroId, first));
    const int nodesAfterFirst = c.engine.getGraph().getNodes().size();

    // Empty space inside the (grown) hull: a point in the hull clear of every member card.
    const auto hull = c.ctl().macroHullBounds(macroId);
    juce::Point<int> empty;
    bool found = false;
    for (int y = hull.getY() + 30; y < hull.getBottom() && !found; y += 20)
        for (int x = hull.getX() + 30; x < hull.getRight() && !found; x += 20) {
            bool free = true;
            for (auto* comp : c.editor.getModuleComponents())
                if (comp != nullptr && comp->getBounds().expanded(4).contains(x, y))
                    free = false;
            if (free && c.ctl().macroHullAt({x, y}) == macroId) {
                empty = {x, y};
                found = true;
            }
        }
    ASSERT_TRUE(found) << "premise: the hull has empty space";

    const auto second = c.dropLfoAt(empty);

    ASSERT_EQ(c.engine.getGraph().getNodes().size(), nodesAfterFirst + 1) << "the second drop adds a module too";
    EXPECT_TRUE(c.isMember(macroId, second));
    EXPECT_NE(first, second);
}

TEST(MacroLibraryDrop, TwoConsecutiveDropsIntoTheSameOpenMacroBothJoin) { twoConsecutiveDropsBothJoin(); }

// The gap a drop opens inside the macro must not decide the join; the borders as they stood do.
TEST(MacroLibraryDrop, TwoConsecutiveDropsBothJoinUnderReducedMotion) {
    ReducedMotionPin pin(true);
    twoConsecutiveDropsBothJoin();
}
TEST(MacroLibraryDrop, TwoConsecutiveDropsBothJoinUnderFullMotion) {
    ReducedMotionPin pin(false);
    twoConsecutiveDropsBothJoin();
}

// A card dropped into a crowded macro lands WHERE it was dropped (under the pointer, as the ghost shows) and the
// members it overlaps are pushed aside, instead of the card dodging to a far free spot. One undo step puts it all back.
TEST(MacroLibraryDrop, DropIntoACrowdedHullLandsAtThePointerAndPushesMembersAside) {
    LibraryDropCanvas c;
    c.editor.setSize(1300, 900);
    const std::vector<NodeID> members{addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 100, 100),
                                      addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 420, 100),
                                      addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 740, 100)};
    const auto macroId = c.openMacro(members);
    const auto pointer = c.ctl().macroHullBounds(macroId).getCentre();
    std::vector<juce::Point<int>> before;
    for (const auto& id : members)
        before.push_back(findComponent(c.editor, id)->getPosition());

    const auto uuid = c.dropLfoAt(pointer);

    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_TRUE(c.isMember(macroId, uuid));
    const auto landed = c.boundsOf(uuid);
    EXPECT_LT(landed.getCentre().getDistanceFrom(pointer), 100) << "lands under the pointer, not at a far free spot";
    EXPECT_TRUE(c.ctl().macroHullBounds(macroId).contains(landed)) << "and the hull grows to hold it";
    EXPECT_TRUE(c.editor.getVisibleCanvasRect().contains(landed.getCentre().toFloat()));
    std::vector<juce::Rectangle<int>> all{landed};
    for (const auto& id : members)
        all.push_back(findComponent(c.editor, id)->getBounds());
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
            EXPECT_FALSE(all[a].intersects(all[b])) << "cards " << a << " and " << b << " overlap";

    ASSERT_TRUE(c.undo.undo());
    for (size_t i = 0; i < members.size(); ++i)
        EXPECT_EQ(findComponent(c.editor, members[i])->getPosition(), before[i]) << "undo restores member " << i;
}

// The safety net: if the landing spot is still off screen the view pans to it.
TEST(MacroLibraryDrop, DropLandingOffScreenPansIntoView) {
    LibraryDropCanvas c;
    c.editor.setSize(1300, 560);
    const auto lfo1 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 100, 100);
    const auto lfo2 = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 420, 100);
    const auto macroId = c.openMacro({lfo1, lfo2});
    const auto hull = c.ctl().macroHullBounds(macroId);
    const juce::Point<int> pointer(hull.getCentreX(), hull.getBottom() - 10); // a card centred here hangs off screen
    ASSERT_TRUE(c.editor.getVisibleCanvasRect().contains(pointer.toFloat()));

    const auto uuid = c.dropLfoAt(pointer);

    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_TRUE(c.editor.getVisibleCanvasRect().contains(c.boundsOf(uuid).getCentre().toFloat()));
}
