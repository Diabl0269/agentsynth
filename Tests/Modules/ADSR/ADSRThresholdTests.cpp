// ADSRThresholdTests.cpp
// The gate threshold (base + CV), the shared Schmitt hysteresis, and the module's channel/port
// layout around the Threshold CV input.

#include "ADSRTestFixture.h"

TEST_F(ADSRTest, GateBelowThresholdDoesNotStartEnvelope) {
    setFloat(adsr, "gateThreshold", 0.8f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.6f);

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_NEAR(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.0f, 1e-4);
}

TEST_F(ADSRTest, GateAboveRaisedThresholdStartsEnvelope) {
    setFloat(adsr, "gateThreshold", 0.8f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.95f);

    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.001f);
}

TEST_F(ADSRTest, ThresholdCVShiftsTheGatePoint) {
    juce::AudioBuffer<float> buf(9, 512);
    buf.clear();
    for (int i = 0; i < 512; ++i) {
        buf.setSample(0, i, 0.4f);  // below the 0.5 default
        buf.setSample(8, i, -0.2f); // pulls threshold down to 0.3
    }
    juce::MidiBuffer emptyMidi;
    adsr.processBlock(buf, emptyMidi);
    EXPECT_GT(buf.getRMSLevel(0, 0, 512), 0.001f);
    EXPECT_NEAR(adsr.getEffectiveThreshold(), 0.3f, 1e-4);
}

TEST_F(ADSRTest, DefaultThresholdIsHalf) { EXPECT_NEAR(adsr.getEffectiveThreshold(), 0.5f, 1e-5f); }

TEST_F(ADSRTest, HysteresisRejectsDitherAroundThreshold) {
    juce::MidiBuffer emptyMidi;
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.6f);
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_TRUE(adsr.isOverThreshold());

    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.48f); // still inside the 0.05 gap below 0.5
    adsr.processBlock(buffer, emptyMidi);
    EXPECT_TRUE(adsr.isOverThreshold());
}

TEST_F(ADSRTest, ChannelLayoutIncludesThresholdCv) {
    // The declared channel count is 15 (Threshold, the five stage-time/level CV jacks on ch9-13
    // and Velocity on ch14) -- see ADSRCVTests.cpp for the jacks' own coverage.
    EXPECT_EQ(adsr.getTotalNumInputChannels(), 15);
    EXPECT_EQ(adsr.getTotalNumOutputChannels(), 15);
    EXPECT_EQ(adsr.getVisibleInputPortCount(), 8);
    EXPECT_EQ(adsr.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(adsr.getInputPortLabel(0), "Gate");
    EXPECT_EQ(adsr.getInputPortLabel(1), "Threshold");
}

TEST_F(ADSRTest, ThresholdChannelIsModCv) {
    auto gate = adsr.mapInputChannel(0);
    EXPECT_EQ(gate.role, PortRole::Gate);
    auto thresh = adsr.mapInputChannel(8);
    EXPECT_EQ(thresh.visibleJackIndex, 1);
    EXPECT_EQ(thresh.role, PortRole::ModCV);
}

// Regression test for FRO1427: with Threshold under the 0.05 hysteresis gap the gate never read as released, so the
// envelope stayed open after the gate dropped to 0 and the sound never stopped.
// Kinds: live-reconfigure
TEST_F(ADSRTest, GateDroppingToZeroReleasesWhateverTheThreshold) {
    juce::MidiBuffer emptyMidi;
    for (const float threshold : {0.0f, 0.001f, 0.03f}) {
        SCOPED_TRACE(threshold);
        adsr.prepareToPlay(44100.0, 512);
        setFloat(adsr, "gateThreshold", threshold);
        setFloat(adsr, "release", 0.01f);

        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, 1.0f);
        adsr.processBlock(buffer, emptyMidi);
        EXPECT_TRUE(adsr.isOverThreshold());

        float tail = 0.0f;
        for (int block = 0; block < 20; ++block) {
            buffer.clear();
            adsr.processBlock(buffer, emptyMidi);
            tail = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        }
        EXPECT_FALSE(adsr.isOverThreshold());
        EXPECT_LT(tail, 1.0e-4f) << "the envelope is still open after the gate fell";
    }
}
