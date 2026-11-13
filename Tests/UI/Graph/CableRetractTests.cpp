// A removed cable retracts into its source jack and fades instead of vanishing, and one that undo brings back grows
// out of its source jack: the arming rules and curves (CableRetractAnimator) and the canvas paths that arm it
// (disconnect, undo, redo, a macro port's delete, a mod source's removal).

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CableRetractAnimator/CableRetractAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <gtest/gtest.h>

namespace {

using VisibleCable = graph_editor_types::VisibleCable;

VisibleCable cable(uint32_t src, uint32_t dst, juce::Point<float> p1, juce::Point<float> p2) {
    VisibleCable c;
    c.id.srcUid = src;
    c.id.dstUid = dst;
    c.p1 = p1;
    c.p2 = p2;
    return c;
}

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct TwoModules {
    ReducedMotionGuard motion{false};
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::AudioProcessorGraph::NodeID osc, filter;

    TwoModules() {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1000);
        osc = add(std::make_unique<OscillatorModule>(), 100, 100);
        filter = add(std::make_unique<FilterModule>(), 700, 100);
    }
    juce::AudioProcessorGraph::NodeID add(std::unique_ptr<juce::AudioProcessor> p, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(p));
        node->properties.set("x", x);
        node->properties.set("y", y);
        editor.updateComponents();
        return node->nodeID;
    }
    std::optional<VisibleCable> cableIntoFilter() {
        for (const auto& c : editor.buildVisibleCables())
            if (c.id.srcUid == osc.uid && c.id.dstUid == filter.uid)
                return c;
        return std::nullopt;
    }
};

} // namespace

TEST(CableRetract, OnlyCablesThatAreGoneBecomeGhostsAndTheyPullBackIntoTheSource) {
    CableRetractAnimator retract;
    const auto kept = cable(1, 2, {0, 0}, {100, 0});
    const auto gone = cable(3, 4, {0, 50}, {200, 50});
    ASSERT_TRUE(retract.arm({kept, gone}, {kept}));
    ASSERT_EQ(retract.ghosts().size(), 1u);
    EXPECT_EQ(retract.ghosts()[0].p2, gone.p2) << "starts where it was";
    EXPECT_FLOAT_EQ(retract.opacity(), 1.0f);

    retract.applyTweenAt(0.5f);
    const auto mid = retract.ghosts()[0].p2;
    EXPECT_FLOAT_EQ(mid.x, gone.p2.x + (gone.p1.x - gone.p2.x) * synth::ui::easeInOutCubic(0.5f));
    EXPECT_LT(mid.x, gone.p2.x);
    EXPECT_GT(mid.x, gone.p1.x) << "part of the way back to the source";
    EXPECT_FLOAT_EQ(retract.opacity(), 1.0f) << "fully drawn until 60% of the way";

    retract.applyTweenAt(1.0f);
    EXPECT_EQ(retract.ghosts()[0].p2, gone.p1) << "fully pulled into the source jack";
    retract.finish();
    EXPECT_FALSE(retract.isLive());
    EXPECT_FALSE(retract.arm({kept}, {kept})) << "nothing removed, nothing armed";
}

// Regression test for FRO565: Cmd+Z made a cable vanish at once.
TEST(CableRetract, UndoingAConnectionRetractsTheCable) {
    TwoModules m;
    m.editor.connectPorts(m.osc, 0, m.filter, 0, /*isMidi=*/false);
    const auto drawn = m.cableIntoFilter();
    ASSERT_TRUE(drawn.has_value()) << "sanity: connected";

    ASSERT_TRUE(m.undo.undo());
    ASSERT_FALSE(m.cableIntoFilter().has_value()) << "the connection is undone";
    const auto& retract = m.editor.getCableRetractForTest();
    ASSERT_TRUE(retract.isLive()) << "the cable retracts instead of vanishing";
    ASSERT_EQ(retract.ghosts().size(), 1u);
    EXPECT_EQ(retract.ghosts()[0].p2, drawn->p2) << "from exactly where it was drawn";
    m.editor.finishCableRetractForTest();
    EXPECT_FALSE(retract.isLive());

    ASSERT_TRUE(m.undo.redo());
    EXPECT_TRUE(m.cableIntoFilter().has_value());
    EXPECT_TRUE(m.editor.getCableRetractForTest().ghosts().empty()) << "a redo that only adds a cable retracts nothing";
    EXPECT_EQ(m.editor.getCableRetractForTest().numGrowing(), 1u) << "it grows";
}

TEST(CableRetract, DisconnectingACableRetractsIt) {
    TwoModules m;
    m.editor.connectPorts(m.osc, 0, m.filter, 0, /*isMidi=*/false);
    const auto drawn = m.cableIntoFilter();
    ASSERT_TRUE(drawn.has_value());

    m.editor.disconnectCable(*drawn);

    EXPECT_FALSE(m.cableIntoFilter().has_value());
    EXPECT_TRUE(m.editor.getCableRetractForTest().isLive());
}

