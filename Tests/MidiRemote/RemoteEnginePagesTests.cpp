// FRO142 (docs/control/midi-remote.md#pages): page filtering in RemoteEngineReconcile.cpp, the page
// Target in RemoteEngineApply.cpp, takeover across a switch, feedback resync, and
// getEffectivePageCount/resetActivePages. Mirrors RemoteEngineApplyTests.cpp's ApplyHarness (one
// real AudioProcessorGraph + FilterModule node, fake clock) so applying a real page-2 assignment
// really moves a real juce::AudioProcessorParameter. Suite name contains "MidiRemote" per the
// ship-task --gtest_filter convention.

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

Control makeControl(const juce::String& id, MessageType type, int channel, int number, ControlKind kind) {
    Control c;
    c.id = id;
    c.name = id;
    c.kind = kind;
    c.message.type = type;
    c.message.channel = channel;
    c.message.number = number;
    return c;
}

ControllerProfile makeProfile(std::vector<Control> controls, int pageCount = 1) {
    ControllerProfile p;
    p.id = kProfileId;
    p.name = kProfileId;
    p.input.identifier = kSource;
    p.input.name = kSource;
    p.controls = std::move(controls);
    p.pageCount = pageCount;
    return p;
}

Assignment makeParamAssignment(const juce::String& id, const juce::String& controlId, MessageType type, int channel,
                               int number, const juce::String& paramId, int page, Takeover takeover = Takeover::jump) {
    Assignment a;
    a.id = id;
    a.control.profileId = kProfileId;
    a.control.controlId = controlId;
    a.spec.type = type;
    a.spec.channel = channel;
    a.spec.number = number;
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = juce::String(kNodeUuid);
    a.target.parameter.paramId = paramId;
    a.takeover = takeover;
    a.page = page;
    return a;
}

Assignment makePageAssignment(const juce::String& id, const juce::String& controlId, int noteNumber,
                              PageCommand command, int gotoPage = 1) {
    Assignment a;
    a.id = id;
    a.control.profileId = kProfileId;
    a.control.controlId = controlId;
    a.spec.type = MessageType::note;
    a.spec.channel = 1;
    a.spec.number = noteNumber;
    a.target.kind = Target::Kind::page;
    a.target.page.command = command;
    a.target.page.page = gotoPage;
    return a;
}

struct PagesHarness {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr node;
    RemoteEngine engine;
    double fakeNowMs = 0.0;

    PagesHarness() {
        node = graph.addNode(std::make_unique<FilterModule>());
        node->properties.set("uuid", juce::String(kNodeUuid));
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            mb->setNodeUuid(kNodeUuid);
        engine.setClock([this] { return fakeNowMs; });
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(node->getProcessor(), "cutoff"); }

    void publish(ControllerProfile profile, std::vector<Assignment> assignments) {
        engine.setProfiles({std::move(profile)});
        engine.setSources({juce::String(kSource)});
        engine.setAssignments(std::move(assignments));
        engine.reconcile(graph);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }
    void advance(double ms) { fakeNowMs += ms; }
};

} // namespace

// ============================================================================
// Page filtering: only the active page's project assignment gets a slot
// ============================================================================

TEST(MidiRemoteEnginePagesTest, OnlyActivePageAssignmentAppliesForASharedControl) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 2),
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, "cutoff", 1),
               makeParamAssignment("a2", "knob", MessageType::cc, 1, 10, "resonance", 2)});

    ASSERT_EQ(h.engine.getActivePage(kProfileId), 1);
    h.send(juce::MidiMessage::controllerEvent(1, 10, 100));
    h.engine.drain();
    const float cutoffAfterPage1 = h.cutoff()->getValue();
    EXPECT_GT(cutoffAfterPage1, 0.5f);
    auto* resonance = findParameterByID(h.node->getProcessor(), "resonance");
    const float resonanceBefore = resonance->getValue();

    h.engine.setActivePage(kProfileId, 2);
    ASSERT_EQ(h.engine.getActivePage(kProfileId), 2);
    h.send(juce::MidiMessage::controllerEvent(1, 10, 100));
    h.engine.drain();
    EXPECT_FLOAT_EQ(h.cutoff()->getValue(), cutoffAfterPage1) << "page 1's target must not move once page 2 is active";
    EXPECT_GT(resonance->getValue(), resonanceBefore) << "the same CC now drives page 2's target";
}

