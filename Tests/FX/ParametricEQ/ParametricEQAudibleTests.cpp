// The Parametric EQ's knobs must reach the sound when they are moved AFTER
// prepareToPlay (the smoothers snap to the knobs there, so a test that sets
// parameters first hides a frozen target). Every case prepares first, then
// moves one parameter, then measures a tone against a reference. Covers all 24
// slots, every type and the cut slope. Then solo audition (play only one band's
// region), the audio thread's no-allocation promise with all 24 bands at their
// steepest, and a recorded CPU figure for that worst case.

#include "../../ClickBoundHelpers.h"
#include "../../Modules/Filter/AllocationCounter.h"
#include "ParametricEQTestHelpers.h"
#include <chrono>
#include <iostream>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace eqtest;

namespace {

/** A prepared EQ with `band` on at (freq, gain, q); the caller moves a knob
 * afterwards. */
struct PreparedEQ {
    ParametricEQModule eq;
    PreparedEQ(int band, float freq, float gain, float q) {
        enableBand(eq, band, freq, gain, q);
        eq.setPlayConfigDetails(kNumChannels, kNumChannels, kSampleRate, kBlockSize);
        eq.prepareToPlay(kSampleRate, kBlockSize);
    }
    static constexpr int kNumChannels = 17; // audio + every CV jack, as the engine allocates them
    float db(float probeHz) { return measureGainDb(eq, probeHz, kNumChannels); }
};

} // namespace

TEST(ParametricEQAudible, Band1GainMovedAfterPrepareLiftsTheLows) {
    PreparedEQ p(0, 100.0f, 0.0f, ParametricEQModule::kDefaultQ);
    EXPECT_NEAR(p.db(40.0f), 0.0f, 0.5f);
    p.eq.setBandGain(0, 12.0f);
    EXPECT_GT(p.db(40.0f), 9.0f);
}

TEST(ParametricEQAudible, Band4GainMovedAfterPrepareLiftsTheHighs) {
    PreparedEQ p(3, 8000.0f, 0.0f, ParametricEQModule::kDefaultQ);
    EXPECT_NEAR(p.db(16000.0f), 0.0f, 0.5f);
    p.eq.setBandGain(3, 12.0f);
    EXPECT_GT(p.db(16000.0f), 9.0f);
}

TEST(ParametricEQAudible, Band1FreqMovedAfterPrepareShiftsTheShelfCorner) {
    PreparedEQ p(0, 100.0f, 12.0f, ParametricEQModule::kDefaultQ);
    const float before = p.db(1000.0f); // a decade above the corner: the shelf is gone
    EXPECT_LT(before, 1.5f);
    p.eq.setBandFreq(0, 4000.0f);
    EXPECT_GT(p.db(1000.0f), before + 6.0f);
}

TEST(ParametricEQAudible, Band4FreqMovedAfterPrepareShiftsTheShelfCorner) {
    PreparedEQ p(3, 8000.0f, 12.0f, ParametricEQModule::kDefaultQ);
    const float before = p.db(1000.0f); // far below the corner: the shelf is gone
    EXPECT_LT(before, 1.5f);
    p.eq.setBandFreq(3, 300.0f);
    EXPECT_GT(p.db(1000.0f), before + 6.0f);
}

TEST(ParametricEQAudible, BellQMovedAfterPrepareNarrowsTheBell) {
    for (const int band : {1, 2}) {
        const float centre = ParametricEQModule::defaultFreqFor(band);
        PreparedEQ p(band, centre, 12.0f, 0.7f);
        const float offCentre = centre * 1.6f;
        const float wide = p.db(offCentre);
        p.eq.setBandQ(band, 8.0f);
        EXPECT_LT(p.db(offCentre), wide - 4.0f) << "band " << band + 1;
    }
}

TEST(ParametricEQAudible, ShelfQMovedAfterPrepareReshapesTheCorner) {
    for (const int band : {0, 3}) {
        const float corner = ParametricEQModule::defaultFreqFor(band);
        PreparedEQ p(band, corner, 12.0f, 0.7f);
        // The corner itself sits at half the shelf gain whatever Q is; the
        // overshoot is an octave out.
        const float probe = band == 0 ? corner * 2.0f : corner * 0.5f;
        const float before = p.db(probe);
        p.eq.setBandQ(band, 8.0f);
        EXPECT_GT(std::abs(p.db(probe) - before), 1.0f) << "band " << band + 1;
    }
}

