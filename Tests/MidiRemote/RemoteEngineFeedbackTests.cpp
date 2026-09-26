// Message thread: RemoteEngine::drain()'s feedback pass (RemoteEngineFeedback.cpp) --
// docs/control/midi-remote.md#controller-feedback. Mirrors RemoteEngineApplyTests.cpp's harness
// shape (a real juce::AudioProcessorGraph + FilterModule node, a fake clock via setClock, drain()
// called directly -- never a real message loop). Suite names contain "MidiRemote" per the
// ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";
constexpr const char* kNodeUuid = "filter-node-uuid";
constexpr const char* kOutputId = "out1";
constexpr const char* kOutputName = "Feedback Out";

class FakeFeedbackSink : public RemoteFeedbackSink {
public:
    struct Sent {
        ControllerProfile::Input device;
        juce::MidiMessage message;
    };
    std::vector<Sent> sent;

    void sendFeedback(const ControllerProfile::Input& outputDevice, const juce::MidiMessage& message) override {
        sent.push_back({outputDevice, message});
    }
};

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

ControllerProfile makeProfile(std::vector<Control> controls, bool withOutput) {
    ControllerProfile profile;
    profile.id = "profile";
    profile.name = "profile";
    profile.input.identifier = kSource;
    profile.input.name = kSource;
    profile.hasOutput = withOutput;
    if (withOutput) {
        profile.output.identifier = kOutputId;
        profile.output.name = kOutputName;
    }
    profile.controls = std::move(controls);
    return profile;
}

Assignment makeParamAssignment(const juce::String& id, const juce::String& controlId, MessageType type, int channel,
                               int number, Encoding encoding, const juce::String& paramId, double rangeMin = 0.0,
                               double rangeMax = 1.0) {
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
    a.takeover = Takeover::jump;
    a.range.min = rangeMin;
    a.range.max = rangeMax;
    return a;
}

struct FeedbackHarness {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr node;
    RemoteEngine engine;
    FakeFeedbackSink sink;
    double fakeNowMs = 0.0;

    FeedbackHarness() {
        node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", juce::String(kNodeUuid));
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            mb->setNodeUuid(kNodeUuid);
        engine.setClock([this] { return fakeNowMs; });
        engine.setFeedbackSink(&sink);
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(node->getProcessor(), "cutoff"); }
    juce::AudioParameterBool* bypassed() {
        return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(node->getProcessor(), "bypassed"));
    }

    void publish(std::vector<ControllerProfile> profiles, std::vector<Assignment> assignments) {
        engine.setProfiles(std::move(profiles));
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(std::move(assignments));
        engine.reconcile(graph);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }
    void advance(double ms) { fakeNowMs += ms; }
};

} // namespace

TEST(MidiRemoteEngineFeedbackTest, ExternalParameterChangeSendsCcOnNextDrain) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_EQ(h.sink.sent[0].device.identifier, kOutputId);
    EXPECT_TRUE(h.sink.sent[0].message.isController());
    EXPECT_EQ(h.sink.sent[0].message.getControllerNumber(), 10);
    EXPECT_EQ(h.sink.sent[0].message.getChannel(), 1);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), juce::roundToInt(0.5f * 127.0f));
}

TEST(MidiRemoteEngineFeedbackTest, UnchangedValueSendsOnlyOnce) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();
    h.engine.drain();
    h.engine.drain();

    EXPECT_EQ(h.sink.sent.size(), 1u);
}

TEST(MidiRemoteEngineFeedbackTest, HardwareEventCooldownSuppressesEchoThenSendsFinalValue) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});

    ASSERT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 10, 100)));
    h.engine.drain(); // applies the hardware event -- stamps hasHardware, and would-be echo is suppressed
    EXPECT_TRUE(h.sink.sent.empty());

    h.advance(100.0);
    h.engine.drain();
    EXPECT_TRUE(h.sink.sent.empty()); // still within the 250 ms cooldown

    h.advance(200.0); // now 300 ms since the hardware event -- cooldown has elapsed
    h.engine.drain();
    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), 100);

    // No further echo once the value has already been sent.
    h.advance(1000.0);
    h.engine.drain();
    EXPECT_EQ(h.sink.sent.size(), 1u);
}

TEST(MidiRemoteEngineFeedbackTest, ProfileWithoutOutputSendsNothing) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, false)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();

    EXPECT_TRUE(h.sink.sent.empty());
}

TEST(MidiRemoteEngineFeedbackTest, BoolParamOnNoteSpecSendsNoteOnAtFullOrZeroVelocity) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("pad", MessageType::note, 1, 20, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "pad", MessageType::note, 1, 20, Encoding::abs7, "bypassed")});

    h.bypassed()->setValueNotifyingHost(1.0f);
    h.engine.drain();
    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_TRUE(h.sink.sent[0].message.isNoteOn());
    EXPECT_EQ(h.sink.sent[0].message.getVelocity(), 127);

    h.bypassed()->setValueNotifyingHost(0.0f);
    h.engine.drain();
    ASSERT_EQ(h.sink.sent.size(), 2u);
    EXPECT_EQ(h.sink.sent[1].message.getVelocity(), 0);
}

