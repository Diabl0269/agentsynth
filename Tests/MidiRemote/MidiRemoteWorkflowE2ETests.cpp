// FRO138 (docs/control/midi-remote-ui.md#tests, the E2E bullet): one whole MIDI Remote workflow, headless, driven
// through the seams a user's hands and a controller's wire actually use -- never
// RemoteEngine::handleMessage()/applyEvent() directly:
//
//   * the right-click "MIDI Learn 'Cutoff'..." item on a REAL ModuleComponent slider (a real right-click through the
//     control's own mouseDown()/mouseUp() and the card's MouseListener half, same idiom as
//     Tests/UI/Graph/ModuleComponent/ModuleComponentMidiLearnTests.cpp), wired to a real MidiLearnController exactly
//     the way MainComponent::wireGraphEditorCallbacks() wires it;
//   * hardware messages entering through AudioEngine::handleIncomingMidiMessageFromSource (a fake device key -- there
//     is no real juce::MidiInput headlessly) and applied by RemoteEngine::drain() against a fake clock (setClock), so
//     kLearnSettleMs / kGestureIdleMs elapse without a single sleep;
//   * the undo step a swept parameter produces comes from the real ModuleComponent::parameterGestureChanged bracket
//     on the GraphEditor's own card, and the automation take from a real AutomationRecorder listening on the same
//     parameter -- the engine's begin/set/end gesture is indistinguishable from a mouse drag to both;
//   * save/reload through ProjectBundle into a SECOND, fresh engine + graph + controller (same profile directory, as
//     a relaunch would find it), and the orphan path through GraphEditor::requestDeleteModule +
//     RemoteEngine::reconcile.
//
// Two nearby seams stand in for things a headless run cannot reach (each says so where it is used):
//   * MidiLearnController::arm() re-registers the engine's sources from
//     AudioEngine::getOpenMidiInputIdentifiers() (empty with no real device), so after arming the test re-registers
//     its fake device key with RemoteEngine::setSources() -- exactly what AudioEngine::onMidiDevicesChanged ->
//     refreshSources() does when a real device opens.
//   * MainComponent::RemoteActionInvokerImpl is a private nested type (it needs a whole app's worth of UI
//     scaffolding), so the action target is checked at RemoteActionInvoker::invokeRemoteCommand with the real
//     AppCommands::getCommandForAction lookup; the verb itself is covered by
//     ShortcutManagerTransportActionsTests.cpp.
//
// Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "ShortcutManager/AppCommands.h"
#include "Timeline/AutomationRecorder.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/MidiRemote/Detect/DetectModeController.h"

#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kDevice = "fake-controller-id";
constexpr const char* kFilterUuid = "workflow-filter-uuid";
constexpr int kCc = 20;
constexpr int kPadNote = 36;
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

class CountingInvoker : public RemoteActionInvoker {
public:
    void invokeRemoteCommand(juce::CommandID commandId) override { invoked.push_back(commandId); }
    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID, NodeCommandKind) override {}
    // FRO236: this suite doesn't exercise continuous targets -- stub, never called.
    double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
    void setContinuousValue(ContinuousTargetKind, double) override {}
    bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }
    std::vector<juce::CommandID> invoked;
};

class CountingGestureListener : public juce::AudioProcessorParameter::Listener {
public:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { (starting ? starts : ends)++; }
    int starts = 0;
    int ends = 0;
};

// One app session's worth of MIDI Remote plumbing: a Hosted engine (never opens hardware -- see Source/CLAUDE.md), a
// real GraphEditor sharing the undo manager, and the real MidiLearnController over a caller-supplied profile directory
// (never the user's settings folder). Declaration order is load-bearing (members die in reverse): the engine and
// everything that reads the graph die before the undo stack that references it.
struct Rig {
    double nowMs = 0.0;
    RemoteEngine remote;
    synth::MidiRemoteProjectDoc doc;
    synth::TimelineDoc timeline;
    AppUndoManager undo;
    StatusBarComponent statusBar;
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    GraphEditor editor{engine, &undo};
    CountingInvoker invoker;
    MidiLearnController controller;
    juce::AudioProcessorGraph::Node::Ptr filter;

