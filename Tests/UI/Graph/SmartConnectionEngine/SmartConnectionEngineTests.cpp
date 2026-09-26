// SmartConnectionEngineTests.cpp
//
// Engine-level coverage for SmartConnectionEngine (FRO77 PR1): drives it directly against a real
// AudioEngine graph through GraphEditor::getCanvasHostForTest() (a minimal host — GraphEditor
// supplies real ModuleComponents/graph/undo, but the test never goes through GraphEditor's own
// drag-preview gesture chain (beginDragPreview/updateDragPreview) or forwarders — it builds a
// DragPreviewState by hand and drives a SEPARATE SmartConnectionEngine instance directly, proving
// the engine's own API works in isolation. The existing GraphEditorSmartConnection*Tests.cpp files
// cover the full gesture chain through GraphEditor's forwarders and are unchanged by this PR.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AudioEngine/AudioEngine.h"

#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h"

namespace {

ModuleComponent* findModuleComponent(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == nodeId)
            return c;
    return nullptr;
}

} // namespace

TEST(SmartConnectionEngineTest, ModeStringRoundTrip) {
    using Mode = SmartConnectionEngine::SmartConnectionMode;

    EXPECT_EQ(SmartConnectionEngine::smartConnectionModeFromString("Off"), Mode::Off);
    EXPECT_EQ(SmartConnectionEngine::smartConnectionModeFromString("NewOnly"), Mode::NewOnly);
    EXPECT_EQ(SmartConnectionEngine::smartConnectionModeFromString("AllMoves"), Mode::AllMoves);
    EXPECT_EQ(SmartConnectionEngine::smartConnectionModeFromString("NewAndUnwired"), Mode::NewAndUnwired);
    // Unrecognised strings fall back to the default mode, same as GraphEditor's own forwarder.
    EXPECT_EQ(SmartConnectionEngine::smartConnectionModeFromString("bogus"), Mode::NewAndUnwired);

    for (auto mode : {Mode::Off, Mode::NewOnly, Mode::NewAndUnwired, Mode::AllMoves}) {
        const auto s = SmartConnectionEngine::smartConnectionModeToString(mode);
        EXPECT_EQ(SmartConnectionEngine::smartConnectionModeFromString(s), mode) << s;
    }
}

TEST(SmartConnectionEngineTest, SuggestsForAFreeJackPair) {
    AudioEngine audioEngine;
    GraphEditor editor(audioEngine);
    editor.setSize(800, 600);

    auto& graph = audioEngine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComponent(editor, oscNode->nodeID);
    ASSERT_NE(oscComp, nullptr);

    SmartConnectionEngine engineUnderTest(editor.getCanvasHostForTest());
    engineUnderTest.setSmartConnectionMode(SmartConnectionEngine::SmartConnectionMode::NewAndUnwired);

    SmartConnectionEngine::DragPreviewState drag;
    drag.active = true;
    drag.selfId = oscNode->nodeID;
    // Land the ghost just to the LEFT of the filter (its own output jack must sit left of the
    // filter's input jack, or the flow-direction check in refreshSmartSuggestions rejects the
    // pair) — a 20px gap, well within the 96px proximity window.
    drag.ghost = juce::Rectangle<int>(400 - oscComp->getWidth() - 20, 100, oscComp->getWidth(), oscComp->getHeight());

    engineUnderTest.refreshSmartSuggestions(drag);

    EXPECT_GT(engineUnderTest.getSmartSuggestionCount(), 0)
        << "a free, compatible jack pair within range should be suggested";
    bool foundOscToFilter = false;
    for (const auto& s : engineUnderTest.getSmartSuggestions()) {
        if (s.ghostIsSource && s.neighborId == filterNode->nodeID)
            foundOscToFilter = true;
    }
    EXPECT_TRUE(foundOscToFilter);
}

