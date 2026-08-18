#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PresetManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/LayoutUtil.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
#include <vector>

class UndoRedoTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;

    void SetUp() override { graph.clear(); }
};

// =============================================================================
// Node-preserving (diffing) restore — see AIStateMapper::applySnapshotPreservingNodes.
//
// A structural undo used to destroy and re-create EVERY node, losing all module runtime state and
// blocking the audio callback while it did so. The restore now diffs the snapshot against the live
// graph, so the tests below are about IDENTITY, not just about the graph ending up the right shape.
// =============================================================================

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

juce::RangedAudioParameter* findParam(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (p->paramID == paramId)
                return dynamic_cast<juce::RangedAudioParameter*>(param);
    return nullptr;
}

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

/**
 * Test 1: UndoAddModule
 * - Graph starts empty (0 nodes)
 * - Use recordStructuralChange to add an OscillatorModule
 * - Verify graph has 1 node
 * - Undo → graph should have 0 nodes
 * - Redo → graph should have 1 node again
 */
TEST_F(UndoRedoTest, UndoAddModule) {
    ASSERT_EQ(graph.getNumNodes(), 0);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });

    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 0);

    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 1);
}

/**
 * Test 2: UndoRemoveModule
 * - Add a module directly, then use recordStructuralChange to remove it
 * - Verify node can be removed
 * - Undo → module should reappear
 */
TEST_F(UndoRedoTest, UndoRemoveModule) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.recordStructuralChange(graph, [this, nodeId] { graph.removeNode(nodeId); });

    ASSERT_EQ(graph.getNumNodes(), 0);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    // Verify it's an Oscillator by checking the processor name
    auto oscillatorNode = graph.getNodes().getFirst();
    ASSERT_NE(oscillatorNode, nullptr);
    ASSERT_EQ(oscillatorNode->getProcessor()->getName(), juce::String("Oscillator"));
}

/**
 * Test 3: UndoAddConnection
 * - Add two modules
 * - Use recordStructuralChange to connect them
 * - Undo → connection removed, redo → connection restored
 */
TEST_F(UndoRedoTest, UndoAddConnection) {
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.recordStructuralChange(
        graph, [this, oscNode, filterNode] { graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}); });

    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.undo();
    // After undo, nodes should exist but connection should be gone
    ASSERT_EQ(graph.getNumNodes(), 2);
    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.redo();
    ASSERT_EQ(graph.getConnections().size(), 1);
}

/**
 * Test 4: UndoParameterChange
 * - Add an oscillator node
 * - Find a float parameter and change it
 * - Verify the change
 * - Undo → parameter should revert
 * - Redo → parameter should return to new value
 */
TEST_F(UndoRedoTest, UndoParameterChange) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;

    // Use the "fine" float parameter (not a choice/int param)
    juce::String paramId = "fine";
    juce::RangedAudioParameter* fineParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == paramId) {
                fineParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(fineParam, nullptr);

    float originalValue = fineParam->getValue();
    float newValue = 0.75f;
    fineParam->setValueNotifyingHost(newValue);

    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, originalValue, newValue);

    ASSERT_NEAR(fineParam->getValue(), newValue, 0.001f);

    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), originalValue, 0.001f);

    undoManager.redo();
    ASSERT_NEAR(fineParam->getValue(), newValue, 0.001f);
}

/**
 * Test 5: UndoPositionChange
 * - Add an oscillator node and set initial position
 * - Use recordPositionChange to move it
 * - Undo → position should revert
 * - Redo → position should return to new location
 */
TEST_F(UndoRedoTest, UndoPositionChange) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;
    node->properties.set("x", 100);
    node->properties.set("y", 200);

    // Simulate drag to new position
    node->properties.set("x", 300);
    node->properties.set("y", 400);

    undoManager.recordPositionChange(graph, nodeId, 100, 200, 300, 400, [] {});

    ASSERT_EQ((int)node->properties["x"], 300);
    ASSERT_EQ((int)node->properties["y"], 400);

    undoManager.undo();
    ASSERT_EQ((int)node->properties["x"], 100);
    ASSERT_EQ((int)node->properties["y"], 200);

    undoManager.redo();
    ASSERT_EQ((int)node->properties["x"], 300);
    ASSERT_EQ((int)node->properties["y"], 400);
}

