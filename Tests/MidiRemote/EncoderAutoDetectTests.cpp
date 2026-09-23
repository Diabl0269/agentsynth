// EncoderAutoDetectTests.cpp -- FRO134 (docs/control/midi-remote-ui.md#inspector-right): the
// two-step "turn left... now right" classifier. Every relative pattern plus absolute, the two
// states that must fail cleanly, and the state machine's phase order.
#include "MidiRemote/EncoderAutoDetect.h"

#include <gtest/gtest.h>

using synth::Encoding;
using synth::midi::EncoderAutoDetect;
using synth::midi::RemoteEvent;

namespace {

RemoteEvent ccEvent(int channel, int number, int raw) {
    RemoteEvent e;
    e.specType = static_cast<std::uint8_t>(synth::MessageType::cc);
    e.specChannel = static_cast<std::uint8_t>(channel);
    e.specNumber = static_cast<std::uint8_t>(number);
    e.rawValue = static_cast<std::uint8_t>(raw);
    e.kind = synth::midi::RemoteEventKind::absolute;
    return e;
}

} // namespace

TEST(EncoderAutoDetectTests, TwosComplementIsDetectedFromSlowAndFastTurns) {
    EXPECT_EQ(EncoderAutoDetect::classify({127, 127, 127}, {1, 1, 1}), Encoding::relTwos);
    EXPECT_EQ(EncoderAutoDetect::classify({126, 124, 120, 110}, {1, 2, 4, 8}), Encoding::relTwos);
}

TEST(EncoderAutoDetectTests, SignMagnitudeIsToldApartFromTwosByTheLeftTurn) {
    EXPECT_EQ(EncoderAutoDetect::classify({65, 65, 65}, {1, 1, 1}), Encoding::relSignMag);
    EXPECT_EQ(EncoderAutoDetect::classify({66, 68, 72}, {2, 3, 5}), Encoding::relSignMag);
}

TEST(EncoderAutoDetectTests, BinaryOffsetIsCentredOn64) {
    EXPECT_EQ(EncoderAutoDetect::classify({63, 63}, {65, 65}), Encoding::relBinOffset);
    EXPECT_EQ(EncoderAutoDetect::classify({62, 60, 55}, {66, 68, 72}), Encoding::relBinOffset);
}

TEST(EncoderAutoDetectTests, AbsoluteKnobFallingThenRisingIsAbs7) {
    EXPECT_EQ(EncoderAutoDetect::classify({100, 90, 80, 70}, {71, 80, 90, 100}), Encoding::abs7);
    EXPECT_EQ(EncoderAutoDetect::classify({40, 30, 20, 10}, {11, 22, 33, 44}), Encoding::abs7);
}

TEST(EncoderAutoDetectTests, AmbiguousOrEmptyInputIsUndetermined) {
    EXPECT_FALSE(EncoderAutoDetect::classify({}, {1}).has_value());
    EXPECT_FALSE(EncoderAutoDetect::classify({127}, {}).has_value());
    // Both directions the same way round: neither a relative pattern nor a sweep.
    EXPECT_FALSE(EncoderAutoDetect::classify({1, 1}, {1, 1}).has_value());
    EXPECT_FALSE(EncoderAutoDetect::classify({127}, {127}).has_value());
}

TEST(EncoderAutoDetectTests, StateMachineWalksLeftThenRightAndReportsTheResult) {
    EncoderAutoDetect detect;
    EXPECT_EQ(detect.phase(), EncoderAutoDetect::Phase::idle);

    synth::MessageSpec spec;
    spec.type = synth::MessageType::cc;
    spec.channel = 0;
    spec.number = 21;
    detect.start(spec);
    EXPECT_EQ(detect.phase(), EncoderAutoDetect::Phase::turnLeft);

    EXPECT_TRUE(detect.feed(ccEvent(1, 21, 127)));
    EXPECT_TRUE(detect.feed(ccEvent(1, 21, 126)));
    EXPECT_EQ(detect.sampleCount(), 2);

    EXPECT_EQ(detect.advance(), EncoderAutoDetect::Phase::turnRight);
    EXPECT_EQ(detect.sampleCount(), 0);
    EXPECT_TRUE(detect.feed(ccEvent(1, 21, 1)));
    EXPECT_TRUE(detect.feed(ccEvent(1, 21, 2)));

    EXPECT_EQ(detect.advance(), EncoderAutoDetect::Phase::done);
    ASSERT_TRUE(detect.result().has_value());
    EXPECT_EQ(*detect.result(), Encoding::relTwos);
}

TEST(EncoderAutoDetectTests, OnlyTheStartedControlsMessageIsCounted) {
    EncoderAutoDetect detect;
    synth::MessageSpec spec;
    spec.type = synth::MessageType::cc;
    spec.channel = 2;
    spec.number = 21;
    detect.start(spec);

    EXPECT_FALSE(detect.feed(ccEvent(2, 22, 127))) << "another CC number";
    EXPECT_FALSE(detect.feed(ccEvent(3, 21, 127))) << "another channel";
    RemoteEvent note = ccEvent(2, 21, 127);
    note.specType = static_cast<std::uint8_t>(synth::MessageType::note);
    EXPECT_FALSE(detect.feed(note)) << "a note is not a CC";
    EXPECT_EQ(detect.sampleCount(), 0);
    EXPECT_TRUE(detect.feed(ccEvent(2, 21, 127)));
}

TEST(EncoderAutoDetectTests, NoSamplesInEitherPhaseEndsUndeterminedNotDone) {
    EncoderAutoDetect detect;
    synth::MessageSpec spec;
    detect.start(spec);
    detect.advance();
    EXPECT_EQ(detect.advance(), EncoderAutoDetect::Phase::undetermined);
    EXPECT_FALSE(detect.result().has_value());
}

TEST(EncoderAutoDetectTests, CancelReturnsToIdleAndIgnoresLaterEvents) {
    EncoderAutoDetect detect;
    synth::MessageSpec spec;
    spec.number = 21;
    detect.start(spec);
    detect.cancel();
    EXPECT_EQ(detect.phase(), EncoderAutoDetect::Phase::idle);
    EXPECT_FALSE(detect.feed(ccEvent(1, 21, 127)));
}