TEST(SmartConnectionEngineTest, NoSuggestionWhenJacksAlreadyConnected) {
    AudioEngine audioEngine;
    GraphEditor editor(audioEngine);
    editor.setSize(1000, 600);

    auto& graph = audioEngine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    // A third module the oscillator is dragged near AFTER it is already wired to the filter —
    // proves "already connected" blocks a suggestion at a DIFFERENT neighbour too (mode
    // NewAndUnwired requires the SOURCE jack free, not just the pair under test), mirroring
    // GraphEditorSmartConnectionTests.cpp's SmartConnectionNewAndUnwiredSkipsAlreadyWiredMove.
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    vcaNode->properties.set("x", 700);
    vcaNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Pre-wire the oscillator's only audio output jack to the filter.
    editor.connectPorts(oscNode->nodeID, 0, filterNode->nodeID, 0, false, false);
    ASSERT_TRUE(editor.nodeHasCables(oscNode->nodeID));

    auto* oscComp = findModuleComponent(editor, oscNode->nodeID);
    ASSERT_NE(oscComp, nullptr);

    SmartConnectionEngine engineUnderTest(editor.getCanvasHostForTest());
    engineUnderTest.setSmartConnectionMode(SmartConnectionEngine::SmartConnectionMode::NewAndUnwired);

    SmartConnectionEngine::DragPreviewState drag;
    drag.active = true;
    drag.selfId = oscNode->nodeID;
    // Now drag the (already-wired) oscillator near the VCA instead.
    drag.ghost = juce::Rectangle<int>(700 - oscComp->getWidth() - 20, 100, oscComp->getWidth(), oscComp->getHeight());

    engineUnderTest.refreshSmartSuggestions(drag);

    EXPECT_EQ(engineUnderTest.getSmartSuggestionCount(), 0)
        << "the oscillator's only output jack is already wired, so nothing new should be offered";
    EXPECT_TRUE(engineUnderTest.areJacksAlreadyConnected(oscNode->nodeID, 0, filterNode->nodeID, 0, false));
}

TEST(SmartConnectionEngineTest, InsertModifierOverrideResamplesOnChange) {
    AudioEngine audioEngine;
    GraphEditor editor(audioEngine);
    editor.setSize(800, 600);

    auto& graph = audioEngine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 40);
    oscNode->properties.set("y", 100);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComponent(editor, oscNode->nodeID);
    ASSERT_NE(oscComp, nullptr);

    SmartConnectionEngine engineUnderTest(editor.getCanvasHostForTest());
    engineUnderTest.setSmartConnectionMode(SmartConnectionEngine::SmartConnectionMode::NewAndUnwired);

    SmartConnectionEngine::DragPreviewState drag;
    drag.active = true;
    drag.selfId = oscNode->nodeID;
    drag.ghost = juce::Rectangle<int>(400 - oscComp->getWidth() - 20, 100, oscComp->getWidth(), oscComp->getHeight());

    // The engine's own baseline sample (lastSampledInsertModifier_) starts false, so the FIRST
    // override reading has to differ from that or the "nothing changed" guard swallows it before
    // ever calling refreshSmartSuggestions.
    engineUnderTest.setInsertModifierOverrideForTests(true);
    engineUnderTest.refreshSuggestionsIfInsertModifierChanged(drag);
    const int countAfterFirstSample = engineUnderTest.getSmartSuggestionCount();
    EXPECT_GT(countAfterFirstSample, 0);

    // Same reading again: nothing changed since the last drag tick, so this is a no-op — the
    // suggestion set (and therefore the count) stays exactly as it was.
    engineUnderTest.refreshSuggestionsIfInsertModifierChanged(drag);
    EXPECT_EQ(engineUnderTest.getSmartSuggestionCount(), countAfterFirstSample);

    // Flip the override: isInsertModifierDown() reflects it immediately, and the next drag tick
    // re-samples and refreshes rather than staying latched on the stale reading.
    engineUnderTest.setInsertModifierOverrideForTests(false);
    EXPECT_FALSE(engineUnderTest.isInsertModifierDown());
    engineUnderTest.refreshSuggestionsIfInsertModifierChanged(drag);
    EXPECT_GT(engineUnderTest.getSmartSuggestionCount(), 0);
}