/**
 * Test 6: MultipleUndoLevels
 * - Add 3 modules, one at a time
 * - Undo all 3 in sequence
 * - Redo all 3 in sequence
 * - Verify node count at each step
 */
TEST_F(UndoRedoTest, MultipleUndoLevels) {
    // Add 3 modules, one at a time
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<FilterModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 3);

    // Undo all 3
    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Redo all 3
    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 3);
}

/**
 * Test 7: ClearUndoHistory
 * - Add a module
 * - Verify canUndo() is true
 * - Clear the undo history
 * - Verify canUndo() and canRedo() are false
 */
TEST_F(UndoRedoTest, ClearUndoHistory) {
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });

    ASSERT_TRUE(undoManager.canUndo());

    undoManager.clearUndoHistory();

    ASSERT_FALSE(undoManager.canUndo());
    ASSERT_FALSE(undoManager.canRedo());
}

/**
 * Test 8: UndoRemoveConnection
 * - Add two modules and connect them
 * - Use recordStructuralChange to disconnect them
 * - Undo → connection should be restored
 * - Redo → connection should be removed again
 */
TEST_F(UndoRedoTest, UndoRemoveConnection) {
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Add a connection first (outside of undo/redo)
    juce::AudioProcessorGraph::Connection conn = {{oscNode->nodeID, 0}, {filterNode->nodeID, 0}};
    graph.addConnection(conn);
    ASSERT_EQ(graph.getConnections().size(), 1);

    // Now record the removal
    undoManager.recordStructuralChange(graph, [this, conn] { graph.removeConnection(conn); });

    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.undo();
    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.redo();
    ASSERT_EQ(graph.getConnections().size(), 0);
}

/**
 * Test 9: ParameterChangeCoalescing
 * - Record multiple parameter changes to the same parameter
 * - Verify that undoing once reverts to the original value (coalescing works)
 */
TEST_F(UndoRedoTest, ParameterChangeCoalescing) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    auto nodeId = node->nodeID;

    // Use the "fine" float parameter
    juce::String paramId = "fine";
    juce::RangedAudioParameter* fineParam = nullptr;
    for (auto* param : node->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == paramId) {
                fineParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(fineParam, nullptr);

    float originalValue = fineParam->getValue();

    // Simulate slider drag: set param, then record each step
    fineParam->setValueNotifyingHost(0.25f);
    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, originalValue, 0.25f);

    fineParam->setValueNotifyingHost(0.50f);
    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, 0.25f, 0.50f);

    fineParam->setValueNotifyingHost(0.75f);
    undoManager.beginNewTransaction();
    undoManager.recordParameterChange(graph, nodeId, paramId, 0.50f, 0.75f);

    ASSERT_NEAR(fineParam->getValue(), 0.75f, 0.001f);

    // Undo should revert through the sequence
    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), 0.50f, 0.001f);

    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), 0.25f, 0.001f);

    undoManager.undo();
    ASSERT_NEAR(fineParam->getValue(), originalValue, 0.001f);
}

/**
 * Test 10: ComplexGraphModification
 * - Add 3 modules
 * - Create connections between them
 * - Record the entire sequence
 * - Undo and redo to verify all changes are preserved
 */
TEST_F(UndoRedoTest, ComplexGraphModification) {
    // Start with empty graph
    ASSERT_EQ(graph.getNumNodes(), 0);
    ASSERT_EQ(graph.getConnections().size(), 0);

    // Add modules
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<FilterModule>()); });

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<VCAModule>()); });

    ASSERT_EQ(graph.getNumNodes(), 3);

    // Get node IDs for connections
    auto oscNode = graph.getNodes()[0];
    auto filterNode = graph.getNodes()[1];
    auto vcaNode = graph.getNodes()[2];

    // Add connections
    undoManager.recordStructuralChange(
        graph, [this, oscNode, filterNode] { graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}); });

    undoManager.recordStructuralChange(
        graph, [this, filterNode, vcaNode] { graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}); });

    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 2);

    // Undo all operations
    undoManager.undo(); // Remove second connection
    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.undo(); // Remove first connection
    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 0);

    undoManager.undo(); // Remove VCA
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.undo(); // Remove Filter
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.undo(); // Remove Oscillator
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Redo all operations
    undoManager.redo(); // Add Oscillator
    ASSERT_EQ(graph.getNumNodes(), 1);

    undoManager.redo(); // Add Filter
    ASSERT_EQ(graph.getNumNodes(), 2);

    undoManager.redo(); // Add VCA
    ASSERT_EQ(graph.getNumNodes(), 3);

    undoManager.redo(); // Add first connection
    ASSERT_EQ(graph.getConnections().size(), 1);

    undoManager.redo(); // Add second connection
    ASSERT_EQ(graph.getConnections().size(), 2);
}

