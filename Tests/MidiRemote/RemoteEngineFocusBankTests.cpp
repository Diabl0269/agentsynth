// FRO141 (docs/control/midi-remote.md#focus-bank): RemoteEngine::setTransientAssignments and the
// explicit-wins/page-independence rules in RemoteEngineReconcile.cpp's addSlot/
// transientBlockedByExplicit. Mirrors RemoteEnginePagesTests.cpp's harness (one real
// AudioProcessorGraph + FilterModule node, fake clock) so a transient binding really moves a real
// juce::AudioProcessorParameter through handleMessage()/drain(). Suite name contains "MidiRemote"
// per the ship-task --gtest_filter convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";
constexpr const char* kNodeUuid = "filter-node-uuid";
constexpr const char* kProfileId = "profile";
constexpr const char* kBankControlId = "bank-control";

ControllerProfile makeProfile(std::vector<Control> controls = {}) {
    ControllerProfile p;
    p.id = kProfileId;
    p.name = kProfileId;
    p.input.identifier = kSource;
    p.input.name = kSource;
    p.controls = std::move(controls);
    return p;
}

Control makeBankControl(MessageType type, int channel, int number) {
    Control c;
    c.id = kBankControlId;
    c.name = "Bank 1";
    c.kind = ControlKind::knob;
    c.message.type = type;
    c.message.channel = channel;
    c.message.number = number;
    c.focusBank = true;
    return c;
}

// FRO141: mirrors MidiLearnController::rebuildFocusBankAssignments' shape -- id
// "focus:<profileId>:<controlId>", spec/encoding from the control, target = parameter.
Assignment makeTransient(const Control& control, const juce::String& paramId, int page = 1) {
    Assignment a;
    a.id = "focus:" + juce::String(kProfileId) + ":" + control.id;
    a.control.profileId = kProfileId;
    a.control.controlId = control.id;
    a.spec = control.message;
    a.specEncoding = control.encoding;
    a.specButtonMode = control.buttonMode;
    a.specControlName = control.name;
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = juce::String(kNodeUuid);
    a.target.parameter.paramId = paramId;
    a.takeover = Takeover::useDefault;
    a.enabled = true;
    a.page = page; // FRO141: irrelevant to the engine's transient handling -- see the doc's own note
    return a;
}

Assignment makeProjectAssignment(const juce::String& id, const Control& control, const juce::String& paramId,
                                 int page) {
    Assignment a = makeTransient(control, paramId, page);
    a.id = id;
    return a;
}

struct FocusBankHarness {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr node;
    RemoteEngine engine;
    double fakeNowMs = 0.0;

    FocusBankHarness() {
        node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", juce::String(kNodeUuid));
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            mb->setNodeUuid(kNodeUuid);
        engine.setDefaultTakeover(Takeover::jump); // no pickup-crossing noise in these tests
        engine.setClock([this] { return fakeNowMs; });
        engine.setSources({juce::String(kSource)});
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(node->getProcessor(), "cutoff"); }

    void setProfile(ControllerProfile profile) {
        engine.setProfiles({std::move(profile)});
        engine.reconcile(graph);
    }

    void setProjectAssignments(std::vector<Assignment> assignments) {
        engine.setAssignments(std::move(assignments));
        engine.reconcile(graph);
    }

    void setTransient(std::vector<Assignment> assignments) {
        engine.setTransientAssignments(std::move(assignments));
        engine.reconcile(graph);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }
    void advance(double ms) { fakeNowMs += ms; }

    ~FocusBankHarness() { engine.endAllGestures(); }
};

} // namespace

// ============================================================================
// A transient assignment drives its parameter through a real MIDI message.
// ============================================================================

TEST(MidiRemoteEngineFocusBankTest, TransientAssignmentDrivesItsParameterThroughHandleMessageAndDrain) {
    FocusBankHarness h;
    const auto control = makeBankControl(MessageType::cc, 1, 20);
    h.setProfile(makeProfile({control}));
    h.setTransient({makeTransient(control, "cutoff")});

    auto* cutoff = h.cutoff();
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();

    ASSERT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 20, 127)));
    h.engine.drain();

    EXPECT_NE(cutoff->getValue(), before) << "the transient binding must resolve and apply immediately";
}

// ============================================================================
// Explicit wins.
// ============================================================================

