// Concern: extractSnippet -- selection filtering, self-contained wiring, origin-relative
// positions, and extra-state capture on request.
#include "SnippetManagerTestHelpers.h"

TEST(SnippetExtract, CapturesOnlySelectedModules) {
    juce::AudioProcessorGraph graph;
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addAt(graph, std::make_unique<FilterModule>(), 400, 100);
    auto vca = addAt(graph, std::make_unique<VCAModule>(), 700, 100);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Two");

    EXPECT_EQ(SnippetManager::getModuleCount(snippet), 2);
    EXPECT_EQ(SnippetManager::getSnippetName(snippet), "Two");

    auto* nodes = arrayOf(snippet, "nodes");
    ASSERT_NE(nodes, nullptr);
    juce::StringArray types;
    for (const auto& n : *nodes)
        types.add(n.getDynamicObject()->getProperty("type").toString());
    EXPECT_TRUE(types.contains("Oscillator"));
    EXPECT_TRUE(types.contains("Filter"));
    EXPECT_FALSE(types.contains("VCA")) << "unselected modules must not be captured";
    juce::ignoreUnused(vca);
}

TEST(SnippetExtract, ExcludesGraphIONodesEvenWhenSelected) {
    // Audio In/Out and MIDI In are singletons that already exist in any target patch; copying them
    // into a snippet would duplicate the output bus on insert.
    juce::AudioProcessorGraph graph;
    auto out = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, out->nodeID}, "WithOutput");

    EXPECT_EQ(SnippetManager::getModuleCount(snippet), 1);
    auto* nodes = arrayOf(snippet, "nodes");
    ASSERT_NE(nodes, nullptr);
    EXPECT_EQ((*nodes)[0].getDynamicObject()->getProperty("type").toString(), "Oscillator");
}

TEST(SnippetExtract, KeepsOnlyConnectionsWithBothEndpointsSelected) {
    juce::AudioProcessorGraph graph;
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAt(graph, std::make_unique<FilterModule>(), 300, 0);
    auto vca = addAt(graph, std::make_unique<VCAModule>(), 600, 0);
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    graph.addConnection({{filter->nodeID, 0}, {vca->nodeID, 0}});

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Pair");

    auto* connections = arrayOf(snippet, "connections");
    ASSERT_NE(connections, nullptr);
    EXPECT_EQ(connections->size(), 1) << "the filter->vca wire leaves the selection and must be dropped";
    auto* conn = (*connections)[0].getDynamicObject();
    EXPECT_EQ((int)conn->getProperty("src"), (int)osc->nodeID.uid);
    EXPECT_EQ((int)conn->getProperty("dst"), (int)filter->nodeID.uid);
}

TEST(SnippetExtract, NormalisesPositionsToSelectionOrigin) {
    juce::AudioProcessorGraph graph;
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 500, 300);
    auto filter = addAt(graph, std::make_unique<FilterModule>(), 800, 380);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Origin");

    auto* nodes = arrayOf(snippet, "nodes");
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->size(), 2);

    // The top-left corner of the selection becomes (0,0); relative offsets are preserved.
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    std::map<juce::String, juce::Point<int>> byType;
    for (const auto& n : *nodes) {
        auto* obj = n.getDynamicObject();
        auto* pos = obj->getProperty("position").getDynamicObject();
        juce::Point<int> p{(int)pos->getProperty("x"), (int)pos->getProperty("y")};
        minX = juce::jmin(minX, p.x);
        minY = juce::jmin(minY, p.y);
        byType[obj->getProperty("type").toString()] = p;
    }
    EXPECT_EQ(minX, 0);
    EXPECT_EQ(minY, 0);
    EXPECT_EQ(byType["Oscillator"], juce::Point<int>(0, 0));
    EXPECT_EQ(byType["Filter"], juce::Point<int>(300, 80)) << "relative layout must be preserved";
}

TEST(SnippetExtract, EmptySelectionYieldsNoNodes) {
    juce::AudioProcessorGraph graph;
    addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);

    auto snippet = SnippetManager::extractSnippet(graph, {}, "Nothing");
    EXPECT_EQ(SnippetManager::getModuleCount(snippet), 0);
}

TEST(SnippetExtract, IgnoresIdsThatAreNotInTheGraph) {
    juce::AudioProcessorGraph graph;
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, NodeID(9999)}, "Stale");
    EXPECT_EQ(SnippetManager::getModuleCount(snippet), 1);
}

