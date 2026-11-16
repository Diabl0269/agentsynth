// The Mackie Control workflow end to end on the MIDI Remote rig (MidiRemoteWorkflowRig.h): a Mackie Control profile
// added through the real MidiLearnController binds its port through RemoteSinkRouter, a fader on that fake port moves
// the strip's gain, and moving the gain from the app drives the motor fader back
// (docs/control/midi-remote-mcu-hui.md).

#include "MidiRemoteWorkflowRig.h"

#include "MidiRemote/Surface/SurfaceController.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerFaderTaper.h"

#include <gtest/gtest.h>
#include <vector>

using namespace midi_remote_rig;

namespace {

constexpr const char* kMcuPort = "fake-mcu";

class RecordingFeedback : public RemoteFeedbackSink {
public:
    void sendFeedback(const ControllerProfile::Input& output, const juce::MidiMessage& message) override {
        if (output.identifier == "fake-mcu-out")
            sent.push_back(message);
    }
    std::vector<juce::MidiMessage> sent;
};

class MidiRemoteWorkflowMcuE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midiremote-mcu-e2e-" + juce::Uuid().toString());
        root_.deleteRecursively();
    }
    void TearDown() override { root_.deleteRecursively(); }
    juce::File root_;
};

} // namespace

TEST_F(MidiRemoteWorkflowMcuE2ETest, AMackieFaderMovesTheStripGainAndTheAppsGainMovesTheMotor) {
    Rig rig(root_.getChildFile("controllers"));
    auto stripNode = rig.addModule(std::make_unique<ChannelStripModule>(), "strip-uuid");
    auto* gain = findParameterByID(stripNode->getProcessor(), "gain");
    ASSERT_NE(gain, nullptr);

    // Wired the way MainComponent::wireControlSurfaces does.
    RecordingFeedback feedback;
    RemoteSinkRouter router(rig.remote);
    SurfaceController surfaces(rig.remote, router, rig.engine.getGraph(), rig.invoker, /*bindsHardware=*/true);
    MacroSet macros;
    surfaces.setFeedbackSink(&feedback);
    surfaces.setClock([&] { return rig.nowMs; });
    surfaces.setColumnsProvider(
        [&] { return buildMixerSnapshot(rig.engine.getGraph(), rig.timeline, macros).columns; });
    rig.controller.onProfilesPublished = [&](const std::vector<ControllerProfile>& p) { surfaces.setProfiles(p); };
    surfaces.refreshLayout();
    rig.engine.setRemoteMessageSink(&router);

    ControllerProfile mcu;
    mcu.id = "mcu";
    mcu.name = "Mackie Control";
    mcu.input = {kMcuPort, "Fake MCU"};
    mcu.hasOutput = true;
    mcu.output = {"fake-mcu-out", "Fake MCU Out"};
    mcu.protocol = ControllerProtocol::mcu;
    ASSERT_TRUE(rig.controller.addProfile(mcu));
    ASSERT_EQ(surfaces.boundUnitCount(), 1);

    // The fader, through the engine's own seam: touch, move to three quarters of travel, release.
    const float before = gain->getValue();
    rig.engine.handleIncomingMidiMessageFromSource(kMcuPort, juce::MidiMessage::noteOn(1, 104, (juce::uint8)127));
    rig.engine.handleIncomingMidiMessageFromSource(kMcuPort, juce::MidiMessage::pitchWheel(1, 12288));
    rig.engine.handleIncomingMidiMessageFromSource(kMcuPort, juce::MidiMessage::noteOn(1, 104, (juce::uint8)0));
    rig.remote.drain();
    const float expectedDb = synth::ui::faderFractionToDb(12288.0f / 16383.0f);
    EXPECT_NEAR(gain->convertFrom0to1(gain->getValue()), expectedDb, 0.06f) << "the strip's gain followed the fader";
    EXPECT_TRUE(rig.controller.getProfiles().front().controls.empty()) << "nothing was learned from the surface";

    // Moving the gain in the app (what the on-screen fader does) sends the motor there on the next tick.
    feedback.sent.clear();
    gain->setValueNotifyingHost(gain->convertTo0to1(-12.0f));
    rig.remote.drain();
    std::vector<juce::MidiMessage> motor;
    for (const auto& m : feedback.sent)
        if (m.isPitchWheel())
            motor.push_back(m);
    ASSERT_EQ(motor.size(), 1u) << "one motor move, nothing else changed";
    EXPECT_EQ(motor[0].getChannel(), 1);
    EXPECT_EQ(motor[0].getPitchWheelValue(), juce::roundToInt(synth::ui::faderDbToFraction(-12.0f) * 16383.0f));
    juce::ignoreUnused(before);

    rig.engine.setRemoteMessageSink(&rig.remote); // the rig's own teardown clears its sink
    rig.controller.onProfilesPublished = nullptr;
}
