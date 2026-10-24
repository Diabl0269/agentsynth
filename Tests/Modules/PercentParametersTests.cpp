// Plain 0..1 amounts (Blend, Width, Mix, Level, Depth, ...) are stored, automated and patched as 0..1 and shown as
// 0-100 %: the helper that formats and parses them, and a guard that every module's 0..1 amount uses it.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "Modules/ParameterText.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace {

// 0..1 floats that are a position in something, or already read in dB, not a plain amount: they keep their unit.
bool keepsItsOwnUnit(const juce::String& id) {
    return id == "position" || id == "start" || id == "end" || id == "morphX" || id == "morphY" || id == "sustain";
}

} // namespace

TEST(PercentParameters, FractionTextRoundTrips) {
    juce::AudioParameterFloat amount("amount", "Amount", juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f,
                                     synth::fractionPercentAttributes());
    const juce::RangedAudioParameter& param = amount; // the text functions are public on the base
    EXPECT_EQ(param.getText(param.convertTo0to1(0.25f), 0), "25 %");
    EXPECT_EQ(param.getText(param.convertTo0to1(0.82f), 0), "82 %");
    EXPECT_EQ(param.getText(0.0f, 0), "0 %");
    EXPECT_EQ(param.getText(1.0f, 0), "100 %");
    EXPECT_EQ(param.getCurrentValueAsText(), "25 %");

    for (const char* typed : {"25", "25%", "25 %", "  25 % "})
        EXPECT_NEAR(param.convertFrom0to1(param.getValueForText(typed)), 0.25f, 1e-5f) << typed;
    for (const float v : {0.0f, 0.01f, 0.5f, 0.73f, 1.0f})
        EXPECT_NEAR(synth::fractionFromText(synth::fractionToText(v)), v, 0.005f) << v;
    EXPECT_EQ(amount.label, "%");
}

// Sibling guard: a new module's 0..1 amount cannot go back to reading "0.25".
TEST(PercentParameters, EveryZeroToOneAmountOfEveryModuleReadsAsPercent) {
    int checked = 0;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto module = synth::AIStateMapper::createModule(type);
        ASSERT_NE(module, nullptr) << type;
        for (auto* p : module->getParameters()) {
            auto* f = dynamic_cast<juce::AudioParameterFloat*>(p);
            if (f == nullptr || f->range.start != 0.0f || f->range.end != 1.0f || keepsItsOwnUnit(f->paramID))
                continue;
            const juce::RangedAudioParameter& param = *f;
            ++checked;
            EXPECT_TRUE(param.getText(0.5f, 0).contains("%"))
                << type << "." << f->paramID << " reads \"" << param.getText(0.5f, 0) << "\"";
            EXPECT_NEAR(param.convertFrom0to1(param.getValueForText("25")), 0.25f, 1e-4f) << type << "." << f->paramID;
        }
    }
    EXPECT_GT(checked, 30) << "the guard saw the amounts it is meant to cover";
}
