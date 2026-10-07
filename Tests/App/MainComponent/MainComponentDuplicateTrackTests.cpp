// Concern: "Duplicate Track" end to end on a real MainComponent -- Cmd+D through a track header's keyPressed
// copies the track's modules (new ids, cables remapped, the macro and Master routing of a new track), clips,
// notes and automation lane directly below it, and one Cmd+Z removes all of it.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/MasterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

#include <map>
#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

NodeID nodeWithUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (uuid.isNotEmpty() && node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

// What a track plays: every node reachable forward from its Track In, stopping at Master (the shared output dock).
std::set<juce::uint32> closureFrom(juce::AudioProcessorGraph& graph, NodeID start) {
    std::set<juce::uint32> seen;
    std::vector<NodeID> pending{start};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto* node = graph.getNodeForId(id);
        if (node == nullptr || dynamic_cast<MasterModule*>(node->getProcessor()) != nullptr ||
            !seen.insert(id.uid).second)
            continue;
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == id)
                pending.push_back(c.destination.nodeID);
    }
    return seen;
}

std::multiset<juce::String> typesOf(juce::AudioProcessorGraph& graph, const std::set<juce::uint32>& ids) {
    std::multiset<juce::String> types;
    for (const auto uid : ids)
        types.insert(
            graph.getNodeForId(NodeID(uid))->getProcessor()->getName().upToFirstOccurrenceOf(" ", false, false));
    return types;
}

int internalEdges(juce::AudioProcessorGraph& graph, const std::set<juce::uint32>& ids) {
    int count = 0;
    for (const auto& c : graph.getConnections())
        if (ids.count(c.source.nodeID.uid) > 0 && ids.count(c.destination.nodeID.uid) > 0)
            ++count;
    return count;
}

int edgesIntoMaster(juce::AudioProcessorGraph& graph) {
    int count = 0;
    for (const auto& c : graph.getConnections())
        if (auto* node = graph.getNodeForId(c.destination.nodeID);
            node != nullptr && dynamic_cast<MasterModule*>(node->getProcessor()) != nullptr)
            ++count;
    return count;
}

int macrosNamed(MainComponent& mc, const juce::String& name) {
    int count = 0;
    for (const auto& macro : mc.getGraphEditor().getMacros().getAll())
        if (macro.name == name)
            ++count;
    return count;
}

const juce::KeyPress kCmdD('d', juce::ModifierKeys::commandModifier, 0);

} // namespace

class DuplicateTrackTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
    }
    void TearDown() override {
        mc.reset();
        MainComponentTest::TearDown();
    }

    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    synth::TimelineDoc& doc() { return mc->getTimelineDoc(); }

    // "+ Track -> Instrument -> Oscillator": Track In -> Oscillator -> ... -> Channel Strip -> Master, boxed in a
    // macro.
    synth::TrackId addInstrumentTrack() {
        mc->getTimelinePanel().applyAddTrackMenuChoice(
            synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        return doc().getTracks().back().id;
    }

    // The Oscillator the instrument track plays, with a uuid and a parameter id to hang a lane on.
    NodeID oscillatorOf(const synth::Track& track) {
        const auto start = nodeWithUuid(graph(), track.bindingUuid);
        for (const auto uid : closureFrom(graph(), start))
            if (dynamic_cast<OscillatorModule*>(graph().getNodeForId(NodeID(uid))->getProcessor()) != nullptr)
                return NodeID(uid);
        return {};
    }

    std::unique_ptr<MainComponent> mc;
};

