// NameCaseSweepTests.cpp
// Bug kind name-case-drift: every parameter name and port label of every registered module reads with a capital
// first letter on each word ("Pad 1 Level", never "Pad 1 level"), because a mod menu row, a card and a command
// palette entry all print it beside its siblings. Indexed names (pads, bands, voices) are in the sweep because a
// module declares every one of them as a parameter or a port.

#include "AI/AIStateMapper/AIStateMapperInternal.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>
#include <set>

namespace {

// Words that are written lower case on purpose: units, and a letter standing for a number.
const std::set<juce::String> kLowerCaseWords = {"dB", "dBFS", "Hz", "kHz", "ms",  "s",  "st", "x",     "per", "of",
                                                "to", "a",    "in", "on",  "and", "or", "vs", "cents", "semi"};

bool isAcronym(const juce::String& word) {
    // ALL-CAPS is for a short acronym (LFO, CV, ADSR, EQ, FM, PWM, HPF, LPF24), not for shouting a whole word.
    return word.length() <= 5 && word == word.toUpperCase();
}

// Debatable names, left as they are for now: a second word in lower case on a Pluck, Mallet, Oscillator and
// Master control. Renaming them moves card goldens, scenario ids and cable targets, so it is a decision, not a fix.
const std::set<juce::String> kKnownSentenceCase = {"Velocity brightness", "Safety limiter", "Fixed rate",
                                                   "Decay tracking",      "Pick hardness",  "Pick position"};

// The first problem found in `name`, empty when it reads right.
juce::String problemWith(const juce::String& name) {
    if (kKnownSentenceCase.count(name) != 0)
        return {};
    juce::StringArray words;
    words.addTokens(name, " /-", "");
    words.removeEmptyStrings();
    for (auto word : words) {
        word = word.trimCharactersAtStart("([+-'\"").trimCharactersAtEnd(")]:.,'\"");
        if (word.isEmpty() || kLowerCaseWords.count(word) != 0)
            continue;
        const auto first = word[0];
        if (juce::CharacterFunctions::isDigit(first) || !juce::CharacterFunctions::isLetter(first))
            continue;
        if (!juce::CharacterFunctions::isUpperCase(first))
            return "word \"" + word + "\" starts in lower case";
        if (word.length() > 1 && word == word.toUpperCase() && !isAcronym(word))
            return "word \"" + word + "\" is all capitals";
    }
    return {};
}

} // namespace

TEST(NameCaseSweep, TheCheckItselfCatchesALowerCaseWord) {
    EXPECT_FALSE(problemWith("Pad 1 level").isEmpty());
    EXPECT_TRUE(problemWith("Pad 1 Level").isEmpty());
    EXPECT_TRUE(problemWith("Cutoff (kHz)").isEmpty());
    EXPECT_TRUE(problemWith("LFO 2 Rate").isEmpty());
    EXPECT_FALSE(problemWith("OVERDRIVE Amount").isEmpty());
}

TEST(NameCaseSweep, EveryParameterNameAndPortLabelOfEveryModuleIsCapitalised) {
    int checked = 0;
    for (const auto& [type, make] : synth::detail::moduleFactory()) {
        auto processor = make();
        ASSERT_NE(processor, nullptr) << type;
        const auto check = [&](const juce::String& kind, const juce::String& name) {
            ++checked;
            const auto problem = problemWith(name);
            EXPECT_TRUE(problem.isEmpty()) << type << " " << kind << " \"" << name << "\": " << problem;
        };
        for (auto* param : processor->getParameters())
            check("parameter", param->getName(64));
        if (auto* module = dynamic_cast<ModuleBase*>(processor.get())) {
            for (const auto& target : module->getModulationTargets())
                check("modulation target", target.name);
            for (int i = 0; i < module->getVisibleInputPortCount(); ++i)
                check("input port", module->getInputPortLabel(i));
            for (int i = 0; i < module->getVisibleOutputPortCount(); ++i)
                check("output port", module->getOutputPortLabel(i));
        }
    }
    EXPECT_GT(checked, 500) << "the sweep reached the modules";
}