// ---- The curves -------------------------------------------------------------------------------------------------

TEST(CableRetract, TheOpacityHoldsUntilSixtyPercentThenFadesLinearly) {
    CableRetractAnimator retract;
    ASSERT_TRUE(retract.arm({cable(1, 2, {0, 0}, {100, 0})}, {}));
    EXPECT_DOUBLE_EQ(retract.durationMs(), 220.0);
    for (const float t : {0.0f, 0.3f, 0.6f}) {
        retract.applyTweenAt(t);
        EXPECT_FLOAT_EQ(retract.opacity(), 1.0f) << t;
    }
    retract.applyTweenAt(0.8f);
    EXPECT_NEAR(retract.opacity(), 0.5f, 1e-5f);
    retract.applyTweenAt(1.0f);
    EXPECT_NEAR(retract.opacity(), 0.0f, 1e-5f);
}

TEST(CableGrow, OnlyCablesNewToTheStepGrowFromTheSourceAndTheLiveOnesAreTheOnesSkipped) {
    CableRetractAnimator animator;
    const auto kept = cable(1, 2, {0, 0}, {100, 0});
    const auto back = cable(3, 4, {0, 50}, {200, 50});
    ASSERT_TRUE(animator.arm({kept}, {kept, back}, {/*growAdded=*/true, /*reduceMotion=*/false}));
    EXPECT_EQ(animator.numGrowing(), 1u);
    EXPECT_TRUE(animator.isGrowing(back.id));
    EXPECT_FALSE(animator.isGrowing(kept.id));
    EXPECT_TRUE(animator.ghosts().empty()) << "nothing was removed";

    EXPECT_EQ(animator.grown(back).p2, back.p1) << "starts at the source jack";
    EXPECT_FLOAT_EQ(animator.growOpacity(), 0.0f);
    animator.applyTweenAt(0.5f);
    EXPECT_FLOAT_EQ(animator.grown(back).p2.x, 200.0f * synth::ui::easeOutCubic(0.5f));
    EXPECT_EQ(animator.grown(back).p1, back.p1);
    EXPECT_FLOAT_EQ(animator.growOpacity(), 1.0f);
    animator.applyTweenAt(0.2f);
    EXPECT_NEAR(animator.growOpacity(), 0.5f, 1e-5f) << "fully opaque after the first 40%";
    animator.applyTweenAt(1.0f);
    EXPECT_EQ(animator.grown(back).p2, back.p2);
    animator.finish();
    EXPECT_FALSE(animator.isLive());
    EXPECT_FALSE(animator.isGrowing(back.id));
}

TEST(CableGrow, AddedCablesDoNotGrowUnlessAsked) {
    CableRetractAnimator animator;
    EXPECT_FALSE(animator.arm({}, {cable(1, 2, {0, 0}, {100, 0})})) << "a disconnect never grows anything";
}

TEST(CableGrow, ABigStepShowsItsNewCablesAtOnce) {
    std::vector<VisibleCable> sixteen, seventeen;
    for (uint32_t i = 0; i < 17; ++i) {
        seventeen.push_back(cable(10 + i, 100, {0, (float)i}, {50, (float)i}));
        if (i < 16)
            sixteen.push_back(seventeen.back());
    }
    CableRetractAnimator animator;
    EXPECT_TRUE(animator.arm({}, sixteen, {true, false}));
    EXPECT_EQ(animator.numGrowing(), 16u);
    EXPECT_FALSE(animator.arm({}, seventeen, {true, false})) << "more than 16: skipped";
    EXPECT_EQ(animator.numGrowing(), 0u);
}

TEST(CableGrow, ReduceMotionIsAnAlphaFadeWithNoGeometryChangeInBothDirections) {
    CableRetractAnimator animator;
    const auto gone = cable(1, 2, {0, 0}, {100, 0});
    const auto back = cable(3, 4, {0, 50}, {200, 50});
    ASSERT_TRUE(animator.arm({gone}, {back}, {true, /*reduceMotion=*/true}));
    EXPECT_DOUBLE_EQ(animator.durationMs(), 80.0);
    for (const float t : {0.0f, 0.5f, 1.0f}) {
        animator.applyTweenAt(t);
        EXPECT_EQ(animator.ghosts()[0].p2, gone.p2) << t;
        EXPECT_EQ(animator.grown(back).p2, back.p2) << t;
        EXPECT_FLOAT_EQ(animator.opacity(), 1.0f - t);
        EXPECT_FLOAT_EQ(animator.growOpacity(), t);
    }
}

