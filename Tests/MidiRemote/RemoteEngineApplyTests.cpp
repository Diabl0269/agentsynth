// Message thread: RemoteEngine::drain()'s apply path (RemoteEngineApply.cpp) -- gestures,
// takeover, range mapping, claims, orphaned targets, the generation guard, and action targets
// (docs/midi_remote.md §4.2, §4.6, §4.9). Every test drives a real juce::AudioProcessorGraph node
// (FilterModule -- a "cutoff" float parameter for the continuous-value tests, and the "bypassed"
// bool parameter every ModuleBase module carries for the button tests) so applying really moves a
// real juce::AudioProcessorParameter through beginChangeGesture/setValueNotifyingHost/
// endChangeGesture, exactly as a mouse would. rebuildAndPublish() arms the drain timer
// (startTimerHz(kDrainHz)) whenever slots exist, but nothing here pumps a message loop, so it
// never actually fires -- every test calls RemoteEngine::drain() directly against a fake clock
// (setClock), per the ticket's "never sleep or poll for time" rule. Suite names contain
// "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";
constexpr const char* kNodeUuid = "filter-node-uuid";

Control makeControl(const juce::String& id, MessageType type, int channel, int number, Encoding encoding,
                    ControlKind kind = ControlKind::knob) {
    Control c;
    c.id = id;
    c.name = id;
    c.kind = kind;
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
                               Takeover takeover = Takeover::jump, double rangeMin = 0.0, double rangeMax = 1.0) {
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
    a.range.min = rangeMin;
    a.range.max = rangeMax;
    return a;
}

// One real AudioProcessorGraph + one real FilterModule node, uuid'd the way the repo does it
// (mirrors ModuleBase::setNodeUuid + node->properties.set("uuid", ...) -- see
// RemoteEngineReconcile.cpp's buildProcessorByUuid), plus a RemoteEngine on a fake clock.
struct ApplyHarness {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr node;
    RemoteEngine engine;
    double fakeNowMs = 0.0;

    ApplyHarness() {
        node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", juce::String(kNodeUuid));
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            mb->setNodeUuid(kNodeUuid);
        engine.setClock([this] { return fakeNowMs; });
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(node->getProcessor(), "cutoff"); }
    juce::AudioParameterBool* bypassed() {
        return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(node->getProcessor(), "bypassed"));
    }

    // setProfiles/setSources/setAssignments (in the order RemoteEngineThreadingTests.cpp uses),
    // then an explicit reconcile(graph) -- a setter alone leaves a BRAND NEW assignment id
    // unresolved (docs/architecture_app_wiring.md §8: "a setter is not a graph change").
    void publish(std::vector<ControllerProfile> profiles, std::vector<Assignment> assignments) {
        engine.setProfiles(std::move(profiles));
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(std::move(assignments));
        engine.reconcile(graph);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }
    void advance(double ms) { fakeNowMs += ms; }
};

class CountingGestureListener : public juce::AudioProcessorParameter::Listener {
public:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool gestureIsStarting) override {
        if (gestureIsStarting)
            ++starts;
        else
            ++ends;
    }
    int starts = 0;
    int ends = 0;
};

class CountingActionInvoker : public RemoteActionInvoker {
public:
    void invokeRemoteCommand(juce::CommandID commandId) override { invoked.push_back(commandId); }
    std::vector<juce::CommandID> invoked;
};

} // namespace

// ============================================================================
// Exactly one gesture pair per sweep (docs/midi_remote.md §4.2's "one undo step, one automation
// touch")
// ============================================================================

TEST(MidiRemoteEngineApplyTest, ExactlyOneGesturePairPerSweep) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump)});

    CountingGestureListener listener;
    h.cutoff()->addListener(&listener);

    // ~20 CC values, each drained separately, with the clock advancing well under kGestureIdleMs
    // between them -- the whole sweep must still collapse to ONE undo step.
    for (int i = 0; i <= 20; ++i) {
        h.send(juce::MidiMessage::controllerEvent(1, 10, (i * 127) / 20));
        h.advance(kGestureIdleMs / 4.0);
        h.engine.drain();
    }
    EXPECT_EQ(listener.starts, 1);
    EXPECT_EQ(listener.ends, 0) << "nothing has gone idle yet -- the gesture must still be open";

    h.advance(kGestureIdleMs + 1.0);
    h.engine.drain(); // no new event: this call only expires the idle gesture
    EXPECT_EQ(listener.starts, 1);
    EXPECT_EQ(listener.ends, 1);

    h.cutoff()->removeListener(&listener);
}

