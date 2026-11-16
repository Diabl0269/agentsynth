// SurfaceController (docs/control/midi-remote-mcu-hui.md#3-surfacecontroller-message-thread-half): banking, the
// touch/idle gesture rules, and the strip buttons reaching the mixer's own paths. The surface is fed through the real
// RemoteSinkRouter, exactly as AudioEngine would hand it a port's messages.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MidiRemote/Surface/SurfaceController.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kPort = "fake-mcu";

class SoloInvoker : public RemoteActionInvoker {
public:
    SoloInvoker(AudioEngine& engine, AppUndoManager& undo)
        : engine_(engine)
        , undo_(undo) {}
    void invokeRemoteCommand(juce::CommandID) override {}
    double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
    void setContinuousValue(ContinuousTargetKind, double) override {}
    bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }
    // The same undo bracket MainComponentRemoteActionInvoker::invokeNodeCommand performs.
    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID node, NodeCommandKind) override {
        auto* strip = dynamic_cast<ChannelStripModule*>(engine_.getGraph().getNodeForId(node)->getProcessor());
        undo_.captureBeforeState(engine_.getGraph());
        engine_.setChannelStripSoloed(node, !strip->isSoloed());
        undo_.pushSnapshotFromCapture(engine_.getGraph());
        ++calls;
    }
    int calls = 0;

private:
    AudioEngine& engine_;
    AppUndoManager& undo_;
};

// What MixerFader and every card do with a gesture: one undo snapshot per begin/end pair.
class UndoGestureListener : public juce::AudioProcessorParameter::Listener {
public:
    UndoGestureListener(AppUndoManager& undo, juce::AudioProcessorGraph& graph)
        : undo_(undo)
        , graph_(graph) {}
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override {
        (starting ? starts : ends)++;
        if (starting)
            undo_.captureBeforeState(graph_);
        else
            undo_.pushSnapshotFromCapture(graph_);
    }
    int starts = 0;
    int ends = 0;

private:
    AppUndoManager& undo_;
    juce::AudioProcessorGraph& graph_;
};

ControllerProfile mcuProfile() {
    ControllerProfile p;
    p.id = "mcu";
    p.name = "Mackie Control";
    p.input = {kPort, "Fake MCU"};
    p.protocol = ControllerProtocol::mcu;
    return p;
}

struct Harness {
    double nowMs = 0.0;
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    TimelineDoc doc;
    MacroSet macros;
    AppUndoManager undo;
    RemoteEngine remote;
    SoloInvoker invoker{engine, undo};
    RemoteSinkRouter router{remote};
    SurfaceController surfaces{remote, router, engine.getGraph(), invoker, /*bindsHardware=*/true};
    std::vector<juce::AudioProcessorGraph::NodeID> strips; // in the mixer's column order

    explicit Harness(int stripCount, int busAt = -1) {
        undo.setRestoreHooks({}, [this] { engine.publishTimeline(doc); });
        for (int i = 0; i < stripCount; ++i) {
            auto module = std::make_unique<ChannelStripModule>();
            if (i == busAt)
                module->setIsBus(true);
            auto node = engine.getGraph().addNode(std::move(module));
            node->properties.set("uuid", "strip-" + juce::String(i));
            strips.push_back(node->nodeID);
        }
        surfaces.setClock([this] { return nowMs; });
        surfaces.setUndoManager(&undo);
        surfaces.setColumnsProvider([this] { return buildMixerSnapshot(engine.getGraph(), doc, macros).columns; });
        surfaces.setProfiles({mcuProfile()});
        surfaces.refreshLayout();
        engine.setRemoteMessageSink(&router);
    }
    ~Harness() {
        engine.setRemoteMessageSink(nullptr);
        surfaces.detachAll();
    }

    void send(const juce::MidiMessage& m) {
        engine.handleIncomingMidiMessageFromSource(kPort, m);
        remote.drain(); // the surfaces drain from RemoteEngine's own tick
    }
    void press(int note) {
        send(juce::MidiMessage::noteOn(1, note, (juce::uint8)127));
        send(juce::MidiMessage::noteOn(1, note, (juce::uint8)0));
    }
    void touch(int strip, bool down) {
        send(juce::MidiMessage::noteOn(1, mcu::kTouchFirstNote + strip, (juce::uint8)(down ? 127 : 0)));
    }
    void fader(int strip, int value) { send(juce::MidiMessage::pitchWheel(strip + 1, value)); }

    ChannelStripModule& strip(int i) {
        return *dynamic_cast<ChannelStripModule*>(engine.getGraph().getNodeForId(strips[(size_t)i])->getProcessor());
    }
    juce::RangedAudioParameter& gain(int i) { return *findParameterByID(&strip(i), "gain"); }
};

} // namespace

TEST(MidiRemoteSurfaceController, BindsOneUnitAndKeepsTheDrainTickAliveWithNoGenericMappings) {
    Harness h(3);
    EXPECT_EQ(h.surfaces.boundUnitCount(), 1);
    EXPECT_TRUE(h.router.owns(kPort));
    h.surfaces.setProfiles({});
    EXPECT_EQ(h.surfaces.boundUnitCount(), 0);
    EXPECT_FALSE(h.router.owns(kPort));
}

TEST(MidiRemoteSurfaceController, APluginBuildBindsNothing) {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    RemoteEngine remote;
    AppUndoManager undo;
    SoloInvoker invoker{engine, undo};
    RemoteSinkRouter router{remote};
    SurfaceController surfaces{remote, router, engine.getGraph(), invoker, /*bindsHardware=*/false};
    surfaces.setProfiles({mcuProfile()});
    EXPECT_EQ(surfaces.boundUnitCount(), 0);
    EXPECT_FALSE(router.owns(kPort));
}

