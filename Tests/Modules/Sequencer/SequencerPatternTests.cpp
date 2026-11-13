#include "Modules/Sequencer/SequencerPattern.h"
#include "Modules/Sequencer/SequencerPatternLegacy.h"
#include "Modules/Sequencer/SequencerScaleSnap.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using namespace synth;

namespace {

juce::var parse(const char* json) { return juce::JSON::parse(juce::String(json)); }

juce::NamedValueSet paramsOf(const char* json) { return parse(json).getDynamicObject()->getProperties(); }

} // namespace

TEST(SequencerPatternTest, DefaultPatternsMatchTheOldDefaults) {
    const auto mono = SequencerPattern::makeDefault(false);
    const int pitches[8] = {53, 65, 54, 61, 53, 57, 54, 60};
    for (int i = 0; i < kMaxSequencerSteps; ++i) {
        EXPECT_EQ(mono.steps[(size_t)i].pitch, pitches[i % 8]) << i; // steps 9.. repeat 1..8
        EXPECT_EQ(mono.steps[(size_t)i].gate, 0.5f);
        EXPECT_EQ(mono.steps[(size_t)i].filter, 0.5f);
    }
    const auto poly = SequencerPattern::makeDefault(true);
    const int roots[8] = {48, 52, 55, 60, 48, 55, 52, 60};
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(poly.steps[(size_t)i].pitch, roots[i % 8]);
        EXPECT_EQ(poly.steps[(size_t)i].chord, SequencerChord::Unison);
    }
}

TEST(SequencerPatternTest, DefaultPatternSavesNoSteps) {
    auto pattern = SequencerPattern::makeDefault(false);
    pattern.seed = 1234;
    const auto json = pattern.toVar(false);
    ASSERT_NE(json.getDynamicObject(), nullptr);
    EXPECT_EQ((int)json["version"], 1);
    EXPECT_EQ((int)json["seed"], 1234);
    EXPECT_EQ(json["steps"].size(), 0);
    EXPECT_FALSE(json.getDynamicObject()->hasProperty("customScale"));
}

TEST(SequencerPatternTest, EditedPatternRoundTripsAndTrimsTrailingDefaults) {
    auto pattern = SequencerPattern::makeDefault(false);
    pattern.seed = 99;
    auto& step = pattern.steps[2];
    step.pitch = 70;
    step.on = false;
    step.gate = 0.25f;
    step.velocity = 90;
    step.chance = 40;
    step.repeat = 3;
    step.slide = true;
    step.nudge = -0.125f;
    step.condHit = 2;
    step.condOf = 4;
    step.filter = 0.75f;
    pattern.customMask = 0x555;
    pattern.customName = "Mine";

    const auto json = pattern.toVar(false);
    EXPECT_EQ(json["steps"].size(), 3);
    const auto back = SequencerPattern::fromVar(juce::JSON::parse(juce::JSON::toString(json)), false, 7);
    EXPECT_EQ(back.steps, pattern.steps);
    EXPECT_EQ(back.seed, 99u);
    EXPECT_EQ(back.customMask, 0x555);
    EXPECT_EQ(back.customName, "Mine");
}

TEST(SequencerPatternTest, PolyChordsRoundTripByName) {
    auto pattern = SequencerPattern::makeDefault(true);
    pattern.steps[1].chord = SequencerChord::ScaleSeventh;
    pattern.steps[3].chord = SequencerChord::Fifths;
    const auto json = juce::JSON::toString(pattern.toVar(true));
    EXPECT_TRUE(json.contains("Scale Seventh"));
    EXPECT_TRUE(json.contains("5ths"));
    const auto back = SequencerPattern::fromVar(juce::JSON::parse(json), true, 0);
    EXPECT_EQ(back.steps[1].chord, SequencerChord::ScaleSeventh);
    EXPECT_EQ(back.steps[3].chord, SequencerChord::Fifths);
}

TEST(SequencerPatternTest, EveryChordIdRoundTrips) {
    for (int i = 0; i < kSequencerChordCount; ++i) {
        const auto id = chordId((SequencerChord)i);
        const auto back = chordFromId(id);
        ASSERT_TRUE(back.has_value()) << id;
        EXPECT_EQ((int)*back, i);
    }
    EXPECT_FALSE(chordFromId("Mystic").has_value());
    EXPECT_FALSE(chordFromId("major").has_value());
}

