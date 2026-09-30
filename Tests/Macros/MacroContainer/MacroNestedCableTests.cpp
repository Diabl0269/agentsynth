// MacroNestedCableTests.cpp
// Cable re-anchoring around nested collapsed macros (docs/layout/cables.md#nested-macros): every
// hidden node maps to its OUTERMOST collapsed ancestor, so a cable under one card is dropped, a cable
// between two cards anchors on both, and only that card's own ports get a jack anchor.

#include "MacroNestedTestFixture.h"

#include "Modules/VCAModule.h"

namespace {
const GraphEditor::VisibleCable* findCable(const std::vector<GraphEditor::VisibleCable>& cables, NodeID src,
                                           NodeID dst) {
    for (const auto& cable : cables)
        if (cable.id.srcUid == src.uid && cable.id.dstUid == dst.uid)
            return &cable;
    return nullptr;
}

bool onVerticalEdge(juce::Point<float> p, juce::Rectangle<int> r) {
    return (juce::approximatelyEqual(p.x, (float)r.getX()) || juce::approximatelyEqual(p.x, (float)r.getRight())) &&
           p.y >= (float)r.getY() && p.y <= (float)r.getBottom();
}
} // namespace

TEST(MacroNestedCable, CableWhollyInsideACollapsedParentIsDropped) {
    // A child member feeding one of the parent's own members.
    NestedMacroFixture f(false, [](NestedMacroFixture& fx) {
        ASSERT_TRUE(fx.engine.getGraph().addConnection({{fx.c1, 0}, {fx.p2, 0}}));
    });
    ASSERT_NE(findCable(f.editor.buildVisibleCables(), f.c1, f.p2), nullptr)
        << "positive control: drawn while everything is open";

    f.ctl().setMacroCollapsed(f.childId, true);
    EXPECT_NE(findCable(f.editor.buildVisibleCables(), f.c1, f.p2), nullptr)
        << "child collapsed, parent open: the cable leaves the child's card";

    f.ctl().setMacroCollapsed(f.parentId, true);
    EXPECT_EQ(findCable(f.editor.buildVisibleCables(), f.c1, f.p2), nullptr)
        << "both ends sit under the parent's card, however deep";
}

TEST(MacroNestedCable, CableBetweenANestedCardAndAnotherCardAnchorsBothEnds) {
    NestedMacroFixture f;
    auto q1 = addModuleAt(f.editor, f.engine, std::make_unique<VCAModule>(), 1500, 300);
    auto q2 = addModuleAt(f.editor, f.engine, std::make_unique<VCAModule>(), 1500, 700);
    ASSERT_TRUE(f.engine.getGraph().addConnection({{f.c1, 0}, {q1, 0}}));
    f.editor.setSelectedNodes({q1, q2});
    const auto otherId = f.ctl().groupSelectionIntoMacro(); // top level, collapsed by default
    ASSERT_FALSE(otherId.isEmpty());
    f.ctl().setMacroCollapsed(f.childId, true); // nested card inside the open parent

    const auto* cable = findCable(f.editor.buildVisibleCables(), f.c1, q1);
    ASSERT_NE(cable, nullptr);
    EXPECT_TRUE(onVerticalEdge(cable->p1, f.card(f.childId)->getBounds())) << "source end on the child's card";
    EXPECT_TRUE(onVerticalEdge(cable->p2, f.card(otherId)->getBounds())) << "destination end on the other card";
}

TEST(MacroNestedCable, HiddenEndAnchorsOnTheOutermostCollapsedCard) {
    NodeID outside;
    NestedMacroFixture f(false, [&](NestedMacroFixture& fx) {
        outside = addModuleAt(fx.editor, fx.engine, std::make_unique<VCAModule>(), 1500, 300);
        ASSERT_TRUE(fx.engine.getGraph().addConnection({{fx.c1, 0}, {outside, 0}}));
    });
    f.ctl().setMacroCollapsed(f.childId, true);
    f.ctl().setMacroCollapsed(f.parentId, true);

    const auto* cable = findCable(f.editor.buildVisibleCables(), f.c1, outside);
    ASSERT_NE(cable, nullptr);
    const auto parentCard = f.card(f.parentId)->getBounds();
    EXPECT_FLOAT_EQ(cable->p1.x, (float)parentCard.getRight()) << "leaves the PARENT's card on its right edge";
    EXPECT_TRUE(onVerticalEdge(cable->p1, parentCard));
}

TEST(MacroNestedCable, NestedPortUnderACollapsedParentTakesTheEdgeAnchorNotItsJack) {
    NodeID outside;
    NestedMacroFixture f(/*autoCreateChildPorts=*/true, [&](NestedMacroFixture& fx) {
        outside = addModuleAt(fx.editor, fx.engine, std::make_unique<VCAModule>(), 1500, 300);
        ASSERT_TRUE(fx.engine.getGraph().addConnection({{fx.c1, 0}, {outside, 0}}));
    });
    const auto* child = f.editor.getMacros().find(f.childId);
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->ports.size(), 1u) << "grouping the child spliced an outlet port onto the crossing cable";
    const auto portNode = nodeIdForUuid(f.engine, child->ports.front().nodeUuid);

    // Child collapsed inside the open parent: the port's own jack on the child's card.
    f.ctl().setMacroCollapsed(f.childId, true);
    const auto layout = f.ctl().macroCardPortLayout(f.childId);
    ASSERT_EQ(layout.size(), 1u);
    const auto* cable = findCable(f.editor.buildVisibleCables(), portNode, outside);
    ASSERT_NE(cable, nullptr);
    EXPECT_EQ(cable->p1, (f.card(f.childId)->getPosition() + layout.front().jackPos).toFloat());

    // Parent collapsed too: the child's port is just an interior node of the parent's card.
    f.ctl().setMacroCollapsed(f.parentId, true);
    cable = findCable(f.editor.buildVisibleCables(), portNode, outside);
    ASSERT_NE(cable, nullptr);
    EXPECT_FLOAT_EQ(cable->p1.x, (float)f.card(f.parentId)->getBounds().getRight())
        << "edge anchor on the parent's card, not a jack";
}