TEST(SnippetExtract, StoresModulationAsIntentAndNeverStoresAttenuverters) {
    // An LFO -> Filter cutoff routing runs through an Attenuverter node in the live graph. The
    // snippet must carry it as a `modulations` entry so the chain is rebuilt on insert.
    juce::AudioProcessorGraph graph;
    auto lfo = addAt(graph, std::make_unique<LFOModule>(), 0, 0);
    auto filter = addAt(graph, std::make_unique<FilterModule>(), 300, 0);
    auto atten = addAt(graph, std::make_unique<AttenuverterModule>(), 150, 0);
    graph.addConnection({{lfo->nodeID, 0}, {atten->nodeID, 0}});
    graph.addConnection({{atten->nodeID, 0}, {filter->nodeID, 1}}); // port 1 == Cutoff

    auto snippet = SnippetManager::extractSnippet(graph, {lfo->nodeID, filter->nodeID, atten->nodeID}, "Wobble");

    EXPECT_EQ(SnippetManager::getModuleCount(snippet), 2) << "the attenuverter must not become a snippet node";

    auto* nodes = arrayOf(snippet, "nodes");
    ASSERT_NE(nodes, nullptr);
    for (const auto& n : *nodes)
        EXPECT_NE(n.getDynamicObject()->getProperty("type").toString(), "Attenuverter");

    auto* modulations = arrayOf(snippet, "modulations");
    ASSERT_NE(modulations, nullptr);
    ASSERT_EQ(modulations->size(), 1);
    auto* mod = (*modulations)[0].getDynamicObject();
    EXPECT_EQ((int)mod->getProperty("source"), (int)lfo->nodeID.uid);
    EXPECT_EQ((int)mod->getProperty("dest"), (int)filter->nodeID.uid);
    EXPECT_EQ((int)mod->getProperty("destPort"), 1);
}

TEST(SnippetExtract, DropsModulationLeavingTheSelection) {
    juce::AudioProcessorGraph graph;
    auto lfo = addAt(graph, std::make_unique<LFOModule>(), 0, 0);
    auto filter = addAt(graph, std::make_unique<FilterModule>(), 300, 0);
    auto atten = addAt(graph, std::make_unique<AttenuverterModule>(), 150, 0);
    graph.addConnection({{lfo->nodeID, 0}, {atten->nodeID, 0}});
    graph.addConnection({{atten->nodeID, 0}, {filter->nodeID, 1}});

    // Only the destination is selected — the modulation source is outside the group.
    auto snippet = SnippetManager::extractSnippet(graph, {filter->nodeID}, "FilterOnly");

    auto* modulations = arrayOf(snippet, "modulations");
    ASSERT_NE(modulations, nullptr);
    EXPECT_EQ(modulations->size(), 0);
}

// ---------------------------------------------------------------------------------------
// selectionOrigin — the corner extraction normalises against, shared with the clipboard
// ---------------------------------------------------------------------------------------

TEST(SnippetOrigin, IsTheTopLeftCornerOfTheSelection) {
    juce::AudioProcessorGraph graph;
    auto a = addAt(graph, std::make_unique<OscillatorModule>(), 400, 90);
    auto b = addAt(graph, std::make_unique<FilterModule>(), 120, 300);
    addAt(graph, std::make_unique<VCAModule>(), 10, 10); // unselected — must not pull the corner

    EXPECT_EQ(SnippetManager::selectionOrigin(graph, {a->nodeID, b->nodeID}), juce::Point<int>(120, 90));
}

TEST(SnippetOrigin, IgnoresIneligibleAndStaleIdsAndDefaultsToTheCanvasOrigin) {
    juce::AudioProcessorGraph graph;
    auto atten = addAt(graph, std::make_unique<AttenuverterModule>(), 5, 5);
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 200, 160);

    // An attenuverter is never a snippet node, so it cannot define the group's corner.
    EXPECT_EQ(SnippetManager::selectionOrigin(graph, {atten->nodeID, osc->nodeID}), juce::Point<int>(200, 160));
    EXPECT_EQ(SnippetManager::selectionOrigin(graph, {atten->nodeID}), juce::Point<int>(0, 0));
    EXPECT_EQ(SnippetManager::selectionOrigin(graph, {}), juce::Point<int>(0, 0));
    EXPECT_EQ(SnippetManager::selectionOrigin(graph, {NodeID(9999)}), juce::Point<int>(0, 0));
}