    explicit Rig(const juce::File& profileDir)
        : controller(engine, editor, remote, doc, undo, statusBar, synth::ControllerProfileStore(profileDir)) {
        remote.setClock([this] { return nowMs; });
        remote.setActionInvoker(&invoker);
        remote.setActionCommandLookup(
            [](const juce::String& actionId) { return AppCommands::getCommandForAction(actionId); });
        remote.setDefaultTakeover(Takeover::jump); // exact follow: the value maps straight onto the parameter
        // The same three callbacks MainComponent::wireGraphEditorCallbacks() wires.
        editor.onQueryMidiMappingsForNode = [this](juce::AudioProcessorGraph::NodeID id) {
            return controller.queryMappings(id);
        };
        editor.onMidiLearnRequested = [this](juce::AudioProcessorGraph::NodeID id, const juce::String& paramId) {
            controller.arm(id, paramId);
        };
        editor.onMidiForgetRequested = [this](juce::AudioProcessorGraph::NodeID id, const juce::String& paramId) {
            controller.forget(id, paramId);
        };
        engine.setRemoteMessageSink(&remote);
    }

    // `remote` outlives `engine` (declared first), so end any gesture a test left inside its idle
    // window while the parameter still exists -- RemoteEngine::endAllGestures()'s lifetime contract.
    ~Rig() {
        engine.setRemoteMessageSink(nullptr);
        remote.endAllGestures();
    }

    juce::AudioProcessorGraph::Node::Ptr addFilter(const juce::String& uuid) {
        auto node = engine.getGraph().addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        editor.updateComponents();
        return node;
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(filter->getProcessor(), "cutoff"); }

    ModuleComponent* card() {
        for (auto* c : editor.getModuleComponents())
            if (c->getModule() == filter->getProcessor())
                return c;
        return nullptr;
    }

    void advance(double ms) { nowMs += ms; }
    void hearDevice() { remote.setSources({juce::String(kDevice)}); }

    void send(const juce::MidiMessage& message) {
        engine.handleIncomingMidiMessageFromSource(kDevice, message);
        remote.drain();
    }
    void sendCc(int value) { send(juce::MidiMessage::controllerEvent(1, kCc, value)); }

    // Sweeps `values` a fifth of the idle window apart, so the whole run is one gesture.
    void sweep(std::initializer_list<int> values, const std::function<void(int)>& afterStep = {}) {
        for (const int v : values) {
            sendCc(v);
            advance(kGestureIdleMs / 5.0);
            if (afterStep)
                afterStep(v);
        }
    }
    void letGestureExpire() {
        advance(kGestureIdleMs + 1.0);
        remote.drain();
    }
};

juce::MouseEvent childMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// Right-clicks `child` and captures the PopupMenu the card builds via its setShowContextMenuHookForTest() seam. The
// control's own handler and the card's registered-MouseListener half are both driven -- see
// ModuleComponentMidiLearnTests.cpp's rightClickChild for why calling only the child would prove nothing.
juce::PopupMenu rightClick(ModuleComponent& card, juce::Component& child) {
    juce::PopupMenu captured;
    card.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const juce::ModifierKeys mods(juce::ModifierKeys::rightButtonModifier);
    const auto down = childMouseEvent(child, mods);
    child.mouseDown(down);
    card.mouseDown(down);
    child.mouseUp(childMouseEvent(child, mods));
    card.setShowContextMenuHookForTest(nullptr);
    return captured;
}

juce::Component* findByComponentID(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

bool menuContains(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == text)
            return true;
    return false;
}

bool invokeMenuItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().text == text && it.getItem().action) {
            it.getItem().action();
            return true;
        }
    }
    return false;
}

juce::PopupMenu rightClickCutoff(Rig& rig) {
    auto* card = rig.card();
    auto* slider = card != nullptr ? findByComponentID(*card, "Cutoff") : nullptr;
    if (slider == nullptr)
        return {};
    return rightClick(*card, *slider);
}

// Right-click Learn on the Filter cutoff, then sweep the device's CC (plus one stray CC, which the majority rule must
// ignore) and let the settle window elapse. True when the learn armed and settled.
bool learnCutoffByRightClick(Rig& rig) {
    if (!invokeMenuItem(rightClickCutoff(rig), "MIDI Learn 'Cutoff'..."))
        return false;
    if (!rig.controller.isArmed())
        return false;
    rig.hearDevice(); // see the file header: arm() re-registers sources from real devices only

    for (const int v : {10, 40, 70, 100, 127}) {
        rig.sendCc(v);
        rig.advance(20.0);
    }
    rig.send(juce::MidiMessage::controllerEvent(1, kCc + 1, 5));
    rig.advance(kLearnSettleMs + 1.0);
    rig.remote.drain();
    return !rig.controller.isArmed() && rig.doc.assignments.size() == 1;
}

