// EnvelopeGeneratorTests.cpp
// Unit tests for synth::EnvelopeGenerator (FRO110): the progress-based AHDSR engine that
// replaces juce::ADSR inside ADSRModule. Exercises the zero-duration-stage cascade, exact
// stage timing/curve endpoints, retrigger-from-current-level, mid-ramp parameter changes, and
// the sustain-0/sustain-1 edge cases that motivated the rewrite.

#include "Modules/Envelope/EnvelopeGenerator.h"
#include <gtest/gtest.h>

using synth::EnvelopeGenerator;
using synth::EnvelopeParameters;
using synth::EnvelopeStage;

namespace {
constexpr double kSampleRate = 1000.0; // 1 ms/sample -- gives exact, easy-to-reason-about sample counts

EnvelopeGenerator makeGenerator() {
    EnvelopeGenerator env;
    env.setSampleRate(kSampleRate);
    env.reset();
    return env;
}
} // namespace

// ---------------------------------------------------------------------------
// Zero-duration stages cost no samples of their own.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, ZeroAttackReachesFullLevelOnFirstSample) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f;
    p.hold = 0.01f; // nonzero so the cascade halts here and we can observe the landing stage
    p.decay = 1.0f;
    p.sustain = 1.0f;

    env.noteOn();
    const float first = env.getNextSample(p);
    EXPECT_FLOAT_EQ(first, 1.0f);
    EXPECT_EQ(env.getStage(), EnvelopeStage::Hold);
}

TEST(EnvelopeGeneratorTest, ZeroReleaseReachesZeroOnFirstSample) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f;
    p.hold = 0.0f;
    p.decay = 0.0f;
    p.sustain = 0.7f;
    p.release = 0.0f;

    env.noteOn();
    ASSERT_FLOAT_EQ(env.getNextSample(p), 0.7f); // settles at sustain instantly (all-zero cascade)
    ASSERT_EQ(env.getStage(), EnvelopeStage::Sustain);

    env.noteOff();
    const float first = env.getNextSample(p);
    EXPECT_FLOAT_EQ(first, 0.0f);
    EXPECT_EQ(env.getStage(), EnvelopeStage::Idle);
    EXPECT_FALSE(env.isActive());
}

TEST(EnvelopeGeneratorTest, ZeroDecayReachesSustainOnFirstSampleAfterAttack) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.005f; // 5 samples at 1 kHz
    p.hold = 0.0f;
    p.decay = 0.0f;
    p.sustain = 0.4f;

    env.noteOn();
    int samples = 0;
    while (env.getStage() == EnvelopeStage::Attack && samples < 100) {
        env.getNextSample(p);
        ++samples;
    }

    // The very call that finishes Attack must already have cascaded through the zero-length
    // Hold and Decay stages, landing on Sustain -- not left sitting in Hold or Decay.
    EXPECT_EQ(env.getStage(), EnvelopeStage::Sustain);
    EXPECT_NEAR(env.getLevel(), 0.4f, 1e-3f);
}

TEST(EnvelopeGeneratorTest, ZeroHoldConsumesNoSamples) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.005f; // 5 samples
    p.hold = 0.0f;
    p.decay = 0.01f; // 10 samples -- long enough to observe decay actually ramping
    p.sustain = 0.3f;
    p.decayCurve = 0.0f; // linear, so the decay ramp is easy to reason about

    env.noteOn();
    int samples = 0;
    while (env.getStage() == EnvelopeStage::Attack && samples < 100) {
        env.getNextSample(p);
        ++samples;
    }

    // The call where Attack finishes must never leave the generator resting in Hold: with
    // hold == 0 it cascades straight through into Decay within that same call.
    EXPECT_EQ(env.getStage(), EnvelopeStage::Decay);
    EXPECT_NEAR(env.getLevel(), 1.0f, 1e-3f);

    const float firstDecaySample = env.getNextSample(p); // decay's own first real step
    EXPECT_LT(firstDecaySample, 1.0f) << "decay must start immediately, not after an extra flat sample";
}

// ---------------------------------------------------------------------------
// Stage timing is exact regardless of curve.
// ---------------------------------------------------------------------------

class EnvelopeGeneratorStageDurationTest : public ::testing::TestWithParam<float> {};

TEST_P(EnvelopeGeneratorStageDurationTest, AttackLastsItsConfiguredDuration) {
    const float curve = GetParam();
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.01f; // 10 samples at 1 kHz
    p.hold = 1.0f;    // stay put once attack finishes so we can count cleanly
    p.attackCurve = curve;

    env.noteOn();
    int samples = 0;
    while (env.getStage() == EnvelopeStage::Attack && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 10, 1) << "curve = " << curve;
}

