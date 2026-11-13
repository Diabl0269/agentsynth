// Concern: "Duplicate Track" end to end on a real MainComponent -- Cmd+D through a track header's keyPressed
// copies the track's modules (new ids, cables remapped, the macro and Master routing of a new track), clips,
// notes and automation lane directly below it, and one Cmd+Z removes all of it.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/LFOModule.h"
#include "Modules/MasterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
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

    // "Add Track -> Instrument -> Oscillator": Track In -> Oscillator -> ... -> Channel Strip -> Master, boxed in a
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

// A modulated parameter on the track: an LFO added the way the lane's "Add modulator" does (it joins the filter's
// macro), with a drawn amount curve on the routing's hidden Attenuverter.
class DuplicateTrackModulatorTest : public DuplicateTrackTest {
protected:
    struct Modulated {
        synth::TrackId track;
        juce::String attenuverterUuid;
        std::vector<synth::AutomationLane::Breakpoint> points{{0.0, 0.25f}, {2.0, -0.5f}, {4.0, 1.0f}};
    };

    Modulated addModulatedCutoff(const juce::String& sourceType = "LFO") {
        Modulated m;
        m.track = addInstrumentTrack();
        const auto start = nodeWithUuid(graph(), doc().getTrack(m.track)->bindingUuid);
        // The first module of the track with a modulatable parameter (the starter instrument has no filter).
        for (const auto uid : closureFrom(graph(), start)) {
            auto* processor = graph().getNodeForId(NodeID(uid))->getProcessor();
            for (auto* param : processor->getParameters()) {
                const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                if (ranged == nullptr ||
                    mc->getGraphEditor().modulationChannelFor(NodeID(uid), ranged->getParameterID()) < 0)
                    continue;
                juce::AudioProcessorGraph::NodeID atten;
                const int raw = mc->getGraphEditor().modulationChannelFor(NodeID(uid), ranged->getParameterID());
                if (sourceType == "LFO")
                    mc->getGraphEditor().addLfoModulator(NodeID(uid), ranged->getParameterID(), /*recordUndo=*/true,
                                                         1.0f, &atten);
                else
                    atten = mc->getGraphEditor().addModulationSourceModule(sourceType, 0, NodeID(uid), raw, 1.0f);
                if (atten.uid == 0)
                    continue;
                m.attenuverterUuid = synth::AIStateMapper::ensureNodeUuid(graph().getNodeForId(atten));
                synth::ui::writeAmountLane(doc(), m.track, m.attenuverterUuid, m.points);
                return m;
            }
        }
        return m;
    }

    int lfoCount(const juce::String& type = "LFO") {
        int count = 0;
        for (auto* node : graph().getNodes())
            if (node->getProcessor()->getName().startsWith(type))
                ++count;
        return count;
    }

    // The amount lane of the (single) routing into the cutoff of the copy track's filter.
    std::vector<synth::AutomationLane::Breakpoint> amountPointsOn(const synth::Track& track) {
        for (const auto& lane : track.lanes)
            if (lane.paramId == synth::ui::kAmountParamId)
                return lane.points;
        return {};
    }
};

TEST_F(DuplicateTrackModulatorTest, ACopyOfATrackWithAModulatorIsWrappedInItsOwnMacroLikeTheOriginal) {
    const auto m = addModulatedCutoff();
    ASSERT_TRUE(m.attenuverterUuid.isNotEmpty()) << "the instrument track has a modulatable parameter";
    const auto sourceName = doc().getTrack(m.track)->name;
    const int macrosBefore = mc->getGraphEditor().getMacros().size();
    ASSERT_EQ(lfoCount(), 1);

    auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(doc().getTracks().size()) - 1);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->keyPressed(kCmdD));

    EXPECT_EQ(lfoCount(), 2) << "the modulator is copied too";
    EXPECT_EQ(mc->getGraphEditor().getMacros().size(), macrosBefore + 1);
    ASSERT_EQ(macrosNamed(*mc, sourceName + " copy"), 1);
    const synth::Macro* copyMacro = nullptr;
    for (const auto& macro : mc->getGraphEditor().getMacros().getAll())
        if (macro.name == sourceName + " copy")
            copyMacro = &macro;
    ASSERT_NE(copyMacro, nullptr);
    int lfosInside = 0;
    for (const auto& uuid : mc->getGraphEditor().getMacros().descendantMembers(copyMacro->id))
        if (auto* node = graph().getNodeForId(nodeWithUuid(graph(), uuid));
            node != nullptr && dynamic_cast<LFOModule*>(node->getProcessor()) != nullptr)
            ++lfosInside;
    EXPECT_EQ(lfosInside, 1) << "the copied LFO sits inside the copy's macro, as the original's does";
}