// Slots 5-24 have no gutter jack; their knobs still reach the sound when moved
// after prepare.
TEST(ParametricEQAudible, EveryHiddenSlotsGainMovedAfterPrepareIsHeard) {
    for (int band = kOriginalBands; band < kMaxBands; ++band) {
        const float centre = ParametricEQModule::defaultFreqFor(band);
        PreparedEQ p(band, centre, 0.0f, 1.0f);
        EXPECT_NEAR(p.db(centre), 0.0f, 0.5f) << "slot " << band + 1;
        p.eq.setBandGain(band, 12.0f);
        EXPECT_GT(p.db(centre), 9.0f) << "slot " << band + 1;
    }
}

// Each type, switched to after prepare on every slot, plays its own shape (the
// drawn level at a probe that tells it apart, within 1 dB; a deep cut just has
// to be deep).
TEST(ParametricEQAudible, EveryTypeSwitchedAfterPrepareOnEverySlotIsHeard) {
    struct Probe {
        BandType type;
        float hz;
    };
    const Probe probes[] = {{BandType::LowShelf, 100.0f}, {BandType::Peak, 1000.0f},    {BandType::HighShelf, 10000.0f},
                            {BandType::LowCut, 250.0f},   {BandType::HighCut, 4000.0f}, {BandType::Notch, 1000.0f},
                            {BandType::BandPass, 100.0f}, {BandType::Tilt, 100.0f}};
    for (int band = 0; band < kMaxBands; ++band)
        for (const auto& probe : probes) {
            PreparedEQ p(band, 1000.0f, 12.0f, 0.7f);
            p.eq.setBandType(band, probe.type == BandType::Peak ? BandType::Tilt : BandType::Peak);
            p.db(probe.hz); // settle on the starting type
            p.eq.setBandType(band, probe.type);
            const float drawn = ParametricEQModule::bandMagnitudeDb(probe.type, 1000.0f, 12.0f, 0.7f, probe.hz);
            const float heard = p.db(probe.hz);
            if (drawn < -30.0f)
                EXPECT_LT(heard, -25.0f) << "slot " << band + 1 << " type " << (int)probe.type;
            else
                EXPECT_NEAR(heard, drawn, 1.0f) << "slot " << band + 1 << " type " << (int)probe.type;
        }
}

TEST(ParametricEQAudible, ACutsSlopeMovedAfterPrepareSteepensIt) {
    for (const int band : {0, 4, kMaxBands - 1}) {
        PreparedEQ p(band, 1000.0f, 0.0f, ParametricEQModule::kDefaultQ);
        p.eq.setBandType(band, BandType::LowCut);
        const float gentle = p.db(250.0f);
        EXPECT_NEAR(gentle, -24.0f, 1.5f) << "slot " << band + 1;
        p.eq.setBandSlope(band, 36);
        EXPECT_LT(p.db(250.0f), gentle - 30.0f) << "slot " << band + 1;
    }
}

// By design: a band that is off plays nothing, whatever its Type and Slope say.
// Moving them is silent until the band is turned on, which is what makes the
// hidden settings safe to carry in a saved patch.
TEST(ParametricEQAudible, ADisabledBandsTypeAndSlopeAreSilentByDesign) {
    for (const int band : {1, 9}) {
        PreparedEQ p(band, 1000.0f, 12.0f, 0.7f);
        p.eq.setBandEnabled(band, false);
        p.eq.setBandType(band, BandType::HighCut);
        p.eq.setBandSlope(band, 96);
        EXPECT_NEAR(p.db(4000.0f), 0.0f, 0.01f) << "slot " << band + 1;
        p.eq.setBandEnabled(band, true);
        EXPECT_LT(p.db(4000.0f), -60.0f) << "slot " << band + 1;
    }
}

