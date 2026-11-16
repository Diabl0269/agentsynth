// RemoteEngine::handleOsc and the snapshot's OSC address interning (docs/control/midi-remote-osc.md#3-address-key,
// #4-argument-normalisation, #7-trust-boundary). Every test drives a real FilterModule node so applying moves a real
// juce::AudioProcessorParameter, and calls RemoteEngine::drain() directly on a fake clock. Suite names contain
// "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "osc:9000";
constexpr const char* kNodeUuid = "filter-node-uuid";

struct OscHarness {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr node;
    RemoteEngine engine;
    double fakeNowMs = 0.0;
    std::vector<Control> controls;
    std::vector<Assignment> assignments;

    OscHarness() {
        node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", juce::String(kNodeUuid));
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            mb->setNodeUuid(kNodeUuid);
        engine.setClock([this] { return fakeNowMs; });
    }

    juce::RangedAudioParameter* param(const juce::String& id) { return findParameterByID(node->getProcessor(), id); }

    // One osc control plus its parameter assignment.
    void addControl(const juce::String& id, const juce::String& address, int argIndex, const juce::String& paramId,
                    double inMin = 0.0, double inMax = 1.0, ControlKind kind = ControlKind::fader) {
        Control control;
        control.id = id;
        control.name = id;
        control.kind = kind;
        control.message.type = MessageType::osc;
        control.message.address = address;
        control.message.argIndex = argIndex;
        control.inRange.min = inMin;
        control.inRange.max = inMax;
        controls.push_back(control);

        Assignment a;
        a.id = id + "-assign";
        a.control.profileId = "profile";
        a.control.controlId = id;
        a.spec = control.message;
        a.target.kind = Target::Kind::parameter;
        a.target.parameter.nodeUuid = juce::String(kNodeUuid);
        a.target.parameter.paramId = paramId;
        a.takeover = Takeover::jump;
        assignments.push_back(a);
    }

    void publish() {
        ControllerProfile profile;
        profile.id = "profile";
        profile.name = "profile";
        profile.input.identifier = kSource;
        profile.input.name = kSource;
        profile.controls = controls;
        engine.setProfiles({profile});
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(assignments);
        engine.reconcile(graph);
    }

    bool osc(const juce::String& address, double value, OscArgKind kind = OscArgKind::floatArg, int argIndex = 0) {
        return engine.handleOsc(kSource, address, argIndex, value, kind);
    }

    std::vector<RemoteEvent> activity() {
        std::vector<RemoteEvent> events;
        engine.drainActivity([&](const juce::String&, const RemoteEvent& e) { events.push_back(e); });
        return events;
    }
};

} // namespace

TEST(MidiRemoteEngineOscTest, MappedFloatAddressBecomesAnAbsoluteEventAppliedInTheDrain) {
    OscHarness h;
    h.addControl("fader", "/1/fader1", 0, "cutoff");
    h.publish();

    ASSERT_TRUE(h.osc("/1/fader1", 0.25));
    const auto events = h.activity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, RemoteEventKind::absolute);
    EXPECT_EQ(events[0].slotIndex, 0);
    EXPECT_EQ(events[0].specType, static_cast<std::uint8_t>(MessageType::osc));
    EXPECT_EQ(events[0].specChannel, 0);
    EXPECT_NEAR(events[0].value, 0.25f, 1e-6f);
    EXPECT_EQ(events[0].rawValue, 32) << "round(0.25 * 127)";

    // The activity ring is a mirror: the events ring still holds the event for the drain.
    h.engine.drain();
    EXPECT_NEAR(h.param("cutoff")->getValue(), 0.25f, 1e-4f);
}

TEST(MidiRemoteEngineOscTest, FloatsOutsideTheInputRangeClampAndAnInvertedRangeInverts) {
    OscHarness h;
    h.addControl("fader", "/a", 0, "cutoff", 0.0, 1.0);
    h.addControl("inv", "/b", 0, "resonance", 1.0, 0.0);
    h.publish();

    ASSERT_TRUE(h.osc("/a", 7.5));
    ASSERT_TRUE(h.osc("/b", 0.25));
    const auto events = h.activity();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_FLOAT_EQ(events[0].value, 1.0f);
    EXPECT_NEAR(events[1].value, 0.75f, 1e-6f);
}