TEST(MidiRemoteSurfaceController, BankingOverElevenStripsAndABusMapsSlotsToMixerColumns) {
    Harness h(12, /*busAt=*/5);
    std::vector<juce::AudioProcessorGraph::NodeID> columns;
    for (const auto& c : buildMixerSnapshot(h.engine.getGraph(), h.doc, h.macros).columns)
        if (c.kind == MixerColumn::Kind::Strip || c.kind == MixerColumn::Kind::Bus)
            columns.push_back(c.nodeId);
    ASSERT_EQ(columns.size(), 12u);

    for (int s = 0; s < 8; ++s)
        EXPECT_EQ(h.surfaces.nodeForStrip(s), columns[(size_t)s]) << "bank 1, strip " << s;

    h.press(mcu::kBankRightNote);
    EXPECT_EQ(h.surfaces.bankOffset(), 4) << "clamped so the last bank is full: 12 - 8";
    for (int s = 0; s < 8; ++s)
        EXPECT_EQ(h.surfaces.nodeForStrip(s), columns[(size_t)(4 + s)]);

    h.press(mcu::kChannelLeftNote);
    EXPECT_EQ(h.surfaces.bankOffset(), 3);
    h.press(mcu::kBankLeftNote);
    EXPECT_EQ(h.surfaces.bankOffset(), 0);
    h.press(mcu::kChannelLeftNote);
    EXPECT_EQ(h.surfaces.bankOffset(), 0) << "never below the first column";
}

TEST(MidiRemoteSurfaceController, TouchThenMovesThenReleaseIsOneGestureAndOneUndoStep) {
    Harness h(2);
    UndoGestureListener listener(h.undo, h.engine.getGraph());
    h.gain(0).addListener(&listener);
    const float before = h.gain(0).getValue();

    h.touch(0, true);
    for (int v : {4000, 8000, 12000, 16383}) {
        h.fader(0, v);
        h.nowMs += 1000.0; // far past the idle window: a held touch never expires
    }
    EXPECT_EQ(h.surfaces.activeGestureCount(), 1);
    h.touch(0, false);

    EXPECT_EQ(listener.starts, 1);
    EXPECT_EQ(listener.ends, 1);
    EXPECT_NEAR(h.gain(0).convertFrom0to1(h.gain(0).getValue()), ChannelStripModule::kMaxGainDb, 0.05f)
        << "full fader travel is +12 dB through the mixer fader's taper";
    ASSERT_TRUE(h.undo.undo());
    EXPECT_NEAR(h.gain(0).getValue(), before, 1.0e-4f) << "one Cmd+Z undoes the whole touch";
    h.gain(0).removeListener(&listener);
}

TEST(MidiRemoteSurfaceController, WithoutTouchNotesTheGestureEndsAfterTheIdleWindow) {
    Harness h(1);
    UndoGestureListener listener(h.undo, h.engine.getGraph());
    h.gain(0).addListener(&listener);
    for (int v : {2000, 4000, 6000}) {
        h.fader(0, v);
        h.nowMs += kGestureIdleMs / 5.0;
    }
    EXPECT_EQ(listener.starts, 1);
    EXPECT_EQ(listener.ends, 0);
    h.nowMs += kGestureIdleMs + 1.0;
    h.remote.drain();
    EXPECT_EQ(listener.ends, 1);
    EXPECT_EQ(h.surfaces.activeGestureCount(), 0);
    h.gain(0).removeListener(&listener);
}

TEST(MidiRemoteSurfaceController, SoloPressInvokesTheNodeCommandOnceAsOneUndoStep) {
    Harness h(3);
    h.press(mcu::kSoloFirstNote + 1);
    EXPECT_EQ(h.invoker.calls, 1) << "the press, not the release";
    EXPECT_TRUE(h.strip(1).isSoloed());
    EXPECT_FALSE(h.strip(0).isSoloed());
    ASSERT_TRUE(h.undo.undo());
    EXPECT_FALSE(h.strip(1).isSoloed());
    EXPECT_FALSE(h.undo.canUndo()) << "exactly one step";
}

TEST(MidiRemoteSurfaceController, MutePressTogglesTheStripsMuteAsOneUndoStep) {
    Harness h(2);
    auto* muted = findParameterByID(&h.strip(1), "muted");
    h.press(mcu::kMuteFirstNote + 1);
    EXPECT_GT(muted->getValue(), 0.5f);
    h.press(mcu::kMuteFirstNote + 1);
    EXPECT_LT(muted->getValue(), 0.5f);
    ASSERT_TRUE(h.undo.undo());
    EXPECT_GT(muted->getValue(), 0.5f);
}

TEST(MidiRemoteSurfaceController, SelectHandsTheStripsNodeToTheApp) {
    Harness h(3);
    juce::AudioProcessorGraph::NodeID selected;
    h.surfaces.onSelectNode = [&](juce::AudioProcessorGraph::NodeID node) { selected = node; };
    h.press(mcu::kSelectFirstNote + 2);
    EXPECT_EQ(selected, h.strips[2]);
    h.press(mcu::kSelectFirstNote + 7);
    EXPECT_EQ(selected, h.strips[2]) << "an empty slot selects nothing";
}

TEST(MidiRemoteSurfaceController, AGestureOnADeletedStripIsDroppedOnReconcile) {
    Harness h(2);
    h.touch(0, true);
    h.fader(0, 5000);
    ASSERT_EQ(h.surfaces.activeGestureCount(), 1);
    h.engine.getGraph().removeNode(h.strips[0]);
    h.surfaces.refreshLayout();
    EXPECT_EQ(h.surfaces.activeGestureCount(), 0);
    h.touch(0, false); // releasing now touches nothing freed
}
