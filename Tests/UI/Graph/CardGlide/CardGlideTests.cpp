// CardGlideTests.cpp
//
// The card glide overlay (CardGlideAnimator): geometry is final and synchronous, the real cards are hidden for the
// glide, hit-tests land on the final rect, cables follow, and loads never glide (undo/redo glide:
// CardGlideUndoTests.cpp). Driven with no VBlank through GraphEditor's advance/finish seams. (docs/layout/animation.md,
// docs/layout/layout.md#making-room-when-something-grows)

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include <gtest/gtest.h>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    NodeID filter(int x, int y) { return addModuleAt(editor, engine, std::make_unique<FilterModule>(), x, y); }
    ModuleComponent* comp(NodeID id) { return findComponent(editor, id); }
    juce::Rectangle<int> rect(NodeID id) { return comp(id)->getBounds(); }
    juce::String uuid(NodeID id) { return uuidOf(engine, id); }

    juce::String group(std::vector<NodeID> ids) {
        editor.setSelectedNodes(ids);
        return ctl().groupSelectionIntoMacro();
    }

    void land() { editor.finishCardGlideForTest(); }

    // A scrambled patch: staggered cards with connections that auto-arrange will want to move.
    std::vector<NodeID> scramble() {
        std::vector<NodeID> ids{osc(1500, 900), filter(300, 1400), osc(900, 200), filter(2000, 1800)};
        engine.getGraph().addConnection({{ids[0], 0}, {ids[1], 0}});
        engine.getGraph().addConnection({{ids[2], 0}, {ids[3], 0}});
        return ids;
    }

    std::map<juce::uint32, juce::Rectangle<int>> boundsByNode(const std::vector<NodeID>& ids) {
        std::map<juce::uint32, juce::Rectangle<int>> out;
        for (auto id : ids)
            out[id.uid] = rect(id);
        return out;
    }
};

juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos, juce::ModifierKeys mods) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), mods, 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &eventComp, &eventComp, juce::Time::getCurrentTime(), localPos.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

// The ModuleComponent (or none) the canvas content resolves `pointInContent` to.
ModuleComponent* moduleAt(juce::Component& content, juce::Point<int> pointInContent) {
    auto* hit = content.getComponentAt(pointInContent);
    return hit != nullptr
               ? (dynamic_cast<ModuleComponent*>(hit) != nullptr ? dynamic_cast<ModuleComponent*>(hit)
                                                                 : hit->findParentComponentOfClass<ModuleComponent>())
               : nullptr;
}

} // namespace

TEST(CardGlide, AutoArrangeArmsWithFinalBoundsAlreadyInPlaceAndMovedCardsHidden) {
    Canvas c;
    const auto ids = c.scramble();
    const auto before = c.boundsByNode(ids);

    c.editor.autoArrange();

    ASSERT_TRUE(c.glide().isLive());
    EXPECT_EQ(c.glide().armCount(), 1);
    int moved = 0;
    for (auto id : ids) {
        const auto final = c.rect(id);
        if (final == before.at(id.uid)) {
            EXPECT_EQ(c.comp(id)->getAlpha(), 1.0f) << "unmoved cards are untouched";
            continue;
        }
        ++moved;
        EXPECT_EQ(c.comp(id)->getAlpha(), 0.0f);
        // Node x/y and the component agree on the final spot at once.
        auto* node = c.engine.getGraph().getNodeForId(id);
        EXPECT_EQ(juce::Point<int>((int)node->properties["x"], (int)node->properties["y"]), final.getPosition());
        // The drawn rect starts at the old spot.
        EXPECT_EQ(c.glide().currentRectFor(c.comp(id)), before.at(id.uid));
    }
    EXPECT_GT(moved, 0);

    c.editor.advanceCardGlideForTest(0.5f);
    for (auto id : ids)
        if (c.comp(id)->getAlpha() == 0.0f)
            EXPECT_NE(c.glide().currentRectFor(c.comp(id)), c.rect(id)) << "still mid-glide at t=0.5";

    c.land();
    EXPECT_FALSE(c.glide().isLive());
    for (auto id : ids)
        EXPECT_EQ(c.comp(id)->getAlpha(), 1.0f);
}

