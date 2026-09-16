// FRO112: readout formatting for ADSR's float params. juce::SliderParameterAttachment installs
// param.getText()/getValueForText() as the slider's textFromValueFunction/valueFromTextFunction,
// so this is exactly what the envelope card's knobs (and any host's generic automation UI) show
// and parse -- see the note on ADSRModule.h's adsrTimeAttributes()/adsrSustainAttributes().

#include "ADSRTestFixture.h"

namespace {
// AudioParameterFloat re-declares getText()/getValueForText() as PRIVATE overrides (JUCE steers
// callers to the base AudioProcessorParameter interface, where they're public) -- SliderParameter
// Attachment reaches them the same way, through the RangedAudioParameter base it's templated on.
juce::String readoutText(juce::AudioParameterFloat* p, float rawValue) {
    return static_cast<juce::AudioProcessorParameter*>(p)->getText(p->convertTo0to1(rawValue), 32);
}
float readoutValue(juce::AudioParameterFloat* p, const juce::String& text) {
    return p->convertFrom0to1(static_cast<juce::AudioProcessorParameter*>(p)->getValueForText(text));
}
} // namespace

TEST_F(ADSRTest, TimeParamsFormatAsMillisecondsBelowOneSecond) {
    auto* attack = floatParam(adsr, "attack");
    ASSERT_NE(attack, nullptr);
    EXPECT_EQ(readoutText(attack, 0.001f), "1.00 ms"); // the ADSR default
    EXPECT_EQ(readoutText(attack, 0.015f), "15.0 ms");
    EXPECT_EQ(readoutText(attack, 0.0f), "0.00 ms");
}

TEST_F(ADSRTest, TimeParamsFormatAsSecondsAtOrAboveOneSecond) {
    auto* decay = floatParam(adsr, "decay");
    ASSERT_NE(decay, nullptr);
    EXPECT_EQ(readoutText(decay, 1.0f), "1.00 s"); // the ADSR default
    EXPECT_EQ(readoutText(decay, 4.999f), "5.00 s");
}

TEST_F(ADSRTest, TimeParamTextRoundTripsThroughValueForText) {
    for (const char* id : {"attack", "hold", "decay", "release"}) {
        auto* p = floatParam(adsr, id);
        ASSERT_NE(p, nullptr) << id;

        EXPECT_NEAR(readoutValue(p, "250 ms"), 0.25f, 1e-4f) << id;
        EXPECT_NEAR(readoutValue(p, "2.0 s"), 2.0f, 1e-4f) << id;
        // A bare number (no unit) is read as seconds, matching the parameter's own unit.
        EXPECT_NEAR(readoutValue(p, "0.5"), 0.5f, 1e-4f) << id;
    }
}

TEST_F(ADSRTest, SustainFormatsAsDecibelsWithMinusInfAtZero) {
    auto* sustain = floatParam(adsr, "sustain");
    ASSERT_NE(sustain, nullptr);
    EXPECT_EQ(readoutText(sustain, 1.0f), "0.0 dB") << "unity is 0.0 dB";
    EXPECT_EQ(readoutText(sustain, 0.0f), "-inf dB") << "zero is -inf dB, never log10(0)";
    EXPECT_LT(readoutText(sustain, 0.5f).getFloatValue(), 0.0f) << "below unity must read as a negative dB value";
}

TEST_F(ADSRTest, SustainDbTextRoundTripsThroughValueForText) {
    auto* sustain = floatParam(adsr, "sustain");
    ASSERT_NE(sustain, nullptr);

    EXPECT_NEAR(readoutValue(sustain, "-inf dB"), 0.0f, 1e-6f);
    EXPECT_NEAR(readoutValue(sustain, "0.0 dB"), 1.0f, 1e-4f);
    // -6 dB ~= 0.501 linear
    EXPECT_NEAR(readoutValue(sustain, "-6.0 dB"), 0.5012f, 5e-3f);
}

// The three bend params never appear as their own knob (FRO112 moved them onto the envelope
// graph's bend handles), but a host's generic automation UI still reads their readout.
TEST_F(ADSRTest, CurveParamsFormatAsPlainTwoDecimalValues) {
    for (const char* id : {"attackCurve", "decayCurve", "releaseCurve"}) {
        auto* p = floatParam(adsr, id);
        ASSERT_NE(p, nullptr) << id;
        EXPECT_EQ(readoutText(p, 0.65f), "0.65") << id;
    }
}

// The readout is display-only -- every param's own NormalisableRange must stay exactly what it
// was (linear, unskewed) for patch compatibility and the AIStateMapper rescale heuristic.
TEST_F(ADSRTest, ReadoutAttributesDoNotChangeTheParameterRange) {
    struct Expected {
        const char* id;
        float start, end;
    };
    for (const auto& e :
         {Expected{"attack", 0.0f, 5.0f}, Expected{"hold", 0.0f, 5.0f}, Expected{"decay", 0.0f, 5.0f},
          Expected{"release", 0.0f, 5.0f}, Expected{"sustain", 0.0f, 1.0f}, Expected{"attackCurve", -1.0f, 1.0f},
          Expected{"decayCurve", -1.0f, 1.0f}, Expected{"releaseCurve", -1.0f, 1.0f}}) {
        auto* p = floatParam(adsr, e.id);
        ASSERT_NE(p, nullptr) << e.id;
        const auto& range = p->getNormalisableRange();
        EXPECT_FLOAT_EQ(range.start, e.start) << e.id;
        EXPECT_FLOAT_EQ(range.end, e.end) << e.id;
        EXPECT_FLOAT_EQ(range.skew, 1.0f) << e.id << " parameter range must stay unskewed";
    }
}
