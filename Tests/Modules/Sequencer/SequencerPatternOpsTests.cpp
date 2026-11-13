#include "Modules/Sequencer/SequencerPatternOps.h"
#include <gtest/gtest.h>

using namespace synth;
namespace ops = synth::sequencer_ops;

namespace {

// Four distinguishable steps (pitch 60, 62, 64, 65), then marker steps that must never move.
SequencerPattern knownPattern() {
    SequencerPattern p;
    const int pitches[4] = {60, 62, 64, 65};
    for (int i = 0; i < 4; ++i) {
        p.steps[(size_t)i].pitch = (std::uint8_t)pitches[i];
        p.steps[(size_t)i].velocity = (std::uint8_t)(10 + i);
        p.steps[(size_t)i].gate = 0.1f * (float)(i + 1);
        p.steps[(size_t)i].on = (i % 2 == 0);
    }
    for (int i = 4; i < kMaxSequencerSteps; ++i)
        p.steps[(size_t)i].pitch = 99;
    return p;
}

MusicalScale cMajor() { return makeScale(0, 0); }
MusicalScale noScale() { return MusicalScale{}; }

void expectTailUntouched(const SequencerPattern& a, const SequencerPattern& b, int from) {
    for (int i = from; i < kMaxSequencerSteps; ++i)
        EXPECT_EQ(a.steps[(size_t)i], b.steps[(size_t)i]) << i;
}

} // namespace

TEST(SequencerPatternOpsTest, ShiftLeftRotatesEveryFieldAndWraps) {
    const auto p = knownPattern();
    const auto out = ops::shiftLeft(p, 4);
    EXPECT_EQ(out.steps[0], p.steps[1]);
    EXPECT_EQ(out.steps[2], p.steps[3]);
    EXPECT_EQ(out.steps[3], p.steps[0]); // the first step wraps to the end
    expectTailUntouched(out, p, 4);
}

TEST(SequencerPatternOpsTest, ShiftRightRotatesEveryFieldAndWraps) {
    const auto p = knownPattern();
    const auto out = ops::shiftRight(p, 4);
    EXPECT_EQ(out.steps[0], p.steps[3]); // the last step wraps to the start
    EXPECT_EQ(out.steps[1], p.steps[0]);
    EXPECT_EQ(out.steps[3], p.steps[2]);
    expectTailUntouched(out, p, 4);
}

TEST(SequencerPatternOpsTest, ShiftLeftThenRightIsTheIdentityAndAFullTurnWrapsRound) {
    const auto p = knownPattern();
    EXPECT_EQ(ops::shiftRight(ops::shiftLeft(p, 4), 4).steps, p.steps);
    EXPECT_EQ(ops::shiftLeft(p, 4, 4).steps, p.steps);
    EXPECT_EQ(ops::shiftLeft(p, 4, 5).steps, ops::shiftLeft(p, 4).steps);
    EXPECT_EQ(ops::shiftLeft(p, 1).steps, p.steps); // one step cannot move
}

TEST(SequencerPatternOpsTest, ReverseFlipsTheActiveStepsOnly) {
    const auto p = knownPattern();
    const auto out = ops::reversed(p, 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(out.steps[(size_t)i], p.steps[(size_t)(3 - i)]) << i;
    expectTailUntouched(out, p, 4);
    EXPECT_EQ(ops::reversed(out, 4).steps, p.steps);
}

TEST(SequencerPatternOpsTest, DoubleCopiesIntoTheSecondHalf) {
    const auto p = knownPattern();
    const auto out = ops::doubled(p, 4);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(out.steps[(size_t)i], p.steps[(size_t)i]);
        EXPECT_EQ(out.steps[(size_t)(4 + i)], p.steps[(size_t)i]);
    }
    expectTailUntouched(out, p, 8);
}

TEST(SequencerPatternOpsTest, DoubleIsOfferedUpTo64StepsOnly) {
    EXPECT_TRUE(ops::canDouble(64));
    EXPECT_FALSE(ops::canDouble(65));
    EXPECT_FALSE(ops::canDouble(128));
    const auto p = knownPattern();
    EXPECT_EQ(ops::doubled(p, 65).steps, p.steps);
}

TEST(SequencerPatternOpsTest, MoveByScaleStepWalksScaleNotes) {
    const auto p = knownPattern();
    const auto up = ops::movedByScaleStep(p, 4, cMajor(), +1);
    EXPECT_EQ(up.steps[0].pitch, 62); // C -> D
    EXPECT_EQ(up.steps[1].pitch, 64); // D -> E
    EXPECT_EQ(up.steps[2].pitch, 65); // E -> F
    EXPECT_EQ(up.steps[3].pitch, 67); // F -> G
    const auto down = ops::movedByScaleStep(p, 4, cMajor(), -1);
    EXPECT_EQ(down.steps[0].pitch, 59);
    EXPECT_EQ(down.steps[3].pitch, 64);
    expectTailUntouched(up, p, 4);
}