TEST_F(DuplicateTrackModulatorTest, TheCopiedModulatorKeepsTheDrawnAmountCurveAndOneUndoRemovesIt) {
    const auto m = addModulatedCutoff();
    ASSERT_TRUE(m.attenuverterUuid.isNotEmpty());
    ASSERT_EQ(amountPointsOn(*doc().getTrack(m.track)).size(), m.points.size());

    auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(doc().getTracks().size()) - 1);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->keyPressed(kCmdD));

    ASSERT_EQ(doc().getTracks().size(), 2u);
    const auto& copy = doc().getTracks().back();
    const auto points = amountPointsOn(copy);
    ASSERT_EQ(points.size(), m.points.size()) << "the copy's amount lane is not flat";
    for (size_t i = 0; i < points.size(); ++i) {
        EXPECT_DOUBLE_EQ(points[i].beat, m.points[i].beat);
        EXPECT_FLOAT_EQ(points[i].value, m.points[i].value);
    }
    for (const auto& lane : copy.lanes)
        EXPECT_NE(lane.nodeUuid, m.attenuverterUuid) << "the copy's lanes drive the copy's own nodes";
    EXPECT_EQ(amountPointsOn(*doc().getTrack(m.track)).size(), m.points.size()) << "the original keeps its curve";

    ASSERT_TRUE(mc->getUndoManager().undo());
    ASSERT_EQ(doc().getTracks().size(), 1u);
    EXPECT_EQ(amountPointsOn(*doc().getTrack(m.track)).size(), m.points.size());
    int amountLanes = 0;
    for (const auto& track : doc().getTracks())
        for (const auto& lane : track.lanes)
            amountLanes += lane.paramId == synth::ui::kAmountParamId ? 1 : 0;
    EXPECT_EQ(amountLanes, 1) << "Cmd+Z removes the copied amount lane with the track";
}

TEST_F(DuplicateTrackModulatorTest, AnEnvelopeModulatorAmongOtherTracksIsCopiedIntoTheCopysMacroToo) {
    addInstrumentTrack();
    const auto m = addModulatedCutoff("ADSR");
    ASSERT_TRUE(m.attenuverterUuid.isNotEmpty());
    const auto sourceName = doc().getTrack(m.track)->name;
    const int envelopesBefore = lfoCount("ADSR");

    auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(doc().getTracks().size()) - 1);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->keyPressed(kCmdD));

    EXPECT_GT(lfoCount("ADSR"), envelopesBefore);
    EXPECT_EQ(macrosNamed(*mc, sourceName + " copy"), 1);
}

TEST_F(DuplicateTrackModulatorTest, DuplicatingTheCopyAgainWrapsTheSecondCopyToo) {
    const auto m = addModulatedCutoff();
    ASSERT_TRUE(m.attenuverterUuid.isNotEmpty());
    for (int round = 0; round < 2; ++round) {
        auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(doc().getTracks().size()) - 1 - round);
        ASSERT_NE(header, nullptr);
        ASSERT_TRUE(header->keyPressed(kCmdD));
    }
    EXPECT_EQ(lfoCount(), 3);
    EXPECT_EQ(mc->getGraphEditor().getMacros().size(), 3);
}

// The lane-header path a person takes: "Add automation..." on a parameter, then "Add modulator... -> New LFO" on that
// lane, then a drawn amount curve. The starter project also holds a free-standing patch with its own Oscillator that
// the lone track adopts for the picker, so `ownInstrument` picks which "Pulse Width" the person automates.
class DuplicateTrackLaneHeaderTest : public DuplicateTrackModulatorTest {
protected:
    struct Setup {
        synth::TrackId track;
        juce::String sourceName;
        juce::String paramId;
        int lfosBefore = 0;
        size_t lanesBefore = 0;
        size_t amountPoints = 0;
    };

