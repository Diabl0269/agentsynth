// FRO253 (docs/control/midi-remote.md#node-command-targets) end-to-end: a hardware press assigned
// to a mixer column's Solo really flips ChannelStripModule::soloed_ and AudioEngine's soloed-strip
// count, through the SAME real seam every parameter/action test in this directory drives --
// AudioEngine::handleIncomingMidiMessageFromSource -> RemoteEngine::drain() -- rather than calling
// RemoteEngine::handleMessage()/applyEvent() directly. The invoker is a small test double that
// performs EXACTLY the calls MainComponent::RemoteActionInvokerImpl::invokeNodeCommand does
// (captureBeforeState/setChannelStripSoloed/pushSnapshotFromCapture): RemoteActionInvokerImpl
// itself is a private nested type of MainComponent, which this headless suite has no reason to
// construct (it needs an ApplicationCommandManager, a ShortcutManager and a whole app's worth of
// UI scaffolding for no benefit here) -- see this file's own TEST bodies for the exact mirroring.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";
constexpr const char* kStripUuid = "strip-node-uuid";

Control makeButtonControl(const juce::String& id, int noteNumber) {
    Control c;
    c.id = id;
    c.name = id;
    c.kind = ControlKind::button;
    c.message.type = MessageType::note;
    c.message.channel = 1;
    c.message.number = noteNumber;
    return c;
}

ControllerProfile makeProfile(const Control& control) {
    ControllerProfile p;
    p.id = "profile";
    p.name = "profile";
    p.input.identifier = kSource;
    p.input.name = kSource;
    p.controls.push_back(control);
    return p;
}

Assignment makeSoloAssignment(int noteNumber) {
    Assignment a;
    a.id = "solo-assignment";
    a.control.profileId = "profile";
    a.control.controlId = "pad";
    a.spec.type = MessageType::note;
    a.spec.channel = 1;
    a.spec.number = noteNumber;
    a.specEncoding = Encoding::abs7;
    a.target.kind = Target::Kind::nodeCommand;
    a.target.nodeCommand.nodeUuid = kStripUuid;
    a.target.nodeCommand.command = NodeCommandKind::toggleSolo;
    return a;
}

// Mirrors MainComponent::RemoteActionInvokerImpl::invokeNodeCommand's body exactly -- see this
// file's own header comment for why that real type isn't used directly.
class ToggleSoloInvoker : public RemoteActionInvoker {
public:
    ToggleSoloInvoker(AudioEngine& engine, AppUndoManager& undo)
        : engine_(engine)
        , undo_(undo) {}

    void invokeRemoteCommand(juce::CommandID) override {}

    // FRO236: this suite doesn't exercise continuous targets -- stub, never called.
    double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
    void setContinuousValue(ContinuousTargetKind, double) override {}
    bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }

    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, NodeCommandKind command) override {
        if (command != NodeCommandKind::toggleSolo)
            return;
        auto& graph = engine_.getGraph();
        auto* node = graph.getNodeForId(nodeId);
        auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
        if (strip == nullptr)
            return;
        undo_.captureBeforeState(graph);
        engine_.setChannelStripSoloed(nodeId, !strip->isSoloed());
        undo_.pushSnapshotFromCapture(graph);
        ++applyCount;
    }

    int applyCount = 0;

private:
    AudioEngine& engine_;
    AppUndoManager& undo_;
};

struct E2EHarness {
    AudioEngine engine{AudioEngine::HostMode::Hosted}; // never opens hardware MIDI -- see Source/CLAUDE.md
    synth::TimelineDoc doc; // unused beyond publishTimeline()'s signature -- no tracks needed here
    AppUndoManager undo;
    RemoteEngine remote;
    ToggleSoloInvoker invoker{engine, undo};
    juce::AudioProcessorGraph::Node::Ptr stripNode;
    ChannelStripModule* strip = nullptr;

    E2EHarness() {
        stripNode = engine.getGraph().addNode(std::make_unique<ChannelStripModule>());
        stripNode->properties.set("uuid", juce::String(kStripUuid));
        strip = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());

        // Mirrors MainComponent: every undo/redo restore ends in the reconcile + publish seam that
        // recounts soloed strips (Source/CLAUDE.md's mixer-solo invariant) -- see MixerSoloTests.cpp's
        // own AppUndoManagerRewindsAcrossGraphRebuild-style fixture for the same wiring.
        undo.setRestoreHooks({}, [this] { engine.publishTimeline(doc); });

        remote.setActionInvoker(&invoker);
        remote.setProfiles({makeProfile(makeButtonControl("pad", 40))});
        remote.setSources({juce::String(kSource)});
        remote.setAssignments({makeSoloAssignment(40)});
        remote.reconcile(engine.getGraph());

        engine.setRemoteMessageSink(&remote);
    }

    ~E2EHarness() { engine.setRemoteMessageSink(nullptr); }

    void pressPad() {
        engine.handleIncomingMidiMessageFromSource(kSource, juce::MidiMessage::noteOn(1, 40, (juce::uint8)100));
        remote.drain();
    }
};

} // namespace

TEST(MidiRemoteNodeCommandE2ETest, PressTogglesSoloAndUpdatesTheEngineSoloedCount) {
    E2EHarness h;
    ASSERT_FALSE(h.strip->isSoloed());
    ASSERT_EQ(h.engine.getSoloedStripCount(), 0);

    h.pressPad();

    EXPECT_TRUE(h.strip->isSoloed());
    EXPECT_EQ(h.engine.getSoloedStripCount(), 1) << "captureBeforeState/pushSnapshotFromCapture's restore "
                                                    "re-publishes the timeline, which recounts soloed strips";
}

TEST(MidiRemoteNodeCommandE2ETest, SecondPressFlipsBack) {
    E2EHarness h;
    h.pressPad();
    ASSERT_TRUE(h.strip->isSoloed());

    h.pressPad();

    EXPECT_FALSE(h.strip->isSoloed());
    EXPECT_EQ(h.engine.getSoloedStripCount(), 0);
    EXPECT_EQ(h.invoker.applyCount, 2);
}

TEST(MidiRemoteNodeCommandE2ETest, UndoRestoresTheStripsSoloState) {
    E2EHarness h;
    h.pressPad();
    ASSERT_TRUE(h.strip->isSoloed());
    ASSERT_TRUE(h.undo.canUndo());

    h.undo.undo();

    EXPECT_FALSE(h.strip->isSoloed());
    EXPECT_EQ(h.engine.getSoloedStripCount(), 0);
}