namespace {

constexpr int kChannels = 17;

void prepare(ParametricEQModule& eq) {
    eq.setPlayConfigDetails(kChannels, kChannels, kSampleRate, kBlockSize);
    eq.prepareToPlay(kSampleRate, kBlockSize);
}

/** Two bands far apart, so a solo on one clearly drops the other's region. */
void twoBands(ParametricEQModule& eq) {
    enableBand(eq, 1, 300.0f, 6.0f, 2.0f);
    enableShapedBand(eq, 7, BandType::Peak, 5000.0f, -6.0f, 2.0f, 12);
}

/** A stereo noise render of `blocks` blocks, channel 0 then 1 per block. */
std::vector<float> renderNoise(ParametricEQModule& eq, int blocks, juce::Random& random,
                               const std::function<void(int)>& beforeBlock = {}) {
    juce::AudioBuffer<float> buffer(kChannels, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int block = 0; block < blocks; ++block) {
        if (beforeBlock)
            beforeBlock(block);
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(ch, i, 0.4f * (random.nextFloat() - 0.5f));
        eq.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.insert(out.end(), buffer.getReadPointer(ch), buffer.getReadPointer(ch) + kBlockSize);
    }
    return out;
}

} // namespace

TEST(ParametricEQSolo, SoloIsNotSavedAndStartsCleared) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.getSoloBand(), -1);
    eq.setSoloBand(7);
    EXPECT_EQ(eq.getSoloBand(), 7);
    juce::MemoryBlock state;
    eq.getStateInformation(state);
    ParametricEQModule restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());
    EXPECT_EQ(restored.getSoloBand(), -1);
    eq.setSoloBand(ParametricEQModule::kMaxBands);
    EXPECT_EQ(eq.getSoloBand(), -1) << "an invalid band clears the solo";
    eq.setSoloBand(3);
    eq.clearSolo();
    EXPECT_EQ(eq.getSoloBand(), -1);
}

TEST(ParametricEQSolo, ASoloedBandPassesItsRegionAndDropsTheRest) {
    for (const int band : {1, 7}) {
        ParametricEQModule eq;
        twoBands(eq);
        prepare(eq);
        const float centre = band == 1 ? 300.0f : 5000.0f;
        const float other = band == 1 ? 5000.0f : 300.0f;
        eq.setSoloBand(band);
        EXPECT_NEAR(measureGainDb(eq, centre, kChannels), 0.0f, 0.5f) << "the soloed band's centre plays dry";
        EXPECT_LT(measureGainDb(eq, other, kChannels), -12.0f) << "the other band's region is dropped";
        EXPECT_LT(measureGainDb(eq, 20.0f, kChannels), -12.0f);
    }
}

TEST(ParametricEQSolo, ASoloOnABandThatIsOffPlaysTheNormalOutput) {
    juce::Random a(7), b(7);
    ParametricEQModule soloed, plain;
    twoBands(soloed);
    twoBands(plain);
    prepare(soloed);
    prepare(plain);
    soloed.setSoloBand(12); // off
    EXPECT_EQ(renderNoise(soloed, 16, a), renderNoise(plain, 16, b));
}

// The bands keep running under a solo, so clearing it continues exactly as a never-soloed EQ.
TEST(ParametricEQSolo, ClearingASoloResumesTheUnsoloedOutputExactly) {
    juce::Random a(11), b(11);
    ParametricEQModule soloed, plain;
    twoBands(soloed);
    twoBands(plain);
    prepare(soloed);
    prepare(plain);
    const auto withSolo = renderNoise(soloed, 24, a, [&](int block) {
        if (block == 4)
            soloed.setSoloBand(1);
        if (block == 12)
            soloed.clearSolo();
    });
    const auto without = renderNoise(plain, 24, b);
    // The solo fades out over ~15 ms (under two blocks), after which the output is the unsoloed EQ bit for bit.
    const size_t resumed = (size_t)14 * 2 * kBlockSize;
    const size_t beforeClear = (size_t)12 * 2 * kBlockSize;
    EXPECT_NE(withSolo[beforeClear - 1], without[beforeClear - 1]) << "the solo was audible";
    EXPECT_TRUE(std::equal(withSolo.begin() + (long)resumed, withSolo.end(), without.begin() + (long)resumed));
}

// ---------- the solo cross-fades: no click on any transition ----------