TEST(MidiRemoteEngineFeedbackTest, InvertedRangeEncodesCorrectly) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff", 1.0, 0.0)});

    h.cutoff()->setValueNotifyingHost(0.25f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    // x = (0.25 - 1.0) / (0.0 - 1.0) = 0.75
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), juce::roundToInt(0.75f * 127.0f));
}

TEST(MidiRemoteEngineFeedbackTest, PitchBendEncodesFourteenBit) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("wheel", MessageType::pitchBend, 1, 0, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "wheel", MessageType::pitchBend, 1, 0, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_TRUE(h.sink.sent[0].message.isPitchWheel());
    EXPECT_EQ(h.sink.sent[0].message.getPitchWheelValue(), juce::roundToInt(0.5f * 16383.0f));
}

TEST(MidiRemoteEngineFeedbackTest, ChannelZeroMeansChannelOne) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 0, 10, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 0, 10, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_EQ(h.sink.sent[0].message.getChannel(), 1);
}

TEST(MidiRemoteEngineFeedbackTest, OrphanedSlotSendsNothing) {
    FeedbackHarness h;
    // No reconcile(graph) with the real node -- the assignment's nodeUuid never resolves, so the
    // slot is orphaned/unparamed and must be skipped.
    h.engine.setFeedbackSink(&h.sink);
    h.engine.setProfiles({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)});
    h.engine.setSources({juce::String(kSource)});
    h.engine.setAssignments(
        {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "does-not-exist")});
    h.engine.reconcile(h.graph);

    h.engine.drain();
    EXPECT_TRUE(h.sink.sent.empty());
}

TEST(MidiRemoteEngineFeedbackTest, ActionTargetSendsNothing) {
    FeedbackHarness h;
    ControllerProfile profile = makeProfile({makeControl("btn", MessageType::note, 1, 30, Encoding::abs7)}, true);
    Assignment action;
    action.id = "act1";
    action.control.profileId = "profile";
    action.control.controlId = "btn";
    action.spec.type = MessageType::note;
    action.spec.channel = 1;
    action.spec.number = 30;
    action.target.kind = Target::Kind::action;
    action.target.action.actionId = "some.action";
    profile.actions.push_back(action);

    h.engine.setFeedbackSink(&h.sink);
    h.engine.setProfiles({profile});
    h.engine.setSources({juce::String(kSource)});
    h.engine.setAssignments({});
    h.engine.reconcile(h.graph);

    h.engine.drain();
    EXPECT_TRUE(h.sink.sent.empty());
}

TEST(MidiRemoteEngineFeedbackTest, SetProfilesAndResendFeedbackForceAResend) {
    FeedbackHarness h;
    auto profiles = std::vector<ControllerProfile>{
        makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)};
    h.publish(profiles, {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();
    ASSERT_EQ(h.sink.sent.size(), 1u);

    h.engine.setProfiles(profiles); // FRO139: republishing clears feedback_ wholesale
    h.engine.drain();
    EXPECT_EQ(h.sink.sent.size(), 2u);

    h.engine.resendFeedback();
    h.engine.drain();
    EXPECT_EQ(h.sink.sent.size(), 3u);
}

TEST(MidiRemoteEngineFeedbackTest, NoSinkDoesNotCrash) {
    FeedbackHarness h;
    h.engine.setFeedbackSink(nullptr);
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)},
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    EXPECT_NO_FATAL_FAILURE(h.engine.drain());
}

// -- 14-bit pairs and NRPN (FRO140) ------------------------------------------------------------------

TEST(MidiRemoteEngineFeedbackTest, PairedAbs14SlotEchoesMsbOnCcNThenLsbOnCcNPlus32) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("fader", MessageType::cc, 1, 21, Encoding::abs14)}, true)},
              {makeParamAssignment("a1", "fader", MessageType::cc, 1, 21, Encoding::abs14, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.3f);
    h.engine.drain();

    // 0.3 * 16383 = 4914.9 -> 4915 = (38 << 7) | 51
    ASSERT_EQ(h.sink.sent.size(), 2u);
    EXPECT_EQ(h.sink.sent[0].device.identifier, kOutputId);
    EXPECT_EQ(h.sink.sent[1].device.identifier, kOutputId);
    EXPECT_TRUE(h.sink.sent[0].message.isController());
    EXPECT_EQ(h.sink.sent[0].message.getControllerNumber(), 21);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), 38);
    EXPECT_EQ(h.sink.sent[0].message.getChannel(), 1);
    EXPECT_TRUE(h.sink.sent[1].message.isController());
    EXPECT_EQ(h.sink.sent[1].message.getControllerNumber(), 53);
    EXPECT_EQ(h.sink.sent[1].message.getControllerValue(), 51);
    EXPECT_EQ(h.sink.sent[1].message.getChannel(), 1);
}

