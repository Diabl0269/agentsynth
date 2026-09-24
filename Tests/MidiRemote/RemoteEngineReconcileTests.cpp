// Message thread: RemoteEngine::reconcile()/rebuildAndPublish (RemoteEngineReconcile.cpp) --
// re-resolving targets against a changing graph, the "a setter is not a graph change" rule
// (docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step), and the
// lookup table's duplicate-key tie-break. Every test inspects the published RemoteMappingSnapshot directly through
// RemoteEngine::publisher() (exposed for tests in the "Diagnostics" section of RemoteEngine.h) rather than inferring
// resolution status only from side effects, since that is the most direct way to pin exactly what reconcile() resolved.
// Suite names contain "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginModule.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";
constexpr const char* kNodeUuid = "filter-node-uuid";

Control makeControl(const juce::String& id, MessageType type, int channel, int number, Encoding encoding) {
    Control c;
    c.id = id;
    c.name = id;
    c.kind = ControlKind::knob;
    c.message.type = type;
    c.message.channel = channel;
    c.message.number = number;
    c.encoding = encoding;
    return c;
}

ControllerProfile makeProfile(std::vector<Control> controls) {
    ControllerProfile profile;
    profile.id = "profile";
    profile.name = "profile";
    profile.input.identifier = kSource;
    profile.input.name = kSource;
    profile.controls = std::move(controls);
    return profile;
}

Assignment makeParamAssignment(const juce::String& id, const juce::String& controlId, MessageType type, int channel,
                               int number, Encoding encoding, const juce::String& paramId,
                               Takeover takeover = Takeover::jump) {
    Assignment a;
    a.id = id;
    a.control.profileId = "profile";
    a.control.controlId = controlId;
    a.spec.type = type;
    a.spec.channel = channel;
    a.spec.number = number;
    a.specEncoding = encoding;
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = juce::String(kNodeUuid);
    a.target.parameter.paramId = paramId;
    a.takeover = takeover;
    return a;
}

// FRO253 (docs/control/midi-remote.md#node-command-targets).
Assignment makeNodeCommandAssignment(const juce::String& id, const juce::String& controlId, MessageType type,
                                     int channel, int number, Encoding encoding, const juce::String& nodeUuid) {
    Assignment a;
    a.id = id;
    a.control.profileId = "profile";
    a.control.controlId = controlId;
    a.spec.type = type;
    a.spec.channel = channel;
    a.spec.number = number;
    a.specEncoding = encoding;
    a.target.kind = Target::Kind::nodeCommand;
    a.target.nodeCommand.nodeUuid = nodeUuid;
    a.target.nodeCommand.command = NodeCommandKind::toggleSolo;
    return a;
}

// FRO236 (docs/control/midi-remote.md#continuous-targets): mirrors makeNodeCommandAssignment above.
Assignment makeContinuousAssignment(const juce::String& id, const juce::String& controlId, MessageType type,
                                    int channel, int number, Encoding encoding, ContinuousTargetKind kind) {
    Assignment a;
    a.id = id;
    a.control.profileId = "profile";
    a.control.controlId = controlId;
    a.spec.type = type;
    a.spec.channel = channel;
    a.spec.number = number;
    a.specEncoding = encoding;
    a.target.kind = Target::Kind::continuous;
    a.target.continuous.kind = kind;
    return a;
}

// Mirrors ModuleBase::setNodeUuid + node->properties.set("uuid", ...), exactly as
// RemoteEngineReconcile.cpp's buildProcessorByUuid expects (it reads node->properties["uuid"]).
void setUuid(juce::AudioProcessorGraph::Node& node, const juce::String& uuid) {
    node.properties.set("uuid", uuid);
    if (auto* mb = dynamic_cast<ModuleBase*>(node.getProcessor()))
        mb->setNodeUuid(uuid);
}

const RemoteMappingSnapshot::Slot* findSlotByAssignmentId(const RemoteEngine& engine, const juce::String& id) {
    const auto* snapshot = engine.publisher().live();
    if (snapshot == nullptr)
        return nullptr;
    for (const auto& slot : snapshot->slots)
        if (slot.assignmentId == id)
            return &slot;
    return nullptr;
}

} // namespace

// ============================================================================
// Deleting the target node orphans the slot; the parameter is never silently rebound
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, DeletedNodeOrphansTheSlotAndNeverRebinds) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);
    const auto nodeId = node->nodeID;

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({makeParamAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});
    engine.reconcile(graph);

    const auto* resolvedSlot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(resolvedSlot, nullptr);
    ASSERT_NE(resolvedSlot->param, nullptr) << "sanity: it must resolve before the node is deleted";

    EXPECT_NE(graph.removeNode(nodeId), nullptr);
    engine.reconcile(graph);

    const auto* orphanedSlot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(orphanedSlot, nullptr);
    EXPECT_EQ(orphanedSlot->param, nullptr) << "the deleted node's parameter must never be silently rebound";

    // Further messages are still consumed (the lookup entry survives a resolution failure) but
    // must move nothing, safely -- applyToParameter's early return on slot.param == nullptr.
    EXPECT_TRUE(engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(1, 10, 127)));
    engine.drain();
    EXPECT_EQ(engine.activeGestureCount(), 0);
}

