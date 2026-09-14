// Concern: node-preserving (diffing) restore -- see AIStateMapper::applySnapshotPreservingNodes.
// A structural undo used to destroy and re-create EVERY node, losing all module runtime state
// and blocking the audio callback while it did so; the restore now diffs the snapshot against
// the live graph, so these tests are about IDENTITY, not just the graph ending up the right shape.
#include "Project/UndoRedo/UndoRedoTestFixture.h"

namespace {

/** Property no snapshot carries, so it survives a kept node and vanishes with a re-created one. */
const juce::Identifier kSurvivorTag("undoRedoTestSurvivorTag");

void tagLiveNodes(juce::AudioProcessorGraph& g) {
    for (auto* node : g.getNodes())
        node->properties.set(kSurvivorTag, 1);
}

int countTaggedNodes(juce::AudioProcessorGraph& g) {
    int n = 0;
    for (auto* node : g.getNodes())
        if (node->properties.contains(kSurvivorTag))
            ++n;
    return n;
}

/**
 * Address-reuse-proof fingerprint of who is in the graph. Pointer sets alone can lie — a freed
 * node can be replaced at the same address — so the tag set is compared alongside them.
 */
struct GraphIdentity {
    std::set<const void*> nodes;
    std::set<const void*> processors;
    std::set<juce::uint32> ids;
    std::set<juce::String> uuids;
    std::set<juce::AudioProcessorGraph::Connection> connections;

    bool operator==(const GraphIdentity& o) const {
        return nodes == o.nodes && processors == o.processors && ids == o.ids && uuids == o.uuids &&
               connections == o.connections;
    }
};

GraphIdentity captureIdentity(juce::AudioProcessorGraph& g) {
    GraphIdentity id;
    for (auto* node : g.getNodes()) {
        id.nodes.insert(node);
        id.processors.insert(node->getProcessor());
        id.ids.insert(node->nodeID.uid);
        id.uuids.insert(node->properties["uuid"].toString());
    }
    for (const auto& c : g.getConnections())
        id.connections.insert(c);
    return id;
}

juce::String uuidOf(juce::AudioProcessorGraph::Node* node) { return node->properties["uuid"].toString(); }

/**
 * Stamps a uuid onto every live node. graphToJSON generates them lazily and writes them back, so
 * in the app the snapshot AppUndoManager takes before a mutation is what does this; a test that
 * wants to fingerprint identity before the first snapshot has to ask for it explicitly.
 */
void stampUuids(juce::AudioProcessorGraph& g) { synth::AIStateMapper::graphToJSON(g); }

/** Counts how many times the graph announced a topology change. */
class TopologyChangeCounter : public juce::ChangeListener {
public:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    int count = 0;
};

void pumpMessageLoop(int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

/** ModuleBase stand-in that records how often its non-parameter state is written. */
class ExtraStateProbeModule : public ModuleBase {
public:
    ExtraStateProbeModule()
        : ModuleBase("Oscillator", 1, 1) {}

    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }

    juce::var getExtraState() const override {
        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        state->setProperty("token", token);
        return juce::var(state.get());
    }

    void setExtraState(const juce::var& state) override {
        ++setExtraStateCalls;
        if (auto* obj = state.getDynamicObject())
            token = obj->getProperty("token").toString();
    }

    juce::String token{"a"};
    int setExtraStateCalls = 0;
};

} // namespace

/**
 * A parameter-only undo must keep every module instance alive. This is the whole point of the
 * diffing restore: a re-created Sequencer forgets its step, a re-created ADSR forgets its stage,
 * and a re-created hosted plugin would have to be re-instantiated from its binary.
 */
