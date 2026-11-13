// CardLayoutTrustTests.cpp
//
// A built-in module's per-instance layout is the node property "cardLayout". It rides the trusted
// paths only (undo snapshots, presets, projects, the in-app clipboard); the untrusted apply path
// ignores it, getPatchSchema() never advertises it, a .agsnip on disk drops it, and a node without
// one serialises exactly as before. See docs/layout/module-card-layout.md#trust-boundary-and-the-ai-patch-format.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/CardLayout.h"
#include "Modules/FilterModule.h"
#include "SnippetManager.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;

using synth::CardLayout;
using synth::SnippetManager;

namespace {

CardLayout hidingDrive() {
    synth::CardParamItem item;
    item.paramId = "cutoff";
    item.widget = synth::CardWidget::KnobLarge;
    synth::CardSection section;
    section.id = "main";
    section.title = "Tone";
    section.items.emplace_back(item);

    CardLayout layout;
    layout.sections = {section};
    layout.hidden.add("drive");
    return layout;
}

CardLayout hidingResonance() {
    auto layout = hidingDrive();
    layout.hidden = juce::StringArray{"resonance"};
    return layout;
}

struct OneFilter {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::NodeID id;

    OneFilter() {
        auto node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("x", 40);
        node->properties.set("y", 60);
        id = node->nodeID;
    }

    juce::var layoutProperty() const { return synth::getCardLayoutOverride(graph, id); }
};

const juce::DynamicObject* firstNode(const juce::var& graphJson) {
    const auto* nodes = graphJson.getDynamicObject()->getProperty("nodes").getArray();
    return nodes != nullptr && !nodes->isEmpty() ? nodes->getReference(0).getDynamicObject() : nullptr;
}

juce::String text(const juce::var& value) { return juce::JSON::toString(value); }

juce::File tempDir() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("agentsynth-cardlayout-trust-" + juce::Uuid().toString());
}

} // namespace

TEST(CardLayoutTrustTest, ANodeWithoutALayoutSerialisesExactlyAsBefore) {
    OneFilter patch;
    const auto before = AIStateMapper::graphToJSON(patch.graph);
    ASSERT_NE(firstNode(before), nullptr);
    EXPECT_FALSE(firstNode(before)->hasProperty("cardLayout"));

    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, nullptr, patch.id, hidingDrive()));
    EXPECT_TRUE(firstNode(AIStateMapper::graphToJSON(patch.graph))->hasProperty("cardLayout"));

    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, nullptr, patch.id, std::nullopt));
    EXPECT_EQ(text(AIStateMapper::graphToJSON(patch.graph)), text(before))
        << "set then cleared leaves the JSON byte-identical to a node that never had one";
}

TEST(CardLayoutTrustTest, TheLayoutRoundTripsThroughGraphJsonOnTheTrustedPath) {
    OneFilter patch;
    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, nullptr, patch.id, hidingDrive()));

    const auto json = AIStateMapper::graphToJSON(patch.graph);
    const auto emitted = firstNode(json)->getProperty("cardLayout");
    ASSERT_TRUE(emitted.isObject());
    EXPECT_EQ(CardLayout::fromVar(emitted).layout, hidingDrive());

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, reloaded, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(reloaded.getNumNodes(), 1);
    const auto restored = synth::getCardLayoutOverride(reloaded, reloaded.getNodes().getFirst()->nodeID);
    EXPECT_EQ(CardLayout::fromVar(restored).layout, hidingDrive());
}

TEST(CardLayoutTrustTest, TheEmittedValueIsACopyNotTheLivePropertyObject) {
    OneFilter patch;
    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, nullptr, patch.id, hidingDrive()));

    const auto json = AIStateMapper::graphToJSON(patch.graph);
    firstNode(json)->getProperty("cardLayout").getDynamicObject()->setProperty("version", 99);

    EXPECT_EQ(CardLayout::fromVar(patch.layoutProperty()).status, CardLayout::ParseStatus::Ok)
        << "mutating a snapshot must not corrupt the node";
}

TEST(CardLayoutTrustTest, UntrustedApplyIgnoresTheLayoutOnNewAndExistingNodes) {
    OneFilter source;
    ASSERT_TRUE(synth::setCardLayoutOverride(source.graph, nullptr, source.id, hidingDrive()));
    const auto json = AIStateMapper::graphToJSON(source.graph);

    juce::AudioProcessorGraph replaced;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, replaced, /*clearExisting=*/true, /*trusted=*/false));
    ASSERT_EQ(replaced.getNumNodes(), 1);
    EXPECT_TRUE(synth::getCardLayoutOverride(replaced, replaced.getNodes().getFirst()->nodeID).isVoid());

    // Merge mode updates a node that already exists; it must not gain the layout either.
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, source.graph, /*clearExisting=*/false, /*trusted=*/false));
    ASSERT_TRUE(synth::setCardLayoutOverride(source.graph, nullptr, source.id, std::nullopt));
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, source.graph, /*clearExisting=*/false, /*trusted=*/false));
    for (auto* node : source.graph.getNodes())
        EXPECT_TRUE(synth::getCardLayoutOverride(source.graph, node->nodeID).isVoid());
}