// ============================================================================
// Re-adding a node with the SAME uuid lets it resolve again on the next reconcile
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, ReaddingANodeWithTheSameUuidResolvesAgain) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({makeParamAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});
    engine.reconcile(graph);
    ASSERT_NE(findSlotByAssignmentId(engine, "a1")->param, nullptr);

    EXPECT_NE(graph.removeNode(node->nodeID), nullptr);
    engine.reconcile(graph);
    ASSERT_EQ(findSlotByAssignmentId(engine, "a1")->param, nullptr) << "sanity: it must have orphaned first";

    auto node2 = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node2, kNodeUuid); // the SAME uuid, on a brand new node/processor
    engine.reconcile(graph);

    const auto* resolvedAgain = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(resolvedAgain, nullptr);
    ASSERT_NE(resolvedAgain->param, nullptr);
    EXPECT_EQ(resolvedAgain->param, findParameterByID(node2->getProcessor(), "cutoff"))
        << "it must resolve against the NEW node's own parameter object, not the old (destroyed) one";
}

// ============================================================================
// A setter alone (setAssignments/setProfiles) is NOT a graph change
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, SetterAloneKeepsTheAlreadyResolvedTargetForAnUnchangedAssignment) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);
    auto* cutoff = findParameterByID(node->getProcessor(), "cutoff");
    cutoff->setValueNotifyingHost(0.5f);

    RemoteEngine engine;
    const Assignment a1 = makeParamAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, "cutoff");
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({a1});
    engine.reconcile(graph);
    ASSERT_NE(findSlotByAssignmentId(engine, "a1")->param, nullptr);

    // setAssignments with the EXACT SAME content, and no intervening reconcile(). A setter passes
    // graph == nullptr to rebuildAndPublish, which must carry the previous resolution forward for
    // an assignment id it already knew about.
    engine.setAssignments({a1});

    const auto* stillResolved = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(stillResolved, nullptr);
    EXPECT_NE(stillResolved->param, nullptr) << "a setter must not un-resolve an unchanged assignment";

    // And the resolution isn't just non-null -- a sweep still actually moves the parameter, with no
    // reconcile() call in between the two setAssignments() calls.
    EXPECT_TRUE(engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(1, 10, 0)));
    engine.drain();
    EXPECT_NEAR(cutoff->getValue(), 0.0f, 1e-5f) << "the sweep must still work after a setter-only republish";
}

// ============================================================================
// A hosted-plugin target with no live instance orphans -- it never rebinds either
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, HostedPluginWithNoLiveInstanceOrphansRatherThanRebinding) {
    constexpr const char* kPluginUuid = "hosted-plugin-node-uuid";

    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<HostedPluginModule>());
    setUuid(*node, kPluginUuid);
    ASSERT_FALSE(dynamic_cast<HostedPluginModule*>(node->getProcessor())->hasInstance())
        << "sanity: a freshly constructed HostedPluginModule has no plugin loaded";

    RemoteEngine engine;
    Assignment a1 = makeParamAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, "gain");
    a1.target.parameter.nodeUuid = kPluginUuid;
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({a1});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->orphaned) << "a live HostedPluginModule node with no instance must ORPHAN -- it cannot "
                                   "currently vouch for any parameter (Source/Timeline/AutomationBinding.h)";
    EXPECT_EQ(slot->param, nullptr);

    // Still consumed (the lookup entry doesn't care whether the target resolved), still a safe no-op.
    EXPECT_TRUE(engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(1, 10, 127)));
    engine.drain();
    EXPECT_EQ(engine.activeGestureCount(), 0);
}

// ============================================================================
// Duplicate packed message key: only the first-inserted control gets a lookup entry
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, DuplicateMessageKeyLeavesTheSecondControlInert) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);
    auto* cutoff = findParameterByID(node->getProcessor(), "cutoff");
    auto* resonance = findParameterByID(node->getProcessor(), "resonance");
    cutoff->setValueNotifyingHost(0.5f);
    resonance->setValueNotifyingHost(0.5f);

    RemoteEngine engine;
    // Both controls share the EXACT same MessageSpec (cc, ch1, num10). A real ControllerProfile
    // would refuse this at the model layer (ControllerProfile::fromVar rejects two controls
    // sharing a spec -- see RemoteModelTests.cpp), but the engine's own table-building must still
    // degrade safely -- a duplicate key would otherwise break the lookup table's binary search.
    const Assignment first = makeParamAssignment("a-first", "c1", MessageType::cc, 1, 10, Encoding::abs7, "cutoff");
    const Assignment second =
        makeParamAssignment("a-second", "c2", MessageType::cc, 1, 10, Encoding::abs7, "resonance");
    engine.setProfiles({makeProfile({makeControl("c1", MessageType::cc, 1, 10, Encoding::abs7),
                                     makeControl("c2", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({first, second});
    engine.reconcile(graph);

    // Both slots exist and both resolved -- the duplication is only in the LOOKUP table, never in
    // the slot list itself.
    ASSERT_NE(findSlotByAssignmentId(engine, "a-first")->param, nullptr);
    ASSERT_NE(findSlotByAssignmentId(engine, "a-second")->param, nullptr);

    engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(1, 10, 0));
    engine.drain();

    EXPECT_NEAR(cutoff->getValue(), 0.0f, 1e-5f) << "the first-inserted control wins the lookup slot";
    EXPECT_NEAR(resonance->getValue(), 0.5f, 1e-6f) << "the second, colliding control must stay completely inert";
}

// ============================================================================
// FRO253: node command targets resolve to a NodeID, orphan/rebind exactly like a parameter target
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, NodeCommandResolvesTheNodeIdForAnExistingUuid) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({makeNodeCommandAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, kNodeUuid)});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->orphaned);
    EXPECT_EQ(slot->nodeId, node->nodeID);
    EXPECT_TRUE(slot->buttonLike)
        << "every node command is button-like (docs/control/midi-remote.md#node-command-targets)";
}