namespace {

/** Three bands around the 440 Hz test tone's neighbourhood and far above it, so every solo changes the level a lot. */
void soloBands(ParametricEQModule& eq) {
    enableBand(eq, 1, 440.0f, 6.0f, 2.0f);
    enableShapedBand(eq, 7, BandType::Peak, 880.0f, -6.0f, 2.0f, 12);
    enableShapedBand(eq, 9, BandType::Peak, 5000.0f, 4.0f, 2.0f, 12);
}

/** Renders the shared click-bound sine through a prepared EQ; `edge` runs between the settle and edge blocks and
 *  `setup` before block 0. Returns the click-bound steps of the left channel. */
clickbound::Steps soloSteps(const std::function<void(ParametricEQModule&)>& setup,
                            const std::function<void(ParametricEQModule&)>& edge) {
    ParametricEQModule eq;
    soloBands(eq);
    eq.setPlayConfigDetails(kChannels, kChannels, clickbound::kRate, clickbound::kBlock);
    eq.prepareToPlay(clickbound::kRate, clickbound::kBlock);
    clickbound::Input in;
    return clickbound::measure(clickbound::render(eq, in, {0}, [&](int b) {
        if (b == 0)
            setup(eq);
        if (b == clickbound::kSettleBlocks)
            edge(eq);
    }));
}

} // namespace

TEST(ParametricEQSoloClick, SoloOffToBandHasNoClick) {
    const auto steps = soloSteps([](auto&) {}, [](auto& eq) { eq.setSoloBand(7); });
    std::cout << "[FRO1783] off->band edge " << steps.edge << " steady " << steps.steady << "\n";
    EXPECT_NO_CLICK(steps);
}

TEST(ParametricEQSoloClick, SoloBandToOtherBandHasNoClick) {
    const auto steps = soloSteps([](auto& eq) { eq.setSoloBand(1); }, [](auto& eq) { eq.setSoloBand(9); });
    std::cout << "[FRO1783] band->other edge " << steps.edge << " steady " << steps.steady << "\n";
    EXPECT_NO_CLICK(steps);
}

TEST(ParametricEQSoloClick, SoloBandToOffHasNoClick) {
    const auto steps = soloSteps([](auto& eq) { eq.setSoloBand(9); }, [](auto& eq) { eq.clearSolo(); });
    std::cout << "[FRO1783] band->off edge " << steps.edge << " steady " << steps.steady << "\n";
    EXPECT_NO_CLICK(steps);
}

TEST(ParametricEQSoloClick, TheSoloedBandSwitchedOffMidSoloHasNoClick) {
    const auto steps = soloSteps([](auto& eq) { eq.setSoloBand(9); }, [](auto& eq) { eq.setBandEnabled(9, false); });
    std::cout << "[FRO1783] soloed band off edge " << steps.edge << " steady " << steps.steady << "\n";
    EXPECT_NO_CLICK(steps);
}

TEST(ParametricEQSoloClick, SwitchingBandsFastNeverResetsAnAudibleFilter) {
    // Three switches in consecutive blocks: each swap waits for the fade-out, so no step appears at any of them.
    const auto steps = soloSteps([](auto& eq) { eq.setSoloBand(1); }, [](auto& eq) { eq.setSoloBand(7); });
    EXPECT_NO_CLICK(steps);
    ParametricEQModule eq;
    soloBands(eq);
    eq.setPlayConfigDetails(kChannels, kChannels, clickbound::kRate, clickbound::kBlock);
    eq.prepareToPlay(clickbound::kRate, clickbound::kBlock);
    const int order[] = {1, 7, 9, 1};
    const auto out = clickbound::render(eq, clickbound::Input{}, {0}, [&](int b) {
        if (b >= 4 && b < 8)
            eq.setSoloBand(order[b - 4]);
    });
    const float steady = clickbound::largestStep(out[0], 2, 4);
    EXPECT_LE(clickbound::largestStep(out[0], 4, 30), clickbound::kTolerance * std::max(steady, clickbound::kFloor));
}

