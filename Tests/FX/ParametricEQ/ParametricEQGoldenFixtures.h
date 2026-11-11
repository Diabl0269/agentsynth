// ParametricEQGoldenFixtures.h
// The four-band EQ's recorded surfaces (Tests/fixtures/parametric-eq/) and how to compare them:
// the "old patch" edits every golden is captured with, the node / catalog / schema producers, and a
// comparison that is exact for structure and text but allows float noise in printed numbers, so a
// golden written on one platform still holds on another.

#pragma once

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/ParametricEQModule.h"
#include "TestPaths.h"
#include <cstdlib>
#include <gtest/gtest.h>

namespace eqgolden {

inline bool writeRequested() {
    const char* env = std::getenv("PEQ_WRITE_GOLDEN");
    return env != nullptr && juce::String(env).trim() == "1";
}

inline juce::File goldenFile(const juce::String& name) {
    return juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/parametric-eq").getChildFile(name);
}

/** One edited parameter of the "old patch": every shown band moved off its default (band 3 edited
 *  but left off), plus the output trim, mix and pan. */
struct LegacyEdit {
    const char* id;
    float value;
};

inline const std::vector<LegacyEdit>& legacyEdits() {
    static const std::vector<LegacyEdit> edits = {
        {"band1On", 1.0f},     {"band1Freq", 120.0f},  {"band1Gain", 4.5f},  {"band1Q", 1.2f},
        {"band2On", 1.0f},     {"band2Freq", 650.0f},  {"band2Gain", -6.0f}, {"band2Q", 2.5f},
        {"band3On", 0.0f},     {"band3Freq", 2500.0f}, {"band3Gain", 3.0f},  {"band3Q", 0.9f},
        {"band4On", 1.0f},     {"band4Freq", 9000.0f}, {"band4Gain", -3.5f}, {"band4Q", 0.5f},
        {"outputGain", -2.0f}, {"mix", 0.8f},          {"pan", 0.25f},
    };
    return edits;
}

inline void applyLegacyEdits(juce::AudioProcessor& module) {
    for (const auto& edit : legacyEdits())
        for (auto* p : module.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
                ranged != nullptr && ranged->paramID == edit.id)
                ranged->setValueNotifyingHost(ranged->convertTo0to1(edit.value));
}

/** nodeToJSON of an EQ added to `graph` with a fixed uuid and position, edited when `edited`. */
inline juce::var eqNodeJson(juce::AudioProcessorGraph& graph, bool edited) {
    auto processor = synth::AIStateMapper::createModule("Parametric EQ");
    if (processor == nullptr)
        return {};
    if (edited)
        applyLegacyEdits(*processor);
    auto node = graph.addNode(std::move(processor));
    node->properties.set("uuid", "00000000-0000-4000-8000-0000000000e0");
    node->properties.set("x", 120);
    node->properties.set("y", 80);
    return synth::AIStateMapper::nodeToJSON(*node);
}

inline juce::var catalogEntry() {
    const auto catalog = juce::JSON::parse(synth::AIStateMapper::getModuleCatalogJson());
    return catalog.getProperty("modules", {}).getProperty("Parametric EQ", {});
}

/** The "#### Parametric EQ" table of the AI's module schema, up to the next module's heading. */
inline juce::String schemaSection() {
    const auto schema = synth::AIStateMapper::getModuleSchema();
    const int start = schema.indexOf("#### Parametric EQ\n");
    if (start < 0)
        return {};
    const int end = schema.indexOf(start + 1, "#### ");
    return end < 0 ? schema.substring(start) : schema.substring(start, end);
}

/** Splits text into number and non-number runs so two renderings can be compared with a tolerance
 *  on the numbers only. */
inline juce::StringArray tokens(const juce::String& text) {
    juce::StringArray out;
    juce::String current;
    bool inNumber = false;
    auto flush = [&] {
        if (current.isNotEmpty())
            out.add(current);
        current.clear();
    };
    for (auto c : text) {
        const bool numberChar = juce::CharacterFunctions::isDigit(c) || c == '.' || c == 'e' || c == '-' || c == '+';
        const bool startsNumber = juce::CharacterFunctions::isDigit(c) || c == '-' || c == '.';
        if (inNumber && !numberChar) {
            flush();
            inNumber = false;
        } else if (!inNumber && startsNumber) {
            flush();
            inNumber = true;
        }
        current << juce::String::charToString(c);
    }
    flush();
    return out;
}

inline bool looksNumeric(const juce::String& token) {
    return token.containsAnyOf("0123456789") && token.containsOnly("0123456789.eE+-");
}

/** Equal text, except that numbers may differ by float noise (1e-5 relative). */
inline bool sameWithinFloatNoise(const juce::String& a, const juce::String& b) {
    const auto ta = tokens(a);
    const auto tb = tokens(b);
    if (ta.size() != tb.size())
        return false;
    for (int i = 0; i < ta.size(); ++i) {
        if (ta[i] == tb[i])
            continue;
        if (!looksNumeric(ta[i]) || !looksNumeric(tb[i]))
            return false;
        const double x = ta[i].getDoubleValue();
        const double y = tb[i].getDoubleValue();
        if (std::abs(x - y) > 1.0e-5 * std::max(1.0, std::max(std::abs(x), std::abs(y))))
            return false;
    }
    return true;
}

inline void expectMatchesGolden(const juce::String& name, const juce::String& actual) {
    const auto golden = goldenFile(name);
    if (writeRequested()) {
        golden.getParentDirectory().createDirectory();
        ASSERT_TRUE(golden.replaceWithText(actual, false, false, "\n"));
    }
    ASSERT_TRUE(golden.existsAsFile()) << "missing golden " << name.toStdString();
    const auto expected = golden.loadFileAsString();
    EXPECT_TRUE(sameWithinFloatNoise(expected, actual))
        << name.toStdString() << " moved.\nexpected: " << expected.toStdString()
        << "\nactual:   " << actual.toStdString();
}

} // namespace eqgolden