TEST(MidiRemoteEngineFeedbackTest, PairedAbs14LsbFirstSlotEchoesTheSameTwoCcsInReverseOrder) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("fader", MessageType::cc, 1, 21, Encoding::abs14LsbFirst)}, true)},
              {makeParamAssignment("a1", "fader", MessageType::cc, 1, 21, Encoding::abs14LsbFirst, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.3f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 2u);
    EXPECT_EQ(h.sink.sent[0].message.getControllerNumber(), 53) << "LSB first";
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), 51);
    EXPECT_EQ(h.sink.sent[1].message.getControllerNumber(), 21);
    EXPECT_EQ(h.sink.sent[1].message.getControllerValue(), 38);
}

TEST(MidiRemoteEngineFeedbackTest, PairedSlotAtFullScaleSendsBothHalvesAt127AndChannelZeroMeansOne) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("fader", MessageType::cc, 0, 21, Encoding::abs14)}, true)},
              {makeParamAssignment("a1", "fader", MessageType::cc, 0, 21, Encoding::abs14, "cutoff")});

    h.cutoff()->setValueNotifyingHost(1.0f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 2u);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), 127);
    EXPECT_EQ(h.sink.sent[1].message.getControllerValue(), 127);
    EXPECT_EQ(h.sink.sent[0].message.getChannel(), 1);
    EXPECT_EQ(h.sink.sent[1].message.getChannel(), 1);
}

TEST(MidiRemoteEngineFeedbackTest, PairedSlotUnchangedValueSendsThePairOnlyOnce) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("fader", MessageType::cc, 1, 21, Encoding::abs14)}, true)},
              {makeParamAssignment("a1", "fader", MessageType::cc, 1, 21, Encoding::abs14, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.3f);
    h.engine.drain();
    h.engine.drain();

    EXPECT_EQ(h.sink.sent.size(), 2u);
}

TEST(MidiRemoteEngineFeedbackTest, PairedSlotWhoseLsbPartnerWouldExceedCc63SendsSinglePlainCc) {
    // Guards isPairedCcSlot's number+32 <= 63 bound. Such a slot can't come from a valid profile
    // (encodingValidForSpec rejects it), but the engine must not emit a CC above the LSB range.
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("fader", MessageType::cc, 1, 40, Encoding::abs14)}, true)},
              {makeParamAssignment("a1", "fader", MessageType::cc, 1, 40, Encoding::abs14, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_EQ(h.sink.sent[0].message.getControllerNumber(), 40);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), juce::roundToInt(0.5f * 127.0f));
}

TEST(MidiRemoteEngineFeedbackTest, NrpnSlotSendsNothing) {
    FeedbackHarness h;
    h.publish({makeProfile({makeControl("nrpn", MessageType::nrpn, 1, 1024, Encoding::abs14)}, true)},
              {makeParamAssignment("a1", "nrpn", MessageType::nrpn, 1, 1024, Encoding::abs14, "cutoff")});

    h.cutoff()->setValueNotifyingHost(0.5f);
    h.engine.drain();

    EXPECT_TRUE(h.sink.sent.empty());
}

// FRO142 (docs/control/midi-remote.md#pages): two pages map the same knob to different parameters;
// only the active page's value is echoed, and a page switch echoes the new page's value.
TEST(MidiRemoteEngineFeedbackTest, OnlyTheActivePageIsEchoedAndASwitchEchoesTheNewPage) {
    FeedbackHarness h;
    auto page1 = makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, Encoding::abs7, "cutoff");
    auto page2 = makeParamAssignment("a2", "knob", MessageType::cc, 1, 10, Encoding::abs7, "resonance");
    page2.page = 2;
    h.publish({makeProfile({makeControl("knob", MessageType::cc, 1, 10, Encoding::abs7)}, true)}, {page1, page2});

    auto* resonance = findParameterByID(h.node->getProcessor(), "resonance");
    ASSERT_NE(resonance, nullptr);
    h.cutoff()->setValueNotifyingHost(0.5f);
    resonance->setValueNotifyingHost(0.2f);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), juce::roundToInt(h.cutoff()->getValue() * 127.0f));

    h.sink.sent.clear();
    h.engine.setActivePage("profile", 2);
    h.engine.drain();

    ASSERT_EQ(h.sink.sent.size(), 1u);
    EXPECT_EQ(h.sink.sent[0].message.getControllerValue(), juce::roundToInt(resonance->getValue() * 127.0f));
}
