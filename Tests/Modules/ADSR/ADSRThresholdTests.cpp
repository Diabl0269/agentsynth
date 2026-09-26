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
    // FRO285 raised the declared channel count to 14 (Threshold plus the five stage-time/level
    // CV jacks on ch9-13); FRO314 raised it again to 17 (Attack/Decay/Release Curve CV on
    // ch14-16) -- see ADSRCVTests.cpp for the new jacks' own coverage.
    EXPECT_EQ(adsr.getTotalNumInputChannels(), 17);
    EXPECT_EQ(adsr.getTotalNumOutputChannels(), 17);
    EXPECT_EQ(adsr.getVisibleInputPortCount(), 10);
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
