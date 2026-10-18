// SamplerVoicePoolTests.cpp -- the polyphonic renderer: voices, stealing, layers, pitch, envelope, pedal, tone.
#include "Modules/Sampler/SamplerVoicePool.h"
#include "SamplerTestWavs.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

using namespace synth::sampler;
using namespace sampler_test;

namespace {

constexpr double kRate = 44100.0;

struct Out {
    std::vector<float> left, right;
};

Out render(VoicePool& pool, int frames, const VoiceParams& p) {
    Out out{std::vector<float>((size_t)frames, 0.0f), std::vector<float>((size_t)frames, 0.0f)};
    pool.render(out.left.data(), out.right.data(), 0, frames, p);
    return out;
}

VoiceParams defaults() {
    VoiceParams p;
    p.releaseSeconds = 0.05f;
    p.maxVoices = 16;
    return p;
}

struct Rig {
    explicit Rig(Instrument::Ptr i)
        : inst(std::move(i)) {
        pool.prepare(kRate);
        pool.setInstrument(inst.get());
    }
    Instrument::Ptr inst;
    VoicePool pool;
};

Instrument::Ptr dcInstrument(float level = 0.5f) { return makeInstrument({makeRegion(dcSample(level))}); }

int upwardZeroCrossings(const std::vector<float>& x) {
    int n = 0;
    for (size_t i = 1; i < x.size(); ++i)
        n += (x[i - 1] < 0.0f && x[i] >= 0.0f) ? 1 : 0;
    return n;
}

} // namespace

TEST(SamplerVoicePoolTest, AChordRendersOneVoicePerNote) {
    Rig rig(dcInstrument());
    const auto p = defaults();
    for (int n : {60, 64, 67})
        rig.pool.noteOn(n, 0.8f, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 3);
    const auto sounding = rig.pool.soundingNotes();
    EXPECT_TRUE(sounding.test(60) && sounding.test(64) && sounding.test(67));
    const auto out = render(rig.pool, 4096, p);
    EXPECT_NEAR(out.left.back(), 1.5f, 1.0e-3f);
    EXPECT_FLOAT_EQ(out.left.back(), out.right.back());
}

TEST(SamplerVoicePoolTest, MaxVoicesCapsAndStealsTheOldest) {
    Rig rig(dcInstrument());
    auto p = defaults();
    p.maxVoices = 2;
    rig.pool.noteOn(60, 0.8f, p);
    rig.pool.noteOn(62, 0.8f, p);
    rig.pool.noteOn(64, 0.8f, p);
    // The stolen voice keeps sounding for its short fade, then is gone.
    EXPECT_EQ(rig.pool.activeVoiceCount(), 3);
    render(rig.pool, 1024, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 2);
    const auto sounding = rig.pool.soundingNotes();
    EXPECT_FALSE(sounding.test(60));
    EXPECT_TRUE(sounding.test(62));
    EXPECT_TRUE(sounding.test(64));
}

TEST(SamplerVoicePoolTest, VelocityPicksTheMatchingLayer) {
    Rig rig(makeInstrument({makeRegion(dcSample(0.2f), 0, 127, 1, 63), makeRegion(dcSample(0.6f), 0, 127, 64, 127)}));
    const auto p = defaults();
    rig.pool.noteOn(60, 20.0f / 127.0f, p);
    EXPECT_NEAR(render(rig.pool, 4096, p).left.back(), 0.2f, 1.0e-3f);
    rig.pool.stopAll();
    rig.pool.noteOn(60, 115.0f / 127.0f, p);
    EXPECT_NEAR(render(rig.pool, 4096, p).left.back(), 0.6f, 1.0e-3f);
}

TEST(SamplerVoicePoolTest, PitchFollowsTheKeyAndTheLiveOffset) {
    auto sine = makeSample(88200, 1, kRate, [](int, int i) {
        return (float)std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate);
    });
    Rig rig(makeInstrument({makeRegion(sine, 0, 127, 1, 127, 69)}));
    auto p = defaults();
    const int frames = 11025;

    rig.pool.noteOn(69, 1.0f, p);
    const int base = upwardZeroCrossings(render(rig.pool, frames, p).left);
    rig.pool.stopAll();
    rig.pool.noteOn(81, 1.0f, p);
    const int octave = upwardZeroCrossings(render(rig.pool, frames, p).left);
    rig.pool.stopAll();
    p.pitchSemis = 12.0;
    rig.pool.noteOn(69, 1.0f, p);
    const int shifted = upwardZeroCrossings(render(rig.pool, frames, p).left);

    EXPECT_NEAR(base, 110, 2);
    EXPECT_NEAR(octave, 2 * base, 3);
    EXPECT_NEAR(shifted, 2 * base, 3);
}

