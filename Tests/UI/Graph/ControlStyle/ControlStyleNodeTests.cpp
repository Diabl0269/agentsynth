// ControlStyleNodeTests.cpp -- a module's own control style is the node property "controlStyle". It follows
// the card layout's trust rules (docs/ai/patch-format.md#per-node-controlstyle): trusted paths only, emitted only
// when set, part of the undo snapshot cache, kept by the in-app clipboard.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/GraphSnapshotCache.h"
#include "Modules/FilterModule.h"
#include "SnippetManager.h"
#include "UI/Graph/CardBody/ControlStyleNodeOverride.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;
using synth::SnippetManager;
using synth::theme::ControlStyleOverride;
using synth::theme::KnobStyle;

namespace {

const ControlStyleOverride kNeonKnobs{KnobStyle::Neon, std::nullopt};
const ControlStyleOverride kRingBoth{KnobStyle::Ring, KnobStyle::Ring};

struct OneFilter {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::NodeID id;

    OneFilter() {
        auto node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("x", 40);
        node->properties.set("y", 60);
        id = node->nodeID;
    }

    ControlStyleOverride style() const { return synth::getControlStyleNodeOverride(graph, id); }
};

const juce::DynamicObject* firstNode(const juce::var& graphJson) {
    const auto* nodes = graphJson.getDynamicObject()->getProperty("nodes").getArray();
    return nodes != nullptr && !nodes->isEmpty() ? nodes->getReference(0).getDynamicObject() : nullptr;
}

juce::String text(const juce::var& value) { return juce::JSON::toString(value); }

} // namespace

TEST(ControlStyleNodeTest, ANodeWithoutAStyleSerialisesExactlyAsBefore) {
    OneFilter patch;
    const auto before = AIStateMapper::graphToJSON(patch.graph);
    EXPECT_FALSE(firstNode(before)->hasProperty("controlStyle"));

    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, kNeonKnobs));
    const auto with = AIStateMapper::graphToJSON(patch.graph);
    ASSERT_TRUE(firstNode(with)->hasProperty("controlStyle"));
    const auto style = firstNode(with)->getProperty("controlStyle");
    EXPECT_EQ(style.getProperty("knob", {}).toString(), "neon");
    EXPECT_FALSE(style.getDynamicObject()->hasProperty("fader"));

    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, {}));
    EXPECT_EQ(text(AIStateMapper::graphToJSON(patch.graph)), text(before));
}

TEST(ControlStyleNodeTest, TheStyleRoundTripsOnTheTrustedPathAndIsIgnoredOnTheUntrustedOne) {
    OneFilter patch;
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, kRingBoth));
    const auto json = AIStateMapper::graphToJSON(patch.graph);

    juce::AudioProcessorGraph trusted;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, trusted, /*clearExisting=*/true, /*trusted=*/true));
    EXPECT_EQ(synth::getControlStyleNodeOverride(trusted, trusted.getNodes().getFirst()->nodeID), kRingBoth);

    juce::AudioProcessorGraph untrusted;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, untrusted, /*clearExisting=*/true, /*trusted=*/false));
    ASSERT_EQ(untrusted.getNumNodes(), 1);
    EXPECT_TRUE(synth::getControlStyleNodeOverride(untrusted, untrusted.getNodes().getFirst()->nodeID).isEmpty());

    // Merge mode updates an existing node: it must not gain the style either.
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, {}));
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, patch.graph, /*clearExisting=*/false, /*trusted=*/false));
    for (auto* node : patch.graph.getNodes())
        EXPECT_TRUE(synth::getControlStyleNodeOverride(patch.graph, node->nodeID).isEmpty());
}

TEST(ControlStyleNodeTest, TheStyleIsNeverAdvertisedInThePatchSchema) {
    EXPECT_FALSE(text(AIStateMapper::getPatchSchema()).contains("controlStyle"));
    EXPECT_FALSE(text(AIStateMapper::getPatchSchemaWithTimelineOps()).contains("controlStyle"));
}

TEST(ControlStyleNodeTest, UndoAndRedoRestoreAndClearTheStyleInOneStepEach) {
    OneFilter patch;
    AppUndoManager undo;
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, &undo, patch.id, kNeonKnobs));
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, &undo, patch.id, kRingBoth));
    EXPECT_EQ(patch.style(), kRingBoth);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(patch.style(), kNeonKnobs);
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(patch.style().isEmpty()) << "undoing the first write removes the property";
    EXPECT_FALSE(undo.canUndo());
    ASSERT_TRUE(undo.redo());
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(patch.style(), kRingBoth);
}

TEST(ControlStyleNodeTest, AWriteThatChangesNothingRecordsNoUndoStep) {
    OneFilter patch;
    AppUndoManager undo;
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, &undo, patch.id, {}));
    EXPECT_FALSE(undo.canUndo());
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, kNeonKnobs));
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, &undo, patch.id, kNeonKnobs));
    EXPECT_FALSE(undo.canUndo());
    EXPECT_FALSE(
        synth::setControlStyleNodeOverride(patch.graph, nullptr, juce::AudioProcessorGraph::NodeID(999), kNeonKnobs));
}

TEST(ControlStyleNodeTest, TheSnapshotCacheSeesAStyleChangeAsAChange) {
    OneFilter patch;
    synth::GraphSnapshotCache cache;
    cache.capture(patch.graph);
    cache.capture(patch.graph);
    EXPECT_EQ(cache.lastRebuiltNodes(), 0) << "nothing changed, nothing rebuilt";

    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, kNeonKnobs));
    const auto changed = cache.capture(patch.graph);
    EXPECT_EQ(cache.lastRebuiltNodes(), 1);
    EXPECT_EQ(text(changed), text(AIStateMapper::graphToJSON(patch.graph)));

    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, kRingBoth));
    cache.capture(patch.graph);
    EXPECT_EQ(cache.lastRebuiltNodes(), 1);
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, {}));
    EXPECT_EQ(text(cache.capture(patch.graph)), text(AIStateMapper::graphToJSON(patch.graph)));
}

TEST(ControlStyleNodeTest, ASnippetOnDiskDropsTheStyleAndTheInAppClipboardKeepsIt) {
    OneFilter patch;
    ASSERT_TRUE(synth::setControlStyleNodeOverride(patch.graph, nullptr, patch.id, kRingBoth));
    const auto clipboard =
        SnippetManager::extractSnippet(patch.graph, {patch.id}, "Clipboard", /*includeExtraState=*/true);
    const auto file = SnippetManager::extractSnippet(patch.graph, {patch.id}, "File", /*includeExtraState=*/false);
    EXPECT_TRUE(firstNode(clipboard)->hasProperty("controlStyle"));
    EXPECT_FALSE(firstNode(file)->hasProperty("controlStyle"));

    juce::AudioProcessorGraph target;
    const auto dropped = SnippetManager::insertSnippet(clipboard, target, {0, 0}, /*includeExtraState=*/false);
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_TRUE(synth::getControlStyleNodeOverride(target, dropped.front()).isEmpty());
    const auto kept = SnippetManager::insertSnippet(clipboard, target, {0, 200}, /*includeExtraState=*/true);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(synth::getControlStyleNodeOverride(target, kept.front()), kRingBoth);
}
