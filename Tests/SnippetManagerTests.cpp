// SnippetManagerTests.cpp
// Headless unit tests for the snippet (module grouping) feature — issue #156.
//
//   • extractSnippet     — selection filtering, self-contained wiring, origin-relative positions
//   • nextFreeIdBase     — ids that cannot collide with the target graph
//   • prepareForInsert   — renumbering + offsetting, and that it never mutates its input
//   • insertSnippet      — merges without disturbing existing nodes; parameter values survive
//                          verbatim (the normalised-rescale corruption regression)
//   • persistence        — save / load / list / delete, name sanitisation, drag payload encoding

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/SnippetManager.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::SnippetManager;
using NodeID = juce::AudioProcessorGraph::NodeID;

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

static juce::AudioProcessorGraph::Node::Ptr addAt(juce::AudioProcessorGraph& graph,
                                                  std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto node = graph.addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    return node;
}

static const juce::Array<juce::var>* arrayOf(const juce::var& v, const char* key) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty(key))
        return nullptr;
    return obj->getProperty(key).getArray();
}

static int countOfType(juce::AudioProcessorGraph& graph, const juce::String& name) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            ++count;
    return count;
}

static juce::RangedAudioParameter* findParam(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == paramId)
                return ranged;
    return nullptr;
}

static void setDenormalised(juce::AudioProcessor* processor, const juce::String& paramId, float value) {
    auto* param = findParam(processor, paramId);
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(value));
}

static float getDenormalised(juce::AudioProcessor* processor, const juce::String& paramId) {
    auto* param = findParam(processor, paramId);
    if (param == nullptr)
        return std::numeric_limits<float>::quiet_NaN();
    return param->getNormalisableRange().convertFrom0to1(param->getValue());
}

// ---------------------------------------------------------------------------------------
// extractSnippet
// ---------------------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------------------
// Name sanitisation + drag payloads
// ---------------------------------------------------------------------------------------

TEST(SnippetName, TrimsAndKeepsOrdinaryNames) {
    EXPECT_EQ(SnippetManager::sanitiseName("  My Supersaw Lead  "), "My Supersaw Lead");
}

TEST(SnippetName, StripsPathSeparatorsSoANameCannotEscapeTheDirectory) {
    auto cleaned = SnippetManager::sanitiseName("../../etc/passwd");
    EXPECT_FALSE(cleaned.contains("/"));
    EXPECT_FALSE(cleaned.contains("\\"));
    EXPECT_FALSE(cleaned.startsWithChar('.'));
}

TEST(SnippetName, RejectsNamesThatSanitiseToNothing) {
    EXPECT_TRUE(SnippetManager::sanitiseName("").isEmpty());
    EXPECT_TRUE(SnippetManager::sanitiseName("   ").isEmpty());
    EXPECT_TRUE(SnippetManager::sanitiseName("...").isEmpty());
}

TEST(SnippetName, CapsLength) {
    auto cleaned = SnippetManager::sanitiseName(juce::String::repeatedString("a", 500));
    EXPECT_LE(cleaned.length(), SnippetManager::kMaxNameLength);
    EXPECT_GT(cleaned.length(), 0);
}

TEST(SnippetPayload, RoundTripsThroughTheDragChannel) {
    const juce::String name = "My Supersaw Lead";
    auto payload = SnippetManager::payloadForName(name);

    EXPECT_TRUE(SnippetManager::isSnippetPayload(payload));
    EXPECT_EQ(SnippetManager::nameFromPayload(payload), name);
}

