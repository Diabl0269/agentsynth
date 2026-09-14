// Concern: nextFreeIdBase/prepareForInsert (ids that cannot collide with the target graph,
// renumbering + offsetting without mutating the source) and insertSnippet (merging without
// disturbing existing nodes; parameter values survive verbatim -- the normalised-rescale
// corruption regression).
#include "Project/SnippetManager/SnippetManagerTestHelpers.h"

// ---------------------------------------------------------------------------------------
// nextFreeIdBase / prepareForInsert
// ---------------------------------------------------------------------------------------

TEST(SnippetIds, NextFreeIdBaseIsAboveEveryExistingId) {
    juce::AudioProcessorGraph graph;
    EXPECT_GE(SnippetManager::nextFreeIdBase(graph), 1u) << "NodeID 0 is the invalid sentinel";

    auto a = addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto b = addAt(graph, std::make_unique<FilterModule>(), 0, 0);
    const auto base = SnippetManager::nextFreeIdBase(graph);
    EXPECT_GT(base, a->nodeID.uid);
    EXPECT_GT(base, b->nodeID.uid);
}

TEST(SnippetPrepare, RenumbersIdsFromBaseAndRewritesConnections) {
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAt(source, std::make_unique<FilterModule>(), 300, 0);
    source.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID, filter->nodeID}, "S");

    auto prepared = SnippetManager::prepareForInsert(snippet, {0, 0}, 500);

    auto* nodes = arrayOf(prepared, "nodes");
    ASSERT_NE(nodes, nullptr);
    std::set<int> ids;
    for (const auto& n : *nodes) {
        const int id = (int)n.getDynamicObject()->getProperty("id");
        EXPECT_GE(id, 500);
        ids.insert(id);
    }
    EXPECT_EQ(ids.size(), 2u) << "each node gets its own fresh id";

    auto* connections = arrayOf(prepared, "connections");
    ASSERT_NE(connections, nullptr);
    ASSERT_EQ(connections->size(), 1);
    auto* conn = (*connections)[0].getDynamicObject();
    EXPECT_TRUE(ids.count((int)conn->getProperty("src")) > 0);
    EXPECT_TRUE(ids.count((int)conn->getProperty("dst")) > 0);
}

TEST(SnippetPrepare, OffsetsPositionsByDropPoint) {
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 40, 40);
    auto filter = addAt(source, std::make_unique<FilterModule>(), 340, 120);
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID, filter->nodeID}, "S");

    auto prepared = SnippetManager::prepareForInsert(snippet, {1000, 2000}, 1);

    auto* nodes = arrayOf(prepared, "nodes");
    ASSERT_NE(nodes, nullptr);
    std::vector<juce::Point<int>> positions;
    for (const auto& n : *nodes) {
        auto* pos = n.getDynamicObject()->getProperty("position").getDynamicObject();
        positions.push_back({(int)pos->getProperty("x"), (int)pos->getProperty("y")});
    }
    ASSERT_EQ(positions.size(), 2u);
    // Origin-relative (0,0) and (300,80) shifted by the drop point.
    EXPECT_TRUE(std::find(positions.begin(), positions.end(), juce::Point<int>(1000, 2000)) != positions.end());
    EXPECT_TRUE(std::find(positions.begin(), positions.end(), juce::Point<int>(1300, 2080)) != positions.end());
}

TEST(SnippetPrepare, NeverMutatesTheSourceSnippet) {
    // The library holds one loaded snippet and may insert it many times; prepareForInsert must be
    // a pure transform or the second drop would land pre-offset with stale ids.
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 10, 20);
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID}, "S");
    const auto before = juce::JSON::toString(snippet);

    SnippetManager::prepareForInsert(snippet, {900, 900}, 42);
    SnippetManager::prepareForInsert(snippet, {900, 900}, 42);

    EXPECT_EQ(juce::JSON::toString(snippet), before);
}

TEST(SnippetPrepare, ClampsIdBaseAwayFromTheInvalidSentinel) {
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 0, 0);
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID}, "S");

    auto prepared = SnippetManager::prepareForInsert(snippet, {0, 0}, 0);
    auto* nodes = arrayOf(prepared, "nodes");
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->size(), 1);
    EXPECT_GE((int)(*nodes)[0].getDynamicObject()->getProperty("id"), 1);
}

// ---------------------------------------------------------------------------------------
// insertSnippet
// ---------------------------------------------------------------------------------------