TEST(SamplerVoicePoolTest, ReleaseDecaysToSilenceWithinTheReleaseTime) {
    Rig rig(dcInstrument());
    auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    render(rig.pool, 2048, p);
    rig.pool.noteOff(60, p);
    const int releaseFrames = (int)(0.05 * kRate);
    const auto out = render(rig.pool, releaseFrames + 512, p);
    EXPECT_LT(std::fabs(out.left[(size_t)releaseFrames]), 1.0e-3f);
    EXPECT_LT(std::fabs(out.left.back()), 1.0e-6f);
    EXPECT_GT(out.left[10], out.left[releaseFrames / 2]);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, RegionReleaseOverridesTheKnob) {
    auto inst = dcInstrument();
    inst->regions[0].ampReleaseSeconds = 0.5f;
    Rig rig(inst);
    auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    render(rig.pool, 1024, p);
    rig.pool.noteOff(60, p);
    const auto out = render(rig.pool, (int)(0.1 * kRate), p);
    EXPECT_GT(out.left.back(), 0.05f);
}

TEST(SamplerVoicePoolTest, SustainPedalHoldsUntilItIsReleased) {
    Rig rig(dcInstrument());
    const auto p = defaults();
    rig.pool.sustainPedal(true, p);
    rig.pool.noteOn(60, 1.0f, p);
    rig.pool.noteOff(60, p);
    const auto held = render(rig.pool, (int)(0.5 * kRate), p);
    EXPECT_NEAR(held.left.back(), 0.5f, 1.0e-3f);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
    rig.pool.sustainPedal(false, p);
    render(rig.pool, (int)(0.2 * kRate), p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, OneShotIgnoresNoteOffAndPlaysToTheEnd) {
    auto inst = makeInstrument({makeRegion(dcSample(0.5f, 8820))});
    inst->regions[0].loop = RegionSpec::Loop::OneShot;
    Rig rig(inst);
    const auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    rig.pool.noteOff(60, p);
    const auto first = render(rig.pool, 4410, p);
    EXPECT_NEAR(first.left.back(), 0.5f, 1.0e-3f);
    render(rig.pool, 8820, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, ContinuousLoopKeepsPlayingPastTheFileEnd) {
    auto looped = makeInstrument({makeRegion(dcSample(0.5f, 1000))});
    looped->regions[0].loop = RegionSpec::Loop::Continuous;
    Rig rig(looped);
    const auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    render(rig.pool, 5000, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);

    Rig plain(makeInstrument({makeRegion(dcSample(0.5f, 1000))}));
    plain.pool.noteOn(60, 1.0f, p);
    render(plain.pool, 5000, p);
    EXPECT_EQ(plain.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, SustainLoopStopsLoopingOnceTheKeyIsReleased) {
    auto inst = makeInstrument({makeRegion(dcSample(0.5f, 1000))});
    inst->regions[0].loop = RegionSpec::Loop::Sustain;
    Rig rig(inst);
    auto p = defaults();
    p.releaseSeconds = 5.0f;
    rig.pool.noteOn(60, 1.0f, p);
    render(rig.pool, 5000, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
    rig.pool.noteOff(60, p);
    render(rig.pool, 2000, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, FullyOpenToneIsBitIdenticalToTheSource) {
    auto noise = makeSample(20000, 1, kRate, [](int, int i) { return 0.4f * std::sin(0.37f * (float)i * (float)i); });
    Rig rig(makeInstrument({makeRegion(noise)}));
    auto p = defaults();
    p.tone = 1.0f;
    rig.pool.noteOn(60, 1.0f, p);
    const auto out = render(rig.pool, 5000, p);
    for (int i = 200; i < 5000; ++i)
        ASSERT_EQ(out.left[(size_t)i], noise->audio.getSample(0, i)) << i;

    rig.pool.stopAll();
    p.tone = 0.2f;
    rig.pool.noteOn(60, 1.0f, p);
    const auto dark = render(rig.pool, 5000, p);
    double diff = 0.0;
    for (int i = 200; i < 5000; ++i)
        diff += std::fabs(dark.left[(size_t)i] - noise->audio.getSample(0, i));
    EXPECT_GT(diff, 1.0);
}

TEST(SamplerVoicePoolTest, FeelZeroPlaysEveryVelocityEquallyLoud) {
    Rig rig(dcInstrument());
    auto p = defaults();
    p.feel = 0.0f;
    rig.pool.noteOn(60, 20.0f / 127.0f, p);
    const float soft = render(rig.pool, 4096, p).left.back();
    rig.pool.stopAll();
    rig.pool.noteOn(60, 120.0f / 127.0f, p);
    const float hard = render(rig.pool, 4096, p).left.back();
    EXPECT_FLOAT_EQ(soft, hard);

    rig.pool.stopAll();
    p.feel = 1.0f;
    rig.pool.noteOn(60, 20.0f / 127.0f, p);
    const float feltSoft = render(rig.pool, 4096, p).left.back();
    rig.pool.stopAll();
    rig.pool.noteOn(60, 120.0f / 127.0f, p);
    const float feltHard = render(rig.pool, 4096, p).left.back();
    EXPECT_GT(feltHard, 2.0f * feltSoft);
}

TEST(SamplerVoicePoolTest, ReleaseSoundFiresReleaseRegionsOnNoteOff) {
    auto inst = makeInstrument({makeRegion(dcSample(0.5f)), makeRegion(dcSample(0.25f))});
    inst->regions[1].releaseTrigger = true;
    Rig rig(inst);
    auto p = defaults();
    p.releaseSeconds = 0.01f;
    p.releaseSound = true;
    rig.pool.noteOn(60, 1.0f, p);
    render(rig.pool, 2048, p);
    rig.pool.noteOff(60, p);
    const auto out = render(rig.pool, 4410, p);
    EXPECT_NEAR(out.left.back(), 0.25f, 1.0e-3f);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);

    rig.pool.stopAll();
    p.releaseSound = false;
    rig.pool.noteOn(60, 1.0f, p);
    rig.pool.noteOff(60, p);
    render(rig.pool, 4410, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, RestrikeOverlapsThenTheOldVoiceFadesOut) {
    Rig rig(dcInstrument());
    const auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    render(rig.pool, 1024, p);
    rig.pool.noteOn(60, 1.0f, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 2);
    render(rig.pool, (int)(0.1 * kRate), p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
}

TEST(SamplerVoicePoolTest, NullInstrumentSilencesAndStopsEverything) {
    Rig rig(dcInstrument());
    const auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    rig.pool.setInstrument(nullptr);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
    rig.pool.noteOn(60, 1.0f, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
    const auto out = render(rig.pool, 256, p);
    EXPECT_EQ(out.left[255], 0.0f);
}

TEST(SamplerVoicePoolTest, StereoSamplesKeepTheirChannels) {
    auto stereo = makeSample(88200, 2, kRate, [](int c, int) { return c == 0 ? 0.2f : 0.7f; });
    Rig rig(makeInstrument({makeRegion(stereo)}));
    const auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    const auto out = render(rig.pool, 4096, p);
    EXPECT_NEAR(out.left.back(), 0.2f, 1.0e-3f);
    EXPECT_NEAR(out.right.back(), 0.7f, 1.0e-3f);
}

TEST(SamplerVoicePoolTest, AllNotesOffReleasesEveryVoice) {
    Rig rig(dcInstrument());
    const auto p = defaults();
    rig.pool.noteOn(60, 1.0f, p);
    rig.pool.noteOn(64, 1.0f, p);
    rig.pool.allNotesOff();
    render(rig.pool, (int)(0.2 * kRate), p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, StealingAtTheVoiceLimitDoesNotClick) {
    auto sine = makeSample(88200, 1, kRate, [](int, int i) {
        return 0.8f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / kRate);
    });
    Rig rig(makeInstrument({makeRegion(sine, 0, 127, 1, 127, 57)}));
    auto p = defaults();
    p.maxVoices = 2;
    rig.pool.noteOn(57, 1.0f, p);
    rig.pool.noteOn(57 + 7, 1.0f, p);
    auto before = render(rig.pool, 4000, p);
    rig.pool.noteOn(57 + 12, 1.0f, p);
    auto after = render(rig.pool, 4000, p);

    // The loudest sample-to-sample step the three summed sines can make on their own.
    float maxStep = 0.0f;
    for (const auto& out : {before.left, after.left})
        for (size_t i = 1; i < out.size(); ++i)
            maxStep = std::max(maxStep, std::fabs(out[i] - out[i - 1]));
    const float ownStep =
        0.8f * 2.0f * juce::MathConstants<float>::pi * (220.0f + 329.6f * 1.0f + 440.0f) / (float)kRate * 1.0f;
    EXPECT_LT(maxStep, ownStep);
    float seam = std::fabs(after.left[0] - before.left.back());
    EXPECT_LT(seam, ownStep);
}

// ---- Glide and mono ----

namespace {

// A looping sine that sounds at about 441 Hz when played at its root (C3 = 60).
Instrument::Ptr sineInstrument() {
    auto sample = makeSample(88200, 1, 44100.0, [](int, int i) { return 0.5f * std::sin(0.0628319f * (float)i); });
    auto region = makeRegion(sample);
    region.loop = RegionSpec::Loop::Continuous;
    return makeInstrument({region});
}

std::vector<float> slice(const std::vector<float>& x, size_t from, size_t count) {
    return std::vector<float>(x.begin() + (long)from, x.begin() + (long)(from + count));
}

// The note struck after a finished one, rendered for `frames`.
Out strikeAfterAnother(float glideSeconds, int frames) {
    Rig rig(sineInstrument());
    auto p = defaults();
    p.glideSeconds = glideSeconds;
    rig.pool.noteOn(60, 0.8f, p);
    render(rig.pool, 512, p);
    rig.pool.noteOff(60, p);
    render(rig.pool, 8192, p);
    rig.pool.noteOn(72, 0.8f, p);
    return render(rig.pool, frames, p);
}

} // namespace

TEST(SamplerVoicePoolTest, GlideSlidesTheNewNoteFromThePreviousPitch) {
    const auto glided = strikeAfterAnother(0.2f, 14000);
    const auto jumped = strikeAfterAnother(0.0f, 14000);
    // Without glide the note is an octave up from the start: 20 cycles per 1000 frames.
    EXPECT_GE(upwardZeroCrossings(slice(jumped.left, 0, 1000)), 19);
    // With glide it starts at the old pitch (10 cycles), is in between half way and ends an octave up.
    EXPECT_LE(upwardZeroCrossings(slice(glided.left, 0, 1000)), 12);
    const int middle = upwardZeroCrossings(slice(glided.left, 4000, 1000));
    EXPECT_GE(middle, 13);
    EXPECT_LE(middle, 18);
    EXPECT_GE(upwardZeroCrossings(slice(glided.left, 12000, 1000)), 19);
}

TEST(SamplerVoicePoolTest, TheFirstNoteOfAFreshPoolDoesNotGlide) {
    Rig rig(sineInstrument());
    auto p = defaults();
    p.glideSeconds = 0.5f;
    rig.pool.noteOn(72, 0.8f, p);
    EXPECT_GE(upwardZeroCrossings(slice(render(rig.pool, 1000, p).left, 0, 1000)), 19);
}

TEST(SamplerVoicePoolTest, MonoKeepsOneVoiceAndStartsNoNewAttack) {
    Rig rig(dcInstrument());
    auto p = defaults();
    p.mono = true;
    p.attackSeconds = 0.05f;
    rig.pool.noteOn(60, 0.8f, p);
    render(rig.pool, 4096, p);
    rig.pool.noteOn(64, 0.8f, p);
    const auto out = render(rig.pool, 1024, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
    EXPECT_TRUE(rig.pool.soundingNotes().test(64));
    EXPECT_FALSE(rig.pool.soundingNotes().test(60));
    EXPECT_NEAR(*std::min_element(out.left.begin(), out.left.end()), 0.5f, 1.0e-3f) << "no dip, no second attack";
}

TEST(SamplerVoicePoolTest, WithoutMonoASecondHeldNoteAddsAVoice) {
    Rig rig(dcInstrument());
    auto p = defaults();
    rig.pool.noteOn(60, 0.8f, p);
    rig.pool.noteOn(64, 0.8f, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 2);
}

TEST(SamplerVoicePoolTest, MonoGoesBackToTheHeldKeyWhenTheNewerOneLifts) {
    Rig rig(dcInstrument());
    auto p = defaults();
    p.mono = true;
    rig.pool.noteOn(60, 0.8f, p);
    rig.pool.noteOn(64, 0.8f, p);
    rig.pool.noteOff(64, p);
    render(rig.pool, 512, p);
    EXPECT_TRUE(rig.pool.soundingNotes().test(60));
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
    rig.pool.noteOff(60, p);
    render(rig.pool, 8192, p);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 0);
}

TEST(SamplerVoicePoolTest, MonoGlidesToTheSecondNoteWithoutRestartingTheSound) {
    Rig rig(sineInstrument());
    auto p = defaults();
    p.mono = true;
    p.glideSeconds = 0.2f;
    rig.pool.noteOn(60, 0.8f, p);
    render(rig.pool, 4096, p);
    rig.pool.noteOn(72, 0.8f, p);
    const auto out = render(rig.pool, 14000, p);
    EXPECT_LE(upwardZeroCrossings(slice(out.left, 0, 1000)), 12);
    EXPECT_GE(upwardZeroCrossings(slice(out.left, 12000, 1000)), 19);
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
}

TEST(SamplerVoicePoolTest, MonoStrikesAFreshNoteWhenTheKeyIsOutsideTheSoundingRecording) {
    Rig rig(makeInstrument({makeRegion(dcSample(0.3f), 0, 71), makeRegion(dcSample(0.6f), 72, 127)}));
    auto p = defaults();
    p.mono = true;
    rig.pool.noteOn(60, 0.8f, p);
    render(rig.pool, 2048, p);
    rig.pool.noteOn(80, 0.8f, p);
    const auto out = render(rig.pool, 8192, p);
    EXPECT_NEAR(out.left.back(), 0.6f, 1.0e-3f) << "the old recording faded out, the new one plays alone";
    EXPECT_EQ(rig.pool.activeVoiceCount(), 1);
}
