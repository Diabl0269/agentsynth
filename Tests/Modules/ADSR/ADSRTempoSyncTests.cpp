// ADSRTempoSyncTests.cpp
// FRO113: the BPM | MS toggle's engine/parameter side. `tempoSync` off (default) leaves the
// existing ms params in sole control (covered by the rest of the ADSR test split); these tests
// cover tempoSync ON -- the four *Div choice params, live tempo tracking, the click-safety of
// flipping the toggle mid-note, offline-render reproducibility, and patch round-trip.

#include "ADSRTestFixture.h"
#include "Transport/TransportService.h"
#include <cmath>

namespace {
// Matches envelopeNoteDivisions() in EnvelopeTempoSync.h: index 5 is the fastest division
// ("1/32"), index 1 is "1/2".
constexpr int kDivQuarter = 2; // "1/4" -- 1 beat
constexpr int kDivFast = 5;    // "1/32" -- 0.125 beat, the fastest available division

// Advances `transport` by `numSamples` in the given block size and returns the sample count
// actually ticked (never more than requested).
void tickTransport(synth::TransportService& transport, int numSamples, int blockSize) {
    int remaining = numSamples;
    while (remaining > 0) {
        const int thisBlock = std::min(blockSize, remaining);
        transport.tick(thisBlock);
        remaining -= thisBlock;
    }
}
} // namespace

// ---------------------------------------------------------------------------
// A synced stage length follows a tempo change.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, SyncedStageLengthHalvesWhenTempoDoubles) {
    setBoolParam(adsr, "tempoSync", true);
    setChoiceIndex(adsr, "attackDiv", kDivFast);
    setChoiceIndex(adsr, "holdDiv", kDivFast);
    setChoiceIndex(adsr, "decayDiv", kDivQuarter); // 1 beat
    setChoiceIndex(adsr, "releaseDiv", kDivFast);
    setFloat(adsr, "sustain", 0.0f); // stays in Sustain stage (silent) once decay lands, never Idle

    synth::TransportService transport;
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;
    transport.prepare(kSampleRate, kBlockSize);
    transport.setBpm(120.0);
    transport.tick(kBlockSize); // drain the setBpm command into the published snapshot
    adsr.setPlayHead(&transport);

    auto blocksToSustain = [&]() -> int {
        adsr.prepareToPlay(kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buf(2, kBlockSize);
        buf.clear();
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        adsr.processBlock(buf, noteOn);
        if (adsr.getPlayheadStage() == synth::EnvelopeStage::Sustain)
            return 1;
        juce::MidiBuffer empty;
        for (int blocks = 1; blocks < 2000; ++blocks) {
            buf.clear();
            adsr.processBlock(buf, empty);
            if (adsr.getPlayheadStage() == synth::EnvelopeStage::Sustain)
                return blocks + 1;
        }
        return -1;
    };

    const int blocksAt120 = blocksToSustain();
    ASSERT_GT(blocksAt120, 0) << "never reached Sustain at 120 BPM";

    transport.setBpm(240.0);
    tickTransport(transport, kBlockSize, kBlockSize); // drain the new tempo into the snapshot

    const int blocksAt240 = blocksToSustain();
    ASSERT_GT(blocksAt240, 0) << "never reached Sustain at 240 BPM";

    // Doubling BPM halves every beat-based stage length, so the block count to reach Sustain
    // should roughly halve too -- generous tolerance for block-granularity (512-sample) polling.
    EXPECT_NEAR(static_cast<double>(blocksAt240), static_cast<double>(blocksAt120) / 2.0,
                static_cast<double>(blocksAt120) * 0.25 + 2)
        << "blocksAt120=" << blocksAt120 << " blocksAt240=" << blocksAt240;
}