TEST_P(EnvelopeGeneratorStageDurationTest, DecayLastsItsConfiguredDuration) {
    const float curve = GetParam();
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f;
    p.hold = 0.0f;
    p.decay = 0.02f; // 20 samples
    p.sustain = 0.25f;
    p.decayCurve = curve;

    env.noteOn();
    env.getNextSample(p); // cascades Attack+Hold, lands in Decay
    ASSERT_EQ(env.getStage(), EnvelopeStage::Decay);

    int samples = 0;
    while (env.getStage() == EnvelopeStage::Decay && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 20, 1) << "curve = " << curve;
}

TEST_P(EnvelopeGeneratorStageDurationTest, ReleaseLastsItsConfiguredDuration) {
    const float curve = GetParam();
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f;
    p.hold = 0.0f;
    p.decay = 0.0f;
    p.sustain = 0.9f;
    p.release = 0.015f; // 15 samples
    p.releaseCurve = curve;

    env.noteOn();
    env.getNextSample(p);
    ASSERT_EQ(env.getStage(), EnvelopeStage::Sustain);

    env.noteOff();
    int samples = 0;
    while (env.getStage() != EnvelopeStage::Idle && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 15, 1) << "curve = " << curve;
}

INSTANTIATE_TEST_SUITE_P(CurveSweep, EnvelopeGeneratorStageDurationTest, ::testing::Values(-1.0f, 0.0f, 1.0f));

// ---------------------------------------------------------------------------
// shape(): exact endpoints and monotonicity for a sweep of curve amounts.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, ShapeEndpointsAreExactAndCurveIsMonotonic) {
    const float curves[] = {-1.0f, -0.7f, -0.3f, -0.001f, 0.0f, 0.001f, 0.3f, 0.7f, 1.0f};
    for (float c : curves) {
        EXPECT_FLOAT_EQ(EnvelopeGenerator::shape(0.0f, c), 0.0f) << "c = " << c;
        EXPECT_FLOAT_EQ(EnvelopeGenerator::shape(1.0f, c), 1.0f) << "c = " << c;

        float previous = -1.0f;
        for (int i = 0; i <= 20; ++i) {
            const float p = static_cast<float>(i) / 20.0f;
            const float value = EnvelopeGenerator::shape(p, c);
            EXPECT_GE(value, previous - 1e-6f) << "c = " << c << " p = " << p << " (not monotonic)";
            previous = value;
        }
    }
}

// ---------------------------------------------------------------------------
// Release always starts from the current level, whatever stage it interrupts.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, ReleaseFromMidAttackStartsAtCurrentLevelAndTakesFullReleaseTime) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.01f; // 10 samples
    p.hold = 0.0f;
    p.decay = 0.0f;
    p.sustain = 1.0f;
    p.release = 0.02f; // 20 samples
    p.releaseCurve = 0.0f;

    env.noteOn();
    for (int i = 0; i < 5; ++i)
        env.getNextSample(p); // halfway through attack
    const float levelAtRelease = env.getLevel();
    ASSERT_GT(levelAtRelease, 0.01f);
    ASSERT_LT(levelAtRelease, 1.0f);

    env.noteOff();
    const float firstReleaseSample = env.getNextSample(p);
    EXPECT_NEAR(firstReleaseSample, levelAtRelease, 0.05f) << "release must continue from the current level";

    int samples = 1;
    while (env.getStage() != EnvelopeStage::Idle && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 20, 1);
}

TEST(EnvelopeGeneratorTest, ReleaseFromMidDecayStartsAtCurrentLevelAndTakesFullReleaseTime) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f;
    p.hold = 0.0f;
    p.decay = 0.02f; // 20 samples
    p.sustain = 0.1f;
    p.release = 0.03f; // 30 samples
    p.releaseCurve = 0.0f;

    env.noteOn();
    env.getNextSample(p); // cascades into Decay
    ASSERT_EQ(env.getStage(), EnvelopeStage::Decay);
    for (int i = 0; i < 10; ++i)
        env.getNextSample(p); // halfway through decay
    const float levelAtRelease = env.getLevel();
    ASSERT_GT(levelAtRelease, 0.1f);
    ASSERT_LT(levelAtRelease, 1.0f);

    env.noteOff();
    const float firstReleaseSample = env.getNextSample(p);
    EXPECT_NEAR(firstReleaseSample, levelAtRelease, 0.05f) << "release must continue from the current level";

    int samples = 1;
    while (env.getStage() != EnvelopeStage::Idle && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 30, 1);
}

