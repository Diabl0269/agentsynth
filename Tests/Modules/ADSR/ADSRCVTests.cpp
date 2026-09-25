// ADSRCVTests.cpp
// FRO285: the five stage-time/level CV jacks (Attack ch9, Hold ch10, Decay ch11, Sustain ch12,
// Release ch13) appended after Threshold (ch8) -- port layout, the normalised-CV convention
// (docs/modules/modulation.md#cv-in-normalised-units), the tempo-sync CV-ignore rule, and that
// none of the five is ever mistaken for a poly gate head. Threshold's own CV coverage stays in
// ADSRThresholdTests.cpp.

#include "ADSRTestFixture.h"
#include "Transport/TransportService.h"

// ---------------------------------------------------------------------------
// Port layout
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, InputPortLabelsForNewCvJacks) {
    EXPECT_EQ(adsr.getInputPortLabel(2), "Attack");
    EXPECT_EQ(adsr.getInputPortLabel(3), "Hold");
    EXPECT_EQ(adsr.getInputPortLabel(4), "Decay");
    EXPECT_EQ(adsr.getInputPortLabel(5), "Sustain");
    EXPECT_EQ(adsr.getInputPortLabel(6), "Release");
}

TEST_F(ADSRTest, StageTimeCvJacksMapToModCv) {
    struct Case {
        int channel;
        int jack;
        const char* label;
    };
    const Case cases[] = {
        {9, 2, "Attack"}, {10, 3, "Hold"}, {11, 4, "Decay"}, {12, 5, "Sustain"}, {13, 6, "Release"},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.label);
        const auto port = adsr.mapInputChannel(c.channel);
        EXPECT_EQ(port.role, PortRole::ModCV);
        EXPECT_EQ(port.visibleJackIndex, c.jack);
        EXPECT_TRUE(port.isPolyGroupHead);
        EXPECT_EQ(port.polyVoiceSpan, 1);
        EXPECT_EQ(adsr.getInputPortLabel(c.jack), juce::String(c.label));
    }
}

// ch9-13 must never register as an extra poly gate voice -- mapInputChannel gives them ModCV,
// never Gate, and processBlock's poly branch only ever reads voiceData[0..7] regardless.
TEST_F(ADSRTest, StageTimeCvChannelsAreNotPolyGateHeads) {
    setPoly(adsr, true);
    juce::AudioBuffer<float> buf(14, 512);
    buf.clear();
    for (int i = 0; i < 512; ++i) {
        buf.setSample(9, i, 1.0f);  // Attack CV driven high
        buf.setSample(10, i, 1.0f); // Hold CV driven high
        buf.setSample(13, i, 1.0f); // Release CV driven high
    }
    juce::MidiBuffer empty;
    adsr.processBlock(buf, empty);
    EXPECT_EQ(adsr.getTriggerCount(), 0) << "ch9-13 must never be read as an extra poly gate voice";
    for (int v = 0; v < 8; ++v)
        EXPECT_NEAR(buf.getRMSLevel(v, 0, 512), 0.0f, 1e-4f) << "voice " << v;
}

// ---------------------------------------------------------------------------
// Attack / Release CV move the stage timing (normalised-CV convention)
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, DefaultAttackFinishesWithinOneBlock) {
    juce::AudioBuffer<float> buf(14, 512);
    buf.clear();
    for (int i = 0; i < 512; ++i)
        buf.setSample(0, i, 1.0f);
    juce::MidiBuffer empty;
    adsr.processBlock(buf, empty);
    EXPECT_NE(adsr.getPlayheadStage(), synth::EnvelopeStage::Attack)
        << "the 1 ms default attack should be long done after one 512-sample block";
}

TEST_F(ADSRTest, AttackCVLengthensAttack) {
    juce::AudioBuffer<float> buf(14, 512);
    buf.clear();
    for (int i = 0; i < 512; ++i) {
        buf.setSample(0, i, 1.0f); // gate on
        buf.setSample(9, i, 0.5f); // Attack CV: sweep the knob halfway to its 5 s ceiling
    }
    juce::MidiBuffer empty;
    adsr.processBlock(buf, empty);
    EXPECT_EQ(adsr.getPlayheadStage(), synth::EnvelopeStage::Attack)
        << "positive Attack CV should still be mid-attack after one block";
}