TEST(MidiRemoteEnginePagesTest, GlobalActionWorksOnEveryPage) {
    PagesHarness h;
    ControllerProfile profile = makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob),
                                             makeControl("pad", MessageType::note, 1, 40, ControlKind::button)},
                                            2);
    Assignment nodeCmd;
    nodeCmd.id = "action";
    nodeCmd.control.profileId = kProfileId;
    nodeCmd.control.controlId = "pad";
    nodeCmd.spec.type = MessageType::note;
    nodeCmd.spec.channel = 1;
    nodeCmd.spec.number = 40;
    nodeCmd.target.kind = Target::Kind::nodeCommand;
    nodeCmd.target.nodeCommand.nodeUuid = juce::String(kNodeUuid);
    nodeCmd.target.nodeCommand.command = NodeCommandKind::toggleSolo;
    profile.actions.push_back(nodeCmd);

    h.engine.setProfiles({profile});
    h.engine.setSources({juce::String(kSource)});
    h.engine.setAssignments({});
    h.engine.reconcile(h.graph);

    struct CountingInvoker : RemoteActionInvoker {
        void invokeRemoteCommand(juce::CommandID) override {}
        void invokeNodeCommand(juce::AudioProcessorGraph::NodeID, NodeCommandKind) override { ++count; }
        double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
        void setContinuousValue(ContinuousTargetKind, double) override {}
        bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }
        int count = 0;
    } invoker;
    h.engine.setActionInvoker(&invoker);
    h.engine.reconcile(h.graph); // resolve slot.nodeId now that the invoker/graph are wired

    h.send(juce::MidiMessage::noteOn(1, 40, (juce::uint8)100));
    h.engine.drain();
    EXPECT_EQ(invoker.count, 1);

    h.engine.setActivePage(kProfileId, 2);
    h.send(juce::MidiMessage::noteOn(1, 40, (juce::uint8)100));
    h.engine.drain();
    EXPECT_EQ(invoker.count, 2) << "a GLOBAL profile action fires on every page";
}

// ============================================================================
// The page Target itself: next/previous/go, driven through the real MIDI path
// ============================================================================

TEST(MidiRemoteEnginePagesTest, NextPreviousAndGoWrapAroundThroughTheRealMessagePath) {
    PagesHarness h;
    ControllerProfile profile = makeProfile({makeControl("nextBtn", MessageType::note, 1, 41, ControlKind::button),
                                             makeControl("prevBtn", MessageType::note, 1, 42, ControlKind::button),
                                             makeControl("goBtn", MessageType::note, 1, 43, ControlKind::button)},
                                            3);
    profile.actions.push_back(makePageAssignment("next", "nextBtn", 41, PageCommand::next));
    profile.actions.push_back(makePageAssignment("prev", "prevBtn", 42, PageCommand::previous));
    profile.actions.push_back(makePageAssignment("go", "goBtn", 43, PageCommand::go, 3));

    h.engine.setProfiles({profile});
    h.engine.setSources({juce::String(kSource)});
    h.engine.setAssignments({});
    h.engine.reconcile(h.graph);

    ASSERT_EQ(h.engine.getActivePage(kProfileId), 1);

    h.send(juce::MidiMessage::noteOn(1, 41, (juce::uint8)100));
    h.engine.drain();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 2);

    h.send(juce::MidiMessage::noteOn(1, 41, (juce::uint8)100));
    h.engine.drain();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 3);

    h.send(juce::MidiMessage::noteOn(1, 41, (juce::uint8)100)); // next past the last page wraps to 1
    h.engine.drain();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 1);

    h.send(juce::MidiMessage::noteOn(1, 42, (juce::uint8)100)); // previous below page 1 wraps to the last
    h.engine.drain();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 3);

    h.send(juce::MidiMessage::noteOn(1, 43, (juce::uint8)100)); // go(3): already there -- a no-op, not a crash
    h.engine.drain();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 3);

    // note-off must never switch a page (press only, like every other button target).
    h.send(juce::MidiMessage::noteOff(1, 41, (juce::uint8)0));
    h.engine.drain();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 3);
}