TEST(SnippetOrigin, AgreesWithTheOriginExtractionNormalisesAgainst) {
    // The contract the clipboard leans on: subtracting selectionOrigin from a node's live position
    // gives exactly the origin-relative position stored in the snippet. If these two ever drift, a
    // paste lands somewhere other than where the caller aimed it.
    juce::AudioProcessorGraph graph;
    auto a = addAt(graph, std::make_unique<OscillatorModule>(), 340, 220);
    auto b = addAt(graph, std::make_unique<FilterModule>(), 700, 180);

    const auto origin = SnippetManager::selectionOrigin(graph, {a->nodeID, b->nodeID});
    auto snippet = SnippetManager::extractSnippet(graph, {a->nodeID, b->nodeID}, "S");

    auto* nodes = arrayOf(snippet, "nodes");
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->size(), 2);
    for (const auto& n : *nodes) {
        auto* nObj = n.getDynamicObject();
        auto* pos = nObj->getProperty("position").getDynamicObject();
        const juce::Point<int> stored{(int)pos->getProperty("x"), (int)pos->getProperty("y")};
        auto* node = graph.getNodeForId(NodeID((juce::uint32)(int)nObj->getProperty("id")));
        ASSERT_NE(node, nullptr);
        const juce::Point<int> live{(int)node->properties["x"], (int)node->properties["y"]};
        EXPECT_EQ(stored, live - origin);
    }
}

// ---------------------------------------------------------------------------------------
// Extra module state — off for anything that reaches disk, on for the in-app clipboard
// ---------------------------------------------------------------------------------------

TEST(SnippetExtraState, IsOmittedByDefaultAndCarriedWhenRequested) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("snippet-extrastate.wav");
    ASSERT_TRUE(writeSilentWav(file, 256)) << "could not stage a sample file";

    juce::AudioProcessorGraph graph;
    auto samplerNode = addAt(graph, std::make_unique<SamplerModule>(), 0, 0);
    auto* sampler = dynamic_cast<SamplerModule*>(samplerNode->getProcessor());
    ASSERT_NE(sampler, nullptr);
    ASSERT_TRUE(sampler->loadSampleFile(file));
    ASSERT_FALSE(sampler->getExtraState().isVoid()) << "the module under test must actually carry extra state";

    const std::vector<NodeID> selection{samplerNode->nodeID};

    auto onDisk = SnippetManager::extractSnippet(graph, selection, "S");
    auto* onDiskNodes = arrayOf(onDisk, "nodes");
    ASSERT_NE(onDiskNodes, nullptr);
    for (const auto& n : *onDiskNodes)
        EXPECT_FALSE(n.getDynamicObject()->hasProperty("state"))
            << "a .agsnip is hand-editable and applies on the trusted path — it must not carry `state`";

    auto inMemory = SnippetManager::extractSnippet(graph, selection, "S", /*includeExtraState=*/true);
    auto* inMemoryNodes = arrayOf(inMemory, "nodes");
    ASSERT_NE(inMemoryNodes, nullptr);
    bool sawState = false;
    for (const auto& n : *inMemoryNodes)
        sawState = sawState || n.getDynamicObject()->hasProperty("state");
    EXPECT_TRUE(sawState) << "the clipboard opts in, so a duplicated module keeps its non-parameter state";

    file.deleteFile();
}

TEST(SnippetExtraState, PrepareForInsertStripsAStateKeyUnlessItWasAskedFor) {
    // The defence in depth that matters: even if a snippet FILE were hand-edited to carry `state`,
    // the default insert path drops it before applyJSONToGraph (which honours it when trusted).
    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "Sampler");
    node->setProperty("state", "/etc/passwd");
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node.get()));
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", nodes);

    auto stripped = SnippetManager::prepareForInsert(juce::var(root.get()), {0, 0}, 1);
    auto* strippedNodes = arrayOf(stripped, "nodes");
    ASSERT_NE(strippedNodes, nullptr);
    ASSERT_EQ(strippedNodes->size(), 1);
    EXPECT_FALSE((*strippedNodes)[0].getDynamicObject()->hasProperty("state"));

    auto kept = SnippetManager::prepareForInsert(juce::var(root.get()), {0, 0}, 1, /*includeExtraState=*/true);
    auto* keptNodes = arrayOf(kept, "nodes");
    ASSERT_NE(keptNodes, nullptr);
    ASSERT_EQ(keptNodes->size(), 1);
    EXPECT_TRUE((*keptNodes)[0].getDynamicObject()->hasProperty("state"));
}
