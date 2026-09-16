// EnvelopeGeneratorTests.cpp
// Unit tests for synth::EnvelopeGenerator (FRO110): the progress-based AHDSR engine that
// replaces juce::ADSR inside ADSRModule. Exercises the zero-duration-stage cascade, exact
// stage timing/curve endpoints, retrigger-from-current-level, mid-ramp parameter changes, and
// the sustain-0/sustain-1 edge cases that motivated the rewrite.
//
// FRO116: Hold is the only stage still genuinely zero-cost at a 0 s parameter (it is pinned
// flat, so there is no level to step across). Attack/Decay/Release floor their *effective* time
// to a fixed click-free minimum instead of cascading for free -- see kMinAttackSeconds /
// kMinRampSeconds in EnvelopeGenerator.h. Most of this file's zero-time tests use a 1 kHz test
// sample rate for easy-to-reason-about sample counts; at that rate the 0.1 ms attack floor is a
// fraction of one sample (so it still collapses into the first sample, coincidentally the same
// result as the old genuinely-instant behaviour) while the 1 ms decay/release floor is on the
// order of one sample too -- see FlooredStageTimesAtRealAudioSampleRate below for the same
// floors at a real audio rate, where they land on unambiguous multi-sample counts instead.

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
// Zero-duration stages: Hold cascades for free; Attack/Decay/Release floor to a short real
// ramp instead (FRO116).
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, ZeroAttackReachesFullLevelOnFirstSample) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f; // floored internally to kMinAttackSeconds (0.1 ms) -- see file header
    p.hold = 0.01f;  // nonzero so the cascade halts here and we can observe the landing stage
    p.decay = 1.0f;
    p.sustain = 1.0f;

    env.noteOn();
    const float first = env.getNextSample(p);
    // At this test's 1 kHz rate the floored 0.1 ms attack is a tenth of one sample period, so
    // progress still clamps to 1.0 (and shape(1, c) == 1.0 exactly) within this first call --
    // the same observable result as the old genuinely-instant behaviour, for a different reason.
    EXPECT_FLOAT_EQ(first, 1.0f);
    EXPECT_EQ(env.getStage(), EnvelopeStage::Hold);
}

TEST(EnvelopeGeneratorTest, ZeroReleaseFloorsToAShortRealRampRatherThanInstantSilence) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.0f; // floored to 0.1 ms -- negligible at this sample rate, see above
    p.hold = 0.0f;
    p.decay = 0.01f; // real, unfloored decay so this test isolates the RELEASE floor only
    p.sustain = 0.7f;
    p.release = 0.0f; // floored internally to kMinRampSeconds (1 ms)

    env.noteOn();
    int settleSamples = 0;
    while (env.getStage() != EnvelopeStage::Sustain && settleSamples < 100) {
        env.getNextSample(p);
        ++settleSamples;
    }
    ASSERT_EQ(env.getStage(), EnvelopeStage::Sustain);
    ASSERT_NEAR(env.getLevel(), 0.7f, 1e-3f);

    env.noteOff();
    // Count samples rather than assuming the floor divides evenly into one call: `time` is a
    // float and `sampleDt_` a double, so `sampleDt_ / time` is not exactly 1.0 even when the two
    // are numerically "the same" 0.001 s -- asserting an exact single-call cascade here would be
    // a floating-point trap, not a real property of the floor.
    int releaseSamples = 0;
    while (env.getStage() != EnvelopeStage::Idle && releaseSamples < 100) {
        env.getNextSample(p);
        ++releaseSamples;
    }
    EXPECT_NEAR(releaseSamples, 1, 1) << "the floored 1 ms release is about one sample at this test's 1 kHz rate";
    EXPECT_FALSE(env.isActive());
    EXPECT_NEAR(env.getLevel(), 0.0f, 1e-3f);
}