TEST(SequencerPatternOpsTest, MoveByScaleStepIsOneSemitoneWithNoScaleAndClamps) {
    auto p = knownPattern();
    p.steps[0].pitch = 127;
    p.steps[1].pitch = 0;
    EXPECT_EQ(ops::movedByScaleStep(p, 4, noScale(), +1).steps[0].pitch, 127);
    EXPECT_EQ(ops::movedByScaleStep(p, 4, noScale(), -1).steps[1].pitch, 0);
    EXPECT_EQ(ops::movedByScaleStep(p, 4, noScale(), +1).steps[2].pitch, 65);
    EXPECT_EQ(ops::movedByScaleStep(p, 4, cMajor(), +1).steps[0].pitch, 127); // already the top scale note
}

TEST(SequencerPatternOpsTest, FitToScaleSnapsOutOfScaleNotesOnly) {
    auto p = knownPattern();
    p.steps[1].pitch = 61; // C#: ties go to the lower C
    p.steps[3].pitch = 66; // F#: ties go to the lower F
    EXPECT_TRUE(ops::fitToScaleWouldChange(p, 4, cMajor()));
    const auto out = ops::fitToScale(p, 4, cMajor());
    EXPECT_EQ(out.steps[0].pitch, 60);
    EXPECT_EQ(out.steps[1].pitch, 60);
    EXPECT_EQ(out.steps[3].pitch, 65);
    expectTailUntouched(out, p, 4);
    EXPECT_FALSE(ops::fitToScaleWouldChange(out, 4, cMajor()));
}

TEST(SequencerPatternOpsTest, FitToScaleWouldChangeIgnoresStepsPastTheActiveCountAndNoScale) {
    auto p = knownPattern();
    p.steps[10].pitch = 61;
    EXPECT_FALSE(ops::fitToScaleWouldChange(p, 4, cMajor()));
    p.steps[1].pitch = 61;
    EXPECT_FALSE(ops::fitToScaleWouldChange(p, 4, noScale()));
}

TEST(SequencerPatternOpsTest, RandomizeStaysInScaleAndRange) {
    const auto p = knownPattern();
    juce::Random rng(42);
    ops::RandomizeSettings s;
    s.minPitch = 48;
    s.maxPitch = 72;
    const auto out = ops::randomized(p, 16, cMajor(), s, rng);
    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(cMajor().contains(out.steps[(size_t)i].pitch)) << i;
        EXPECT_GE(out.steps[(size_t)i].pitch, 48);
        EXPECT_LE(out.steps[(size_t)i].pitch, 72);
    }
    expectTailUntouched(out, p, 16);
}

TEST(SequencerPatternOpsTest, RandomizeIsDeterministicForASeed) {
    const auto p = knownPattern();
    ops::RandomizeSettings s;
    s.keepRhythm = false;
    juce::Random a(7), b(7), c(8);
    const auto first = ops::randomized(p, 16, cMajor(), s, a);
    const auto second = ops::randomized(p, 16, cMajor(), s, b);
    const auto other = ops::randomized(p, 16, cMajor(), s, c);
    EXPECT_EQ(first.steps, second.steps);
    EXPECT_NE(first.steps, other.steps);
}

TEST(SequencerPatternOpsTest, KeepRhythmLeavesOnAndGateAlone) {
    const auto p = knownPattern();
    juce::Random rng(3);
    ops::RandomizeSettings s;
    s.keepRhythm = true;
    s.density = 0.0f;
    const auto out = ops::randomized(p, 4, cMajor(), s, rng);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(out.steps[(size_t)i].on, p.steps[(size_t)i].on) << i;
        EXPECT_EQ(out.steps[(size_t)i].gate, p.steps[(size_t)i].gate) << i;
        EXPECT_EQ(out.steps[(size_t)i].velocity, p.steps[(size_t)i].velocity) << i;
    }
}

TEST(SequencerPatternOpsTest, DensityControlsHowManyStepsPlay) {
    const auto p = knownPattern();
    juce::Random rng(11);
    ops::RandomizeSettings s;
    s.keepRhythm = false;

    s.density = 1.0f;
    auto out = ops::randomized(p, 64, cMajor(), s, rng);
    for (int i = 0; i < 64; ++i)
        EXPECT_TRUE(out.steps[(size_t)i].on);

    s.density = 0.5f;
    int on = 0;
    out = ops::randomized(p, 128, cMajor(), s, rng);
    for (int i = 0; i < 128; ++i)
        on += out.steps[(size_t)i].on ? 1 : 0;
    EXPECT_GT(on, 40);
    EXPECT_LT(on, 88);
}

TEST(SequencerPatternOpsTest, ZeroDensityStillPlaysOneStep) {
    const auto p = knownPattern();
    juce::Random rng(5);
    ops::RandomizeSettings s;
    s.keepRhythm = false;
    s.density = 0.0f;
    const auto out = ops::randomized(p, 16, cMajor(), s, rng);
    int on = 0;
    for (int i = 0; i < 16; ++i)
        on += out.steps[(size_t)i].on ? 1 : 0;
    EXPECT_EQ(on, 1);
}

TEST(SequencerPatternOpsTest, RandomizeChangesNothingElse) {
    auto p = knownPattern();
    p.steps[2].chance = 40;
    p.steps[2].nudge = 0.25f;
    juce::Random rng(9);
    ops::RandomizeSettings s;
    const auto out = ops::randomized(p, 4, cMajor(), s, rng);
    EXPECT_EQ(out.steps[2].chance, 40);
    EXPECT_EQ(out.steps[2].nudge, 0.25f);
    EXPECT_EQ(out.seed, p.seed);
}
