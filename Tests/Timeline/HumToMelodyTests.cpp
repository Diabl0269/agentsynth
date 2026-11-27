// HumToMelodyTests.cpp -- the pitch tracker, note cutter and quantiser behind "Convert to melody", on synthetic sung
// melodies (HumSynth.h). Every case would fail with the feature absent: it asserts the notes the hum contains.
#include "HumSynth.h"
#include "Music/SongKey.h"
#include "Timeline/Melody/HumToMelody.h"
#include "Timeline/Melody/PitchTracker.h"
#include <cmath>
#include <gtest/gtest.h>
#include <random>

using namespace synth::melody;

namespace {

constexpr double kSpb = 0.5;

MelodyOptions optionsFor(const std::vector<float>& audio, double sr, MelodyMode mode) {
    MelodyOptions options;
    options.mode = mode;
    options.secondsPerBeat = kSpb;
    options.lengthBeats = (double)audio.size() / sr / kSpb;
    options.gridBeats = 0.5;
    return options;
}

std::vector<int> pitchesOf(const std::vector<MelodyNote>& notes) {
    std::vector<int> result;
    for (const auto& note : notes)
        result.push_back(note.pitch);
    return result;
}

const std::vector<int> kCMajorTune{60, 62, 64, 65, 67, 65, 64, 62};

humsynth::Spec cleanSpec() {
    humsynth::Spec spec;
    spec.notes = kCMajorTune;
    return spec;
}

} // namespace

TEST(HumToMelodyTests, CleanMelodyIsTrackedWithinFiftyCents) {
    humsynth::Truth truth;
    const auto spec = cleanSpec();
    const auto audio = humsynth::render(spec, &truth);
    const auto frames = trackPitch(audio.data(), (int)audio.size(), spec.sampleRate);
    ASSERT_FALSE(frames.empty());

    int steady = 0, voiced = 0, close = 0;
    for (const auto& frame : frames)
        for (size_t i = 0; i < truth.startSec.size(); ++i) {
            // Steady part of the note: past the scoop or glide in, clear of the release.
            if (frame.timeSec < truth.startSec[i] + 0.12 || frame.timeSec > truth.endSec[i] - 0.06)
                continue;
            ++steady;
            if (!frame.voiced)
                continue;
            ++voiced;
            // Vibrato (20 cents) rides on top of the detuned pitch, so allow it inside the 50 cents.
            if (std::abs(frame.midi - truth.cents[i]) < 0.5)
                ++close;
        }
    EXPECT_GT(voiced, steady * 9 / 10) << "the steady part of a sung note is voiced";
    EXPECT_GE(close, voiced * 95 / 100);
}

TEST(HumToMelodyTests, AsSungGivesTheSungPitchesAndOnsets) {
    humsynth::Truth truth;
    const auto spec = cleanSpec();
    const auto audio = humsynth::render(spec, &truth);
    const auto notes = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate,
                                   optionsFor(audio, spec.sampleRate, MelodyMode::AsSung));
    EXPECT_EQ(pitchesOf(notes), kCMajorTune);
    ASSERT_EQ(notes.size(), truth.startSec.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        EXPECT_NEAR(notes[i].startBeat * kSpb, truth.startSec[i], 0.05) << "note " << i;
        EXPECT_GE(notes[i].velocity, 60);
        EXPECT_LE(notes[i].velocity, 120);
    }
}

TEST(HumToMelodyTests, WideVibratoDoesNotSplitNotes) {
    auto spec = cleanSpec();
    spec.notes = {62, 67, 64};
    spec.noteSec = 1.2;
    spec.vibCents = 35.0;
    spec.detuneCents = 0.0;
    const auto audio = humsynth::render(spec);
    const auto notes = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate,
                                   optionsFor(audio, spec.sampleRate, MelodyMode::AsSung));
    EXPECT_EQ(pitchesOf(notes), spec.notes);
}

TEST(HumToMelodyTests, SloppyBreathyHumSnapsToTheKey) {
    auto spec = cleanSpec();
    spec.detuneCents = 30.0;
    spec.breath = 0.3;
    spec.roomNoise = 0.01;
    const auto audio = humsynth::render(spec);
    auto options = optionsFor(audio, spec.sampleRate, MelodyMode::SnapToKey);
    options.key = synth::parseKeyText("C Major");
    ASSERT_TRUE(options.key.has_value());
    const auto notes = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate, options);
    EXPECT_EQ(pitchesOf(notes), kCMajorTune);
}