const RemoteMappingSnapshot::Slot* findSlot(const RemoteEngine& engine, const juce::String& assignmentId) {
    const auto* snapshot = engine.publisher().live();
    if (snapshot == nullptr)
        return nullptr;
    for (const auto& slot : snapshot->slots)
        if (slot.assignmentId == assignmentId)
            return &slot;
    return nullptr;
}

juce::RangedAudioParameter* cutoffOfNode(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return findParameterByID(node->getProcessor(), "cutoff");
    return nullptr;
}

// A bare transport ticked the way AudioEngine::renderPass ticks it, plus a real AutomationRecorder attached to the
// rig's timeline and undo stack with one Touch lane on the Filter cutoff -- Tests/Timeline/AutomationRecordTests.cpp's
// recipe, wired to the rig's engine the way MainComponent::wireMidiRemoteEngine() wires the claim predicate. Declared
// AFTER the rig in a test body so it is destroyed first (its destructor removes its parameter listener).
struct TouchTake {
    synth::TransportService transport;
    AutomationRecorder recorder;
    synth::LaneId lane;

    explicit TouchTake(Rig& rig) {
        transport.prepare(kSampleRate, kBlock);
        recorder.attachTo(rig.timeline, rig.undo, transport);
        auto* param = rig.cutoff();
        AutomationLane::RangeSnapshot range;
        range.minValue = param->getNormalisableRange().start;
        range.maxValue = param->getNormalisableRange().end;
        range.defaultValue = param->convertFrom0to1(param->getDefaultValue());
        lane = rig.timeline.addLane(rig.timeline.addTrack(TrackKind::Midi, "Track 1"), kFilterUuid, "cutoff", range);
        rig.timeline.setLaneRecordMode(lane, static_cast<int>(LaneRecordMode::Touch));
        recorder.bindLane(lane, param, rig.filter);
        recorder.setGlobalRecordEnable(true);
        rig.remote.setParameterClaimedPredicate(
            [this](const juce::AudioProcessorParameter* p) { return recorder.getAudioState().claims.isClaimed(p); });
        transport.play();
        transport.tick(0);
        recorder.update();
    }

    // A quarter beat of playback (120 BPM at 48 kHz: 24000 samples per beat), published like the next real callback.
    void step() {
        transport.tick(kBlock * 12);
        transport.tick(0);
        recorder.update();
    }
};

} // namespace

class MidiRemoteWorkflowE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midiremote-workflow-e2e-" + juce::Uuid().toString());
        root_.deleteRecursively();
        profileDir_ = root_.getChildFile("controllers");
        bundleDir_ = root_.getChildFile("Workflow" + juce::String(ProjectBundle::kBundleExtension));
    }
    void TearDown() override { root_.deleteRecursively(); }

    // A rig with the Filter node in it, ready for a right-click learn.
    std::unique_ptr<Rig> makeRigWithFilter() {
        auto rig = std::make_unique<Rig>(profileDir_);
        rig->filter = rig->addFilter(kFilterUuid);
        return rig;
    }

    juce::File root_, profileDir_, bundleDir_;
};