TEST(SequencerPatternTest, HostileValuesAreClamped) {
    const auto v = parse(R"({"seed": 99999999999, "steps": [
        {"pitch": 500, "gate": 7, "velocity": 0, "chance": 1000, "repeat": 99, "nudge": -9, "filter": 5,
         "condition": "9/3", "chord": "Nonsense"},
        {"pitch": -5, "gate": -1, "velocity": 9999, "chance": -4, "repeat": 0, "nudge": 9, "filter": -1,
         "condition": "0/0"}]})");
    const auto p = SequencerPattern::fromVar(v, true, 1);
    EXPECT_EQ(p.seed, 4294967295u);
    EXPECT_EQ(p.steps[0].pitch, 127);
    EXPECT_FLOAT_EQ(p.steps[0].gate, 1.0f);
    EXPECT_EQ(p.steps[0].velocity, 1);
    EXPECT_EQ(p.steps[0].chance, 100);
    EXPECT_EQ(p.steps[0].repeat, 8);
    EXPECT_FLOAT_EQ(p.steps[0].nudge, -0.5f);
    EXPECT_FLOAT_EQ(p.steps[0].filter, 1.0f);
    EXPECT_EQ(p.steps[0].condOf, 3);
    EXPECT_EQ(p.steps[0].condHit, 3);
    EXPECT_EQ(p.steps[0].chord, SequencerChord::Unison); // unknown id ignored
    EXPECT_EQ(p.steps[1].pitch, 0);
    EXPECT_FLOAT_EQ(p.steps[1].gate, 0.05f);
    EXPECT_EQ(p.steps[1].velocity, 127);
    EXPECT_EQ(p.steps[1].chance, 0);
    EXPECT_EQ(p.steps[1].repeat, 1);
    EXPECT_FLOAT_EQ(p.steps[1].nudge, 0.5f);
    EXPECT_FLOAT_EQ(p.steps[1].filter, 0.0f);
    EXPECT_EQ(p.steps[1].condOf, 1); // "0/0" is not a condition: the step keeps the default 1/1
}

TEST(SequencerPatternTest, WrongTypesAndUnknownKeysAreIgnored) {
    const auto v = parse(R"({"version": "x", "seed": "abc", "bogus": 1, "customScale": 5, "steps": [
        7, "text", null, {"pitch": "high", "gate": [1], "on": "yes", "chord": 5, "unknown": {}}, {"pitch": "61"}]})");
    const auto p = SequencerPattern::fromVar(v, false, 42);
    const auto defaults = SequencerPattern::makeDefault(false);
    EXPECT_EQ(p.seed, 42u);
    EXPECT_EQ(p.steps[0], defaults.steps[0]); // non-objects keep the factory step
    EXPECT_EQ(p.steps[1], defaults.steps[1]);
    EXPECT_EQ(p.steps[3], SequencerStep{}); // an object starts from the plain defaults
    EXPECT_EQ(p.steps[4].pitch, 61);        // a numeric string is a number
    EXPECT_EQ(p.customMask, 0xFFF);
}

TEST(SequencerPatternTest, NonFiniteNumbersAreIgnored) {
    auto* step = new juce::DynamicObject();
    step->setProperty("pitch", std::numeric_limits<double>::quiet_NaN());
    step->setProperty("gate", std::numeric_limits<double>::infinity());
    step->setProperty("nudge", -std::numeric_limits<double>::infinity());
    juce::Array<juce::var> steps;
    steps.add(juce::var(step));
    auto* root = new juce::DynamicObject();
    root->setProperty("steps", juce::var(steps));
    const auto p = SequencerPattern::fromVar(juce::var(root), false, 0);
    EXPECT_EQ(p.steps[0].pitch, 60);
    EXPECT_FLOAT_EQ(p.steps[0].gate, 0.5f);
    EXPECT_FLOAT_EQ(p.steps[0].nudge, 0.0f);
}

