// SurfaceRenderer (docs/control/midi-remote-mcu-hui.md#4-the-surface-renderer-feedback) against a fake feedback sink:
// diff-only sends, the touched-fader rule and the exact Mackie Control bytes.

#include "MidiRemote/Surface/SurfaceRenderer.h"

#include <gtest/gtest.h>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

class FakeSink : public RemoteFeedbackSink {
public:
    void sendFeedback(const ControllerProfile::Input& output, const juce::MidiMessage& message) override {
        outputs.push_back(output.identifier);
        sent.push_back(message);
    }
    std::vector<juce::String> outputs;
    std::vector<juce::MidiMessage> sent;

    int count() const { return static_cast<int>(sent.size()); }
    void clear() {
        sent.clear();
        outputs.clear();
    }
};

std::vector<juce::uint8> bytes(const juce::MidiMessage& m) {
    return {m.getRawData(), m.getRawData() + m.getRawDataSize()};
}

constexpr int kElements = mcu::kStripsPerUnit + SurfaceFrame::kManagedLeds; // 8 faders + 32 LEDs

} // namespace

TEST(MidiRemoteSurfaceRenderer, FirstRenderIsAFullResyncThenOnlyDifferencesAreSent) {
    FakeSink sink;
    SurfaceRenderer renderer(sink, {"mcu-out", "MCU Out"});
    SurfaceFrame frame;
    renderer.render(frame);
    EXPECT_EQ(sink.count(), kElements) << "a new unit shows nothing we sent: every fader and LED goes out";
    EXPECT_EQ(sink.outputs.front(), "mcu-out");

    sink.clear();
    renderer.render(frame);
    EXPECT_EQ(sink.count(), 0) << "nothing changed, nothing sent";

    frame.fader[2] = 12000;
    frame.led[mcu::kMuteFirstNote + 2] = true;
    renderer.render(frame);
    ASSERT_EQ(sink.count(), 2);
    EXPECT_EQ(bytes(sink.sent[0]), (std::vector<juce::uint8>{0xE2, 12000 & 0x7F, 12000 >> 7}))
        << "pitch bend on channel 3, 14-bit LSB first";
    EXPECT_EQ(bytes(sink.sent[1]), (std::vector<juce::uint8>{0x90, 18, 0x7F})) << "MUTE 3 LED on is note 18 vel 7F";

    sink.clear();
    frame.led[mcu::kMuteFirstNote + 2] = false;
    renderer.render(frame);
    ASSERT_EQ(sink.count(), 1);
    EXPECT_EQ(bytes(sink.sent[0]), (std::vector<juce::uint8>{0x90, 18, 0x00})) << "LED off is velocity 00";
}

TEST(MidiRemoteSurfaceRenderer, ATouchedFaderIsNeverDrivenAndItsFinalValueGoesOutOnRelease) {
    FakeSink sink;
    SurfaceRenderer renderer(sink, {"mcu-out", "MCU Out"});
    SurfaceFrame frame;
    renderer.render(frame);
    sink.clear();

    renderer.setTouched(0, true);
    for (int v : {1000, 2000, 3000}) {
        frame.fader[0] = v;
        renderer.render(frame);
    }
    EXPECT_EQ(sink.count(), 0) << "the hand is on the fader: no motor echo";

    renderer.setTouched(0, false);
    renderer.render(frame);
    ASSERT_EQ(sink.count(), 1);
    EXPECT_EQ(bytes(sink.sent[0]), (std::vector<juce::uint8>{0xE0, 3000 & 0x7F, 3000 >> 7}));

    sink.clear();
    renderer.setTouched(1, true);
    renderer.setTouched(1, false);
    renderer.render(frame);
    EXPECT_EQ(sink.count(), 1) << "release resends even an unchanged value: the motor may sit off it";
}

TEST(MidiRemoteSurfaceRenderer, MarkDirtyResendsEverythingOnce) {
    FakeSink sink;
    SurfaceRenderer renderer(sink, {"mcu-out", "MCU Out"});
    SurfaceFrame frame;
    renderer.render(frame);
    sink.clear();
    renderer.markDirty(); // a bank change or the device-ready message
    renderer.render(frame);
    EXPECT_EQ(sink.count(), kElements);
    sink.clear();
    renderer.render(frame);
    EXPECT_EQ(sink.count(), 0);
}