// ============================================================================
// 1. Right-click Learn -> the CC that was swept -> an assignment and an auto-created profile
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, RightClickLearnSettlesOnTheSweptCcAndCreatesTheAssignmentAndAutoProfile) {
    auto rig = makeRigWithFilter();
    const auto unmapped = rightClickCutoff(*rig);
    ASSERT_TRUE(menuContains(unmapped, "MIDI Learn 'Cutoff'..."));
    ASSERT_FALSE(menuContains(unmapped, "Forget MIDI"));

    ASSERT_TRUE(learnCutoffByRightClick(*rig));

    ASSERT_EQ(rig->doc.assignments.size(), 1u);
    const auto& a = rig->doc.assignments[0];
    EXPECT_EQ(a.spec.type, MessageType::cc);
    EXPECT_EQ(a.spec.number, kCc) << "the swept CC wins the majority over the one stray CC";
    EXPECT_EQ(a.target.parameter.paramId, "cutoff");
    EXPECT_EQ(a.target.parameter.nodeUuid, juce::String(kFilterUuid));

    ASSERT_EQ(rig->controller.getProfiles().size(), 1u) << "an unknown device auto-creates its profile";
    const auto& profile = rig->controller.getProfiles()[0];
    EXPECT_EQ(profile.input.identifier, juce::String(kDevice));
    EXPECT_EQ(profile.controls.size(), 1u);
    EXPECT_EQ(synth::ControllerProfileStore(profileDir_).loadAll().profiles.size(), 1u) << "and persists it";

    const auto mapped = rightClickCutoff(*rig);
    EXPECT_TRUE(menuContains(mapped, "MIDI: CC 20 on " + juce::String(kDevice)));
    EXPECT_TRUE(menuContains(mapped, "Forget MIDI"));
}

// ============================================================================
// 2. The sweep drives the parameter as ONE gesture
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, SweepMovesTheCutoffAsOneGesturePair) {
    auto rig = makeRigWithFilter();
    ASSERT_TRUE(learnCutoffByRightClick(*rig));
    CountingGestureListener listener;
    rig->cutoff()->addListener(&listener);

    rig->sweep({10, 30, 50, 70, 90, 110, 127}, [&](int v) {
        EXPECT_NEAR(rig->cutoff()->getValue(), static_cast<float>(v) / 127.0f, 1.0e-3f)
            << "the parameter follows CC " << v;
    });
    EXPECT_EQ(listener.starts, 1);
    EXPECT_EQ(listener.ends, 0) << "still inside kGestureIdleMs of the last value";

    rig->letGestureExpire();
    EXPECT_EQ(listener.starts, 1);
    EXPECT_EQ(listener.ends, 1) << "the gesture ends once, kGestureIdleMs after the last value";
    EXPECT_EQ(rig->remote.activeGestureCount(), 0);
    rig->cutoff()->removeListener(&listener);
}

// ============================================================================
// 3. Touch automation records the swept parameter
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, TouchRecorderCapturesTheSweptParameterAsALaneTake) {
    auto rig = makeRigWithFilter();
    ASSERT_TRUE(learnCutoffByRightClick(*rig));
    const float before = rig->cutoff()->getValue();
    TouchTake take(*rig);

    rig->sweep({10, 40, 70, 100, 127}, [&](int) { take.step(); });
    EXPECT_TRUE(take.recorder.getAudioState().claims.isClaimed(rig->cutoff())) << "the recorder holds the gesture";
    rig->letGestureExpire();
    EXPECT_FALSE(take.recorder.getAudioState().claims.isClaimed(rig->cutoff())) << "and lets go when it ends";

    const auto* lane = rig->timeline.getLane(take.lane);
    ASSERT_NE(lane, nullptr);
    ASSERT_GE(lane->points.size(), 2u) << "a Touch gesture end commits the take to the lane";
    const double finalHz = rig->cutoff()->convertFrom0to1(rig->cutoff()->getValue());
    EXPECT_NEAR(lane->points.back().value, finalHz, finalHz * 0.005) << "the take ends at the value the sweep ended on";
    EXPECT_GT(lane->points.back().beat, lane->points.front().beat);

    // An armed Touch sweep costs two steps on the shared stack -- the parameter value (the ModuleComponent gesture
    // bracket) and the take (the recorder's own commit), exactly as a mouse drag over an armed lane does. Two Cmd+Z
    // take back the sweep and leave the learn.
    ASSERT_TRUE(rig->undo.undo());
    ASSERT_TRUE(rig->undo.undo());
    EXPECT_TRUE(rig->timeline.getLane(take.lane)->points.empty()) << "undoing the take restores the lane";
    EXPECT_NEAR(rig->cutoff()->getValue(), before, 1.0e-4f) << "and the value";
    EXPECT_TRUE(take.recorder.isGlobalRecordEnabled()) << "never the arming";
    EXPECT_EQ(rig->doc.assignments.size(), 1u);
}