// ---------------------------------------------------------------------------
// Switching tempoSync mid-note causes no jump in level.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, TogglingTempoSyncMidDecayCausesNoLevelJump) {
    // MS mode uses a slow 3 s decay; the tempo-synced alternative (fastest division, no
    // playhead so it falls back to the 120 BPM default) is far shorter -- chosen deliberately
    // to maximise the slope discontinuity the toggle could otherwise cause, so a passing test
    // is a real guarantee, not a vacuous one.
    setFloat(adsr, "attack", 0.0f);
    setFloat(adsr, "hold", 0.0f);
    setFloat(adsr, "sustain", 0.4f);
    setFloat(adsr, "decay", 3.0f);
    setChoiceIndex(adsr, "decayDiv", kDivFast);

    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    adsr.processBlock(buf, noteOn); // Attack+Hold floor instantly; well into Decay after this

    // A few more blocks to get a well-established mid-ramp level (not yet close to settling on
    // sustain, ~21% into the slow 3 s decay).
    juce::MidiBuffer empty;
    for (int i = 0; i < 19; ++i) {
        buf.clear();
        adsr.processBlock(buf, empty);
    }
    ASSERT_EQ(adsr.getPlayheadStage(), synth::EnvelopeStage::Decay) << "test setup expects to still be mid-decay";

    const float levelBeforeToggle = buf.getSample(0, buf.getNumSamples() - 1);
    ASSERT_GT(levelBeforeToggle, 0.4f) << "expected a mid-ramp level above sustain, not yet settled";

    setBoolParam(adsr, "tempoSync", true); // flip mid-note: MS decay time -> a much shorter synced one

    buf.clear();
    adsr.processBlock(buf, empty);
    const float levelAfterToggle = buf.getSample(0, 0);

    // Same click-safety bound AntiClickTests.cpp uses for the release-mode-change contract: no
    // single-sample step should look like an instant cut/jump.
    EXPECT_LT(std::abs(levelAfterToggle - levelBeforeToggle), 0.25f * levelBeforeToggle)
        << "toggling tempoSync mid-note must not jump the level, only change the ongoing slope";
}

// ---------------------------------------------------------------------------
// An offline render (fixed tempo, no live wall clock) is reproducible.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, TempoSyncedOfflineRenderIsReproducible) {
    auto render = []() {
        ADSRModule m;
        setBoolParam(m, "tempoSync", true);
        setChoiceIndex(m, "attackDiv", kDivFast);
        setChoiceIndex(m, "decayDiv", kDivQuarter);
        setFloat(m, "sustain", 0.5f);
        setChoiceIndex(m, "releaseDiv", kDivFast);

        synth::TransportService transport;
        constexpr double kSampleRate = 44100.0;
        constexpr int kBlockSize = 256;
        transport.prepare(kSampleRate, kBlockSize);
        transport.setBpm(133.0); // a non-round tempo, deliberately, to stress float conversion
        transport.tick(kBlockSize);
        m.setPlayHead(&transport);
        m.prepareToPlay(kSampleRate, kBlockSize);

        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        buf.clear();
        m.processBlock(buf, noteOn);
        for (int s = 0; s < kBlockSize; ++s)
            out.push_back(buf.getSample(0, s));

        for (int block = 0; block < 20; ++block) {
            juce::MidiBuffer midi;
            if (block == 10)
                midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            buf.clear();
            m.processBlock(buf, midi);
            for (int s = 0; s < kBlockSize; ++s)
                out.push_back(buf.getSample(0, s));
        }
        return out;
    };

    const auto first = render();
    const auto second = render();

    ASSERT_EQ(first.size(), second.size());
    for (size_t i = 0; i < first.size(); ++i)
        ASSERT_EQ(first[i], second[i]) << "sample " << i << " differs between two identical offline renders";
}

// ---------------------------------------------------------------------------
// Patch save/load round-trips the new parameters.
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, StateRoundTripRestoresTempoSyncParameters) {
    setBoolParam(adsr, "tempoSync", true);
    setChoiceIndex(adsr, "attackDiv", 0); // non-default (default is kDivFast == 5)
    setChoiceIndex(adsr, "holdDiv", 3);
    setChoiceIndex(adsr, "decayDiv", 4);
    setChoiceIndex(adsr, "releaseDiv", 0);

    juce::MemoryBlock state;
    adsr.getStateInformation(state);

    ADSRModule restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());

    EXPECT_TRUE(boolParam(restored, "tempoSync")->get());
    EXPECT_EQ(choiceParam(restored, "attackDiv")->getIndex(), 0);
    EXPECT_EQ(choiceParam(restored, "holdDiv")->getIndex(), 3);
    EXPECT_EQ(choiceParam(restored, "decayDiv")->getIndex(), 4);
    EXPECT_EQ(choiceParam(restored, "releaseDiv")->getIndex(), 0);
}