// ============================================================================
// A button applies begin+set+end within one drain, with no gesture left open
// ============================================================================

TEST(MidiRemoteEngineApplyTest, ButtonAppliesBeginSetEndWithinOneDrain) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("btn", MessageType::note, 1, 20, Encoding::abs7, ControlKind::button)})},
              {makeParamAssignment("a1", "btn", MessageType::note, 1, 20, Encoding::abs7, "bypassed")});

    h.send(juce::MidiMessage::noteOn(1, 20, (juce::uint8)100));
    h.engine.drain();
    EXPECT_TRUE(h.bypassed()->get());
    EXPECT_EQ(h.engine.activeGestureCount(), 0) << "a button apply must never leave a lingering gesture";

    h.send(juce::MidiMessage::noteOff(1, 20));
    h.engine.drain();
    EXPECT_FALSE(h.bypassed()->get());
    EXPECT_EQ(h.engine.activeGestureCount(), 0);
}

// ============================================================================
// Takeover: jump / pickup / scale / useDefault
// ============================================================================

TEST(MidiRemoteEngineApplyTest, JumpTakeoverMovesOnTheFirstEvent) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("j", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "j", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump)});
    h.cutoff()->setValueNotifyingHost(0.9f); // starting point far from where CC 0 maps to

    h.send(juce::MidiMessage::controllerEvent(1, 10, 0));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 0.0f, 1e-5f) << "jump takes the hardware value on the very first event";
}

TEST(MidiRemoteEngineApplyTest, PickupTakeoverWaitsForTheHardwareToCrossTheParameter) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("p", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "p", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::pickup)});
    h.cutoff()->setValueNotifyingHost(0.5f);
    const float cur = h.cutoff()->getValue();

    // First event: well below cur. Pickup must not move the parameter at all.
    h.send(juce::MidiMessage::controllerEvent(1, 10, 10));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), cur, 1e-6f) << "pickup must not jump on the first event";

    // Sweep upward one CC step at a time. Pickup engages exactly at the crossing (hw goes from
    // below cur to at-or-above it) and tracks EXACTLY afterward.
    float prevHw = 10.0f / 127.0f;
    bool engaged = false;
    for (int cc = 11; cc <= 127; ++cc) {
        h.send(juce::MidiMessage::controllerEvent(1, 10, cc));
        h.engine.drain();
        const float hw = cc / 127.0f;
        if (!engaged && prevHw <= cur && hw >= cur)
            engaged = true;
        if (engaged)
            EXPECT_NEAR(h.cutoff()->getValue(), hw, 1e-3f) << "once engaged, pickup tracks exactly, cc=" << cc;
        else
            EXPECT_NEAR(h.cutoff()->getValue(), cur, 1e-6f) << "must not move before crossing, cc=" << cc;
        prevHw = hw;
    }
    EXPECT_TRUE(engaged) << "the upward sweep must have crossed the starting value";
}

TEST(MidiRemoteEngineApplyTest, ScaleTakeoverConvergesWithoutJumpingOrOvershooting) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("s", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "s", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::scale)});
    h.cutoff()->setValueNotifyingHost(0.5f);
    const float cur = h.cutoff()->getValue();

    // First event: hw = 1.0, far from cur. Scale must not jump.
    h.send(juce::MidiMessage::controllerEvent(1, 10, 127));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), cur, 1e-6f) << "scale must not jump on the first event";

    float prevValue = cur;
    float prevHw = 1.0f;
    for (int cc = 126; cc >= 0; --cc) {
        h.send(juce::MidiMessage::controllerEvent(1, 10, cc));
        h.engine.drain();
        const float value = h.cutoff()->getValue();
        const float hw = cc / 127.0f;
        ASSERT_GE(value, 0.0f) << "cc=" << cc;
        ASSERT_LE(value, 1.0f) << "cc=" << cc;
        EXPECT_LE(std::abs(value - hw), std::abs(prevValue - prevHw) + 1e-4f)
            << "scale must converge toward hw, never diverge further, cc=" << cc;
        prevValue = value;
        prevHw = hw;
    }
    EXPECT_NEAR(h.cutoff()->getValue(), 0.0f, 1e-3f)
        << "scale reaches hw exactly once hw itself reaches a rail (here, 0.0)";
}