// ============================================================================
// 4. Undo reverts the whole sweep in one step
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, UndoRevertsTheWholeSweepInOneStep) {
    auto rig = makeRigWithFilter();
    ASSERT_TRUE(learnCutoffByRightClick(*rig));
    const float before = rig->cutoff()->getValue();

    rig->sweep({10, 30, 50, 70, 90, 110, 127});
    rig->letGestureExpire();
    ASSERT_NEAR(rig->cutoff()->getValue(), 1.0f, 1.0e-3f);

    ASSERT_TRUE(rig->undo.undo());
    EXPECT_NEAR(rig->cutoff()->getValue(), before, 1.0e-4f)
        << "seven CC values, one Cmd+Z, back to the pre-sweep value";
    EXPECT_EQ(rig->doc.assignments.size(), 1u) << "and only the sweep was undone -- the learn is the step below it";
    EXPECT_TRUE(rig->undo.canUndo());

    ASSERT_TRUE(rig->undo.undo());
    EXPECT_TRUE(rig->doc.assignments.empty()) << "the next Cmd+Z removes the assignment (the profile stays)";
    EXPECT_EQ(rig->controller.getProfiles().size(), 1u);
}

// ============================================================================
// 5. Save, reload into a fresh session: the assignment resolves and still drives the parameter
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, SaveAndReloadIntoAFreshSessionResolvesTheAssignmentAndStillDrivesTheParameter) {
    juce::String assignmentId;
    {
        auto rig = makeRigWithFilter();
        ASSERT_TRUE(learnCutoffByRightClick(*rig));
        assignmentId = rig->doc.assignments[0].id;
        const auto saved = ProjectBundle::save(bundleDir_, rig->engine.getGraph(), rig->timeline,
                                               rig->editor.getPatchDocument(), rig->editor.getMacros(), rig->doc);
        ASSERT_TRUE(saved.ok) << saved.message;
    }

    // A second session, constructed after the first learn so its controller loads the persisted profile like a
    // relaunch.
    Rig fresh(profileDir_);
    fresh.remote.setProfiles(fresh.controller.getProfiles()); // wireMidiRemoteEngine() primes these once at launch
    fresh.editor.detachAllModuleComponents(); // what MainComponent::loadBundleFromFile does before the load
    const auto loaded = ProjectBundle::load(bundleDir_, fresh.engine.getGraph(), fresh.timeline,
                                            fresh.editor.getPatchDocument(), fresh.editor.getMacros(), fresh.doc);
    ASSERT_TRUE(loaded.ok) << loaded.message;
    fresh.editor.updateComponents();
    fresh.controller.publishAssignments(); // the same call MainComponent makes right after a load
    fresh.hearDevice();

    ASSERT_EQ(fresh.doc.assignments.size(), 1u);
    EXPECT_EQ(fresh.doc.assignments[0].id, assignmentId);
    auto* cutoff = cutoffOfNode(fresh.engine.getGraph(), kFilterUuid);
    ASSERT_NE(cutoff, nullptr);
    const auto* slot = findSlot(fresh.remote, assignmentId);
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->orphaned) << "the reloaded assignment resolves";
    EXPECT_EQ(slot->param, cutoff);

    cutoff->setValueNotifyingHost(0.9f);
    fresh.sendCc(32);
    EXPECT_NEAR(cutoff->getValue(), 32.0f / 127.0f, 1.0e-3f) << "and still drives the same parameter";
    fresh.letGestureExpire();
}