TEST(MidiRemoteEngineOscTest, IntArgumentUsesTheControlsInputRange) {
    OscHarness h;
    h.addControl("fader", "/int", 0, "cutoff", 0.0, 127.0);
    h.publish();

    ASSERT_TRUE(h.osc("/int", 100, OscArgKind::intArg));
    const auto events = h.activity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].value, 100.0f / 127.0f, 1e-6f);
    EXPECT_EQ(events[0].rawValue, 100);
}

TEST(MidiRemoteEngineOscTest, TrueAndFalseOnAButtonPressAndReleaseThroughTheButtonDecode) {
    OscHarness h;
    h.addControl("btn", "/1/toggle1", 0, "bypassed", 0.0, 1.0, ControlKind::button);
    h.publish();

    ASSERT_TRUE(h.osc("/1/toggle1", 1.0, OscArgKind::boolArg));
    auto events = h.activity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, RemoteEventKind::buttonPress);
    h.engine.drain();
    EXPECT_GT(h.param("bypassed")->getValue(), 0.5f);

    ASSERT_TRUE(h.osc("/1/toggle1", 0.0, OscArgKind::boolArg));
    events = h.activity();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, RemoteEventKind::buttonRelease);
    h.engine.drain();
    EXPECT_LT(h.param("bypassed")->getValue(), 0.5f);

    // A 0/1 float or int on a button decodes the same way.
    ASSERT_TRUE(h.osc("/1/toggle1", 1.0, OscArgKind::intArg));
    EXPECT_EQ(h.activity().front().kind, RemoteEventKind::buttonPress);
}

