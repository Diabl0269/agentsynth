// A removed cable retracts into its source jack and fades instead of vanishing: the ghost arming rule
// (CableRetractAnimator) and the canvas paths that arm it (disconnect, undo, redo).

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CableRetractAnimator/CableRetractAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
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

struct TwoModules {
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
    EXPECT_LT(mid.x, gone.p2.x);
    EXPECT_GT(mid.x, gone.p1.x) << "part of the way back to the source";
    EXPECT_FLOAT_EQ(retract.opacity(), 0.5f);

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
    EXPECT_FALSE(m.editor.getCableRetractForTest().isLive()) << "a redo that only adds a cable retracts nothing";
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