TEST(SequencerPatternTest, HugeArraysAreCappedAndMissingStepsKeepTheFactoryPattern) {
    juce::Array<juce::var> steps;
    for (int i = 0; i < 100000; ++i) {
        auto* step = new juce::DynamicObject();
        step->setProperty("pitch", 10);
        steps.add(juce::var(step));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("steps", juce::var(steps));
    const auto p = SequencerPattern::fromVar(juce::var(root), false, 0);
    for (const auto& step : p.steps)
        EXPECT_EQ(step.pitch, 10);

    const auto few = SequencerPattern::fromVar(parse(R"({"steps": [{"pitch": 1}]})"), false, 0);
    EXPECT_EQ(few.steps[0].pitch, 1);
    EXPECT_EQ(few.steps[1].pitch, 65);
    EXPECT_EQ(SequencerPattern::fromVar(juce::var(), false, 3).steps[2].pitch, 54);
}

TEST(SequencerPatternTest, CustomScaleIsClamped) {
    const auto p = SequencerPattern::fromVar(parse(R"({"customScale": {"name": "Odd", "mask": 99999}})"), false, 0);
    EXPECT_EQ(p.customMask, 0xFFF);
    EXPECT_EQ(p.customName, "Odd");
}

TEST(SequencerLegacyTest, MonoKeysAreAdoptedByNameAndRestStepsTurnOff) {
    auto pattern = SequencerPattern::makeDefault(false);
    const auto params = paramsOf(R"({"Pitch 1": 48, "Pitch 2": 0, "Pitch 3": 500, "Gate 1": 0.8, "Gate 2": 9,
        "F.Env 4": 0.9, "Pitch 8": "62"})");
    EXPECT_TRUE(adoptLegacyStepParams(pattern, params, false));
    EXPECT_EQ(pattern.steps[0].pitch, 48);
    EXPECT_FLOAT_EQ(pattern.steps[0].gate, 0.8f);
    EXPECT_FALSE(pattern.steps[1].on);
    EXPECT_EQ(pattern.steps[1].pitch, 65); // a rest keeps the written pitch
    EXPECT_FLOAT_EQ(pattern.steps[1].gate, 1.0f);
    EXPECT_EQ(pattern.steps[2].pitch, 127);
    EXPECT_FLOAT_EQ(pattern.steps[3].filter, 0.9f);
    EXPECT_EQ(pattern.steps[7].pitch, 62);
    EXPECT_EQ(pattern.steps[4], SequencerPattern::makeDefault(false).steps[4]);
}

TEST(SequencerLegacyTest, PolyKeysAcceptChordNamesAndIndexes) {
    auto pattern = SequencerPattern::makeDefault(true);
    const auto params = paramsOf(R"({"Step 1 Root": 50, "Step 1 Chord": "Minor", "Step 2 Chord": 3,
        "Step 3 Chord": "maj7", "Step 4 Chord": "Banana", "Step 5 Chord": "7", "Step 2 Root": 400, "Gate 6": 0.2})");
    EXPECT_TRUE(adoptLegacyStepParams(pattern, params, true));
    EXPECT_EQ(pattern.steps[0].pitch, 50);
    EXPECT_EQ(pattern.steps[0].chord, SequencerChord::Minor);
    EXPECT_EQ(pattern.steps[1].chord, SequencerChord::Maj7);
    EXPECT_EQ(pattern.steps[1].pitch, 127);
    EXPECT_EQ(pattern.steps[2].chord, SequencerChord::Maj7);
    EXPECT_EQ(pattern.steps[3].chord, SequencerChord::Unison);
    EXPECT_EQ(pattern.steps[4].chord, SequencerChord::Random);
    EXPECT_FLOAT_EQ(pattern.steps[5].gate, 0.2f);
}

TEST(SequencerLegacyTest, NothingToAdoptChangesNothing) {
    auto pattern = SequencerPattern::makeDefault(false);
    EXPECT_FALSE(adoptLegacyStepParams(pattern, paramsOf(R"({"run": true, "bpm": 100, "gate": 1.5})"), false));
    EXPECT_FALSE(adoptLegacyStepParams(pattern, paramsOf(R"({"Step 1 Root": 50})"), false));
    EXPECT_EQ(pattern.steps, SequencerPattern::makeDefault(false).steps);
}

TEST(SequencerScaleTest, ChoicesAreNoScaleThePresetsThenCustom) {
    const auto& choices = sequencerScaleChoices();
    ASSERT_EQ(choices.size(), (int)builtInScalePresets().size() + 2);
    EXPECT_EQ(choices[0], "No scale");
    EXPECT_EQ(choices[1], "Major");
    EXPECT_EQ(choices[choices.size() - 1], "Custom");
    EXPECT_EQ(sequencerCustomScaleIndex(), choices.size() - 1);
}

TEST(SequencerScaleTest, SnapTableFollowsRootScaleAndMask) {
    SequencerScaleSnap snap;
    snap.update(0, 0, 0xFFF);
    EXPECT_EQ(snap.snap(64), 64);
    snap.update(0, 2, 0xFFF); // Natural Minor
    EXPECT_EQ(snap.snap(64), 63);
    snap.update(2, 1, 0xFFF);         // D Major
    EXPECT_EQ(snap.snap(60), 59 + 0); // C is not in D major; C# (61) and B (59) tie -> lower
    snap.update(0, sequencerCustomScaleIndex(), 0b101);
    EXPECT_EQ(snap.snap(61), 60);
    EXPECT_EQ(snap.snap(-5), 0);
    EXPECT_EQ(snap.snap(500), snap.snap(127));
    EXPECT_EQ(sequencerPlayedPitch(64, 0, 0, 2, 0xFFF), 63);
}