TEST(SnippetPayload, PlainModuleNamesAreNotSnippetPayloads) {
    EXPECT_FALSE(SnippetManager::isSnippetPayload("Oscillator"));
    EXPECT_TRUE(SnippetManager::nameFromPayload("Oscillator").isEmpty());
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

class SnippetPersistence : public ::testing::Test {
protected:
    void SetUp() override {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-snippet-tests");
        dir.deleteRecursively();
        dir.createDirectory();

        juce::AudioProcessorGraph graph;
        auto osc = graph.addNode(std::make_unique<OscillatorModule>());
        osc->properties.set("x", 0);
        osc->properties.set("y", 0);
        auto filter = graph.addNode(std::make_unique<FilterModule>());
        filter->properties.set("x", 300);
        filter->properties.set("y", 0);
        graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        sample = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Sample");
    }

    void TearDown() override { dir.deleteRecursively(); }

    juce::File dir;
    juce::var sample;
};

TEST_F(SnippetPersistence, SaveThenLoadRoundTrips) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Sample", sample));

    auto file = SnippetManager::fileForName(dir, "Sample");
    ASSERT_TRUE(file.existsAsFile());
    EXPECT_TRUE(file.getFileName().endsWith(SnippetManager::kFileExtension));

    auto loaded = SnippetManager::loadSnippet(file);
    ASSERT_TRUE(loaded.isObject());
    EXPECT_EQ(SnippetManager::getSnippetName(loaded), "Sample");
    EXPECT_EQ(SnippetManager::getModuleCount(loaded), 2);
}

TEST_F(SnippetPersistence, SavedSnippetInsertsFromDisk) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Sample", sample));
    auto loaded = SnippetManager::loadSnippet(SnippetManager::fileForName(dir, "Sample"));

    juce::AudioProcessorGraph target;
    auto added = SnippetManager::insertSnippet(loaded, target, {100, 100});

    EXPECT_EQ(added.size(), 2u);
    EXPECT_EQ(target.getConnections().size(), 1);
}

TEST_F(SnippetPersistence, SaveRewritesTheNameToTheSanitisedForm) {
    // The sidebar label and the filename must never disagree.
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "  Padded Name  ", sample));

    auto loaded = SnippetManager::loadSnippet(SnippetManager::fileForName(dir, "Padded Name"));
    ASSERT_TRUE(loaded.isObject());
    EXPECT_EQ(SnippetManager::getSnippetName(loaded), "Padded Name");
}

TEST_F(SnippetPersistence, RefusesToSaveAnEmptySnippetOrAnUnusableName) {
    juce::AudioProcessorGraph empty;
    auto emptySnippet = SnippetManager::extractSnippet(empty, {}, "Nothing");

    EXPECT_FALSE(SnippetManager::saveSnippet(dir, "Nothing", emptySnippet));
    EXPECT_FALSE(SnippetManager::saveSnippet(dir, "   ", sample));
    EXPECT_TRUE(SnippetManager::listSnippets(dir).isEmpty());
}

TEST_F(SnippetPersistence, ListSnippetsReportsNameAndModuleCountSortedByName) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Zeta", sample));
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "alpha", sample));

    auto list = SnippetManager::listSnippets(dir);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list[0].name, "alpha") << "sorted case-insensitively";
    EXPECT_EQ(list[1].name, "Zeta");
    EXPECT_EQ(list[0].moduleCount, 2);
}

TEST_F(SnippetPersistence, ListSnippetsSkipsUnreadableFiles) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Good", sample));
    dir.getChildFile(juce::String("Broken") + SnippetManager::kFileExtension).replaceWithText("{ not json");

    auto list = SnippetManager::listSnippets(dir);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].name, "Good");
}

TEST_F(SnippetPersistence, ListSnippetsOnMissingDirectoryIsEmpty) {
    auto missing = dir.getChildFile("does-not-exist");
    EXPECT_TRUE(SnippetManager::listSnippets(missing).isEmpty());
}

TEST_F(SnippetPersistence, DeleteRemovesTheFile) {
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Sample", sample));
    ASSERT_EQ(SnippetManager::listSnippets(dir).size(), 1);

    EXPECT_TRUE(SnippetManager::deleteSnippet(dir, "Sample"));
    EXPECT_TRUE(SnippetManager::listSnippets(dir).isEmpty());
    EXPECT_FALSE(SnippetManager::deleteSnippet(dir, "Sample")) << "deleting twice reports failure";
}

TEST_F(SnippetPersistence, LoadingAMissingFileYieldsAVoidVar) {
    EXPECT_FALSE(SnippetManager::loadSnippet(dir.getChildFile("nope.agsnip")).isObject());
}