TEST(MidiRemoteEngineApplyTest, UseDefaultTakeoverResolvesToTheEngineWideDefault) {
    ApplyHarness h;
    h.engine.setDefaultTakeover(Takeover::jump);
    h.publish({makeProfile({makeControl("d", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "d", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::useDefault)});
    h.cutoff()->setValueNotifyingHost(0.9f);

    h.send(juce::MidiMessage::controllerEvent(1, 10, 0));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 0.0f, 1e-5f)
        << "useDefault must resolve to whatever setDefaultTakeover installed -- jump, here";
}

// ============================================================================
// Relative encodings bypass takeover entirely
// ============================================================================

TEST(MidiRemoteEngineApplyTest, RelativeEncodingMovesOnTheFirstEventEvenUnderPickup) {
    ApplyHarness h;
    h.publish(
        {makeProfile({makeControl("rel", MessageType::cc, 1, 10, Encoding::relTwos)})},
        {makeParamAssignment("a1", "rel", MessageType::cc, 1, 10, Encoding::relTwos, "cutoff", Takeover::pickup)});
    h.cutoff()->setValueNotifyingHost(0.5f);
    const float cur = h.cutoff()->getValue();

    h.send(juce::MidiMessage::controllerEvent(1, 10, 1)); // relTwos: +1/127
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), juce::jlimit(0.0f, 1.0f, cur + 1.0f / 127.0f), 1e-5f)
        << "a relative delta must move the parameter on the very first event -- takeover never applies to it";
}

// ============================================================================
// Range mapping, including inversion
// ============================================================================

TEST(MidiRemoteEngineApplyTest, RangeMapsHardwareValueThroughMinMax) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("r1", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "r1", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump, 0.25,
                                   0.75)});

    h.send(juce::MidiMessage::controllerEvent(1, 10, 0));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 0.25f, 1e-5f);

    h.send(juce::MidiMessage::controllerEvent(1, 10, 127));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 0.75f, 1e-5f);
}

TEST(MidiRemoteEngineApplyTest, InvertedRangeFlipsHardwareDirection) {
    ApplyHarness h;
    h.publish(
        {makeProfile({makeControl("r2", MessageType::cc, 1, 11, Encoding::abs7)})},
        {makeParamAssignment("a1", "r2", MessageType::cc, 1, 11, Encoding::abs7, "cutoff", Takeover::jump, 1.0, 0.0)});

    h.send(juce::MidiMessage::controllerEvent(1, 11, 0));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 1.0f, 1e-5f) << "an inverted range maps CC 0 to the HIGH end";

    h.send(juce::MidiMessage::controllerEvent(1, 11, 127));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 0.0f, 1e-5f) << "...and CC 127 to the LOW end";
}

// ============================================================================
// Claim respected -- a real mouse (or automation gesture) wins
// ============================================================================

TEST(MidiRemoteEngineApplyTest, ClaimedParameterIsUntouchedUntilReleased) {
    ApplyHarness h;
    h.publish({makeProfile({makeControl("c1", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "c1", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump)});
    h.cutoff()->setValueNotifyingHost(0.5f);
    const float cur = h.cutoff()->getValue();

    h.engine.setParameterClaimedPredicate([&](const juce::AudioProcessorParameter* p) { return p == h.cutoff(); });
    h.send(juce::MidiMessage::controllerEvent(1, 10, 127));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), cur, 1e-6f) << "a claimed parameter must not move";
    EXPECT_EQ(h.engine.activeGestureCount(), 0) << "a claimed parameter must never open a gesture";

    h.engine.setParameterClaimedPredicate([](const juce::AudioProcessorParameter*) { return false; });
    h.send(juce::MidiMessage::controllerEvent(1, 10, 127));
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), 1.0f, 1e-5f) << "once released, the very same sweep moves it";
}

// ============================================================================
// Orphaned slot -- a target that never resolves moves nothing
// ============================================================================

// This exercises the "unresolved" flavour of orphan (slot.param == nullptr, slot.orphaned ==
// false): a nodeUuid that names no node in the graph at all. The OTHER flavour (slot.orphaned ==
// true: a live HostedPluginModule instance whose parameter set can't produce a safe match) is
// exercised in RemoteEngineReconcileTests.cpp, per the ticket's own split of concerns -- both reach
// the exact same early return in applyToParameter (`slot.orphaned || slot.param == nullptr`).
TEST(MidiRemoteEngineApplyTest, OrphanedTargetMovesNothingAndOpensNoGesture) {
    ApplyHarness h;
    Assignment orphan =
        makeParamAssignment("a1", "c1", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump);
    orphan.target.parameter.nodeUuid = "node-that-does-not-exist-in-the-graph";
    h.publish({makeProfile({makeControl("c1", MessageType::cc, 1, 10, Encoding::abs7)})}, {orphan});
    h.cutoff()->setValueNotifyingHost(0.5f);
    const float cur = h.cutoff()->getValue();

    EXPECT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 10, 127)))
        << "the message is still consumed -- the LOOKUP entry exists regardless of whether the "
           "target resolved (docs/midi_remote.md's orphan-controller contract)";
    h.engine.drain();
    EXPECT_NEAR(h.cutoff()->getValue(), cur, 1e-6f) << "an orphaned target must never move ANY parameter";
    EXPECT_EQ(h.engine.activeGestureCount(), 0);
}