TEST_F(UndoRedoTest, ParamOnlyUndoPreservesNodeInstances) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    auto* filter = graph.addNode(std::make_unique<FilterModule>()).get();
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});

    auto* fine = findParam(osc->getProcessor(), "fine");
    ASSERT_NE(fine, nullptr);
    const float originalNormalised = fine->getValue();

    undoManager.captureBeforeState(graph); // Assigns uuids to every live node.
    tagLiveNodes(graph);
    const auto before = captureIdentity(graph);

    fine->setValueNotifyingHost(0.75f);
    undoManager.pushSnapshotFromCapture(graph);
    ASSERT_TRUE(undoManager.canUndo());

    ASSERT_TRUE(undoManager.undo());
    EXPECT_NEAR(fine->getValue(), originalNormalised, 0.001f);
    EXPECT_EQ(countTaggedNodes(graph), 2) << "a parameter undo must not destroy and re-create any node";
    EXPECT_TRUE(captureIdentity(graph) == before) << "node instances, ids, uuids and wiring must all survive";

    ASSERT_TRUE(undoManager.redo());
    EXPECT_NEAR(fine->getValue(), 0.75f, 0.001f);
    EXPECT_EQ(countTaggedNodes(graph), 2) << "a parameter redo must not destroy and re-create any node either";
    EXPECT_TRUE(captureIdentity(graph) == before);
}

/**
 * The acceptance criterion behind issue #197: a parameter-only undo performs ZERO topology
 * operations, so JUCE never rebuilds its render sequence and the audio callback never blocks.
 * The graph announces every topology change to its ChangeBroadcaster, so a silent restore is
 * observable as zero change messages.
 */
TEST_F(UndoRedoTest, ParamOnlyUndoPerformsNoTopologyOperations) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    auto* filter = graph.addNode(std::make_unique<FilterModule>()).get();
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    graph.addConnection({{osc->nodeID, 1}, {filter->nodeID, 1}});

    auto* fine = findParam(osc->getProcessor(), "fine");
    ASSERT_NE(fine, nullptr);

    undoManager.captureBeforeState(graph);
    fine->setValueNotifyingHost(0.75f);
    undoManager.pushSnapshotFromCapture(graph);

    const auto connectionsBefore = captureIdentity(graph).connections;

    // Drain anything the setup queued, then start listening.
    pumpMessageLoop();
    TopologyChangeCounter counter;
    graph.addChangeListener(&counter);

    ASSERT_TRUE(undoManager.undo());
    pumpMessageLoop();
    EXPECT_EQ(counter.count, 0) << "a parameter-only undo must not touch graph topology";

    ASSERT_TRUE(undoManager.redo());
    pumpMessageLoop();
    EXPECT_EQ(counter.count, 0) << "a parameter-only redo must not touch graph topology";

    EXPECT_EQ(graph.getNumNodes(), 2);
    EXPECT_TRUE(captureIdentity(graph).connections == connectionsBefore);

    graph.removeChangeListener(&counter);
}

/** Undoing an add removes exactly the added node and leaves every other instance untouched. */
TEST_F(UndoRedoTest, NodeAddUndoRemovesOnlyThatNode) {
    graph.addNode(std::make_unique<OscillatorModule>());
    graph.addNode(std::make_unique<FilterModule>());

    stampUuids(graph);
    tagLiveNodes(graph);
    const auto before = captureIdentity(graph);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 3);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), 2);
    EXPECT_EQ(countTaggedNodes(graph), 2) << "the two survivors must be the original instances";
    EXPECT_TRUE(captureIdentity(graph) == before);

    // Redo re-adds the VCA without disturbing the survivors.
    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(graph.getNumNodes(), 3);
    EXPECT_EQ(countTaggedNodes(graph), 2);
}

/** Undoing a delete re-creates only the deleted node, and restores its uuid from the snapshot. */
TEST_F(UndoRedoTest, NodeDeleteUndoRecreatesOnlyDeletedNode) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    auto* filter = graph.addNode(std::make_unique<FilterModule>()).get();
    auto* vca = graph.addNode(std::make_unique<VCAModule>()).get();
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    graph.addConnection({{filter->nodeID, 0}, {vca->nodeID, 0}});

    stampUuids(graph);
    tagLiveNodes(graph);

    const auto filterId = filter->nodeID;
    const juce::String filterUuid = uuidOf(filter);
    const juce::String oscUuid = uuidOf(osc);
    ASSERT_TRUE(filterUuid.isNotEmpty());

    undoManager.recordStructuralChange(graph, [this, filterId] { graph.removeNode(filterId); });
    ASSERT_EQ(graph.getNumNodes(), 2);

    ASSERT_TRUE(undoManager.undo());
    ASSERT_EQ(graph.getNumNodes(), 3);
    EXPECT_EQ(countTaggedNodes(graph), 2) << "only the deleted node should have been re-created";

    auto* restored = graph.getNodeForId(filterId);
    ASSERT_NE(restored, nullptr) << "the re-created node must reclaim its original node id";
    EXPECT_EQ(uuidOf(restored), filterUuid) << "identity must be restored from the snapshot, not regenerated";
    EXPECT_FALSE(restored->properties.contains(kSurvivorTag));
    EXPECT_EQ(restored->getProcessor()->getName(), juce::String("Filter"));

    // The survivors kept their identity, and the wiring around the restored node is back.
    EXPECT_EQ(uuidOf(graph.getNodeForId(osc->nodeID)), oscUuid);
    EXPECT_EQ(graph.getConnections().size(), 2u);
}