TEST(SnippetInsert, AddsModulesAndWiringWithoutTouchingExistingNodes) {
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAt(source, std::make_unique<FilterModule>(), 300, 0);
    source.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID, filter->nodeID}, "OscFilter");

    // Target already holds an Oscillator, so a naive merge would "update" it instead of adding one.
    juce::AudioProcessorGraph target;
    auto existing = addAt(target, std::make_unique<OscillatorModule>(), 50, 50);
    const auto existingId = existing->nodeID;

    auto added = SnippetManager::insertSnippet(snippet, target, {800, 400});

    EXPECT_EQ(added.size(), 2u);
    EXPECT_EQ(countOfType(target, "Oscillator"), 2) << "the snippet's oscillator must be a NEW node";
    EXPECT_EQ(countOfType(target, "Filter"), 1);
    EXPECT_NE(target.getNodeForId(existingId), nullptr) << "the pre-existing node must survive";

    // The pre-existing oscillator kept its own position — it was not overwritten by the snippet.
    auto* survivor = target.getNodeForId(existingId);
    EXPECT_EQ((int)survivor->properties["x"], 50);
    EXPECT_EQ((int)survivor->properties["y"], 50);

    // And the snippet's internal wire came across.
    bool foundWire = false;
    for (const auto& conn : target.getConnections())
        if (conn.source.nodeID != existingId && conn.destination.nodeID != existingId)
            foundWire = true;
    EXPECT_TRUE(foundWire);
}

TEST(SnippetInsert, PlacesTheGroupAtTheDropPointPreservingRelativeLayout) {
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addAt(source, std::make_unique<FilterModule>(), 400, 180);
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID, filter->nodeID}, "S");

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(snippet, target, {1000, 500});
    ASSERT_EQ(added.size(), 2u);

    std::map<juce::String, juce::Point<int>> byType;
    for (auto id : added) {
        auto* node = target.getNodeForId(id);
        ASSERT_NE(node, nullptr);
        byType[node->getProcessor()->getName()] = {(int)node->properties["x"], (int)node->properties["y"]};
    }
    EXPECT_EQ(byType["Oscillator"], juce::Point<int>(1000, 500));
    EXPECT_EQ(byType["Filter"], juce::Point<int>(1300, 580));
}

TEST(SnippetInsert, PreservesParameterValuesThatLookNormalised) {
    // REGRESSION (issue #156): the untrusted apply path treats a value inside [0,1] on a wider
    // range as a normalised value from an AI model and rescales it. LFO rateHz spans 0.01..20, so a
    // deliberate 0.5 Hz would come back as roughly 5 Hz if snippets went down that path. Snippets
    // are validated strictly but applied on the trusted path precisely to avoid this.
    juce::AudioProcessorGraph source;
    auto lfo = addAt(source, std::make_unique<LFOModule>(), 0, 0);
    setDenormalised(lfo->getProcessor(), "rateHz", 0.5f);
    ASSERT_NEAR(getDenormalised(lfo->getProcessor(), "rateHz"), 0.5f, 0.02f);

    auto snippet = SnippetManager::extractSnippet(source, {lfo->nodeID}, "SlowLFO");

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(snippet, target, {0, 0});
    ASSERT_EQ(added.size(), 1u);

    auto* inserted = target.getNodeForId(added[0]);
    ASSERT_NE(inserted, nullptr);
    EXPECT_NEAR(getDenormalised(inserted->getProcessor(), "rateHz"), 0.5f, 0.02f)
        << "a legitimate sub-1 parameter value must not be rescaled on insert";
}

TEST(SnippetInsert, RebuildsModulationChains) {
    juce::AudioProcessorGraph source;
    auto lfo = addAt(source, std::make_unique<LFOModule>(), 0, 0);
    auto filter = addAt(source, std::make_unique<FilterModule>(), 300, 0);
    auto atten = addAt(source, std::make_unique<AttenuverterModule>(), 150, 0);
    source.addConnection({{lfo->nodeID, 0}, {atten->nodeID, 0}});
    source.addConnection({{atten->nodeID, 0}, {filter->nodeID, 1}});
    auto snippet = SnippetManager::extractSnippet(source, {lfo->nodeID, filter->nodeID}, "Wobble");

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(snippet, target, {0, 0});

    EXPECT_EQ(added.size(), 2u) << "the rebuilt attenuverter is not reported as a selectable module";
    EXPECT_EQ(countOfType(target, "Attenuverter"), 1) << "the modulation chain must be recreated on insert";
}