TEST(MidiRemoteEngineFocusBankTest, ExplicitProjectAssignmentOnTheActivePageBlocksTheTransient) {
    FocusBankHarness h;
    const auto control = makeBankControl(MessageType::cc, 1, 20);
    h.setProfile(makeProfile({control}));
    // An explicit project assignment on the SAME control, driving "resonance" instead.
    h.setProjectAssignments({makeProjectAssignment("explicit", control, "resonance", /*page=*/1)});
    h.setTransient({makeTransient(control, "cutoff")});

    auto* cutoff = h.cutoff();
    auto* resonance = findParameterByID(h.node->getProcessor(), "resonance");
    ASSERT_NE(cutoff, nullptr);
    ASSERT_NE(resonance, nullptr);
    const float cutoffBefore = cutoff->getValue();
    const float resonanceBefore = resonance->getValue();

    ASSERT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 20, 127)));
    h.engine.drain();

    EXPECT_EQ(cutoff->getValue(), cutoffBefore) << "the transient binding must not apply once shadowed";
    EXPECT_NE(resonance->getValue(), resonanceBefore) << "the explicit assignment is what actually fires";
}

TEST(MidiRemoteEngineFocusBankTest, ExplicitProjectAssignmentOnAnInactivePageDoesNotBlockTheTransient) {
    FocusBankHarness h;
    const auto control = makeBankControl(MessageType::cc, 1, 20);
    ControllerProfile profile = makeProfile({control});
    profile.pageCount = 2;
    h.setProfile(profile);
    // Page 2's mapping -- page 1 is (and stays) active, so this must never shadow the transient.
    h.setProjectAssignments({makeProjectAssignment("explicit-page-2", control, "resonance", /*page=*/2)});
    h.setTransient({makeTransient(control, "cutoff")});
    ASSERT_EQ(h.engine.getActivePage(kProfileId), 1);

    auto* cutoff = h.cutoff();
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();

    ASSERT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 20, 127)));
    h.engine.drain();

    EXPECT_NE(cutoff->getValue(), before) << "an inactive-page mapping on the same control must not block it";
}

TEST(MidiRemoteEngineFocusBankTest, GlobalProfileActionOnTheControlBlocksTheTransient) {
    FocusBankHarness h;
    const auto control = makeBankControl(MessageType::cc, 1, 20);
    ControllerProfile profile = makeProfile({control});
    Assignment action;
    action.id = "global-action";
    action.control.profileId = kProfileId;
    action.control.controlId = control.id;
    action.spec = control.message;
    action.target.kind = Target::Kind::action;
    action.target.action.actionId = "transportRecord";
    action.enabled = true;
    profile.actions = {action};
    h.setProfile(profile);
    h.setTransient({makeTransient(control, "cutoff")});

    auto* cutoff = h.cutoff();
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();

    ASSERT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 20, 127)));
    h.engine.drain();

    EXPECT_EQ(cutoff->getValue(), before) << "a global action on the same control must shadow the transient too";
}

// ============================================================================
// Clearing.
// ============================================================================

TEST(MidiRemoteEngineFocusBankTest, ClearingTransientAssignmentsRemovesTheLookup) {
    FocusBankHarness h;
    const auto control = makeBankControl(MessageType::cc, 1, 20);
    h.setProfile(makeProfile({control}));
    h.setTransient({makeTransient(control, "cutoff")});
    ASSERT_TRUE(h.send(juce::MidiMessage::controllerEvent(1, 20, 127))) << "sanity: the binding is live first";
    h.engine.drain();
    h.advance(kGestureIdleMs + 1.0);
    h.engine.drain();

    h.setTransient({}); // e.g. deselecting every module

    auto* cutoff = h.cutoff();
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();
    EXPECT_FALSE(h.send(juce::MidiMessage::controllerEvent(1, 20, 64))) << "no assignment left to consume the message";
    h.engine.drain();
    EXPECT_EQ(cutoff->getValue(), before);
}

TEST(MidiRemoteEngineFocusBankTest, GetTransientAssignmentsReflectsTheLastSetVector) {
    FocusBankHarness h;
    const auto control = makeBankControl(MessageType::cc, 1, 20);
    h.setProfile(makeProfile({control}));
    EXPECT_TRUE(h.engine.getTransientAssignments().empty());

    h.setTransient({makeTransient(control, "cutoff")});
    ASSERT_EQ(h.engine.getTransientAssignments().size(), 1u);
    EXPECT_EQ(h.engine.getTransientAssignments()[0].target.parameter.paramId, "cutoff");

    h.setTransient({});
    EXPECT_TRUE(h.engine.getTransientAssignments().empty());
}