// ============================================================================
// Generation guard: a stale queued event must not be reinterpreted against a shifted table
// ============================================================================

TEST(MidiRemoteEngineApplyTest, StaleQueuedEventIsDiscardedAfterATableReorder) {
    ApplyHarness h;
    auto node2 = h.graph.addNode(std::make_unique<FilterModule>());
    constexpr const char* kUuid2 = "second-filter-node-uuid";
    node2->properties.set("uuid", juce::String(kUuid2));
    if (auto* mb = dynamic_cast<ModuleBase*>(node2->getProcessor()))
        mb->setNodeUuid(kUuid2);
    auto* cutoff2 = findParameterByID(node2->getProcessor(), "cutoff");
    ASSERT_NE(cutoff2, nullptr);

    h.publish({makeProfile({makeControl("g1", MessageType::cc, 1, 10, Encoding::abs7)})},
              {makeParamAssignment("a1", "g1", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump)});
    h.cutoff()->setValueNotifyingHost(0.5f);
    cutoff2->setValueNotifyingHost(0.5f);
    const float cur1 = h.cutoff()->getValue();
    const float cur2 = cutoff2->getValue();

    // Queue an event against the CURRENT table (the only slot, index 0, resolves to cutoff() on
    // node 1) -- but do NOT drain it yet.
    h.send(juce::MidiMessage::controllerEvent(1, 10, 127));

    // Republish a DIFFERENT assignment set before that event is drained: same control, still the
    // only slot (index 0 again), but now targeting a DIFFERENT parameter on a DIFFERENT node. An
    // explicit reconcile() (not just setAssignments) so the new target actually resolves --
    // otherwise a broken generation guard would apply to a null parameter and prove nothing.
    Assignment reassigned =
        makeParamAssignment("a2", "g1", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", Takeover::jump);
    reassigned.target.parameter.nodeUuid = kUuid2;
    h.engine.setAssignments({reassigned});
    h.engine.reconcile(h.graph);

    h.engine.drain();

    // RemoteEvent::generation is what must save both parameters here: the queued event was
    // stamped with the OLD snapshot's generation, which no longer matches the live one, so
    // drain() must discard it outright rather than reinterpret slot 0 against the new table.
    EXPECT_NEAR(h.cutoff()->getValue(), cur1, 1e-6f) << "the stale event must not move the OLD parameter";
    EXPECT_NEAR(cutoff2->getValue(), cur2, 1e-6f) << "...nor the NEW parameter now sitting at the same slot index";
}

// ============================================================================
// Action targets: fire on press only, for both momentary and toggle button modes
// ============================================================================

TEST(MidiRemoteEngineApplyTest, ActionFiresOnPressOnlyForBothButtonModes) {
    for (const ButtonMode mode : {ButtonMode::momentary, ButtonMode::toggle}) {
        ApplyHarness h;
        CountingActionInvoker invoker;
        h.engine.setActionInvoker(&invoker);
        h.engine.setActionCommandLookup(
            [](const juce::String& actionId) -> juce::CommandID { return actionId == "test.action" ? 4242 : 0; });

        Assignment action;
        action.id = "a1";
        action.control.profileId = "profile";
        action.control.controlId = "btn";
        action.spec.type = MessageType::note;
        action.spec.channel = 1;
        action.spec.number = 30;
        action.specEncoding = Encoding::abs7;
        action.specButtonMode = mode;
        action.target.kind = Target::Kind::action;
        action.target.action.actionId = "test.action";

        h.publish({makeProfile({makeControl("btn", MessageType::note, 1, 30, Encoding::abs7, ControlKind::button)})},
                  {action});

        h.send(juce::MidiMessage::noteOn(1, 30, (juce::uint8)100)); // press
        h.engine.drain();
        h.send(juce::MidiMessage::noteOff(1, 30)); // release
        h.engine.drain();

        const char* modeName = mode == ButtonMode::momentary ? "momentary" : "toggle";
        ASSERT_EQ(invoker.invoked.size(), 1u)
            << "press fires exactly once and release fires nothing, buttonMode=" << modeName;
        EXPECT_EQ(invoker.invoked.front(), 4242) << "buttonMode=" << modeName;
    }
}
