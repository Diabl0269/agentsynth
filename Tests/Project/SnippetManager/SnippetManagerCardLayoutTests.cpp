// SnippetManagerCardLayoutTests.cpp -- FRO137: a plugin-card layout override (HostedPluginModule's
// trusted-only "cardLayout" extra-state key) rides the SAME `includeExtraState`/`trustedPayload`
// rules every other module's non-parameter `state` already follows (see
// SnippetManagerExtractTests.cpp's own SnippetExtraState group) -- there is no cardLayout-specific
// code anywhere in this path, so this suite exists to prove that general mechanism actually covers
// it, not to add a new one.

#include "../../StubPluginInstance.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "SnippetManagerTestHelpers.h"
#include <chrono>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Snippet Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE04;
    description.deprecatedUid = 0xC0DE04;
    description.fileOrIdentifier = "/nonexistent/test/path/SnippetPlugin.vst3";
    return description;
}

/** A hosted plugin node with a card-layout override set, ready to extract/insert. */
struct HostedNode {
    StubBackend backend;
    HostedPluginModule* module = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;

    void addTo(juce::AudioProcessorGraph& graph) {
        backend.setFactory([]() -> std::unique_ptr<StubPluginInstance> {
            return std::make_unique<StubPluginInstance>(
                2, 2, "Snippet Plugin", 0xC0DE04, "VST3",
                std::vector<StubParamSpec>{{"cutoff", "Cutoff", 0.0f, {}, false}});
        });
        auto module_ = std::make_unique<HostedPluginModule>();
        module = module_.get();
        node = graph.addNode(std::move(module_));
        node->properties.set("x", 0);
        node->properties.set("y", 0);
        module->prepareToPlay(kSampleRate, kBlockSize);
        module->loadPlugin(stubDescription(), backend);
        EXPECT_TRUE(pumpUntil([this] { return module->hasInstance(); }));

        synth::CardLayout layout;
        synth::CardSlot slot;
        slot.paramId = "cutoff";
        layout.slots.push_back(slot);
        module->setCardLayoutOverride(layout.toVar());
    }
};

} // namespace

TEST(SnippetCardLayout, IsOmittedByDefaultAndCarriedWhenRequested) {
    juce::AudioProcessorGraph graph;
    HostedNode hosted;
    hosted.addTo(graph);
    ASSERT_FALSE(hosted.module->getCardLayoutOverride().isVoid()) << "the module under test must actually carry one";

    const std::vector<NodeID> selection{hosted.node->nodeID};

    auto onDisk = SnippetManager::extractSnippet(graph, selection, "S");
    auto* onDiskNodes = arrayOf(onDisk, "nodes");
    ASSERT_NE(onDiskNodes, nullptr);
    for (const auto& n : *onDiskNodes)
        EXPECT_FALSE(n.getDynamicObject()->hasProperty("state"))
            << "a .agsnip on disk must not carry cardLayout (it lives inside the module's `state`)";

    auto inMemory = SnippetManager::extractSnippet(graph, selection, "S", /*includeExtraState=*/true);
    auto* inMemoryNodes = arrayOf(inMemory, "nodes");
    ASSERT_NE(inMemoryNodes, nullptr);
    bool sawCardLayout = false;
    for (const auto& n : *inMemoryNodes) {
        if (auto* state = n.getDynamicObject()->getProperty("state").getDynamicObject())
            sawCardLayout = sawCardLayout || state->hasProperty("cardLayout");
    }
    EXPECT_TRUE(sawCardLayout) << "the in-app clipboard opts in, so a duplicated plugin card keeps its chosen knobs";
}

TEST(SnippetCardLayout, TrustedInsertCarriesTheLayoutOntoTheNewNode) {
    juce::AudioProcessorGraph sourceGraph;
    HostedNode hosted;
    hosted.addTo(sourceGraph);
    const std::vector<NodeID> selection{hosted.node->nodeID};

    // Same pairing every trusted insert uses (Source/CLAUDE.md): captured WITH extra state, applied
    // WITH trustedPayload=true (SnippetManager::insertSnippet's own doc comment on what that means).
    auto snippet = SnippetManager::extractSnippet(sourceGraph, selection, "S", /*includeExtraState=*/true);

    juce::AudioProcessorGraph targetGraph;
    const auto added = SnippetManager::insertSnippet(snippet, targetGraph, {0, 0}, /*includeExtraState=*/true, nullptr,
                                                     /*trustedPayload=*/true);
    ASSERT_EQ(added.size(), 1u);

    auto* pastedModule = dynamic_cast<HostedPluginModule*>(targetGraph.getNodeForId(added[0])->getProcessor());
    ASSERT_NE(pastedModule, nullptr);
    // HostedPluginModule::setExtraState() applies "cardLayout" synchronously, independently of the
    // (async) plugin identity load -- see its own comment on why the two are separate concerns.
    ASSERT_FALSE(pastedModule->getCardLayoutOverride().isVoid());

    const auto parsed = synth::CardLayout::fromVar(pastedModule->getCardLayoutOverride());
    ASSERT_EQ(parsed.status, synth::CardLayout::ParseStatus::Ok);
    ASSERT_EQ(parsed.layout.slots.size(), 1u);
    EXPECT_EQ(parsed.layout.slots[0].paramId, "cutoff");
}

// NOTE: `trustedPayload` alone does not gate `state`/`cardLayout` -- insertSnippet always applies
// on the trusted path once validation passes (SnippetManager.cpp's own comment: "The apply itself
// then runs on the TRUSTED path, and that pairing is deliberate"). The REAL gate is
// `includeExtraState`, decided by the CALLER from where the JSON came from
// (GraphEditorCommands.cpp's own dropped-snippet-FILE call passes `includeExtraState=false`,
// exactly what this test mirrors) -- see SnippetManagerExtractTests.cpp's
// `SnippetExtraState.PrepareForInsertStripsAStateKeyUnlessItWasAskedFor` for the same rule proven
// generically for `state`.
TEST(SnippetCardLayout, InsertingFromDiskNeverAppliesTheLayoutEvenIfTheSourceSnippetCarriedOne) {
    juce::AudioProcessorGraph sourceGraph;
    HostedNode hosted;
    hosted.addTo(sourceGraph);
    const std::vector<NodeID> selection{hosted.node->nodeID};

    // Simulates a hand-edited/forged `.agsnip` that somehow carries `state` (extractSnippet's own
    // includeExtraState=true stands in for that) -- prepareForInsert must still strip it when the
    // INSERT itself asks for includeExtraState=false, the real on-disk-drop shape.
    auto snippet = SnippetManager::extractSnippet(sourceGraph, selection, "S", /*includeExtraState=*/true);

    juce::AudioProcessorGraph targetGraph;
    const auto added = SnippetManager::insertSnippet(snippet, targetGraph, {0, 0}, /*includeExtraState=*/false);
    ASSERT_EQ(added.size(), 1u);

    auto* pastedModule = dynamic_cast<HostedPluginModule*>(targetGraph.getNodeForId(added[0])->getProcessor());
    ASSERT_NE(pastedModule, nullptr);
    EXPECT_TRUE(pastedModule->getCardLayoutOverride().isVoid())
        << "a dropped snippet file never carries state -- prepareForInsert strips it before the apply ever sees it";
}