// ============================================================================
// Takeover must not jump after a switch, and a switch republishes + re-sends feedback
// ============================================================================

TEST(MidiRemoteEnginePagesTest, TakeoverDoesNotJumpAfterASwitch) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 2),
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, "cutoff", 1, Takeover::jump),
               makeParamAssignment("a2", "knob", MessageType::cc, 1, 10, "resonance", 2, Takeover::scale)});

    // Move page 1's target away from resonance's current (default) value first.
    h.send(juce::MidiMessage::controllerEvent(1, 10, 20));
    h.engine.drain();

    auto* resonance = findParameterByID(h.node->getProcessor(), "resonance");
    const float resonanceBefore = resonance->getValue();

    h.engine.setActivePage(kProfileId, 2);
    // Page 2's assignment ("a2") has never seen a hardware event -- its Scale takeover state is
    // brand new, so the FIRST hardware value after the switch must not move the parameter at all
    // (RemoteEngineApply.cpp's isNewGesture -> shouldApply=false branch), regardless of how far the
    // hardware sits from the parameter's current value.
    h.send(juce::MidiMessage::controllerEvent(1, 10, 100)); // hardware nowhere near the parameter's rest value
    h.engine.drain();
    EXPECT_FLOAT_EQ(resonance->getValue(), resonanceBefore) << "the newly active page's takeover must not jump";

    // A second, DIFFERENT hardware value converges normally -- an ordinary Scale sweep, not a jump
    // (scaleTarget moves the parameter by how far the hardware itself moved since the last event,
    // so the second event must differ from the first or nothing would move).
    h.send(juce::MidiMessage::controllerEvent(1, 10, 127));
    h.engine.drain();
    EXPECT_GT(resonance->getValue(), resonanceBefore);
}

// ============================================================================
// resetActivePages() / getEffectivePageCount()
// ============================================================================

TEST(MidiRemoteEnginePagesTest, ProjectLoadResetsActivePageToOne) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 3), {});
    h.engine.setActivePage(kProfileId, 3);
    ASSERT_EQ(h.engine.getActivePage(kProfileId), 3);

    h.engine.resetActivePages();
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 1);
}

TEST(MidiRemoteEnginePagesTest, EffectivePageCountIsWidenedByTheHighestAssignmentPage) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 2),
              {makeParamAssignment("a1", "knob", MessageType::cc, 1, 10, "cutoff", 5)});
    EXPECT_EQ(h.engine.getEffectivePageCount(kProfileId), 5) << "a project assignment may reference a page beyond "
                                                                "the profile's own declared pageCount";
}

TEST(MidiRemoteEnginePagesTest, EffectivePageCountDefaultsToProfilePageCountWithNoAssignments) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 4), {});
    EXPECT_EQ(h.engine.getEffectivePageCount(kProfileId), 4);
}

TEST(MidiRemoteEnginePagesTest, SetActivePageClampsToTheEffectiveCount) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 2), {});
    h.engine.setActivePage(kProfileId, 99);
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 2);
    h.engine.setActivePage(kProfileId, 0);
    EXPECT_EQ(h.engine.getActivePage(kProfileId), 1);
}

TEST(MidiRemoteEnginePagesTest, SetActivePageFiresOnActivePageChangedOnlyWhenItActuallyChanges) {
    PagesHarness h;
    h.publish(makeProfile({makeControl("knob", MessageType::cc, 1, 10, ControlKind::knob)}, 2), {});
    int fired = 0;
    juce::String lastProfile;
    int lastPage = -1;
    h.engine.onActivePageChanged = [&](const juce::String& profileId, int newPage) {
        ++fired;
        lastProfile = profileId;
        lastPage = newPage;
    };
    h.engine.setActivePage(kProfileId, 1); // already page 1 -- no-op
    EXPECT_EQ(fired, 0);
    h.engine.setActivePage(kProfileId, 2);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(lastProfile, juce::String(kProfileId));
    EXPECT_EQ(lastPage, 2);
}
