// How a pan position, a decibel level, a ratio, a share of a range and a whole-number pitch step read as text, and how
// typed text reads back.

#include "Modules/ParameterText.h"

#include <gtest/gtest.h>

TEST(ParameterText, PanReadsLeftCentreRightAndParsesBack) {
    EXPECT_EQ(synth::panToText(-0.4f), "L 40");
    EXPECT_EQ(synth::panToText(0.0f), "C");
    EXPECT_EQ(synth::panToText(0.004f), "C");
    EXPECT_EQ(synth::panToText(0.25f), "R 25");
    EXPECT_EQ(synth::panToText(-1.0f), "L 100");
    EXPECT_EQ(synth::panToText(1.0f), "R 100");
    for (const char* typed : {"L 40", "l40", "L40", "-0.4", " L 40 "})
        EXPECT_NEAR(synth::panFromText(typed), -0.4f, 1e-5f) << typed;
    for (const char* typed : {"C", "c", "centre", "0", ""})
        EXPECT_NEAR(synth::panFromText(typed), 0.0f, 1e-5f) << typed;
    for (const char* typed : {"R 25", "r25", "0.25"})
        EXPECT_NEAR(synth::panFromText(typed), 0.25f, 1e-5f) << typed;
    for (const float v : {-1.0f, -0.4f, 0.0f, 0.25f, 1.0f})
        EXPECT_NEAR(synth::panFromText(synth::panToText(v)), v, 0.005f) << v;
}

TEST(ParameterText, PanParameterTypesBackThroughItsOwnText) {
    juce::AudioParameterFloat pan("pan", "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                  synth::panAttributes());
    const juce::RangedAudioParameter& param = pan;
    EXPECT_EQ(param.getText(param.convertTo0to1(-0.4f), 0), "L 40");
    for (const char* typed : {"L 40", "-0.4"})
        EXPECT_NEAR(param.convertFrom0to1(param.getValueForText(typed)), -0.4f, 1e-4f) << typed;
}

TEST(ParameterText, DecibelsKeepTheirUnitAndNeverReadNegativeZero) {
    EXPECT_EQ(synth::decibelsToText(-6.0f), "-6.0 dB");
    EXPECT_EQ(synth::decibelsToText(-0.01f), "0.0 dB");
    EXPECT_EQ(synth::decibelsToText(12.0f, 2), "12.00 dB");
    EXPECT_NEAR(synth::decibelsFromText("-6.0 dB"), -6.0f, 1e-5f);
    EXPECT_NEAR(synth::decibelsFromText("-6dB"), -6.0f, 1e-5f);
    EXPECT_NEAR(synth::decibelsFromText("3"), 3.0f, 1e-5f);
}

TEST(ParameterText, RatioAndRangePercentRoundTrip) {
    EXPECT_EQ(synth::ratioToText(4.0f), "4.0:1");
    EXPECT_NEAR(synth::ratioFromText("4:1"), 4.0f, 1e-5f);
    juce::AudioParameterFloat drive("drive", "Drive", juce::NormalisableRange<float>(1.0f, 20.0f), 1.0f,
                                    synth::rangePercentAttributes(1.0f, 20.0f));
    const juce::RangedAudioParameter& param = drive;
    EXPECT_EQ(param.getText(0.0f, 0), "0 %");
    EXPECT_EQ(param.getText(1.0f, 0), "100 %");
    EXPECT_NEAR(param.convertFrom0to1(param.getValueForText("50 %")), 10.5f, 1e-3f);
}

TEST(ParameterText, PitchStepsReadWithSemitoneAndOctaveUnits) {
    juce::AudioParameterInt coarse(juce::ParameterID("coarse", 1), "Coarse", -12, 12, 0,
                                   synth::semitoneStepAttributes());
    juce::AudioParameterInt octave(juce::ParameterID("octave", 1), "Octave", -4, 4, 0, synth::octaveStepAttributes());
    const juce::RangedAudioParameter& coarseParam = coarse;
    const juce::RangedAudioParameter& octaveParam = octave;
    EXPECT_EQ(coarseParam.getText(coarseParam.convertTo0to1(-7.0f), 0), "-7 st");
    EXPECT_EQ(octaveParam.getText(octaveParam.convertTo0to1(2.0f), 0), "2 oct");
    EXPECT_NEAR(octaveParam.convertFrom0to1(octaveParam.getValueForText("-2 oct")), -2.0f, 1e-4f);
    EXPECT_NEAR(coarseParam.convertFrom0to1(coarseParam.getValueForText("5")), 5.0f, 1e-4f);
}
