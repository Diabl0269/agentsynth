// Every knob's readout carries its unit in the value text ("12 ms", "-6 dB", "L 40"), and the text it prints reads
// back to the same text: a new parameter cannot ship reading as a bare number.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <string>
#include <vector>

namespace {

// A parameter whose unit is not in the shared list: the suffix its text must still end with ("" for a unit-less
// quantity, which then only has to read back to itself), and why. Keep this list to a handful.
struct UnitException {
    const char* type;
    const char* idSuffix; // matches a parameter id by its ending
    const char* suffix;
    const char* reason;
};

const std::vector<UnitException>& unitExceptions() {
    static const std::vector<UnitException> list = {
        {"Bitcrusher", "depth", " bit", "a bit count: its unit is the bit"},
        {"Sequencer", "bpm", "", "the card caption BPM is the unit and the value box is too narrow for it"},
        {"Poly Sequencer", "bpm", "", "the card caption BPM is the unit and the value box is too narrow for it"},
        {"Parametric EQ", "Q", "", "a filter's quality factor has no unit"},
    };
    return list;
}

const UnitException* exceptionFor(const juce::String& type, const juce::String& id) {
    for (const auto& e : unitExceptions())
        if (type == e.type && id.endsWith(e.idSuffix))
            return &e;
    return nullptr;
}

bool endsWithUnit(const juce::String& text) {
    for (const char* unit : {"%", " dB", " ms", " s", " Hz", " kHz", " st", " ct", " oct", " :1", ":1"})
        if (text.endsWith(unit))
            return true;
    return text.endsWith(juce::String(juce::CharPointer_UTF8("\xc2\xb0")));
}

bool isPanText(const juce::String& text) {
    return text == "C" ||
           ((text.startsWith("L ") || text.startsWith("R ")) && text.substring(2).containsOnly("0123456789"));
}

bool readsWithUnit(const juce::String& text) { return endsWithUnit(text) || isPanText(text); }

} // namespace

// Guard: every float parameter of every module prints a unit at the bottom, middle and top of its travel, and the text
// it prints reads back to itself.
TEST(ParameterUnits, EveryFloatParameterOfEveryModuleReadsWithAUnitAndRoundTrips) {
    int checked = 0;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto module = synth::AIStateMapper::createModule(type);
        ASSERT_NE(module, nullptr) << type;
        for (auto* p : module->getParameters()) {
            auto* f = dynamic_cast<juce::AudioParameterFloat*>(p);
            if (f == nullptr)
                continue;
            const std::string key = std::string(type.toRawUTF8()) + "." + f->paramID.toStdString();
            const auto* exception = exceptionFor(type, f->paramID);
            const juce::RangedAudioParameter& param = *f;
            ++checked;
            for (const float normalised : {0.0f, 0.5f, 1.0f}) {
                const auto text = param.getText(normalised, 0);
                EXPECT_TRUE(exception != nullptr ? text.endsWith(exception->suffix) : readsWithUnit(text))
                    << key << " at " << normalised << " reads \"" << text << "\"";
                EXPECT_EQ(param.getText(param.getValueForText(text), 0), text)
                    << key << " at " << normalised << ": \"" << text << "\" does not read back to itself";
            }
        }
    }
    EXPECT_GT(checked, 150) << "the guard saw the parameters it is meant to cover";
}
