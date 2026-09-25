// MixerInsertListTests.cpp -- FRO15 in-app finding: MixerInsertList's own paint()/mouseDown()
// overlap bug, and the moveRow regression the bus insert-discovery fix opened.
//
//   * overlap  -- when the list has zero entries AND is non-linear, the "(no inserts)" placeholder
//                 and the "Edit on canvas" link used to anchor at the SAME row (both at
//                 entries_.size() * kRowHeight == 0), so a click meant for the placeholder's row
//                 fired onEditOnCanvas too. Proven behaviourally (mouseDown's hit test), not by
//                 pixel-diffing paint() -- see this file's own comment on the fix for why the two
//                 methods share one anchor formula.
//   * moveRow  -- a bus's sourceNodeId_ is invalid (no external predecessor -- docs/mixer/mixer.md
//                 docs/mixer/sends-and-buses.md D6), so moving a row to the very front of its chain must refuse rather
//                 than splice against an invalid id and orphan the node (reorderInsert's second step has no rollback of
//                 its own).
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerInsertList.h"
#include <gtest/gtest.h>

namespace {

juce::MouseEvent makeMouseDownAt(juce::Component& component, juce::Point<int> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &component, &component, juce::Time::getCurrentTime(), position.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

} // namespace

TEST(MixerInsertListTests, EmptyNonLinearListDoesNotOverlapThePlaceholderAndTheEditOnCanvasLink) {
    synth::ui::MixerInsertList list;
    list.setEntries({}, /*linear=*/false, "target-uuid", {}, {});
    list.setSize(140, list.getPreferredHeight());

    // getPreferredHeight() for zero entries, non-linear: one placeholder row + one link row.
    const int rowHeight = list.getPreferredHeight() / 2;
    ASSERT_GT(rowHeight, 0);

    juce::String captured;
    list.onEditOnCanvas = [&](const juce::String& uuid) { captured = uuid; };

    // A click inside row 0 (the "(no inserts)" placeholder) must NOT be treated as the link --
    // before the fix, linkRowTop was entries_.size() * kRowHeight == 0, so this click hit it too.
    list.mouseDown(makeMouseDownAt(list, {10, rowHeight / 2}));
    EXPECT_TRUE(captured.isEmpty()) << "the empty-state placeholder row must not double as the link";

    // A click inside row 1 (the actual link row, now correctly shifted past the placeholder) must
    // fire onEditOnCanvas.
    list.mouseDown(makeMouseDownAt(list, {10, rowHeight + rowHeight / 2}));
    EXPECT_EQ(captured, "target-uuid") << "the link row itself must still work";
}

TEST(MixerInsertListTests, MovingABusInsertToTheFrontRefusesRatherThanOrphaningIt) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);
    synth::TimelineDoc doc;

    const synth::DefaultChannelLayout layout{{-100, 0}, {0, 0}, {100, 0}, {200, 0}, {300, 0}};
    const auto channel = synth::buildBusChannel(graph, layout);
    ASSERT_NE(channel.strip, nullptr);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, editor.getMacros());
    const synth::MixerColumn* busColumn = nullptr;
    for (const auto& column : snapshot.columns)
        if (column.nodeId == channel.strip->nodeID)
            busColumn = &column;
    ASSERT_NE(busColumn, nullptr);
    ASSERT_EQ(busColumn->inserts.size(), 3u);
    ASSERT_EQ(busColumn->sourceNodeId, juce::AudioProcessorGraph::NodeID{})
        << "a bus's own chain has no external source -- the exact precondition this guards";
    const int nodeCountBefore = graph.getNumNodes();

    synth::ui::MixerInsertList list;
    list.configure(graph, undoManager, editor.getMacros(), editor);
    list.setEntries(busColumn->inserts, busColumn->insertChainIsLinear, busColumn->editOnCanvasTargetUuid,
                    busColumn->sourceNodeId, busColumn->nodeId);

    bool mutated = false;
    list.onMutated = [&] { mutated = true; };

    // Row 0 is the Gate (asserted by the model test) -- move it up further still, to before the
    // front of the bus's own chain, where there is no external predecessor to splice against.
    list.moveRow(0, -1);

    EXPECT_FALSE(mutated) << "nothing should have changed -- see spliceInInsert's own contract";
    EXPECT_EQ(graph.getNumNodes(), nodeCountBefore) << "the Gate must not be spliced out and left unspliced-in";

    const auto after = synth::buildMixerSnapshot(graph, doc, editor.getMacros());
    const synth::MixerColumn* afterColumn = nullptr;
    for (const auto& column : after.columns)
        if (column.nodeId == channel.strip->nodeID)
            afterColumn = &column;
    ASSERT_NE(afterColumn, nullptr);
    ASSERT_EQ(afterColumn->inserts.size(), 3u) << "the bus's own chain must still be intact";
    EXPECT_EQ(afterColumn->inserts[0].name, "Gate");
    EXPECT_EQ(afterColumn->inserts[1].name, "Parametric EQ");
    EXPECT_EQ(afterColumn->inserts[2].name, "Compressor");
}