    Setup build(bool ownInstrument) {
        Setup setup;
        setup.track = addInstrumentTrack();
        setup.sourceName = doc().getTrack(setup.track)->name;
        synth::ui::TrackHeaderHost::AutomatableParameter pulseWidth;
        for (const auto& parameter : mc->getAutomatableParametersForTest(setup.track))
            if (parameter.parameterName == "Pulse Width" &&
                (mc->getGraphEditor().getMacros().findByMember(parameter.nodeUuid) != nullptr) == ownInstrument)
                pulseWidth = parameter;
        EXPECT_TRUE(pulseWidth.nodeUuid.isNotEmpty());
        setup.paramId = pulseWidth.paramId;
        const auto lane = mc->addAutomationLaneForTest(setup.track, pulseWidth);
        EXPECT_TRUE(lane.isValid());
        mc->getTimelinePanel().setTrackAutomationExpanded(setup.track, true);
        // "Add modulator..." on the lane header, "New LFO" (the picker's first row) picked with Return.
        auto* laneHeader = mc->getTimelinePanel().laneHeaderForTest(lane);
        EXPECT_NE(laneHeader, nullptr);
        if (laneHeader == nullptr)
            return setup;
        std::unique_ptr<synth::ui::ModMatrixPicker> picker;
        synth::ui::test_hooks::addModulatorPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
        laneHeader->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kAddModulatorMenuId);
        synth::ui::test_hooks::addModulatorPickerHookForTest() = nullptr;
        EXPECT_NE(picker, nullptr);
        if (picker == nullptr)
            return setup;
        picker->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
        setup.lfosBefore = lfoCount();
        juce::String attenuverterUuid;
        for (const auto& routing : mc->getAudioEngine().getModulationRoutings())
            if (routing.attenuverterNodeID.uid != 0)
                attenuverterUuid =
                    synth::AIStateMapper::ensureNodeUuid(graph().getNodeForId(routing.attenuverterNodeID));
        const std::vector<synth::AutomationLane::Breakpoint> points{{0.0, 0.25f}, {2.0, -0.5f}, {4.0, 1.0f}};
        EXPECT_TRUE(synth::ui::writeAmountLane(doc(), setup.track, attenuverterUuid, points));
        setup.amountPoints = points.size();
        setup.lanesBefore = doc().getTrack(setup.track)->lanes.size();
        return setup;
    }

    void duplicateLastTrack() {
        auto* header = mc->getTimelinePanel().getTrackHeaderAt(static_cast<int>(doc().getTracks().size()) - 1);
        ASSERT_NE(header, nullptr);
        ASSERT_TRUE(header->keyPressed(kCmdD));
    }

    bool lfoInMacroNamed(const juce::String& name) {
        for (const auto& macro : mc->getGraphEditor().getMacros().getAll())
            if (macro.name == name)
                for (const auto& uuid : mc->getGraphEditor().getMacros().descendantMembers(macro.id))
                    if (auto* node = graph().getNodeForId(nodeWithUuid(graph(), uuid));
                        node != nullptr && dynamic_cast<LFOModule*>(node->getProcessor()) != nullptr)
                        return true;
        return false;
    }
};

TEST_F(DuplicateTrackLaneHeaderTest, ALaneOnTheTracksOwnInstrumentAndItsNewLfoAreCopiedWithTheTrack) {
    const auto setup = build(/*ownInstrument=*/true);
    ASSERT_TRUE(lfoInMacroNamed(setup.sourceName)) << "New LFO joins the macro of the knob it moves";
    duplicateLastTrack();

    const auto& copy = doc().getTracks().back();
    EXPECT_EQ(copy.lanes.size(), setup.lanesBefore) << "the Pulse Width lane and the amount lane both come along";
    bool pulseLane = false;
    for (const auto& lane : copy.lanes)
        pulseLane = pulseLane || lane.paramId == setup.paramId;
    EXPECT_TRUE(pulseLane);
    EXPECT_EQ(amountPointsOn(copy).size(), setup.amountPoints);
    EXPECT_EQ(lfoCount(), setup.lfosBefore + 1);
    EXPECT_TRUE(lfoInMacroNamed(setup.sourceName + " copy"));
}

// The starter patch is not part of the track: an LFO on its Oscillator lands loose (it has no macro to join), and a
// duplicate leaves that patch, its lane and its LFO with the original instead of cloning the shared starter patch.
TEST_F(DuplicateTrackLaneHeaderTest, ALaneOnTheFreeStandingStarterPatchStaysWithTheOriginalTrack) {
    const auto setup = build(/*ownInstrument=*/false);
    ASSERT_FALSE(lfoInMacroNamed(setup.sourceName)) << "the starter patch's Oscillator has no macro to join";
    duplicateLastTrack();

    const auto& copy = doc().getTracks().back();
    EXPECT_TRUE(copy.lanes.empty());
    EXPECT_EQ(doc().getTracks().size(), 2u);
    EXPECT_EQ(lfoCount(), setup.lfosBefore);
    EXPECT_EQ(doc().getTrack(setup.track)->lanes.size(), setup.lanesBefore);
}
