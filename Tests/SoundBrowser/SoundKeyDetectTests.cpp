// SoundKeyDetectTests.cpp -- a loop that names no key gets one from its audio: synthesised chord loops land
// in their key, noise and silence get none, and the scan uses it only for loops that name no key and are not drums.
#include "SoundBrowser/SoundKeyDetect.h"
#include "SoundBrowser/SoundNameGuess.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include <gtest/gtest.h>
#include <iostream>
#include <random>

using namespace sound_browser_test;
using namespace synth::soundbrowser;

namespace {

constexpr double kRate = 44100.0;

double hzOf(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// One second per chord, each note a tone with three decaying harmonics, the way a pad or keys loop sounds.
std::vector<float> chords(const std::vector<std::vector<int>>& progression, int transpose = 0) {
    const int perChord = (int)kRate;
    std::vector<float> out((size_t)perChord * progression.size(), 0.0f);
    for (size_t c = 0; c < progression.size(); ++c)
        for (const int note : progression[c])
            for (int h = 1; h <= 3; ++h) {
                const double hz = hzOf(note + transpose) * h;
                for (int i = 0; i < perChord; ++i)
                    out[c * (size_t)perChord + (size_t)i] +=
                        (float)(0.08 / h * std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kRate));
            }
    return out;
}

// i - iv - V - i in A minor (the V is E major, with its G#), and I - IV - V - I in C major.
const std::vector<std::vector<int>> kAMinor{{57, 60, 64}, {62, 65, 69}, {52, 56, 59}, {57, 60, 64}};
const std::vector<std::vector<int>> kCMajor{{60, 64, 67}, {65, 69, 72}, {67, 71, 74}, {60, 64, 67}};

juce::String keyOf(const std::vector<float>& audio) {
    return keyFromChroma(chromaOf(audio.data(), (int)audio.size(), kRate));
}

void writeAudio(const juce::File& file, const std::vector<float>& audio) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto* stream = new juce::FileOutputStream(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, kRate, 1, 16, {}, 0));
    juce::AudioBuffer<float> buffer(1, (int)audio.size());
    buffer.copyFrom(0, 0, audio.data(), (int)audio.size());
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

const SoundEntry* findByName(const SoundSnapshot& s, const juce::String& name) {
    for (const auto& e : s.entries)
        if (e.name == name)
            return &e;
    return nullptr;
}

} // namespace

TEST(SoundKeyDetect, AMinorChordLoopIsAMinorAndAMajorOneIsMajor) {
    EXPECT_EQ(keyOf(chords(kAMinor)), "A minor");
    EXPECT_EQ(keyOf(chords(kCMajor)), "C major");
}

TEST(SoundKeyDetect, EveryTransposedLoopLandsInItsOwnKey) {
    for (int shift = 0; shift < 12; ++shift) {
        EXPECT_EQ(keyOf(chords(kAMinor, shift)), noteNameOf(57 + shift) + " minor") << shift;
        EXPECT_EQ(keyOf(chords(kCMajor, shift)), noteNameOf(60 + shift) + " major") << shift;
    }
}

TEST(SoundKeyDetect, NoiseSilenceAndALowRumbleGetNoKeyRatherThanAGuess) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
    std::vector<float> noise((size_t)(4 * kRate));
    for (auto& s : noise)
        s = dist(rng);
    EXPECT_EQ(keyOf(noise), "");
    EXPECT_EQ(keyOf(std::vector<float>((size_t)(4 * kRate), 0.0f)), "");
    // Noise bursts that decay, like a drum loop's hits.
    std::vector<float> hits((size_t)(4 * kRate), 0.0f);
    for (size_t i = 0; i < hits.size(); ++i)
        hits[i] = dist(rng) * (float)std::exp(-(double)(i % 11025) / 1500.0);
    EXPECT_EQ(keyOf(hits), "");
    EXPECT_EQ(keyFromChroma(Chroma{}), "");
    EXPECT_EQ(keyOf({}), "");
}