TEST_F(DuplicateTrackTest, CmdDOnTheFocusedRowCopiesModulesClipsAndAutomationBelowTheOriginal) {
    const auto sourceId = addInstrumentTrack();
    const auto& source = *doc().getTrack(sourceId);
    const juce::String sourceName = source.name;
    const juce::String sourceBinding = source.bindingUuid;

    const auto osc = oscillatorOf(source);
    ASSERT_NE(osc.uid, 0u);
    const auto oscUuid = synth::AIStateMapper::ensureNodeUuid(graph().getNodeForId(osc));
    auto* param =
        dynamic_cast<juce::RangedAudioParameter*>(graph().getNodeForId(osc)->getProcessor()->getParameters()[0]);
    ASSERT_NE(param, nullptr);
    const auto clip = doc().addClip(sourceId, 4.0, 2.0, "Verse");
    ASSERT_TRUE(doc()
                    .addNote(clip,
                             [] {
                                 synth::MidiNote note;
                                 note.pitch = 62;
                                 return note;
                             }())
                    .isValid());
    const auto lane = doc().addLane(sourceId, oscUuid, param->getParameterID(), synth::AutomationLane::RangeSnapshot{});
    ASSERT_TRUE(lane.isValid());
    ASSERT_TRUE(doc().addBreakpoint(lane, 1.0, 0.5));

    const auto sourceNodes = closureFrom(graph(), nodeWithUuid(graph(), sourceBinding));
    const auto nodesBefore = graph().getNodes().size();
    const int masterEdgesBefore = edgesIntoMaster(graph());
    int ownMasterEdges = 0; // the track's own cables into Master (the default patch feeds it too)
    for (const auto& c : graph().getConnections())
        if (sourceNodes.count(c.source.nodeID.uid) > 0 && graph().getNodeForId(c.destination.nodeID) != nullptr &&
            dynamic_cast<MasterModule*>(graph().getNodeForId(c.destination.nodeID)->getProcessor()) != nullptr)
            ++ownMasterEdges;
    const int macrosBefore = mc->getGraphEditor().getMacros().size();
    const auto tracksBefore = doc().getTracks().size();
    ASSERT_GT(ownMasterEdges, 0);

    // The real key event, through the focused row.
    auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(tracksBefore) - 1);
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(header->getTrackId(), sourceId);
    ASSERT_TRUE(header->keyPressed(kCmdD));

    // The copy sits directly below, named and bound to a node of its own.
    ASSERT_EQ(doc().getTracks().size(), tracksBefore + 1);
    const auto& copy = doc().getTracks().back();
    EXPECT_EQ(copy.name, sourceName + " copy");
    ASSERT_NE(copy.id, sourceId);
    EXPECT_EQ(doc().getTrack(sourceId)->bindingUuid, sourceBinding) << "the original stays bound to its own node";
    ASSERT_TRUE(copy.bindingUuid.isNotEmpty());
    EXPECT_NE(copy.bindingUuid, sourceBinding);
    const auto copyStart = nodeWithUuid(graph(), copy.bindingUuid);
    ASSERT_NE(copyStart.uid, 0u);

    // New module ids and uuids, same modules, same cables, nothing shared with the original.
    const auto copyNodes = closureFrom(graph(), copyStart);
    ASSERT_GE(copyNodes.size(), 3u);
    for (const auto uid : copyNodes)
        EXPECT_EQ(sourceNodes.count(uid), 0u) << "the copy must not reuse or alias an original node";
    // A direct cable into a parameter jack comes back through the hidden attenuverter paste rebuilds for it (amount
    // 1, so the signal is unchanged), which adds one node and one cable per such hop.
    auto copyTypes = typesOf(graph(), copyNodes);
    const auto hops = static_cast<int>(copyTypes.count("Attenuverter"));
    copyTypes.erase("Attenuverter");
    EXPECT_EQ(copyTypes, typesOf(graph(), sourceNodes));
    EXPECT_EQ(internalEdges(graph(), copyNodes), internalEdges(graph(), sourceNodes) + hops)
        << "cables remapped, not lost";
    EXPECT_EQ(static_cast<size_t>(graph().getNodes().size()), static_cast<size_t>(nodesBefore) + copyNodes.size());
    std::set<juce::String> uuids;
    for (auto* node : graph().getNodes())
        if (const auto uuid = node->properties["uuid"].toString(); uuid.isNotEmpty())
            EXPECT_TRUE(uuids.insert(uuid).second) << "every node keeps a unique uuid";

    // Routed like a new track: a macro of its own, and its output into Master alongside the original's.
    EXPECT_EQ(mc->getGraphEditor().getMacros().size(), macrosBefore + 1);
    EXPECT_EQ(macrosNamed(*mc, sourceName + " copy"), 1);
    EXPECT_EQ(edgesIntoMaster(graph()), masterEdgesBefore + ownMasterEdges);

    // Clips, notes and the lane follow, the lane bound to the copy's own oscillator.
    ASSERT_EQ(copy.clips.size(), 1u);
    EXPECT_NE(copy.clips[0].id, clip);
    EXPECT_DOUBLE_EQ(copy.clips[0].startBeat, 4.0);
    ASSERT_EQ(copy.clips[0].notes.size(), 1u);
    EXPECT_EQ(copy.clips[0].notes[0].pitch, 62);
    ASSERT_EQ(copy.lanes.size(), 1u);
    EXPECT_EQ(copy.lanes[0].paramId, param->getParameterID());
    ASSERT_EQ(copy.lanes[0].points.size(), 1u);
    const auto laneNode = nodeWithUuid(graph(), copy.lanes[0].nodeUuid);
    EXPECT_EQ(copyNodes.count(laneNode.uid), 1u);
    EXPECT_NE(dynamic_cast<OscillatorModule*>(graph().getNodeForId(laneNode)->getProcessor()), nullptr);
    EXPECT_EQ(doc().getTrack(sourceId)->lanes.size(), 1u);
    EXPECT_EQ(doc().getTrack(sourceId)->lanes[0].nodeUuid, oscUuid);
}