TEST(CardGlide, ArrangingAnAlreadyArrangedCanvasArmsNothing) {
    Canvas c;
    c.scramble();
    c.editor.autoArrange();
    c.land();
    const int arms = c.glide().armCount();

    c.editor.autoArrange();

    EXPECT_FALSE(c.glide().isLive());
    EXPECT_EQ(c.glide().armCount(), arms);
}

// A click mid-glide acts on the card's FINAL rect; its old spot is empty canvas.
TEST(CardGlide, MouseHitsTheFinalRectDuringTheGlideAndNotTheOldSpot) {
    Canvas c;
    const auto ids = c.scramble();
    const auto before = c.boundsByNode(ids);
    c.editor.autoArrange();
    c.editor.advanceCardGlideForTest(0.5f);

    NodeID target;
    for (auto id : ids)
        if (c.rect(id) != before.at(id.uid) && !c.rect(id).intersects(before.at(id.uid)))
            target = id;
    ASSERT_NE(target, NodeID{}) << "needs a card whose old and new spots are disjoint";
    auto* card = c.comp(target);
    ASSERT_EQ(card->getAlpha(), 0.0f);

    auto* content = card->getParentComponent();
    EXPECT_NE(moduleAt(*content, before.at(target.uid).getCentre()), card) << "nothing is hit at the old spot";

    const auto pressLocal = juce::Point<int>(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const auto finalPoint = card->getPosition() + pressLocal;
    ASSERT_EQ(moduleAt(*content, finalPoint), card) << "the final rect routes the mouse to the card";

    c.editor.setSelectedNodes({});
    card->mouseDown(realMouseEvent(*card, pressLocal, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    card->mouseUp(realMouseEvent(*card, pressLocal, juce::ModifierKeys()));
    EXPECT_TRUE(c.editor.getSelection().contains(target));
    c.land();
}

// Expanding pushes the neighbour (one glide), collapsing brings it back (one glide).
TEST(CardGlide, ExpandAndCollapseEachGlideTheNeighbourWithOneArm) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto macroId = c.group({m1, m2});
    ASSERT_FALSE(macroId.isEmpty());
    c.land();
    const int base = c.glide().armCount();
    const auto home = c.rect(neighbour);

    c.ctl().setMacroCollapsed(macroId, false);

    ASSERT_NE(c.rect(neighbour), home);
    EXPECT_EQ(c.glide().armCount(), base + 1) << "one arm for the whole expand";
    EXPECT_EQ(c.comp(neighbour)->getAlpha(), 0.0f);
    EXPECT_EQ(c.glide().currentRectFor(c.comp(neighbour)), home);
    c.land();
    EXPECT_EQ(c.comp(neighbour)->getAlpha(), 1.0f);

    const auto pushed = c.rect(neighbour);
    c.ctl().setMacroCollapsed(macroId, true);

    EXPECT_EQ(c.rect(neighbour), home) << "the neighbour came home";
    EXPECT_EQ(c.glide().armCount(), base + 2) << "one arm for the whole collapse";
    EXPECT_EQ(c.glide().currentRectFor(c.comp(neighbour)), pushed);
    c.land();
}

TEST(CardGlide, RetargetingMidGlideStartsFromTheDrawnRectAndKeepsTheOriginalAlpha) {
    Canvas c;
    const auto ids = c.scramble();
    const auto before = c.boundsByNode(ids);
    c.editor.autoArrange();
    NodeID mover;
    for (auto id : ids)
        if (c.rect(id) != before.at(id.uid))
            mover = id;
    ASSERT_NE(mover, NodeID{});
    c.editor.advanceCardGlideForTest(0.5f);
    const auto drawn = c.glide().currentRectFor(c.comp(mover));
    ASSERT_NE(drawn, c.rect(mover));

    {
        CardGlideAnimator::Scope scope(c.glide());
        c.ctl().moveUnitBy("n:" + juce::String((juce::int64)mover.uid), {60, 40});
    }

    EXPECT_EQ(c.glide().currentRectFor(c.comp(mover)), drawn) << "retargets from where it is drawn";
    EXPECT_EQ(c.comp(mover)->getAlpha(), 0.0f);
    c.land();
    EXPECT_EQ(c.comp(mover)->getAlpha(), 1.0f) << "the saved alpha is the original one, not the hidden 0";
}

TEST(CardGlide, NothingRepaintsAfterTheGlideFinishes) {
    Canvas c;
    c.scramble();
    c.editor.autoArrange();
    c.land();
    const int repaints = c.glide().repaintCount();
    const int arms = c.glide().armCount();

    c.editor.advanceCardGlideForTest(0.7f);
    {
        CardGlideAnimator::Scope scope(c.glide());
    } // a mutation that moves nothing

    EXPECT_FALSE(c.glide().isLive());
    EXPECT_TRUE(c.glide().dirtyArea().isEmpty());
    EXPECT_EQ(c.glide().repaintCount(), repaints);
    EXPECT_EQ(c.glide().armCount(), arms);
}

TEST(CardGlide, NestedScopesArmOnce) {
    Canvas c;
    const auto a = c.osc(300, 300);
    {
        CardGlideAnimator::Scope outer(c.glide());
        c.ctl().moveUnitBy("n:" + juce::String((juce::int64)a.uid), {100, 0});
        {
            CardGlideAnimator::Scope inner(c.glide());
            c.ctl().moveUnitBy("n:" + juce::String((juce::int64)a.uid), {0, 100});
        }
        EXPECT_FALSE(c.glide().isLive()) << "only the outermost exit arms";
    }
    EXPECT_TRUE(c.glide().isLive());
    EXPECT_EQ(c.glide().armCount(), 1);
    c.land();
}

// A cable touching a gliding card follows it: its endpoint is offset by the card's (drawn - final) offset.
TEST(CardGlide, CableEndpointsFollowAGlidingCardAndSettleOnTheFinalAnchor) {
    Canvas c;
    const auto ids = c.scramble();
    const auto src = ids[0];
    const auto dst = ids[1];
    const auto oldSrc = c.rect(src);
    c.editor.autoArrange();
    ASSERT_NE(c.rect(src), oldSrc);

    c.editor.advanceCardGlideForTest(1.0f);
    const auto settled = c.editor.buildVisibleCables();
    c.editor.advanceCardGlideForTest(0.0f);
    const auto atStart = c.editor.buildVisibleCables();

    auto find = [&](const std::vector<GraphEditor::VisibleCable>& cables) {
        for (const auto& cable : cables)
            if (cable.id.srcUid == src.uid && cable.id.dstUid == dst.uid)
                return cable;
        return GraphEditor::VisibleCable{};
    };
    const auto end = find(settled);
    const auto start = find(atStart);
    ASSERT_EQ(end.id.srcUid, src.uid);
    const auto offset = c.glide().offsetFor(src.uid);
    EXPECT_NE(offset, juce::Point<float>());
    EXPECT_EQ(start.p1, end.p1 + offset);
    EXPECT_EQ(start.p1 - end.p1, juce::Point<float>((float)(oldSrc.getX() - c.rect(src).getX()),
                                                    (float)(oldSrc.getY() - c.rect(src).getY())));

    c.land();
    EXPECT_EQ(find(c.editor.buildVisibleCables()).p1, end.p1);
}

// ---- Never on load ----

TEST(CardGlide, OpeningAProjectWithOpenCollapsedAndNestedMacrosNeverGlides) {
    Canvas c;
    auto node = [&](int x, int y) {
        auto n = c.engine.getGraph().addNode(std::make_unique<OscillatorModule>());
        n->properties.set("x", x);
        n->properties.set("y", y);
        n->properties.set("uuid", juce::Uuid().toDashedString());
        return n->properties["uuid"].toString();
    };
    auto addMacro = [&](const juce::String& name, std::vector<juce::String> members, bool collapsed, int x, int y,
                        const juce::String& parent = {}) {
        synth::Macro macro;
        macro.id = name;
        macro.name = name;
        macro.collapsed = collapsed;
        macro.parentId = parent;
        macro.bounds = {x, y, 160, 60};
        macro.members = std::move(members);
        c.editor.getMacros().add(macro);
    };
    addMacro("open", {node(300, 300), node(600, 300)}, false, 300, 300);
    addMacro("shut", {node(300, 900), node(600, 900)}, true, 300, 900);
    const auto outerMember = node(1200, 300);
    addMacro("outer", {outerMember, node(1500, 300)}, false, 1200, 300);
    addMacro("inner", {node(1300, 700)}, true, 1300, 700, "outer");
    c.glide(); // touch

    c.editor.updateComponents();

    EXPECT_EQ(c.glide().armCount(), 0);
    EXPECT_FALSE(c.glide().isLive());
    for (auto* comp : c.editor.getModuleComponents())
        EXPECT_EQ(comp->getAlpha(), 1.0f);
}