TEST(EnvelopeGeneratorTest, ZeroDecayFloorsToAShortRealRampRatherThanCascadingForFree) {
    auto env = makeGenerator();
    EnvelopeParameters p;
    p.attack = 0.005f; // 5 samples at 1 kHz
    p.hold = 0.0f;     // genuinely zero -- still cascades for free
    p.decay = 0.0f;    // floored internally to kMinRampSeconds (1 ms)
    p.sustain = 0.4f;

    env.noteOn();
    int attackSamples = 0;
    while (env.getStage() == EnvelopeStage::Attack && attackSamples < 100) {
        env.getNextSample(p);
        ++attackSamples;
    }

    // Attack+Hold still cascade for free (Hold is genuinely 0 s), but Decay's floored time is a
    // real, nonzero ramp now: the call that finishes Attack must stop AT Decay, not cascade
    // straight through it into Sustain the way a genuinely-zero Decay used to.
    EXPECT_EQ(env.getStage(), EnvelopeStage::Decay)
        << "a floored (not genuinely zero) Decay must take its own sample, not cascade for free";

    int decaySamples = 0;
    while (env.getStage() == EnvelopeStage::Decay && decaySamples < 100) {
        env.getNextSample(p);
        ++decaySamples;
    }
    EXPECT_NEAR(decaySamples, 1, 1) << "the floored 1 ms decay is about one sample at this test's 1 kHz rate";
    EXPECT_EQ(env.getStage(), EnvelopeStage::Sustain);
    EXPECT_NEAR(env.getLevel(), 0.4f, 1e-3f);
}

// ---------------------------------------------------------------------------
// The floors at a real audio sample rate, where they land on unambiguous multi-sample counts
// (unlike the 1 kHz tests above, where 0.1 ms/1 ms happen to be a fraction of, or close to, one
// whole sample period).
// ---------------------------------------------------------------------------

TEST(EnvelopeGeneratorTest, FlooredStageTimesAtRealAudioSampleRate) {
    EnvelopeGenerator attackEnv;
    attackEnv.setSampleRate(48000.0);
    attackEnv.reset();

    EnvelopeParameters attackParams;
    attackParams.attack = 0.0f;      // floored to 0.1 ms = 4.8 samples at 48 kHz
    attackParams.hold = 1.0f;        // hold the landing stage so Attack's count is unambiguous
    attackParams.attackCurve = 0.0f; // linear, so the sample count is exact

    attackEnv.noteOn();
    int attackSamples = 0;
    while (attackEnv.getStage() == EnvelopeStage::Attack && attackSamples < 1000) {
        attackEnv.getNextSample(attackParams);
        ++attackSamples;
    }
    EXPECT_NEAR(attackSamples, 5, 1) << "0.1 ms floor at 48 kHz is 4.8 samples, rounding up to 5";

    EnvelopeGenerator decayEnv;
    decayEnv.setSampleRate(48000.0);
    decayEnv.reset();

    EnvelopeParameters decayParams;
    decayParams.attack = 0.0f;
    decayParams.hold = 0.0f;
    decayParams.decay = 0.0f; // floored to 1 ms = 48 samples at 48 kHz
    decayParams.sustain = 0.5f;
    decayParams.decayCurve = 0.0f;

    decayEnv.noteOn();
    int attackHoldSamples = 0;
    while (decayEnv.getStage() == EnvelopeStage::Attack && attackHoldSamples < 1000) {
        decayEnv.getNextSample(decayParams);
        ++attackHoldSamples;
    }
    ASSERT_EQ(decayEnv.getStage(), EnvelopeStage::Decay);

    int decaySamples = 0;
    while (decayEnv.getStage() == EnvelopeStage::Decay && decaySamples < 1000) {
        decayEnv.getNextSample(decayParams);
        ++decaySamples;
    }
    EXPECT_NEAR(decaySamples, 48, 1) << "1 ms floor at 48 kHz is 48 samples";
    EXPECT_EQ(decayEnv.getStage(), EnvelopeStage::Sustain);
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
    p.decay = 0.0f; // floored internally to kMinRampSeconds -- no longer a same-call cascade
    p.sustain = 0.9f;
    p.release = 0.015f; // 15 samples
    p.releaseCurve = curve;

    env.noteOn();
    // Attack+Hold still cascade for free, but the floored Decay now takes a real sample or two
    // of its own (FRO116) rather than landing on Sustain within the very first call.
    int settleSamples = 0;
    while (env.getStage() != EnvelopeStage::Sustain && settleSamples < 1000) {
        env.getNextSample(p);
        ++settleSamples;
    }
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