TEST(MidiRemoteEngineOscTest, NonFiniteAndMalformedAddressesAreRejectedBeforeLookup) {
    OscHarness h;
    h.addControl("fader", "/ok", 0, "cutoff");
    h.publish();

    EXPECT_FALSE(h.osc("/ok", std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(h.osc("/ok", std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(h.osc("/ok", -std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(h.osc("/o*", 0.5)) << "a wildcard never fans out";
    EXPECT_FALSE(h.osc("/{ok,no}", 0.5));
    EXPECT_FALSE(h.osc("/[a-z]k", 0.5));
    EXPECT_FALSE(h.osc("/o?", 0.5));
    EXPECT_FALSE(h.osc("ok", 0.5)) << "no leading slash";
    EXPECT_FALSE(h.osc("", 0.5));
    EXPECT_FALSE(h.osc("/o k", 0.5));
    EXPECT_FALSE(h.osc("/ok\n", 0.5));
    EXPECT_FALSE(h.osc("/" + juce::String::repeatedString("a", 199), 0.5)) << "200 bytes";
    EXPECT_TRUE(h.activity().empty());

    EXPECT_TRUE(h.osc("/ok", 0.5)) << "the mapped address still works after the rejects";
}

TEST(MidiRemoteEngineOscTest, AnUnmappedAddressOrSourceOrArgumentAppliesNothing) {
    OscHarness h;
    h.addControl("fader", "/mapped", 0, "cutoff");
    h.publish();
    const float before = h.param("cutoff")->getValue();

    EXPECT_FALSE(h.osc("/other", 0.9));
    EXPECT_FALSE(h.osc("/mapped", 0.9, OscArgKind::floatArg, 1)) << "argIndex 1 is not mapped";
    EXPECT_FALSE(h.engine.handleOsc("osc:other", "/mapped", 0, 0.9, OscArgKind::floatArg));
    EXPECT_TRUE(h.activity().empty());
    h.engine.drain();
    EXPECT_FLOAT_EQ(h.param("cutoff")->getValue(), before);
}

TEST(MidiRemoteEngineOscTest, AnXyPadMapsTwoControlsByArgIndex) {
    OscHarness h;
    h.addControl("x", "/xy", 0, "cutoff");
    h.addControl("y", "/xy", 1, "resonance");
    h.publish();

    ASSERT_TRUE(h.osc("/xy", 0.3, OscArgKind::floatArg, 0));
    ASSERT_TRUE(h.osc("/xy", 0.8, OscArgKind::floatArg, 1));
    const auto events = h.activity();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_NE(events[0].slotIndex, events[1].slotIndex);
    h.engine.drain();
    EXPECT_NEAR(h.param("cutoff")->getValue(), 0.3f, 1e-4f);
    EXPECT_NEAR(h.param("resonance")->getValue(), 0.8f, 1e-4f);
}

TEST(MidiRemoteEngineOscTest, AFloodOfTenThousandPacketsCapsAtTheRingAndTheDrainSurvives) {
    OscHarness h;
    h.addControl("fader", "/flood", 0, "cutoff");
    h.publish();

    int accepted = 0;
    for (int i = 0; i < 10000; ++i)
        accepted += h.osc("/flood", static_cast<double>(i % 100) / 100.0) ? 1 : 0;
    EXPECT_EQ(accepted, 10000) << "a full ring drops the newest event, never the call";

    EXPECT_LE(h.activity().size(), static_cast<std::size_t>(SourceLane::kActivityCapacity));
    h.engine.drain();
    EXPECT_GE(h.param("cutoff")->getValue(), 0.0f);

    // The ring is usable again afterwards.
    ASSERT_TRUE(h.osc("/flood", 0.5));
    h.engine.drain();
    EXPECT_NEAR(h.param("cutoff")->getValue(), 0.5f, 1e-4f);
}

// An OSC address reaches a target exactly as a MIDI CC does: the same decoded 0..1 gives the same parameter value.
TEST(MidiRemoteEngineOscTest, AnOscAddressMapsToATargetExactlyAsAMidiCcDoes) {
    OscHarness viaOsc;
    viaOsc.addControl("fader", "/cutoff", 0, "cutoff", 0.0, 127.0);
    viaOsc.publish();
    ASSERT_TRUE(viaOsc.osc("/cutoff", 100, OscArgKind::intArg));
    viaOsc.engine.drain();

    OscHarness viaCc;
    Control control;
    control.id = "knob";
    control.name = "knob";
    control.message.type = MessageType::cc;
    control.message.channel = 1;
    control.message.number = 10;
    Assignment a;
    a.id = "a";
    a.control.profileId = "profile";
    a.control.controlId = "knob";
    a.spec = control.message;
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = juce::String(kNodeUuid);
    a.target.parameter.paramId = "cutoff";
    a.takeover = Takeover::jump;
    viaCc.controls = {control};
    viaCc.assignments = {a};
    viaCc.publish();
    viaCc.engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(1, 10, 100));
    viaCc.engine.drain();

    EXPECT_FLOAT_EQ(viaOsc.param("cutoff")->getValue(), viaCc.param("cutoff")->getValue());
}

TEST(MidiRemoteEngineOscTest, MidiTrafficNeverDecodesToAnOscMessage) {
    OscHarness h;
    h.addControl("fader", "/x", 0, "cutoff");
    h.publish();

    for (int channel = 1; channel <= 16; ++channel) {
        EXPECT_FALSE(h.engine.handleMessage(kSource, juce::MidiMessage::controllerEvent(channel, 0, 64)));
        EXPECT_FALSE(h.engine.handleMessage(kSource, juce::MidiMessage::pitchWheel(channel, 100)));
    }
    for (const auto& event : h.activity())
        EXPECT_NE(event.specType, static_cast<std::uint8_t>(MessageType::osc));
}

TEST(MidiRemoteEngineOscTest, AddressInterningDedupesAndSortsPerSource) {
    OscHarness h;
    h.addControl("a", "/b", 1, "cutoff");
    h.addControl("b", "/a", 0, "resonance");
    h.publish();

    // Both (address, argIndex) keys resolve, in either order of arrival.
    EXPECT_TRUE(h.osc("/a", 0.1, OscArgKind::floatArg, 0));
    EXPECT_TRUE(h.osc("/b", 0.2, OscArgKind::floatArg, 1));
    EXPECT_FALSE(h.osc("/b", 0.2, OscArgKind::floatArg, 0));
    EXPECT_FALSE(h.osc("/a", 0.2, OscArgKind::floatArg, 1));
}