TEST_F(ADSRTest, ReleaseCVLengthensRelease) {
    auto stageAfterRelease = [](bool applyReleaseCV) {
        ADSRModule m;
        m.prepareToPlay(44100.0, 512);
        setFloat(m, "attack", 0.001f);
        setFloat(m, "decay", 0.001f);
        setFloat(m, "sustain", 1.0f);
        setFloat(m, "release", 0.001f); // fast baseline release

        juce::AudioBuffer<float> buf(14, 512);
        juce::MidiBuffer empty;
        buf.clear();
        for (int i = 0; i < 512; ++i)
            buf.setSample(0, i, 1.0f);
        m.processBlock(buf, empty); // reach Sustain

        buf.clear(); // gate off -> Release
        if (applyReleaseCV)
            for (int i = 0; i < 512; ++i)
                buf.setSample(13, i, 0.5f); // Release CV: sweep halfway to the 5 s ceiling
        m.processBlock(buf, empty);
        return m.getPlayheadStage();
    };

    EXPECT_NE(stageAfterRelease(false), synth::EnvelopeStage::Release)
        << "the 1 ms release should finish within one block with no CV";
    EXPECT_EQ(stageAfterRelease(true), synth::EnvelopeStage::Release)
        << "Release CV should still be mid-release after one block, once it lengthens the release";
}

// ---------------------------------------------------------------------------
// Sustain CV (always applies, even tempo-synced -- it's a level, not a stage time)
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, SustainCVRaisesSustainLevel) {
    setFloat(adsr, "attack", 0.001f);
    setFloat(adsr, "decay", 0.001f);
    setFloat(adsr, "sustain", 0.2f);
    setFloat(adsr, "release", 5.0f);

    juce::AudioBuffer<float> buf(14, 512);
    juce::MidiBuffer empty;
    // Run enough blocks for the 20 ms sustain smoother to settle once Decay lands on it. Gate and
    // CV must be re-stamped every block, not just before the first one: processBlock writes the
    // envelope's own audio output back into channel 0 (the same buffer carries the gate in and
    // the envelope out) and clears every CV channel at the end of each call, so a buffer filled
    // only once would feed the module its own stale output as "gate" from block 2 onward and see
    // the Sustain CV jack go silent after block 1.
    for (int b = 0; b < 20; ++b) {
        buf.clear();
        for (int i = 0; i < 512; ++i) {
            buf.setSample(0, i, 1.0f);
            buf.setSample(12, i, 0.5f); // Sustain CV: sweep halfway toward its own 0..1 ceiling
        }
        adsr.processBlock(buf, empty);
    }

    EXPECT_GT(buf.getSample(0, 511), 0.2f + 0.05f)
        << "Sustain CV should raise the settled level well above the unmodulated 0.2 sustain";
}

// ---------------------------------------------------------------------------
// Tempo sync: time CV is ignored for a stage while that stage is tempo-synced
// ---------------------------------------------------------------------------

TEST_F(ADSRTest, DecayCVIgnoredWhileTempoSynced) {
    setBoolParam(adsr, "tempoSync", true);
    setChoiceIndex(adsr, "attackDiv", 5); // fastest ("1/32")
    setChoiceIndex(adsr, "holdDiv", 5);
    setChoiceIndex(adsr, "decayDiv", 2); // "1/4" -- 1 beat @ 120 BPM
    setChoiceIndex(adsr, "releaseDiv", 5);
    setFloat(adsr, "sustain", 0.0f); // stays in Sustain (silent) once Decay lands, never Idle

    synth::TransportService transport;
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;
    transport.prepare(kSampleRate, kBlockSize);
    transport.setBpm(120.0);
    transport.tick(kBlockSize); // drain the setBpm command into the published snapshot
    adsr.setPlayHead(&transport);

    auto blocksToSustain = [&](float decayCV) -> int {
        adsr.prepareToPlay(kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buf(14, kBlockSize);
        buf.clear();
        for (int i = 0; i < kBlockSize; ++i)
            buf.setSample(11, i, decayCV); // Decay CV
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        adsr.processBlock(buf, noteOn);
        if (adsr.getPlayheadStage() == synth::EnvelopeStage::Sustain)
            return 1;
        juce::MidiBuffer empty;
        for (int blocks = 1; blocks < 2000; ++blocks) {
            buf.clear();
            for (int i = 0; i < kBlockSize; ++i)
                buf.setSample(11, i, decayCV);
            adsr.processBlock(buf, empty);
            if (adsr.getPlayheadStage() == synth::EnvelopeStage::Sustain)
                return blocks + 1;
        }
        return -1;
    };

    const int blocksNoCV = blocksToSustain(0.0f);
    ASSERT_GT(blocksNoCV, 0) << "never reached Sustain with no Decay CV";
    const int blocksWithCV = blocksToSustain(1.0f); // full-scale CV, would slam Decay to its 5 s ceiling if honoured
    ASSERT_GT(blocksWithCV, 0) << "never reached Sustain with Decay CV";

    EXPECT_EQ(blocksWithCV, blocksNoCV)
        << "Decay CV must be ignored while tempoSync is on -- Decay's timing comes from decayDiv + "
           "tempo alone";
}