// ---------------------------------------------------------------------------
// Retrigger mid-release: attack from the current level, not from 0, not instantly.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, RetriggerMidReleaseAttacksFromCurrentLevelNotZeroNotInstantly) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.02f; // 20 samples
    p.hold = 0.0f;
    p.decay = 0.0f;
    p.sustain = 0.9f;
    p.release = 0.05f; // 50 samples
    p.attackCurve = 0.0f;
    p.releaseCurve = 0.0f;

    env.noteOn();
    int settleSamples = 0;
    while (env.getStage() != EnvelopeStage::Sustain && settleSamples < 1000) {
        env.getNextSample(p);
        ++settleSamples;
    }
    ASSERT_EQ(env.getStage(), EnvelopeStage::Sustain);
    env.noteOff();
    for (int i = 0; i < 10; ++i)
        env.getNextSample(p); // partway through release
    const float levelAtRetrigger = env.getLevel();
    ASSERT_GT(levelAtRetrigger, 0.05f);
    ASSERT_LT(levelAtRetrigger, 0.9f);

    env.noteOn();
    ASSERT_EQ(env.getStage(), EnvelopeStage::Attack);
    const float firstSampleAfterRetrigger = env.getNextSample(p);

    // Not instant (jumping straight to 1) and not reset to 0 -- it must pick up from where the
    // release left off and climb from there.
    EXPECT_NEAR(firstSampleAfterRetrigger, levelAtRetrigger, 0.05f);
    EXPECT_LT(firstSampleAfterRetrigger, 1.0f);

    // And it takes the FULL attack time to get back up, not a shortened one.
    int samples = 1;
    while (env.getStage() == EnvelopeStage::Attack && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 20, 1);
}

// ---------------------------------------------------------------------------
// Changing a stage's time mid-ramp changes the slope, never the level, discontinuously.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, ChangingAttackTimeMidRampHasNoDiscontinuity) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.02f; // 20 samples
    p.hold = 1.0f;
    p.attackCurve = 0.0f;

    env.noteOn();
    for (int i = 0; i < 10; ++i)
        env.getNextSample(p);
    const float beforeChange = env.getLevel();

    p.attack = 0.05f; // slow the ramp down mid-flight
    const float afterChange = env.getNextSample(p);

    EXPECT_NEAR(afterChange, beforeChange, 0.05f) << "a time change must not teleport the level";
    EXPECT_GT(afterChange, beforeChange) << "still rising, just more slowly";
}

// ---------------------------------------------------------------------------
// sustain == 0 and sustain == 1 both behave: no NaNs, no short-circuiting.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, SustainZeroStillRunsAFullAttackAndDecay) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.01f; // 10 samples
    p.hold = 0.0f;
    p.decay = 0.01f; // 10 samples
    p.sustain = 0.0f;
    p.release = 0.01f;

    env.noteOn();
    float peak = 0.0f;
    for (int i = 0; i < 30; ++i)
        peak = std::max(peak, env.getNextSample(p));
    EXPECT_NEAR(peak, 1.0f, 1e-3f) << "attack must still reach full level even though sustain is 0";
    EXPECT_EQ(env.getStage(), EnvelopeStage::Sustain);
    EXPECT_NEAR(env.getLevel(), 0.0f, 1e-3f);

    env.noteOff();
    const float afterRelease = env.getNextSample(p);
    EXPECT_NEAR(afterRelease, 0.0f, 1e-3f);
}

TEST(EnvelopeGeneratorTest, SustainOneHoldsFlatThenReleasesFully) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.01f; // 10 samples
    p.hold = 0.0f;
    p.decay = 0.01f; // 10 samples, but degenerate: start == target == 1
    p.sustain = 1.0f;
    p.release = 0.02f; // 20 samples

    env.noteOn();
    for (int i = 0; i < 10; ++i)
        env.getNextSample(p); // through attack
    ASSERT_NEAR(env.getLevel(), 1.0f, 1e-3f);

    // Decay is degenerate when sustain == 1 (start == target == 1): the level must stay flat.
    for (int i = 0; i < 15; ++i)
        EXPECT_NEAR(env.getNextSample(p), 1.0f, 1e-3f);
    EXPECT_NEAR(env.getLevel(), 1.0f, 1e-3f);

    env.noteOff();
    int samples = 0;
    while (env.getStage() != EnvelopeStage::Idle && samples < 1000) {
        env.getNextSample(p);
        ++samples;
    }
    EXPECT_NEAR(samples, 20, 1);
}

// ---------------------------------------------------------------------------
// The user's actual scenario: a gapless note sequence at sustain 0 must keep producing sound.
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, GaplessNoteSequenceAtSustainZeroKeepsProducingSoundOnEveryNote) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.01f; // 10 samples
    p.hold = 0.0f;
    p.decay = 0.02f; // 20 samples
    p.sustain = 0.0f;
    p.release = 0.05f;

    for (int note = 0; note < 4; ++note) {
        env.noteOn();
        float peak = 0.0f;
        for (int i = 0; i < 40; ++i)
            peak = std::max(peak, env.getNextSample(p));
        EXPECT_GT(peak, 0.9f) << "note " << note << " did not re-attack audibly at sustain 0";
    }
}