/**
 * Test 11: RedoWithParameterValueInUnitInterval
 * - Create a Filter module with a parameter that has range > [0,1]
 * - Set parameter to a value within [0,1] (e.g., drive = 1.0, range 1.0-10.0)
 * - Record a structural change (module addition)
 * - Undo (module removed)
 * - Redo (module restored)
 * - Verify the parameter value is still correct (1.0, not 10.0)
 *
 * This tests the fix for GitHub issue #53: Redo after module replacement
 * corrupts new module's parameters. The bug was that values in [0,1] were
 * being double-converted: normalized value from state was incorrectly treated
 * as a normalized value and converted again.
 */
TEST_F(UndoRedoTest, RedoWithParameterValueInUnitInterval) {
    // Create a Filter module (has parameters with ranges > [0,1])
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto nodeId = filterNode->nodeID;

    // Find the "drive" parameter (range 1.0-10.0)
    juce::String driveParamId = "drive";
    juce::RangedAudioParameter* driveParam = nullptr;
    for (auto* param : filterNode->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == driveParamId) {
                driveParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(driveParam, nullptr);

    // Set drive to 1.0 (within [0,1] but valid for range 1.0-10.0)
    float targetValue = 1.0f;
    driveParam->setValueNotifyingHost(driveParam->getNormalisableRange().convertTo0to1(targetValue));

    // Verify the value was set correctly
    float denormalized = driveParam->getNormalisableRange().convertFrom0to1(driveParam->getValue());
    ASSERT_NEAR(denormalized, targetValue, 0.001f);

    // Record a structural change (add the Filter module)
    // Note: We already added it above, so we'll remove it and re-add it via undo/redo
    graph.removeNode(nodeId);
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Now add it back via recordStructuralChange
    undoManager.recordStructuralChange(graph, [this] {
        auto node = graph.addNode(std::make_unique<FilterModule>());
        // Set the same parameter to the same value
        for (auto* param : node->getProcessor()->getParameters()) {
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                if (p->paramID == "drive") {
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param)) {
                        ranged->setValueNotifyingHost(ranged->getNormalisableRange().convertTo0to1(1.0f));
                    }
                }
            }
        }
    });

    ASSERT_EQ(graph.getNumNodes(), 1);

    // Get the new module and verify parameter
    auto newFilterNode = graph.getNodes().getFirst();
    ASSERT_NE(newFilterNode, nullptr);
    juce::RangedAudioParameter* newDriveParam = nullptr;
    for (auto* param : newFilterNode->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "drive") {
                newDriveParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(newDriveParam, nullptr);

    float valueAfterAdd = newDriveParam->getNormalisableRange().convertFrom0to1(newDriveParam->getValue());
    ASSERT_NEAR(valueAfterAdd, 1.0f, 0.001f) << "Parameter value should be 1.0 after initial add";

    // Undo (removes the module)
    undoManager.undo();
    ASSERT_EQ(graph.getNumNodes(), 0);

    // Redo (restores the module with the same parameters)
    undoManager.redo();
    ASSERT_EQ(graph.getNumNodes(), 1);

    // Verify the parameter value is still correct (not double-converted to 10.0)
    auto redoFilterNode = graph.getNodes().getFirst();
    ASSERT_NE(redoFilterNode, nullptr);
    juce::RangedAudioParameter* redoDriveParam = nullptr;
    for (auto* param : redoFilterNode->getProcessor()->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "drive") {
                redoDriveParam = dynamic_cast<juce::RangedAudioParameter*>(param);
                break;
            }
        }
    }
    ASSERT_NE(redoDriveParam, nullptr);

    float valueAfterRedo = redoDriveParam->getNormalisableRange().convertFrom0to1(redoDriveParam->getValue());
    ASSERT_NEAR(valueAfterRedo, 1.0f, 0.001f)
        << "Parameter value should be 1.0 after redo, not 10.0 (double-converted)";
}