TEST_F(DuplicateTrackTest, OneUndoRemovesTheWholeCopyAndRedoBringsItBack) {
    const auto sourceId = addInstrumentTrack();
    doc().addClip(sourceId, 0.0, 4.0, "Clip");
    const auto nodesBefore = graph().getNodes().size();
    const int connectionsBefore = static_cast<int>(graph().getConnections().size());
    const int macrosBefore = mc->getGraphEditor().getMacros().size();
    const auto tracksBefore = doc().getTracks().size();

    auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(tracksBefore) - 1);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->keyPressed(kCmdD));
    ASSERT_EQ(doc().getTracks().size(), tracksBefore + 1);
    const auto nodesAfter = graph().getNodes().size();
    const int connectionsAfter = static_cast<int>(graph().getConnections().size());
    ASSERT_GT(nodesAfter, nodesBefore);

    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(doc().getTracks().size(), tracksBefore) << "ONE Cmd+Z removes the copy -- not just its half of the work";
    EXPECT_EQ(graph().getNodes().size(), nodesBefore);
    EXPECT_EQ(static_cast<int>(graph().getConnections().size()), connectionsBefore);
    EXPECT_EQ(mc->getGraphEditor().getMacros().size(), macrosBefore);
    ASSERT_NE(doc().getTrack(sourceId), nullptr);
    EXPECT_EQ(doc().getTrack(sourceId)->clips.size(), 1u) << "the original is untouched";

    ASSERT_TRUE(mc->getUndoManager().redo());
    EXPECT_EQ(doc().getTracks().size(), tracksBefore + 1);
    EXPECT_EQ(graph().getNodes().size(), nodesAfter);
    EXPECT_EQ(static_cast<int>(graph().getConnections().size()), connectionsAfter);
    EXPECT_EQ(mc->getGraphEditor().getMacros().size(), macrosBefore + 1);
}

TEST_F(DuplicateTrackTest, AnUnboundTrackIsCopiedWithItsClipsAlone) {
    const auto id = doc().addTrack(synth::TrackKind::Midi, "Loose");
    doc().addClip(id, 0.0, 4.0, "Clip");
    const auto nodesBefore = graph().getNodes().size();

    auto* header = mc->getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->keyPressed(kCmdD));

    ASSERT_EQ(doc().getTracks().size(), 2u);
    EXPECT_EQ(doc().getTracks()[1].name, "Loose copy");
    EXPECT_EQ(doc().getTracks()[1].clips.size(), 1u);
    EXPECT_TRUE(doc().getTracks()[1].bindingUuid.isEmpty());
    EXPECT_EQ(graph().getNodes().size(), nodesBefore);
}

// Undo lands exactly on the document before the duplicate (graph, timeline and macros as written out) and redo on the
// one after it, in a small project and a grown one: the snapshots behind the step reuse every untouched node, and
// that must never change what the step restores.
TEST_F(DuplicateTrackTest, UndoAndRedoRestoreTheExactDocumentAtAnySize) {
    const auto state = [this] {
        return juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph())) + juce::JSON::toString(doc().toVar()) +
               juce::JSON::toString(mc->getGraphEditor().getMacros().toVar());
    };
    for (int tracks : {1, 12}) {
        while ((int)doc().getTracks().size() < tracks)
            addInstrumentTrack();
        const auto before = state();
        auto* header = mc->getTimelinePanel().getTrackHeaderAt(0);
        ASSERT_NE(header, nullptr);
        ASSERT_TRUE(header->keyPressed(kCmdD));
        const auto after = state();
        ASSERT_NE(after, before);

        ASSERT_TRUE(mc->getUndoManager().undo());
        EXPECT_EQ(state(), before) << tracks << " tracks";
        ASSERT_TRUE(mc->getUndoManager().redo());
        EXPECT_EQ(state(), after) << tracks << " tracks";
        ASSERT_TRUE(mc->getUndoManager().undo());
        EXPECT_EQ(state(), before) << tracks << " tracks";
    }
}
