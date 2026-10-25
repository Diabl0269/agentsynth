// A cable released on a jack must not touch a card the connection itself rebuilt. endConnectionDrag walks the
// cards for a drop target; the connect it makes can rebuild them (a macro port minted across a boundary, a Dual
// I/O change), so the walk must stop at the first connection and never read the array again.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// With the six modules each test adds, the canvas holds exactly 16 cards before the drop: the 17th makes the card array
// grow past its allocation, so a walk that goes on after the connect reads freed memory (visible to a sanitizer build).
constexpr int kFillerCards = 10;

NodeID addAt(AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    return node->nodeID;
}

ModuleComponent* cardOf(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

int edgesInto(AudioEngine& engine, NodeID dst) {
    int n = 0;
    for (const auto& c : engine.getGraph().getConnections())
        if (c.destination.nodeID == dst && !c.destination.isMIDI())
            ++n;
    return n;
}

bool hasEdge(AudioEngine& engine, NodeID src, int srcCh, NodeID dst, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == src && c.source.channelIndex == srcCh && c.destination.nodeID == dst &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

juce::Point<int> inputJackOn(ModuleComponent& card, int jack) {
    return card.getBounds().getPosition() + card.getPortCenter(jack, /*isInput=*/true);
}

} // namespace

// Regression test for FRO1075: the drop crashed reading a card the connect had freed, on a Delay that already had
// three cables and a source inside a macro.
TEST_F(GraphEditorTest, DroppingACableThatMintsAMacroPortLeavesOneConnectionAndNoStaleCard) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    editor.setSize(1600, 1200);

    const auto delay = addAt(engine, std::make_unique<DelayModule>(), 1000, 100);
    std::vector<NodeID> feeders;
    for (int i = 0; i < 3; ++i) {
        feeders.push_back(addAt(engine, std::make_unique<OscillatorModule>(), 100, 700 + i * 10));
        engine.getGraph().addConnection({{feeders.back(), 0}, {delay, 0}});
        engine.getGraph().addConnection({{feeders.back(), 1}, {delay, 1}});
    }
    const auto member = addAt(engine, std::make_unique<OscillatorModule>(), 100, 100);
    const auto filler = addAt(engine, std::make_unique<FilterModule>(), 100, 400);
    for (int i = 0; i < kFillerCards;
         ++i) // cards after the target in the array: a walk that goes on has something to read
        addAt(engine, std::make_unique<FilterModule>(), 1300, 50 + i * 20);
    editor.updateComponents();

    editor.setSelectedNodes({member, filler});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    sizeModuleComponents(editor);

    auto* memberCard = cardOf(editor, member);
    auto* delayCard = cardOf(editor, delay);
    ASSERT_NE(memberCard, nullptr);
    ASSERT_NE(delayCard, nullptr);
    const int before = edgesInto(engine, delay);
    ASSERT_EQ(before, 6);

    editor.beginConnectionDrag(memberCard, /*jack=*/0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(inputJackOn(*delayCard, 0));

    EXPECT_GT(edgesInto(engine, delay), before) << "the cable landed";
    EXPECT_FALSE(hasEdge(engine, member, 0, delay, 0)) << "across the macro boundary it goes through a port";
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
}

TEST_F(GraphEditorTest, DroppingTheRightOutputOfAStereoSourceOnADelayWithThreeCablesWiresBothLegs) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    editor.setSize(1600, 1200);

    const auto delay = addAt(engine, std::make_unique<DelayModule>(), 900, 100);
    for (int i = 0; i < 3; ++i) {
        const auto feeder = addAt(engine, std::make_unique<OscillatorModule>(), 100, 600 + i * 10);
        engine.getGraph().addConnection({{feeder, 0}, {delay, 0}});
        engine.getGraph().addConnection({{feeder, 1}, {delay, 1}});
    }
    const auto stereo = addAt(engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* stereoCard = cardOf(editor, stereo);
    auto* delayCard = cardOf(editor, delay);
    ASSERT_NE(stereoCard, nullptr);
    ASSERT_NE(delayCard, nullptr);
    const int before = edgesInto(engine, delay);

    editor.beginConnectionDrag(stereoCard, /*jack=*/0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(inputJackOn(*delayCard, 0));

    EXPECT_EQ(edgesInto(engine, delay), before + 2) << "a collapsed stereo pair takes both legs";
    undo.undo();
    EXPECT_EQ(edgesInto(engine, delay), before) << "one undo step";
}

TEST_F(GraphEditorTest, ADropOverTwoOverlappingCardsConnectsOnlyTheOneOnTop) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    editor.setSize(1600, 1200);

    const auto below = addAt(engine, std::make_unique<DelayModule>(), 900, 100);
    const auto above = addAt(engine, std::make_unique<DelayModule>(), 900, 100);
    const auto source = addAt(engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* belowCard = cardOf(editor, below);
    auto* aboveCard = cardOf(editor, above);
    auto* sourceCard = cardOf(editor, source);
    ASSERT_NE(belowCard, nullptr);
    ASSERT_NE(aboveCard, nullptr);
    ASSERT_NE(sourceCard, nullptr);
    aboveCard->setBounds(belowCard->getBounds());
    aboveCard->toFront(false);

    editor.beginConnectionDrag(sourceCard, /*jack=*/0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(inputJackOn(*aboveCard, 0));

    EXPECT_GT(edgesInto(engine, above), 0) << "the card on top takes the cable";
    EXPECT_EQ(edgesInto(engine, below), 0) << "the one underneath is not wired as well";
}

TEST_F(GraphEditorTest, DroppingACableOnAMacroMemberThatAlreadyHasThreeCablesMintsOneInletAndNoStaleCard) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    editor.setSize(1600, 1200);

    const auto delay = addAt(engine, std::make_unique<DelayModule>(), 300, 100);
    const auto filler = addAt(engine, std::make_unique<FilterModule>(), 300, 400);
    std::vector<NodeID> feeders;
    for (int i = 0; i < 3; ++i) {
        feeders.push_back(addAt(engine, std::make_unique<OscillatorModule>(), 1000, 100 + i * 10));
        engine.getGraph().addConnection({{feeders.back(), 0}, {delay, 0}});
        engine.getGraph().addConnection({{feeders.back(), 1}, {delay, 1}});
    }
    const auto source = addAt(engine, std::make_unique<OscillatorModule>(), 1000, 600);
    for (int i = 0; i < kFillerCards; ++i)
        addAt(engine, std::make_unique<FilterModule>(), 1300, 50 + i * 20);
    editor.updateComponents();

    editor.setSelectedNodes({delay, filler});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    sizeModuleComponents(editor);
    auto* sourceCard = cardOf(editor, source);
    auto* delayCard = cardOf(editor, delay);
    ASSERT_NE(sourceCard, nullptr);
    ASSERT_NE(delayCard, nullptr);

    editor.beginConnectionDrag(sourceCard, /*jack=*/0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(inputJackOn(*delayCard, 0));

    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_FALSE(hasEdge(engine, source, 0, delay, 0)) << "across the boundary it goes through an inlet";
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
}