TEST(HumToMelodyTests, SnapModeIsOnScaleAndOnGridAndAsSungKeepsOffGridOnsets) {
    auto spec = cleanSpec();
    spec.leadSec = 0.13; // an onset that is off any half-beat grid
    const auto audio = humsynth::render(spec);
    auto snap = optionsFor(audio, spec.sampleRate, MelodyMode::SnapToKey);
    snap.key = synth::parseKeyText("C Major");
    const auto snapped = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate, snap);
    ASSERT_FALSE(snapped.empty());
    for (size_t i = 0; i < snapped.size(); ++i) {
        EXPECT_TRUE(snap.key->contains(snapped[i].pitch));
        const double start = snapped[i].startBeat / snap.gridBeats;
        const double end = (snapped[i].startBeat + snapped[i].lengthBeats) / snap.gridBeats;
        EXPECT_NEAR(start, std::round(start), 1.0e-9);
        EXPECT_NEAR(end, std::round(end), 1.0e-9);
        EXPECT_GE(snapped[i].lengthBeats, snap.gridBeats - 1.0e-9);
        if (i + 1 < snapped.size())
            EXPECT_LE(snapped[i].startBeat + snapped[i].lengthBeats, snapped[i + 1].startBeat + 1.0e-9);
    }

    const auto sung = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate,
                                  optionsFor(audio, spec.sampleRate, MelodyMode::AsSung));
    ASSERT_FALSE(sung.empty());
    const double onGrid = sung.front().startBeat / 0.5;
    EXPECT_GT(std::abs(onGrid - std::round(onGrid)), 0.1) << "as sung keeps the singer's timing";
}

TEST(HumToMelodyTests, SilenceAndNoiseHaveNoNotes) {
    const double sr = 22050.0;
    std::vector<float> silence((size_t)(sr * 2), 0.0f);
    EXPECT_TRUE(
        humToMelody(silence.data(), (int)silence.size(), sr, optionsFor(silence, sr, MelodyMode::AsSung)).empty());

    std::mt19937 rng(7);
    std::normal_distribution<float> gauss(0.0f, 0.2f);
    std::vector<float> noise((size_t)(sr * 3));
    for (auto& v : noise)
        v = gauss(rng);
    EXPECT_TRUE(humToMelody(noise.data(), (int)noise.size(), sr, optionsFor(noise, sr, MelodyMode::AsSung)).empty());
}

TEST(HumToMelodyTests, NoSongKeyDetectsTheHumsKey) {
    // A full scale, so the key is not ambiguous (the first five notes of it fit F major as well).
    auto spec = cleanSpec();
    spec.notes = {60, 62, 64, 65, 67, 69, 71, 72};
    const auto audio = humsynth::render(spec);
    const auto cut = cutNotes(trackPitch(audio.data(), (int)audio.size(), spec.sampleRate));
    const auto key = detectHumKey(cut);
    for (int pitchClass : {0, 2, 4, 5, 7, 9, 11})
        EXPECT_TRUE(key.contains(pitchClass)) << pitchClass;
    EXPECT_FALSE(key.contains(1));

    auto options = optionsFor(audio, spec.sampleRate, MelodyMode::SnapToKey); // no key given
    synth::MusicalScale used;
    const auto notes = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate, options, &used);
    EXPECT_EQ(used, key);
    EXPECT_EQ(pitchesOf(notes), spec.notes);
}

TEST(HumToMelodyTests, RepeatedNoteAfterAShortGapIsTwoNotes) {
    auto spec = cleanSpec();
    spec.notes = {64, 64};
    spec.gapSec = 0.1;
    const auto audio = humsynth::render(spec);
    const auto notes = humToMelody(audio.data(), (int)audio.size(), spec.sampleRate,
                                   optionsFor(audio, spec.sampleRate, MelodyMode::AsSung));
    EXPECT_EQ(pitchesOf(notes), (std::vector<int>{64, 64}));
}

TEST(HumToMelodyTests, StreamingBlocksGiveTheSameFramesAsOneCall) {
    const auto spec = cleanSpec();
    const auto audio = humsynth::render(spec);
    PitchTracker whole;
    whole.reset(spec.sampleRate);
    std::vector<PitchFrame> expected;
    whole.push(audio.data(), (int)audio.size(), expected);

    PitchTracker streamed;
    streamed.reset(spec.sampleRate);
    std::vector<PitchFrame> actual;
    std::mt19937 rng(99);
    size_t position = 0;
    while (position < audio.size()) {
        const size_t block = std::min<size_t>(1 + rng() % 3000, audio.size() - position);
        streamed.push(audio.data() + position, (int)block, actual);
        position += block;
    }
    ASSERT_FALSE(expected.empty());
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i].timeSec, expected[i].timeSec);
        EXPECT_EQ(actual[i].midi, expected[i].midi);
        EXPECT_EQ(actual[i].clarity, expected[i].clarity);
        EXPECT_EQ(actual[i].voiced, expected[i].voiced);
    }
}