/**
 * Test 12: PolyPad_RoutingSurvivesUndoRedo
 *
 * Verifies that the "Poly Pad" preset (index 6) connections survive a full
 * undo/redo cycle modelled by the graphToJSON -> applyJSONToGraph round-trip
 * (the same operation that AppUndoManager::SnapshotAction performs on
 * undo and redo).
 *
 * The following connections must survive:
 *   (A) 8 direct poly-bus edges: Amp Env ch0-7 → VCA ch8-15
 *       (per-voice amplitude envelope modulation)
 *
 * The env->osc Level CV wire (Amp Env ch0 → Oscillator ch12) was removed from
 * the factory preset because that channel is shared across all voices and was
 * coupling all voices to voice-0's envelope. The capability itself still works
 * (see IntegrationTests::PolyPad_EnvToOscModulatesOscLevel).
 *
 * Additionally, the restored patch must NOT introduce any new AttenuverterModule
 * nodes — these connections are direct poly-bus connections, not mod-matrix
 * routings, and the serializer must preserve them as-is without substituting
 * attenuverter chains.
 *
 * Strategy (mirrors AppUndoManager behaviour):
 *   1. Load Poly Pad (preset 6) into graph g.
 *   2. Snapshot: J0 = graphToJSON(g).
 *   3. Simulate "undo-apply-different-state": load preset 0 into g (clobbers Poly Pad).
 *   4. Simulate "redo-restore-original-state": applyJSONToGraph(J0, g, clear=true, trusted=true).
 *   5. Count the required connections; assert both groups are fully present.
 *   6. Assert zero AttenuverterModule nodes were introduced for these paths.
 */