// ============================================================================
// 6. Delete the node: the assignment is orphaned (never rebound), then Forget
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, DeletingTheNodeOrphansTheAssignmentAndNeverRebindsThenForgetRemovesIt) {
    auto rig = makeRigWithFilter();
    ASSERT_TRUE(learnCutoffByRightClick(*rig));
    const juce::String assignmentId = rig->doc.assignments[0].id;
    ASSERT_NE(findSlot(rig->remote, assignmentId), nullptr);
    ASSERT_NE(findSlot(rig->remote, assignmentId)->param, nullptr) << "sanity: resolved before the delete";

    rig->editor.requestDeleteModule(rig->filter->nodeID);
    rig->filter = nullptr;
    rig->remote.reconcile(rig->engine.getGraph());

    // A deleted node leaves the slot unresolved (Slot::orphaned is the hosted-plugin-drift flag; the panel shows this
    // state as "(missing module)", see OrphanControllerTests.cpp).
    const auto* orphan = findSlot(rig->remote, assignmentId);
    ASSERT_NE(orphan, nullptr);
    EXPECT_EQ(orphan->param, nullptr);
    ASSERT_EQ(rig->doc.assignments.size(), 1u) << "an orphan stays in the project until the user forgets it";

    // A NEW filter with its own uuid must not silently inherit the orphan's mapping.
    rig->filter = rig->addFilter("another-filter-uuid");
    rig->remote.reconcile(rig->engine.getGraph());
    EXPECT_EQ(findSlot(rig->remote, assignmentId)->param, nullptr) << "never silently rebound";
    rig->cutoff()->setValueNotifyingHost(0.9f);
    rig->sendCc(64);
    EXPECT_NEAR(rig->cutoff()->getValue(), 0.9f, 1.0e-6f) << "an orphan is consumed but moves nothing";
    EXPECT_EQ(rig->remote.activeGestureCount(), 0);
    EXPECT_TRUE(rig->controller.queryMappings(rig->filter->nodeID).empty());

    // Forget by assignment id: the panel Inspector's Forget button forwards here, and it is the orphan's only action.
    ASSERT_TRUE(rig->controller.forgetAssignment(assignmentId));
    EXPECT_TRUE(rig->doc.assignments.empty());
    EXPECT_EQ(findSlot(rig->remote, assignmentId), nullptr);
    EXPECT_EQ(rig->statusBar.getTransientMessageForTest(), "MIDI mapping removed");
}

// ============================================================================
// 7. A transport pad fires the toggle play/stop command
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, TransportPadInvokesTheTogglePlayStopCommandOnPressOnly) {
    Rig rig(profileDir_);
    rig.controller.armAction("transportTogglePlayStop");
    rig.hearDevice();
    rig.send(juce::MidiMessage::noteOn(1, kPadNote, (juce::uint8)100));
    rig.advance(20.0);
    rig.send(juce::MidiMessage::noteOff(1, kPadNote));
    rig.advance(kLearnSettleMs + 1.0);
    rig.remote.drain();

    ASSERT_FALSE(rig.controller.isArmed());
    ASSERT_EQ(rig.controller.getProfiles().size(), 1u);
    ASSERT_EQ(rig.controller.getProfiles()[0].actions.size(), 1u) << "an action assignment lives on the profile";
    EXPECT_EQ(rig.controller.getProfiles()[0].actions[0].target.action.actionId, "transportTogglePlayStop");
    EXPECT_TRUE(rig.doc.assignments.empty()) << "and never in the project";
    EXPECT_TRUE(rig.invoker.invoked.empty()) << "the learning press is consumed by the learn, it does not fire";

    rig.send(juce::MidiMessage::noteOn(1, kPadNote, (juce::uint8)100));
    rig.send(juce::MidiMessage::noteOff(1, kPadNote));
    EXPECT_EQ(rig.invoker.invoked, std::vector<juce::CommandID>{AppCommands::togglePlayback})
        << "one press, one command; the release does not fire";

    rig.send(juce::MidiMessage::noteOn(1, kPadNote, (juce::uint8)100));
    EXPECT_EQ(rig.invoker.invoked.size(), 2u);
}

// ============================================================================
// 8. Hosted mode: the source is "Host MIDI", fed through processHostBlock's MIDI path
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, HostedModeLearnsAndDrivesTheCutoffFromTheHostsMidiBlock) {
    auto rig = makeRigWithFilter();
    rig->engine.prepareForHost(kSampleRate, kBlock, 2, 2);

    // One host block carrying `value` on the learned CC; returns how many MIDI events the block still holds afterwards.
    const auto hostBlock = [&](int value) {
        juce::AudioBuffer<float> audio(2, kBlock);
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, kCc, value), 0);
        rig->engine.processHostBlock(audio, midi);
        rig->remote.drain();
        return midi.getNumEvents();
    };

    rig->controller.arm(rig->filter->nodeID, "cutoff"); // Hosted: refreshSources() registers hostSourceKey()
    for (const int v : {10, 40, 70, 100, 127}) {
        hostBlock(v);
        rig->advance(20.0);
    }
    rig->advance(kLearnSettleMs + 1.0);
    rig->remote.drain();

    ASSERT_EQ(rig->doc.assignments.size(), 1u);
    ASSERT_EQ(rig->controller.getProfiles().size(), 1u);
    EXPECT_EQ(rig->controller.getProfiles()[0].input.identifier, hostSourceKey());
    EXPECT_EQ(rig->controller.getProfiles()[0].name, "Host MIDI");
    EXPECT_EQ(hostSourceKey(), juce::String("host"));

    rig->cutoff()->setValueNotifyingHost(0.9f);
    EXPECT_EQ(hostBlock(64), 0) << "a mapped message is consumed, it never reaches the graph as MIDI";
    EXPECT_NEAR(rig->cutoff()->getValue(), 64.0f / 127.0f, 1.0e-3f);
    rig->letGestureExpire();
    rig->engine.releaseFromHost();
}

