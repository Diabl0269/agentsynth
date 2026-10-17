// MixerInsertListTests.cpp -- MixerInsertList's read-only state, and the moveRow regression the bus
// insert-discovery fix opened.
//
//   * read-only -- a branching chain lists its rows with no "+ Insert" row and no link row of its own: the column
//                  header's "Edit on canvas" button is the way into the canvas (MixerColumnComponentTests.cpp).
//   * moveRow  -- a bus's sourceNodeId_ is invalid (no external predecessor -- docs/mixer/mixer.md
//                 docs/mixer/sends-and-buses.md D6), so moving a row to the very front of its chain must refuse rather
//                 than splice against an invalid id and orphan the node (reorderInsert's second step has no rollback of
//                 its own).
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerInsertList/MixerInsertList.h"
#include "UI/Mixer/MixerSections/MixerSectionLayout.h"
#include <gtest/gtest.h>

namespace {

juce::MouseEvent makeMouseDownAt(juce::Component& component, juce::Point<int> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &component, &component, juce::Time::getCurrentTime(), position.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

} // namespace

// Regression test for FRO894: a chain that stops being editable swapped its "+ Insert" row for an "Edit on canvas"
// link row that did nothing. The list now has no link row at all; "+ Insert" is simply absent while read-only.
TEST(MixerInsertListTests, AReadOnlyListHasNoLinkRowAndAClickOnItDoesNothing) {
    synth::ui::MixerInsertList list;
    list.setEntries({}, /*linear=*/false, {}, {});
    list.setSize(140, list.getPreferredHeight());

    EXPECT_EQ(list.getPreferredHeight(), synth::ui::MixerSectionLayout::kInsertRowHeight)
        << "the empty state is one placeholder row, no link row beneath it";
    EXPECT_FALSE(list.hasAddRow());
    EXPECT_EQ(list.getRowCount(), 0);

    bool mutated = false;
    list.onMutated = [&] { mutated = true; };
    list.mouseDown(makeMouseDownAt(list, {10, synth::ui::MixerSectionLayout::kInsertRowHeight / 2}));
    list.mouseDown(makeMouseDownAt(list, {10, synth::ui::MixerSectionLayout::kInsertRowHeight + 4}));
    EXPECT_FALSE(mutated);
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
    list.setEntries(busColumn->inserts, busColumn->insertChainIsLinear, busColumn->sourceNodeId, busColumn->nodeId);

    bool mutated = false;
    list.onMutated = [&] { mutated = true; };

    // Row 1 is the EQ (asserted by the model test) -- move it up to row 0, the front of the
    // bus's own chain, where there is no external predecessor to splice against.
    list.moveRow(1, -1);

    EXPECT_FALSE(mutated) << "nothing should have changed -- see spliceInInsert's own contract";
    EXPECT_EQ(graph.getNumNodes(), nodeCountBefore) << "the EQ must not be spliced out and left unspliced-in";

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