TEST(MidiRemoteEngineReconcileTest, NodeCommandOrphansForAMissingUuid) {
    juce::AudioProcessorGraph graph; // no node with this uuid at all

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments(
        {makeNodeCommandAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, "no-such-node-uuid")});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->orphaned);
    EXPECT_EQ(slot->nodeId, juce::AudioProcessorGraph::NodeID{});
}

TEST(MidiRemoteEngineReconcileTest, NodeCommandSetterAloneKeepsThePreviousResolution) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);

    RemoteEngine engine;
    const Assignment a1 = makeNodeCommandAssignment("a1", "c", MessageType::cc, 1, 10, Encoding::abs7, kNodeUuid);
    engine.setProfiles({makeProfile({makeControl("c", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({a1});
    engine.reconcile(graph);
    ASSERT_FALSE(findSlotByAssignmentId(engine, "a1")->orphaned);
    ASSERT_EQ(findSlotByAssignmentId(engine, "a1")->nodeId, node->nodeID);

    // setAssignments with graph == nullptr must NOT re-resolve -- same "a setter is not a graph
    // change" rule resolveParameterTarget's own setter branch follows.
    engine.setAssignments({a1});

    const auto* stillResolved = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(stillResolved, nullptr);
    EXPECT_FALSE(stillResolved->orphaned);
    EXPECT_EQ(stillResolved->nodeId, node->nodeID);
}

// ============================================================================
// Continuous targets (FRO236, docs/control/midi-remote.md#continuous-targets)
// ============================================================================

TEST(MidiRemoteEngineReconcileTest, ContinuousSlotOnAKnobIsNotButtonLike) {
    juce::AudioProcessorGraph graph;

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments(
        {makeContinuousAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, ContinuousTargetKind::bpm)});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->buttonLike) << "a continuous target on a knob control must not be buttonLike, unlike an "
                                      "action/nodeCommand target";
    EXPECT_FALSE(slot->orphaned) << "bpm is never orphaned -- there is nothing to resolve against a graph";
    EXPECT_EQ(slot->param, nullptr);
}

TEST(MidiRemoteEngineReconcileTest, PlayheadIsNeverOrphanedAndHasNoParam) {
    juce::AudioProcessorGraph graph; // empty -- there is nothing playhead could resolve against anyway

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({makeContinuousAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7,
                                                    ContinuousTargetKind::playhead)});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->orphaned);
    EXPECT_EQ(slot->param, nullptr);
    EXPECT_EQ(slot->continuous, ContinuousTargetKind::playhead);
}

TEST(MidiRemoteEngineReconcileTest, MasterVolumeResolvesThroughTheInjectedLookup) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(std::make_unique<FilterModule>());
    setUuid(*node, kNodeUuid);
    auto* cutoff = findParameterByID(node->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);

    RemoteEngine engine;
    // Stands in for MainComponentSetup.cpp's real lookup (Master node -> its "gain" param) -- what
    // it resolves through doesn't matter to RemoteEngine, only that it IS a real parameter.
    engine.setContinuousParameterLookup(
        [cutoff](juce::AudioProcessorGraph&, ContinuousTargetKind kind) -> juce::AudioProcessorParameter* {
            return kind == ContinuousTargetKind::masterVolume ? cutoff : nullptr;
        });
    engine.setProfiles({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({makeContinuousAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7,
                                                    ContinuousTargetKind::masterVolume)});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->orphaned);
    EXPECT_EQ(slot->param, cutoff);
}

TEST(MidiRemoteEngineReconcileTest, MasterVolumeOrphansWhenTheLookupResolvesNothing) {
    juce::AudioProcessorGraph graph; // no lookup installed at all -- resolves to nullptr always

    RemoteEngine engine;
    engine.setProfiles({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)})});
    engine.setSources({juce::String(kSource)});
    engine.setAssignments({makeContinuousAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7,
                                                    ContinuousTargetKind::masterVolume)});
    engine.reconcile(graph);

    const auto* slot = findSlotByAssignmentId(engine, "a1");
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->orphaned);
    EXPECT_EQ(slot->param, nullptr);
}