TEST_F(UndoRedoTest, PolyPad_RoutingSurvivesUndoRedo) {
    // Step 1 — Load Poly Pad (preset 6) into graph
    ASSERT_TRUE(synth::PresetManager::loadPreset(6, graph));

    // Sanity: locate Amp Env (ADSR), Oscillator, and VCA by type
    juce::AudioProcessorGraph::NodeID adsrID, oscID, vcaID;
    for (auto* node : graph.getNodes()) {
        auto* proc = node->getProcessor();
        if (auto* mb = dynamic_cast<ADSRModule*>(proc)) {
            // Preset 6 has one ADSR named "Amp Env"
            if (mb->getName().contains("Amp Env") || mb->getName().contains("ADSR"))
                adsrID = node->nodeID;
        } else if (dynamic_cast<OscillatorModule*>(proc)) {
            oscID = node->nodeID;
        } else if (dynamic_cast<VCAModule*>(proc)) {
            vcaID = node->nodeID;
        }
    }
    ASSERT_NE(adsrID.uid, 0u) << "Poly Pad should have an Amp Env (ADSR) node";
    ASSERT_NE(oscID.uid, 0u) << "Poly Pad should have an Oscillator node";
    ASSERT_NE(vcaID.uid, 0u) << "Poly Pad should have a VCA node";

    // Helper: count specific connections in the current graph
    auto countConns = [&](juce::AudioProcessorGraph::NodeID srcID, int srcCh, juce::AudioProcessorGraph::NodeID dstID,
                          int dstCh) -> int {
        int count = 0;
        for (const auto& conn : graph.getConnections()) {
            if (conn.source.nodeID == srcID && conn.source.channelIndex == srcCh && conn.destination.nodeID == dstID &&
                conn.destination.channelIndex == dstCh)
                ++count;
        }
        return count;
    };

    // Verify the connections exist in the freshly loaded preset (pre-cycle baseline)
    int envVcaConnsBaseline = 0;
    for (int v = 0; v < 8; ++v)
        envVcaConnsBaseline += countConns(adsrID, v, vcaID, 8 + v);

    ASSERT_EQ(envVcaConnsBaseline, 8) << "Poly Pad baseline: expected 8 ADSR->VCA poly-bus connections";

    // Step 2 — Snapshot the loaded Poly Pad state
    juce::var J0 = synth::AIStateMapper::graphToJSON(graph);

    // Step 3 — Overwrite with a completely different preset (simulates the
    // modification that would be "undone")
    ASSERT_TRUE(synth::PresetManager::loadPreset(0, graph)); // Default preset — no poly modules
    EXPECT_EQ(countConns(adsrID, 0, vcaID, 8), 0)
        << "After loading preset 0, original Poly Pad connections should be gone";

    // Step 4 — Re-apply the Poly Pad snapshot (simulates undo/redo restore)
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(J0, graph, /*clearExisting=*/true, /*trusted=*/true))
        << "applyJSONToGraph should succeed for a freshly-captured Poly Pad snapshot";

    // Re-locate nodes by type after round-trip (node IDs may differ after clear+rebuild)
    adsrID = vcaID = oscID = {};
    for (auto* node : graph.getNodes()) {
        auto* proc = node->getProcessor();
        if (auto* mb = dynamic_cast<ADSRModule*>(proc)) {
            if (mb->getName().contains("Amp Env") || mb->getName().contains("ADSR"))
                adsrID = node->nodeID;
        } else if (dynamic_cast<OscillatorModule*>(proc)) {
            oscID = node->nodeID;
        } else if (dynamic_cast<VCAModule*>(proc)) {
            vcaID = node->nodeID;
        }
    }
    ASSERT_NE(adsrID.uid, 0u) << "Post-roundtrip: Amp Env node should exist";
    ASSERT_NE(oscID.uid, 0u) << "Post-roundtrip: Oscillator node should exist";
    ASSERT_NE(vcaID.uid, 0u) << "Post-roundtrip: VCA node should exist";

    // Step 5 — Verify all 8 ADSR->VCA poly-bus connections survived
    int envVcaConnsAfter = 0;
    for (int v = 0; v < 8; ++v)
        envVcaConnsAfter += countConns(adsrID, v, vcaID, 8 + v);
    EXPECT_EQ(envVcaConnsAfter, 8)
        << "Post-undo/redo: expected 8 ADSR->VCA per-voice connections (ch0->ch8, ch1->ch9 ... ch7->ch15)";

    // Step 6 — Assert NO AttenuverterModule nodes were introduced for these paths.
    // Direct poly-bus connections must remain direct — serialization must not
    // silently convert them to attenuverter-chain routings.
    int attenuverterCount = 0;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()))
            ++attenuverterCount;
    }
    EXPECT_EQ(attenuverterCount, 0) << "Post-undo/redo: Poly Pad should have ZERO AttenuverterModule nodes; "
                                       "found "
                                    << attenuverterCount
                                    << " — direct poly connections were incorrectly "
                                       "converted to attenuverter chains during serialization round-trip";
}

/**
 * Test: AutoArrangeIsSingleUndoStep
 *
 * - Load a preset with N modules at known positions.
 * - Record undo-stack depth before autoArrange.
 * - Call autoArrange().
 * - Verify that modules moved (at least one position changed).
 * - ONE undo() restores ALL original x/y positions.
 * - ONE redo() reapplies ALL arranged positions.
 * - Undo stack depth increased by exactly 1 after autoArrange.
 */
