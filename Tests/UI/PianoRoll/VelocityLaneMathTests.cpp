// VelocityLaneMath unit tests: the velocity strip's pure maths — the y <-> velocity mapping and its
// clamps, which stick a press grabs (chords tie-broken by y), the pen/ramp line values including the
// "a fast stroke skips no stick" interpolation, and humanize's range, clamping and determinism.

#include "UI/PianoRoll/VelocityLane/VelocityLaneMath.h"

#include <gtest/gtest.h>

using namespace synth::ui::velocitylane;
using synth::NoteId;

namespace {
constexpr float kTop = 6.0f;
constexpr float kBottom = 60.0f;

StickPoint stick(std::int64_t id, int x, int velocity) {
    return {NoteId{id}, x, yForVelocity(velocity, kTop, kBottom)};
}

int valueFor(const std::vector<std::pair<NoteId, int>>& hits, std::int64_t id) {
    for (const auto& [noteId, v] : hits)
        if (noteId.value == id)
            return v;
    return -1;
}
} // namespace

TEST(VelocityLaneMathTest, EveryVelocityRoundTripsThroughItsY) {
    for (int v = 1; v <= 127; ++v)
        EXPECT_EQ(velocityForY(yForVelocity(v, kTop, kBottom), kTop, kBottom), v) << "velocity " << v;
    EXPECT_FLOAT_EQ(yForVelocity(127, kTop, kBottom), kTop);
    EXPECT_FLOAT_EQ(yForVelocity(1, kTop, kBottom), kBottom);
}

TEST(VelocityLaneMathTest, OutOfRangeValuesAndPointersClampToTheMidiRange) {
    EXPECT_EQ(velocityForY(kTop - 50.0f, kTop, kBottom), 127) << "above the strip pins at the top value";
    EXPECT_EQ(velocityForY(kBottom + 50.0f, kTop, kBottom), 1) << "below it pins at 1, never 0";
    EXPECT_FLOAT_EQ(yForVelocity(500, kTop, kBottom), kTop);
    EXPECT_FLOAT_EQ(yForVelocity(-3, kTop, kBottom), kBottom);
    EXPECT_EQ(clampVelocity(0), 1);
    EXPECT_EQ(clampVelocity(128), 127);
}

TEST(VelocityLaneMathTest, PickStickTakesTheNearestWithinToleranceAndNothingOutsideIt) {
    const std::vector<StickPoint> sticks{stick(1, 100, 64), stick(2, 120, 64)};
    ASSERT_TRUE(pickStick(sticks, {103.0f, 30.0f}, 5).has_value());
    EXPECT_EQ(*pickStick(sticks, {103.0f, 30.0f}, 5), 0u);
    EXPECT_EQ(*pickStick(sticks, {117.0f, 30.0f}, 5), 1u);
    EXPECT_FALSE(pickStick(sticks, {110.0f, 30.0f}, 5).has_value()) << "10 px from both: empty strip";
}

TEST(VelocityLaneMathTest, ChordSticksSharingAnXAreTieBrokenByTheNearestHead) {
    const std::vector<StickPoint> chord{stick(1, 100, 20), stick(2, 100, 110), stick(3, 100, 64)};
    const float nearLoud = yForVelocity(105, kTop, kBottom);
    const float nearQuiet = yForVelocity(25, kTop, kBottom);
    EXPECT_EQ(*pickStick(chord, {101.0f, nearLoud}, 5), 1u);
    EXPECT_EQ(*pickStick(chord, {99.0f, nearQuiet}, 5), 0u);
}

TEST(VelocityLaneMathTest, LineCoversEveryStickOnTheSpanInclusiveAndInterpolatesY) {
    const std::vector<StickPoint> sticks{stick(1, 100, 64), stick(2, 150, 64), stick(3, 200, 64), stick(4, 260, 64)};
    // One segment spanning 100..200 — what a single fast mouse event produces.
    const auto hits = lineVelocities(sticks, {100.0f, yForVelocity(20, kTop, kBottom)},
                                     {200.0f, yForVelocity(120, kTop, kBottom)}, kTop, kBottom);
    ASSERT_EQ(hits.size(), 3u) << "both endpoints and the middle stick; the one at 260 is off the span";
    EXPECT_EQ(valueFor(hits, 1), 20);
    EXPECT_EQ(valueFor(hits, 3), 120);
    EXPECT_NEAR(valueFor(hits, 2), 70, 1) << "halfway along the line, halfway between the values";
}

TEST(VelocityLaneMathTest, LineWorksRightToLeftAndAZeroLengthSegmentSetsTheStickUnderIt) {
    const std::vector<StickPoint> sticks{stick(1, 100, 64), stick(2, 200, 64)};
    const auto reversed = lineVelocities(sticks, {200.0f, yForVelocity(90, kTop, kBottom)},
                                         {100.0f, yForVelocity(10, kTop, kBottom)}, kTop, kBottom);
    EXPECT_EQ(valueFor(reversed, 2), 90);
    EXPECT_EQ(valueFor(reversed, 1), 10);

    const auto press = lineVelocities(sticks, {100.2f, yForVelocity(33, kTop, kBottom)},
                                      {100.2f, yForVelocity(33, kTop, kBottom)}, kTop, kBottom);
    ASSERT_EQ(press.size(), 1u);
    EXPECT_EQ(valueFor(press, 1), 33);
}

TEST(VelocityLaneMathTest, HumanizeStaysWithinTheRangeAndClampsAtBothEnds) {
    juce::Random random(7);
    for (int i = 0; i < 500; ++i) {
        const int mid = humanizedVelocity(64, 10, random);
        EXPECT_GE(mid, 54);
        EXPECT_LE(mid, 74);
        const int low = humanizedVelocity(1, 20, random);
        EXPECT_GE(low, 1);
        EXPECT_LE(low, 21);
        const int high = humanizedVelocity(127, 20, random);
        EXPECT_GE(high, 107);
        EXPECT_LE(high, 127);
    }
}

TEST(VelocityLaneMathTest, HumanizeIsDeterministicForASeededSource) {
    juce::Random a(1234), b(1234);
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(humanizedVelocity(80, 20, a), humanizedVelocity(80, 20, b));
    juce::Random c(99);
    EXPECT_EQ(humanizedVelocity(80, 0, c), 80) << "a zero range changes nothing";
}