TEST(SoundKeyDetect, AShortLoopUnderOneFrameIsStillHeard) {
    auto audio = chords({{57, 60, 64}, {52, 56, 59}});
    audio.resize((size_t)(0.3 * kRate));
    EXPECT_FALSE(keyOf(audio).isEmpty()) << "zero padded to one frame";
}

TEST(SoundKeyDetectScan, ALoopThatNamesNoKeyGetsOneFromItsAudioAndTheNameStillWins) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    writeAudio(tree.packs.getChildFile("p/Loops/warm pad loop.wav"), chords(kAMinor));
    writeAudio(tree.packs.getChildFile("p/Loops/warm pad loop Cmaj.wav"), chords(kAMinor));
    writeAudio(tree.packs.getChildFile("p/Loops/kick drum loop.wav"), chords(kAMinor));
    writeAudio(tree.packs.getChildFile("p/stab.wav"), chords(kAMinor));
    auto index = makeIndex(tree, true);
    scanAndWait(*index);
    const auto snap = index->snapshot();

    const auto* pad = findByName(*snap, "warm pad loop");
    ASSERT_NE(pad, nullptr);
    EXPECT_EQ(pad->kind, SoundKind::Loop);
    EXPECT_EQ(pad->key, "A minor");
    EXPECT_TRUE(pad->keyDetected);
    EXPECT_TRUE(synth::ui::soundKeyMetaText(*pad, "").contains("(detected)"));

    const auto* named = findByName(*snap, "warm pad loop Cmaj");
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->key, "C major") << "a key in the name is the file's own word";
    EXPECT_FALSE(named->keyDetected);

    const auto* drums = findByName(*snap, "kick drum loop");
    ASSERT_NE(drums, nullptr);
    EXPECT_EQ(drums->key, "") << "a drum loop gets no key";

    const auto* stab = findByName(*snap, "stab");
    ASSERT_NE(stab, nullptr);
    EXPECT_EQ(stab->kind, SoundKind::OneShot);
    EXPECT_EQ(stab->key, "") << "one-shots stay header-only";

    // The cache keeps the detected key and where it came from, with nothing opened again.
    auto again = makeIndex(tree, true);
    scanAndWait(*again);
    EXPECT_EQ(again->headersReadForTests(), 0);
    const auto* cached = findByName(*again->snapshot(), "warm pad loop");
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached->key, "A minor");
    EXPECT_TRUE(cached->keyDetected);
}

TEST(SoundKeyDetectScan, TheKeyFilterFindsALoopByItsDetectedKey) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    writeAudio(tree.packs.getChildFile("p/Loops/warm pad loop.wav"), chords(kAMinor));
    auto index = makeIndex(tree);
    scanAndWait(*index);
    SoundFilter filter;
    filter.key = "A minor";
    EXPECT_EQ(index->search("", filter).size(), 1u);
    filter.key = "C major";
    EXPECT_EQ(index->search("", filter).size(), 0u);
}

// Not a check: lists the key found for every loop under AGENTSYNTH_KEY_PROBE_DIR (a real sample library), to see the
// detector on recordings rather than synthesised chords. Skipped when the variable is not set.
TEST(SoundKeyDetectProbe, ListsTheKeyOfEveryAudioFileUnderAFolder) {
    const auto dir = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_KEY_PROBE_DIR", {});
    if (dir.isEmpty())
        GTEST_SKIP() << "set AGENTSYNTH_KEY_PROBE_DIR to a folder of loops";
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& item :
         juce::RangedDirectoryIterator(juce::File(dir), true, "*.wav;*.aif;*.aiff", juce::File::findFiles)) {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(item.getFile()));
        if (reader == nullptr)
            continue;
        const auto key = detectKey(*reader);
        std::cout << "[key] " << item.getFile().getFileName() << " -> " << (key.isEmpty() ? "(none)" : key) << "\n";
    }
}