// ---- The canvas ---------------------------------------------------------------------------------------------------

namespace {
int cablesPaintedNow(GraphEditor& editor) {
    graph_editor_paint::workCounters() = {};
    (void)editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    return graph_editor_paint::workCounters().cablesPainted;
}
} // namespace

TEST(CableGrow, UndoingADisconnectGrowsExactlyThatCableBackAndTheNormalPaintSkipsIt) {
    TwoModules m;
    m.editor.connectPorts(m.osc, 0, m.filter, 0, /*isMidi=*/false);
    const auto drawn = m.cableIntoFilter();
    ASSERT_TRUE(drawn.has_value());
    m.editor.disconnectCable(*drawn);
    ASSERT_FALSE(m.cableIntoFilter().has_value());
    ASSERT_TRUE(m.editor.getCableRetractForTest().isLive()) << "the disconnect retracts";
    m.editor.finishCableRetractForTest();

    ASSERT_TRUE(m.undo.undo());

    const auto& anim = m.editor.getCableRetractForTest();
    ASSERT_TRUE(m.cableIntoFilter().has_value()) << "the cable is back in the graph at once";
    EXPECT_EQ(anim.numGrowing(), 1u);
    EXPECT_TRUE(anim.isGrowing(drawn->id));
    EXPECT_TRUE(anim.ghosts().empty());
    EXPECT_EQ(cablesPaintedNow(m.editor), 0) << "a growing cable is not painted in the normal pass";
    m.editor.advanceCableRetractForTest(0.5f);
    EXPECT_EQ(cablesPaintedNow(m.editor), 0);

    m.editor.finishCableRetractForTest();
    EXPECT_FALSE(anim.isGrowing(drawn->id));
    EXPECT_EQ(cablesPaintedNow(m.editor), 1) << "once it has grown the cable paints normally";
}

TEST(CableGrow, RedoRetractsTheCableAgain) {
    TwoModules m;
    m.editor.connectPorts(m.osc, 0, m.filter, 0, /*isMidi=*/false);
    ASSERT_TRUE(m.undo.undo());
    m.editor.finishCableRetractForTest();
    ASSERT_TRUE(m.undo.redo());
    EXPECT_EQ(m.editor.getCableRetractForTest().numGrowing(), 1u) << "redo brings it back: it grows";
    m.editor.finishCableRetractForTest();
    ASSERT_TRUE(m.undo.undo());
    EXPECT_TRUE(m.editor.getCableRetractForTest().isLive());
    EXPECT_EQ(m.editor.getCableRetractForTest().ghosts().size(), 1u);
    EXPECT_EQ(m.editor.getCableRetractForTest().numGrowing(), 0u);
}

TEST(CableGrow, UnderReduceMotionTheCanvasMotionIsAnEightyMillisecondFadeOnly) {
    TwoModules m;
    ReducedMotionGuard reduced(true);
    m.editor.connectPorts(m.osc, 0, m.filter, 0, /*isMidi=*/false);
    const auto drawn = m.cableIntoFilter();
    ASSERT_TRUE(drawn.has_value());
    ASSERT_TRUE(m.undo.undo());
    const auto& anim = m.editor.getCableRetractForTest();
    ASSERT_TRUE(anim.isLive());
    EXPECT_DOUBLE_EQ(anim.durationMs(), 80.0);
    EXPECT_EQ(anim.ghosts()[0].p2, drawn->p2);
    m.editor.advanceCableRetractForTest(0.5f);
    EXPECT_EQ(anim.ghosts()[0].p2, drawn->p2) << "nothing moves";
    EXPECT_FLOAT_EQ(anim.opacity(), 0.5f);
    m.editor.finishCableRetractForTest();
    ASSERT_TRUE(m.undo.redo());
    EXPECT_EQ(anim.numGrowing(), 1u);
    EXPECT_EQ(anim.grown(*m.cableIntoFilter()).p2, drawn->p2);
}

TEST(CableGrow, ABigUndoShowsItsCablesAtOnce) {
    TwoModules m;
    std::vector<juce::AudioProcessorGraph::NodeID> oscs;
    for (int i = 0; i < 17; ++i)
        oscs.push_back(m.add(std::make_unique<OscillatorModule>(), 100, 150 + 20 * i));
    m.undo.recordStructuralChange(m.engine.getGraph(), [&] {
        for (const auto id : oscs)
            m.engine.getGraph().addConnection({{id, 0}, {m.filter, 0}});
    });
    m.editor.updateComponents();
    ASSERT_TRUE(m.undo.undo());
    m.editor.finishCableRetractForTest();
    ASSERT_TRUE(m.undo.redo());
    EXPECT_EQ(m.editor.getCableRetractForTest().numGrowing(), 0u) << "17 cables: no grow";
}