// ============================================================================
// 9. FRO140: a 14-bit knob -- Detect adds ONE control, assigning it drives the parameter at full resolution
// ============================================================================

TEST_F(MidiRemoteWorkflowE2ETest, FourteenBitKnobIsDetectedAsOneControlAndSweepsTheCutoffWithoutStairSteps) {
    auto rig = makeRigWithFilter();

    ControllerProfile profile;
    profile.id = "profile-14bit";
    profile.name = "Fader box";
    profile.input.identifier = kDevice;
    profile.input.name = kDevice;
    ASSERT_TRUE(rig->controller.addProfile(profile));
    rig->hearDevice();

    // Touch the knob: an MSB then its LSB, unmapped, so both surface as plain activity events for Detect.
    rig->engine.handleIncomingMidiMessageFromSource(kDevice, juce::MidiMessage::controllerEvent(1, kCc, 64));
    rig->engine.handleIncomingMidiMessageFromSource(kDevice, juce::MidiMessage::controllerEvent(1, kCc + 32, 0));
    std::vector<RemoteEvent> touched;
    rig->remote.drainActivity([&](const juce::String&, const RemoteEvent& e) { touched.push_back(e); });
    ASSERT_EQ(touched.size(), 2u);
    // The stamps are real (RemoteEngineTwoHalfDecodeTests asserts they are set); the gap is pinned here so a
    // preempted CI runner cannot turn the 5 ms pairing window into a flake.
    touched[1].timeMs = static_cast<std::uint16_t>(touched[0].timeMs + 2);

    synth::ui::DetectModeController detect;
    detect.setActive(true);
    ControllerProfile working = rig->controller.getProfiles().front();
    for (const auto& e : touched)
        detect.handleEvent(working, e);

    ASSERT_EQ(working.controls.size(), 1u) << "both halves of one knob are ONE control";
    EXPECT_EQ(working.controls[0].encoding, Encoding::abs14);
    EXPECT_EQ(working.controls[0].message.number, kCc);
    ASSERT_TRUE(rig->controller.updateProfile(working));

    ASSERT_EQ(rig->controller.assignControl(profile.id, working.controls[0].id,
                                            PickTarget::parameter(rig->filter->nodeID, "cutoff")),
              AssignStatus::assigned);
    rig->hearDevice(); // the controller re-registers real devices only; see the file header
    ASSERT_EQ(rig->doc.assignments.size(), 1u);
    EXPECT_EQ(rig->doc.assignments[0].specEncoding, Encoding::abs14);

    // A slow sweep of the fine byte under one coarse value: every LSB step is its own 14-bit position, so the
    // parameter climbs in ~1/16383 steps instead of holding still (a 7-bit reading would ignore the LSB).
    rig->send(juce::MidiMessage::controllerEvent(1, kCc, 64));
    float previous = -1.0f;
    for (int lsb = 0; lsb < 128; ++lsb) {
        rig->send(juce::MidiMessage::controllerEvent(1, kCc + 32, lsb));
        const float expected = static_cast<float>((64 << 7) | lsb) / 16383.0f;
        EXPECT_NEAR(rig->cutoff()->getValue(), expected, 1.0e-4f) << "LSB " << lsb;
        if (previous >= 0.0f)
            EXPECT_GT(rig->cutoff()->getValue(), previous) << "no stair-step at LSB " << lsb;
        previous = rig->cutoff()->getValue();
        rig->advance(kGestureIdleMs / 10.0);
    }
    rig->letGestureExpire();
}