// Listen proof: writes the four transitions, one second apart, to $AGENTSYNTH_WRITE_PROOF_WAV (a file path).
// Skipped unless the variable is set.
TEST(ParametricEQSoloClick, WritesTheListenProofWhenAskedTo) {
    const auto path = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_WRITE_PROOF_WAV", {});
    if (path.isEmpty())
        GTEST_SKIP() << "set AGENTSYNTH_WRITE_PROOF_WAV to a .wav path to write the proof";
    ParametricEQModule eq;
    soloBands(eq);
    prepare(eq);
    constexpr int kSeconds = 6;
    const int blocks = (int)(kSampleRate * kSeconds) / kBlockSize;
    juce::AudioBuffer<float> buffer(kChannels, kBlockSize);
    juce::AudioBuffer<float> rendered(2, blocks * kBlockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const auto at = [&](double seconds) { return (int)(seconds * kSampleRate) / kBlockSize; };
    for (int block = 0; block < blocks; ++block) {
        if (block == at(1.0))
            eq.setSoloBand(1); // off -> band
        if (block == at(2.0))
            eq.setSoloBand(9); // band -> other band
        if (block == at(3.0))
            eq.clearSolo(); // band -> off
        if (block == at(4.0))
            eq.setSoloBand(7);
        if (block == at(5.0))
            eq.setBandEnabled(7, false); // the soloed band switched off
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i, phase += 1.0) {
            float v = 0.0f; // a chord of saws: dense harmonics, so every band has something to filter
            for (const double hz : {110.0, 165.0, 220.0, 330.0})
                v += 0.12f * (float)(2.0 * std::fmod(phase * hz / kSampleRate, 1.0) - 1.0);
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        eq.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            rendered.copyFrom(ch, block * kBlockSize, buffer, ch, 0, kBlockSize);
    }
    const juce::File file(path);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    ASSERT_NE(stream, nullptr);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), kSampleRate, 2, 24, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release();
    ASSERT_TRUE(writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples()));
    writer.reset();
    float worst = 0.0f;
    for (int i = 1; i < rendered.getNumSamples(); ++i)
        worst = std::max(worst, std::abs(rendered.getSample(0, i) - rendered.getSample(0, i - 1)));
    std::cout << "[FRO1783] proof max step " << worst << " -> " << path << "\n";
}

TEST(ParametricEQPerformance, ProcessBlockNeverAllocatesWithEveryBandAtItsSteepest) {
    ParametricEQModule eq;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        enableShapedBand(eq, b, b % 2 == 0 ? BandType::LowCut : BandType::HighCut,
                         ParametricEQModule::defaultFreqFor(b), 0.0f, ParametricEQModule::kDefaultQ, 96);
    prepare(eq);
    juce::AudioBuffer<float> buffer(kChannels, kBlockSize);
    juce::MidiBuffer midi;
    buffer.clear();
    eq.processBlock(buffer, midi); // first block outside the scope, as the engine's would be
    synth::test::AllocationScope scope;
    for (int block = 0; block < 8; ++block) {
        // Every kind of change the audio thread meets: a knob moving, a type and slope switch, a band toggling, a solo.
        eq.setBandGain(3, (float)block);
        eq.setBandType(5, block % 2 == 0 ? BandType::Notch : BandType::LowCut);
        eq.setBandSlope(6, block % 2 == 0 ? 18 : 96);
        eq.setBandEnabled(9, block % 2 == 0);
        eq.setSoloBand(block % 3 == 0 ? 2 : -1);
        buffer.clear();
        buffer.setSample(0, 0, 1.0f);
        eq.processBlock(buffer, midi);
    }
    // The setters above run on this thread too; they notify the host but must not allocate either.
    EXPECT_EQ(scope.count(), 0);
}

// Worst case: 24 bands, each a 96 dB/oct cut (8 biquads), stereo, 48 kHz, 512-sample blocks. Recorded, not asserted.
TEST(ParametricEQPerformance, RecordsTheCostOfTwentyFourSteepestBands) {
    ParametricEQModule eq;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        enableShapedBand(eq, b, b % 2 == 0 ? BandType::LowCut : BandType::HighCut,
                         ParametricEQModule::defaultFreqFor(b), 0.0f, ParametricEQModule::kDefaultQ, 96);
    prepare(eq);
    juce::AudioBuffer<float> buffer(kChannels, kBlockSize);
    juce::MidiBuffer midi;
    juce::Random random(3);
    constexpr int kBlocks = 2000;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.4f * (random.nextFloat() - 0.5f));
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kBlocks; ++block)
        eq.processBlock(buffer, midi);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double audioSeconds = (double)kBlocks * kBlockSize / kSampleRate;
    std::cout << "[  RECORD  ] Parametric EQ, 24 bands x 96 dB/oct, stereo 48 kHz/512: " << seconds * 1.0e6 / kBlocks
              << " us per block, " << 100.0 * seconds / audioSeconds << "% of one core's real time\n";
    SUCCEED();
}