/** A connection edit applies as a delta: no node is added, removed or re-created. */
TEST_F(UndoRedoTest, ConnectionUndoPreservesAllNodeInstances) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    auto* filter = graph.addNode(std::make_unique<FilterModule>()).get();

    stampUuids(graph);
    tagLiveNodes(graph);
    const auto before = captureIdentity(graph);
    ASSERT_TRUE(before.connections.empty());

    undoManager.recordStructuralChange(
        graph, [this, osc, filter] { graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}}); });
    ASSERT_EQ(graph.getConnections().size(), 1u);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(countTaggedNodes(graph), 2);
    EXPECT_TRUE(captureIdentity(graph) == before) << "undoing a wire must only remove the wire";

    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(countTaggedNodes(graph), 2) << "redoing a wire must not re-create the nodes it joins";
    EXPECT_EQ(graph.getConnections().size(), 1u);
}

/**
 * An AI merge patch renumbers the ids it was given, so undoing one is the case most likely to
 * confuse an identity-based restore. The graph must come back exactly as it was.
 */
TEST_F(UndoRedoTest, AiMergePatchUndoRestoresGraphExactly) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    auto* filter = graph.addNode(std::make_unique<FilterModule>()).get();
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});

    auto* cutoff = findParam(filter->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);
    const float cutoffBefore = cutoff->getNormalisableRange().convertFrom0to1(cutoff->getValue());

    stampUuids(graph);
    tagLiveNodes(graph);
    const auto before = captureIdentity(graph);
    const juce::String jsonBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));

    // Untrusted merge: the ids in the patch are renumbered as the nodes are created.
    const juce::var patch = juce::JSON::parse(R"({"nodes":[{"id":77,"type":"Delay","params":{}}],
                                                  "connections":[]})");
    ASSERT_TRUE(patch.isObject());
    undoManager.recordStructuralChange(graph, [this, &patch] {
        synth::AIStateMapper::applyJSONToGraph(patch, graph, /*clearExisting=*/false, /*trusted=*/false);
    });
    ASSERT_EQ(graph.getNumNodes(), 3) << "the merge patch should have added a node";

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), 2);
    EXPECT_EQ(countTaggedNodes(graph), 2) << "the pre-merge modules must be the same instances";
    EXPECT_TRUE(captureIdentity(graph) == before);
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), jsonBefore)
        << "the restored graph must serialize identically to the pre-merge graph";
    EXPECT_NEAR(cutoff->getNormalisableRange().convertFrom0to1(cutoff->getValue()), cutoffBefore, 0.01f);
}

/** Non-parameter module state is re-applied only when it actually differs. */
TEST_F(UndoRedoTest, ExtraStateIsReappliedOnlyWhenItChanged) {
    auto* probeNode = graph.addNode(std::make_unique<ExtraStateProbeModule>()).get();
    auto* probe = dynamic_cast<ExtraStateProbeModule*>(probeNode->getProcessor());
    ASSERT_NE(probe, nullptr);
    graph.addNode(std::make_unique<FilterModule>());

    // (a) An undo that leaves this module's state alone must not write it back — setExtraState
    //     reloads a sample/wavetable from disk in the real modules.
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });
    probe->setExtraStateCalls = 0;
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(probe->setExtraStateCalls, 0) << "unchanged extra state must not be re-applied on every undo";
    EXPECT_EQ(probe->token, juce::String("a"));

    // (b) An undo that does change it must write it back, exactly once.
    undoManager.captureBeforeState(graph);
    probe->token = "b";
    undoManager.pushSnapshotFromCapture(graph);
    probe->setExtraStateCalls = 0;

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(probe->setExtraStateCalls, 1);
    EXPECT_EQ(probe->token, juce::String("a"));
}

