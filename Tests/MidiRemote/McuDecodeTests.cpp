// McuSurface's MIDI-thread decode (docs/control/midi-remote-mcu-hui.md#2-protocolsurface-midi-path-half): every wire
// number is the doc's Mackie Control table. Suite names contain "MidiRemote" so they match the verification filter.

#include "MidiRemote/Surface/McuProtocol.h"
#include "MidiRemote/Surface/McuSurface.h"

#include <gtest/gtest.h>
#include <vector>

using namespace synth::midi;

namespace {

std::vector<SurfaceEvent> decodeAll(McuSurface& surface, std::initializer_list<juce::MidiMessage> messages) {
    for (const auto& m : messages)
        surface.decode(m);
    std::vector<SurfaceEvent> out;
    surface.drain([&](const SurfaceEvent& e) { out.push_back(e); });
    return out;
}

juce::MidiMessage note(int number, int velocity) {
    return juce::MidiMessage::noteOn(1, number, static_cast<juce::uint8>(velocity));
}

} // namespace

TEST(MidiRemoteMcuDecode, PitchBendOnChannelsOneToNineIsAFaderWithTheFourteenBitValue) {
    McuSurface surface;
    const auto events =
        decodeAll(surface, {juce::MidiMessage::pitchWheel(1, 0), juce::MidiMessage::pitchWheel(8, 16383),
                            juce::MidiMessage::pitchWheel(9, 8192), juce::MidiMessage::pitchWheel(10, 100)});
    ASSERT_EQ(events.size(), 3u) << "channel 10 is not an MCU fader";
    EXPECT_EQ(events[0].kind, SurfaceEventKind::fader);
    EXPECT_EQ(events[0].index, 0);
    EXPECT_EQ(events[0].value, 0);
    EXPECT_EQ(events[1].index, 7);
    EXPECT_EQ(events[1].value, 16383);
    EXPECT_EQ(events[2].index, mcu::kMasterFaderIndex) << "channel 9 is the master fader";
    EXPECT_EQ(events[2].value, 8192);
}

TEST(MidiRemoteMcuDecode, TouchNotesAreTouchedAboveVelocity64AndReleasedAtOrBelow) {
    McuSurface surface;
    const auto events = decodeAll(surface, {note(104, 127), note(111, 65), note(112, 127), note(104, 0), note(105, 64),
                                            juce::MidiMessage::noteOff(1, 106)});
    ASSERT_EQ(events.size(), 6u);
    for (const auto& e : events)
        EXPECT_EQ(e.kind, SurfaceEventKind::touch);
    EXPECT_EQ(events[0].index, 0);
    EXPECT_EQ(events[0].value, 1);
    EXPECT_EQ(events[1].index, 7);
    EXPECT_EQ(events[1].value, 1) << "65 > 64 is touched";
    EXPECT_EQ(events[2].index, 8) << "112 is the master fader's touch";
    EXPECT_EQ(events[3].value, 0);
    EXPECT_EQ(events[4].value, 0) << "64 is not above 64";
    EXPECT_EQ(events[5].index, 2);
    EXPECT_EQ(events[5].value, 0) << "a note-off releases";
}

TEST(MidiRemoteMcuDecode, StripAndBankButtonsAreButtonsCarryingTheirNoteNumber) {
    McuSurface surface;
    const auto events = decodeAll(surface, {note(mcu::kMuteFirstNote, 127), note(mcu::kSoloFirstNote + 3, 127),
                                            note(mcu::kSelectFirstNote + 7, 127), note(mcu::kBankLeftNote, 127),
                                            note(mcu::kBankRightNote, 127), note(mcu::kChannelLeftNote, 127),
                                            note(mcu::kChannelRightNote, 127), note(mcu::kMuteFirstNote, 0)});
    ASSERT_EQ(events.size(), 8u);
    const int expected[] = {16, 11, 31, 46, 47, 48, 49, 16};
    for (std::size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(events[i].kind, SurfaceEventKind::button);
        EXPECT_EQ(events[i].index, expected[i]);
    }
    EXPECT_EQ(events[0].value, 1);
    EXPECT_EQ(events[7].value, 0) << "velocity 0 is the release";
}

TEST(MidiRemoteMcuDecode, VPotsAndJogAreSignMagnitudeDeltas) {
    McuSurface surface;
    const auto events = decodeAll(surface, {juce::MidiMessage::controllerEvent(1, 16, 0x03),
                                            juce::MidiMessage::controllerEvent(1, 23, 0x41),
                                            juce::MidiMessage::controllerEvent(1, mcu::kJogCc, 0x45)});
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].kind, SurfaceEventKind::encoderDelta);
    EXPECT_EQ(events[0].value, 3);
    EXPECT_EQ(events[1].index, 7);
    EXPECT_EQ(events[1].value, -1) << "bit 6 set is counter-clockwise";
    EXPECT_EQ(events[2].kind, SurfaceEventKind::jog);
    EXPECT_EQ(events[2].value, -5);
}

TEST(MidiRemoteMcuDecode, DeviceReadySysexIsCopiedWholeAndRecognised) {
    McuSurface surface;
    const juce::uint8 ready[] = {0xF0, 0x00, 0x00, 0x66, 0x14, 0x01, 1, 2, 3, 4, 5, 6, 7, 0xF7};
    const juce::uint8 other[] = {0xF0, 0x00, 0x00, 0x66, 0x14, 0x03, 0xF7};
    const auto events =
        decodeAll(surface, {juce::MidiMessage(ready, sizeof(ready)), juce::MidiMessage(other, sizeof(other))});
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, SurfaceEventKind::sysex);
    EXPECT_EQ(events[0].sysexLen, sizeof(ready));
    EXPECT_EQ(events[0].sysex[0], 0xF0);
    EXPECT_EQ(events[0].sysex[sizeof(ready) - 1], 0xF7);
    EXPECT_TRUE(McuSurface::isDeviceReady(events[0]));
    EXPECT_FALSE(McuSurface::isDeviceReady(events[1])) << "type 03 is connection-confirmed, not device-ready";
}

TEST(MidiRemoteMcuDecode, SysexLongerThanTheSlotIsDroppedAndCounted) {
    McuSurface surface;
    std::vector<juce::uint8> lcd{0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00};
    lcd.insert(lcd.end(), 56, 'A');
    lcd.push_back(0xF7);
    const auto events = decodeAll(surface, {juce::MidiMessage(lcd.data(), static_cast<int>(lcd.size()))});
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(surface.droppedCount(), 1);
}