TEST(CardLayoutTrustTest, TheLayoutIsNeverAdvertisedInThePatchSchema) {
    EXPECT_FALSE(text(AIStateMapper::getPatchSchema()).contains("cardLayout"));
    EXPECT_FALSE(text(AIStateMapper::getPatchSchemaWithTimelineOps()).contains("cardLayout"));
}

TEST(CardLayoutTrustTest, ASnippetOnDiskDropsTheLayoutAndTheInAppClipboardKeepsIt) {
    OneFilter patch;
    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, nullptr, patch.id, hidingDrive()));

    const auto clipboard =
        SnippetManager::extractSnippet(patch.graph, {patch.id}, "Clipboard", /*includeExtraState=*/true);
    const auto file = SnippetManager::extractSnippet(patch.graph, {patch.id}, "File", /*includeExtraState=*/false);
    EXPECT_TRUE(firstNode(clipboard)->hasProperty("cardLayout"));
    EXPECT_FALSE(firstNode(file)->hasProperty("cardLayout"));

    const auto dir = tempDir();
    ASSERT_TRUE(dir.createDirectory());
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Tone", clipboard));
    const auto written = SnippetManager::saveSnippet(dir, "Plain", file);
    ASSERT_TRUE(written);
    const auto reread = SnippetManager::loadSnippet(dir.getChildFile("Plain.agsnip"));
    ASSERT_NE(firstNode(reread), nullptr);
    EXPECT_FALSE(firstNode(reread)->hasProperty("cardLayout"));
    dir.deleteRecursively();

    // Inserting a hand-edited file that carries one: dropped unless the caller asked for extra state.
    juce::AudioProcessorGraph target;
    const auto dropped = SnippetManager::insertSnippet(clipboard, target, {0, 0}, /*includeExtraState=*/false);
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_TRUE(synth::getCardLayoutOverride(target, dropped.front()).isVoid());

    const auto kept = SnippetManager::insertSnippet(clipboard, target, {0, 200}, /*includeExtraState=*/true);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(CardLayout::fromVar(synth::getCardLayoutOverride(target, kept.front())).layout, hidingDrive());
}

TEST(CardLayoutTrustTest, ANodePreservingUndoRestoresAndClearsTheLayout) {
    OneFilter patch;
    AppUndoManager undo;
    auto* processor = patch.graph.getNodeForId(patch.id)->getProcessor();

    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, &undo, patch.id, hidingDrive()));
    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, &undo, patch.id, hidingResonance()));
    EXPECT_EQ(CardLayout::fromVar(patch.layoutProperty()).layout, hidingResonance());

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(CardLayout::fromVar(patch.layoutProperty()).layout, hidingDrive()) << "the previous layout comes back";
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(patch.layoutProperty().isVoid()) << "undoing the first layout removes the property";
    EXPECT_FALSE(undo.canUndo());

    ASSERT_TRUE(undo.redo());
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(CardLayout::fromVar(patch.layoutProperty()).layout, hidingResonance());

    EXPECT_EQ(patch.graph.getNodeForId(patch.id)->getProcessor(), processor)
        << "the node was kept, not rebuilt, by the undo";
}

TEST(CardLayoutTrustTest, WritingTheSameLayoutOrClearingNothingRecordsNoUndoStep) {
    OneFilter patch;
    AppUndoManager undo;

    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, &undo, patch.id, std::nullopt));
    EXPECT_FALSE(undo.canUndo());

    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, &undo, patch.id, hidingDrive()));
    ASSERT_TRUE(undo.undo());
    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, nullptr, patch.id, hidingDrive()));
    AppUndoManager second;
    ASSERT_TRUE(synth::setCardLayoutOverride(patch.graph, &second, patch.id, hidingDrive()));
    EXPECT_FALSE(second.canUndo()) << "an identical write is a no-op";
}

TEST(CardLayoutTrustTest, ASetterForAMissingNodeFails) {
    OneFilter patch;
    EXPECT_FALSE(
        synth::setCardLayoutOverride(patch.graph, nullptr, juce::AudioProcessorGraph::NodeID(999), hidingDrive()));
    EXPECT_TRUE(synth::getCardLayoutOverride(patch.graph, juce::AudioProcessorGraph::NodeID(999)).isVoid());
}