/**
 * Fallback: a live node with no uuid makes identity undecidable, so the restore must give up and
 * let the original destroy-and-rebuild apply run. The graph still ends up correct — it is only the
 * node instances that are lost, which is what the missing tags prove.
 */
TEST_F(UndoRedoTest, RestoreFallsBackToFullRebuildWhenIdentityIsUnusable) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    auto* filter = graph.addNode(std::make_unique<FilterModule>()).get();
    graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});

    stampUuids(graph);
    tagLiveNodes(graph);
    ASSERT_EQ(countTaggedNodes(graph), 2);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 3);

    // Corrupt identity on a live node the snapshot still contains.
    graph.getNodes().getFirst()->properties.remove(juce::Identifier("uuid"));

    ASSERT_TRUE(undoManager.undo());

    // Correct end state...
    EXPECT_EQ(graph.getNumNodes(), 2);
    EXPECT_EQ(graph.getConnections().size(), 1u);
    // ...reached the expensive way: every node was destroyed and re-created.
    EXPECT_EQ(countTaggedNodes(graph), 0) << "the full-rebuild fallback should have run";
}

/** A snapshot whose identities are ambiguous is refused outright, with the graph left untouched. */
TEST_F(UndoRedoTest, PreservingApplyRefusesAmbiguousSnapshotWithoutMutating) {
    graph.addNode(std::make_unique<OscillatorModule>());
    graph.addNode(std::make_unique<FilterModule>());

    const juce::var snapshot = synth::AIStateMapper::graphToJSON(graph);
    const auto before = captureIdentity(graph);

    auto duplicateUuid = [](const juce::var& source) {
        const juce::var copy = juce::JSON::parse(juce::JSON::toString(source));
        auto* nodes = copy.getDynamicObject()->getProperty("nodes").getArray();
        (*nodes)[1].getDynamicObject()->setProperty("uuid", (*nodes)[0].getDynamicObject()->getProperty("uuid"));
        return copy;
    };

    EXPECT_FALSE(synth::AIStateMapper::applySnapshotPreservingNodes(duplicateUuid(snapshot), graph))
        << "two nodes claiming one identity must be refused";
    EXPECT_TRUE(captureIdentity(graph) == before) << "a refused snapshot must leave the graph untouched";

    auto retypeFirstNode = [](const juce::var& source) {
        const juce::var copy = juce::JSON::parse(juce::JSON::toString(source));
        auto* nodes = copy.getDynamicObject()->getProperty("nodes").getArray();
        (*nodes)[0].getDynamicObject()->setProperty("type", "Reverb");
        return copy;
    };

    EXPECT_FALSE(synth::AIStateMapper::applySnapshotPreservingNodes(retypeFirstNode(snapshot), graph))
        << "a uuid whose module type changed must be refused";
    EXPECT_TRUE(captureIdentity(graph) == before);

    auto dropUuid = [](const juce::var& source) {
        const juce::var copy = juce::JSON::parse(juce::JSON::toString(source));
        auto* nodes = copy.getDynamicObject()->getProperty("nodes").getArray();
        (*nodes)[0].getDynamicObject()->removeProperty("uuid");
        return copy;
    };

    EXPECT_FALSE(synth::AIStateMapper::applySnapshotPreservingNodes(dropUuid(snapshot), graph))
        << "a snapshot node with no identity must be refused";
    EXPECT_TRUE(captureIdentity(graph) == before);

    // A merge delta is a change, not a target state — there is nothing to diff against.
    EXPECT_FALSE(synth::AIStateMapper::applySnapshotPreservingNodes(
        juce::JSON::parse(R"({"nodes":[],"connections":[],"remove":[1]})"), graph));
    EXPECT_TRUE(captureIdentity(graph) == before);

    // The unmodified snapshot, by contrast, applies and changes nothing.
    EXPECT_TRUE(synth::AIStateMapper::applySnapshotPreservingNodes(snapshot, graph));
    EXPECT_TRUE(captureIdentity(graph) == before);
}