TEST(SnippetInsert, InsertingTwiceProducesTwoIndependentCopies) {
    juce::AudioProcessorGraph source;
    auto osc = addAt(source, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAt(source, std::make_unique<FilterModule>(), 300, 0);
    source.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    auto snippet = SnippetManager::extractSnippet(source, {osc->nodeID, filter->nodeID}, "S");

    juce::AudioProcessorGraph target;
    auto first = SnippetManager::insertSnippet(snippet, target, {0, 0});
    auto second = SnippetManager::insertSnippet(snippet, target, {900, 0});

    ASSERT_EQ(first.size(), 2u);
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(countOfType(target, "Oscillator"), 2);
    EXPECT_EQ(countOfType(target, "Filter"), 2);
    EXPECT_EQ(target.getConnections().size(), 2) << "each copy keeps its own internal wire";

    for (auto a : first)
        for (auto b : second)
            EXPECT_NE(a, b);
}

TEST(SnippetInsert, DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch) {
    // REGRESSION: applyJSONToGraph's merge mode auto-connects new audio nodes with no outgoing wire
    // to Audio Output (a convenience for AI-authored patches). Snippet insertion must opt out —
    // otherwise dropping a snippet into the patch it came from wires the copy's leaf modules
    // straight into the live output, which is audible and completely unexpected.
    juce::AudioProcessorGraph graph;
    // Configure play settings BEFORE adding the IO node — the Audio Output node's input channel
    // count derives from the graph's output channel count, and connections into an unconfigured
    // output node fail silently.
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto out = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    auto osc = addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAt(graph, std::make_unique<FilterModule>(), 300, 0);
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filter->nodeID, 0}, {out->nodeID, 0}}));

    // Select the Osc -> Filter chain; its wire to Audio Output is outside the selection.
    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Chain");
    ASSERT_EQ(arrayOf(snippet, "connections")->size(), 1) << "only the internal wire is captured";

    std::set<juce::uint32> before;
    for (auto* node : graph.getNodes())
        before.insert(node->nodeID.uid);

    auto added = SnippetManager::insertSnippet(snippet, graph, {900, 400});
    ASSERT_EQ(added.size(), 2u);

    std::set<juce::AudioProcessorGraph::NodeID> newNodes(added.begin(), added.end());

    // No wire may cross between the inserted group and anything that was already there.
    for (const auto& conn : graph.getConnections()) {
        const bool srcIsNew = newNodes.count(conn.source.nodeID) > 0;
        const bool dstIsNew = newNodes.count(conn.destination.nodeID) > 0;
        EXPECT_EQ(srcIsNew, dstIsNew) << "connection " << conn.source.nodeID.uid << " -> "
                                      << conn.destination.nodeID.uid << " crosses the snippet boundary";
    }

    // Specifically: nothing new is attached to the existing Audio Output.
    for (const auto& conn : graph.getConnections())
        EXPECT_FALSE(conn.destination.nodeID == out->nodeID && newNodes.count(conn.source.nodeID) > 0)
            << "an inserted module was auto-connected to the patch's Audio Output";

    // The original chain is untouched.
    int originalToOutput = 0;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == filter->nodeID && conn.destination.nodeID == out->nodeID)
            ++originalToOutput;
    EXPECT_EQ(originalToOutput, 1);
}

TEST(SnippetInsert, DoesNotAttachInsertedModulesToAnExistingMidiSource) {
    // Same opt-out, MIDI half: merge mode also wires new MIDI-accepting nodes to an existing MIDI
    // source. An inserted Oscillator must not start responding to the patch's keyboard.
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto midiIn = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    auto existingOsc = addAt(graph, std::make_unique<OscillatorModule>(), 0, 0);
    ASSERT_TRUE(graph.addConnection({{midiIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {existingOsc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    auto source = addAt(graph, std::make_unique<OscillatorModule>(), 600, 0);
    auto snippet = SnippetManager::extractSnippet(graph, {source->nodeID}, "LoneOsc");

    auto added = SnippetManager::insertSnippet(snippet, graph, {900, 400});
    ASSERT_EQ(added.size(), 1u);

    for (const auto& conn : graph.getConnections())
        EXPECT_FALSE(conn.destination.nodeID == added[0])
            << "the inserted oscillator was auto-wired to the patch's MIDI source";
}

TEST(SnippetInsert, RejectsAnEmptySnippet) {
    juce::AudioProcessorGraph source;
    auto snippet = SnippetManager::extractSnippet(source, {}, "Empty");

    juce::AudioProcessorGraph target;
    EXPECT_TRUE(SnippetManager::insertSnippet(snippet, target, {0, 0}).empty());
    EXPECT_EQ(target.getNumNodes(), 0);
}

TEST(SnippetInsert, RejectsMalformedSnippetJSONWithoutPartiallyApplying) {
    // A hand-edited/corrupt snippet file must be refused whole, not applied halfway.
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> nodes;
    juce::DynamicObject::Ptr good = new juce::DynamicObject();
    good->setProperty("id", 1);
    good->setProperty("type", "Oscillator");
    nodes.add(juce::var(good.get()));
    juce::DynamicObject::Ptr bad = new juce::DynamicObject();
    bad->setProperty("id", 2);
    bad->setProperty("type", "NotARealModuleType");
    nodes.add(juce::var(bad.get()));
    root->setProperty("nodes", nodes);

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(juce::var(root.get()), target, {0, 0});

    EXPECT_TRUE(added.empty());
    EXPECT_EQ(target.getNumNodes(), 0) << "an invalid snippet must not add the valid nodes either";
}

TEST(SnippetInsert, IgnoresConnectionsReferencingAbsentNodes) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> nodes;
    juce::DynamicObject::Ptr n = new juce::DynamicObject();
    n->setProperty("id", 1);
    n->setProperty("type", "Oscillator");
    nodes.add(juce::var(n.get()));
    root->setProperty("nodes", nodes);

    juce::Array<juce::var> connections;
    juce::DynamicObject::Ptr c = new juce::DynamicObject();
    c->setProperty("src", 1);
    c->setProperty("srcPort", 0);
    c->setProperty("dst", 77); // not in this snippet
    c->setProperty("dstPort", 0);
    connections.add(juce::var(c.get()));
    root->setProperty("connections", connections);

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(juce::var(root.get()), target, {0, 0});

    EXPECT_EQ(added.size(), 1u);
    EXPECT_EQ(target.getConnections().size(), 0) << "the dangling wire is dropped during preparation";
}