TEST_F(UndoRedoTest, AutoArrangeIsSingleUndoStep) {
    // Load a preset with several modules into the graph via PresetManager
    ASSERT_TRUE(synth::PresetManager::loadPreset(0, graph)) << "Failed to load default preset";
    ASSERT_GE(graph.getNumNodes(), 3) << "Default preset should have at least 3 nodes";

    // Capture original positions
    std::map<juce::AudioProcessorGraph::NodeID, juce::Point<int>> originalPositions;
    for (auto* node : graph.getNodes()) {
        int x = static_cast<int>(node->properties.getWithDefault("x", 0));
        int y = static_cast<int>(node->properties.getWithDefault("y", 0));
        originalPositions[node->nodeID] = {x, y};
    }

    // Clear undo history so we start with a known depth
    undoManager.clearUndoHistory();
    ASSERT_FALSE(undoManager.canUndo()) << "Undo stack should be empty before test";

    // Build a GraphEditor with the undo manager so autoArrange() has access to both
    AudioEngine engine;
    // Load the same preset into the engine's graph
    engine.getGraph().clear();
    ASSERT_TRUE(synth::PresetManager::loadPreset(0, engine.getGraph()));

    GraphEditor editor(engine, &undoManager);
    // Wire the editor to the undo manager exactly as MainComponent does. Without this the undo/redo
    // SnapshotActions cannot detach module components before clearing the graph, so a MidiKeyboardComponent
    // outlives its module's MidiKeyboardState and removeListener()s on freed memory (heap-use-after-free).
    undoManager.setGraphEditor(&editor);
    editor.setSize(1200, 800);
    editor.updateComponents();

    // Capture positions from the engine graph before arrange
    std::map<juce::AudioProcessorGraph::NodeID, juce::Point<int>> beforeArrange;
    for (auto* node : engine.getGraph().getNodes()) {
        int x = static_cast<int>(node->properties.getWithDefault("x", 0));
        int y = static_cast<int>(node->properties.getWithDefault("y", 0));
        beforeArrange[node->nodeID] = {x, y};
    }

    // Call autoArrange — this should use captureBeforeState + pushSnapshotFromCapture
    editor.autoArrange();

    // Capture positions after arrange
    std::map<juce::AudioProcessorGraph::NodeID, juce::Point<int>> afterArrange;
    for (auto* node : engine.getGraph().getNodes()) {
        int x = static_cast<int>(node->properties.getWithDefault("x", 0));
        int y = static_cast<int>(node->properties.getWithDefault("y", 0));
        afterArrange[node->nodeID] = {x, y};
    }

    // At least some module should have moved
    bool anyMoved = false;
    for (auto& [id, pos] : afterArrange) {
        if (beforeArrange.count(id) && beforeArrange[id] != pos) {
            anyMoved = true;
            break;
        }
    }
    EXPECT_TRUE(anyMoved) << "autoArrange should have moved at least one module";

    // Undo stack should now have exactly 1 entry
    EXPECT_TRUE(undoManager.canUndo()) << "Should be able to undo after autoArrange";
    EXPECT_FALSE(undoManager.canRedo()) << "Should not be able to redo before undoing";

    // ONE undo restores original positions
    EXPECT_TRUE(undoManager.undo()) << "Undo should succeed";

    // After undo, verify positions match pre-arrange state
    for (auto* node : engine.getGraph().getNodes()) {
        if (!beforeArrange.count(node->nodeID))
            continue;
        int x = static_cast<int>(node->properties.getWithDefault("x", 0));
        int y = static_cast<int>(node->properties.getWithDefault("y", 0));
        EXPECT_EQ(x, beforeArrange[node->nodeID].x)
            << "After undo, node " << node->nodeID.uid << " x should be restored";
        EXPECT_EQ(y, beforeArrange[node->nodeID].y)
            << "After undo, node " << node->nodeID.uid << " y should be restored";
    }

    // No further undo available (only 1 step was pushed)
    EXPECT_FALSE(undoManager.canUndo()) << "After undoing the single autoArrange step, stack should be empty";

    // ONE redo reapplies arranged positions
    EXPECT_TRUE(undoManager.canRedo()) << "Should be able to redo after undoing";
    EXPECT_TRUE(undoManager.redo()) << "Redo should succeed";

    for (auto* node : engine.getGraph().getNodes()) {
        if (!afterArrange.count(node->nodeID))
            continue;
        int x = static_cast<int>(node->properties.getWithDefault("x", 0));
        int y = static_cast<int>(node->properties.getWithDefault("y", 0));
        EXPECT_EQ(x, afterArrange[node->nodeID].x)
            << "After redo, node " << node->nodeID.uid << " x should match arranged position";
        EXPECT_EQ(y, afterArrange[node->nodeID].y)
            << "After redo, node " << node->nodeID.uid << " y should match arranged position";
    }
}
