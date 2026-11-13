// DrumKitMigrationTests.cpp
// A project saved before the Drum Kit had pads opens through the real project load as 8 pads carrying its group
// values, and renders the same samples as the old one-voice-per-drum kit (LegacyDrumKitRender.h) for a fixed beat.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "LegacyDrumKitRender.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kLength = 140000;

// Every group away from its default, so a pad that missed its group's value changes the sound.
const legacy_drums::GroupValues& savedValues() {
    static const legacy_drums::GroupValues values = {
        {"kickLevel", 0.7f},    {"kickTune", 2.0f},     {"kickDecay", 0.5f},   {"snareLevel", 0.6f},
        {"snareTune", -3.0f},   {"snareDecay", 0.3f},   {"clapLevel", 0.5f},   {"clapTune", 1.0f},
        {"clapDecay", 0.3f},    {"hatLevel", 0.4f},     {"hatTune", 1.5f},     {"closedDecay", 0.08f},
        {"openDecay", 0.6f},    {"tomLevel", 0.6f},     {"tomTune", -2.0f},    {"tomDecay", 0.4f},
        {"cymbalLevel", 0.45f}, {"cymbalTune", 0.5f},   {"cymbalDecay", 0.5f}, {"cowbellLevel", 0.7f},
        {"cowbellTune", -1.0f}, {"cowbellDecay", 0.3f}, {"level", 0.8f}};
    return values;
}

using Beat = std::vector<legacy_drums::NoteHit>;

// A note-on carries its velocity as a 7-bit byte, so the old kit is handed the velocity the module will read.
Beat asMidiVelocities(Beat hits) {
    for (auto& hit : hits)
        hit.velocity = juce::MidiMessage::noteOn(10, hit.note, hit.velocity).getFloatVelocity();
    return hits;
}

// Every drum the old kit had, at mixed velocities, with an open hat choked by a pedal hat, a retriggered kick and a
// note no drum is on. Of two notes that shared one old voice (35/36, 38/40, 42/44, 49/57, 51/59) it plays only the
// first: the old voice kept its filters' state from one hit to the next, so the second note started from where the
// first left off, while each pad has a fresh voice of its own. partnerBeat() plays the others.
const Beat& beat() {
    static const Beat hits = asMidiVelocities(
        {{36, 1.0f, 0},      {38, 0.8f, 5000},  {42, 0.7f, 8000},   {46, 0.9f, 12000},  {39, 0.6f, 15000},
         {45, 0.9f, 22000},  {41, 0.7f, 25000}, {49, 1.0f, 26000},  {51, 0.6f, 30000},  {56, 0.8f, 33000},
         {37, 0.9f, 36000},  {43, 0.5f, 38000}, {36, 0.4f, 40000},  {47, 0.6f, 42000},  {60, 1.0f, 45000},
         {48, 0.8f, 47000},  {50, 0.9f, 50000}, {42, 1.0f, 100000}, {46, 0.6f, 100000}, {36, 1.0f, 120000},
         {38, 1.0f, 120000}, {42, 1.0f, 120000}});
    return hits;
}

// The second note of each shared voice, each the first hit on its old voice, and the pedal hat choking an open hat.
const Beat& partnerBeat() {
    static const Beat hits = asMidiVelocities(
        {{35, 1.0f, 0}, {40, 0.7f, 10000}, {46, 0.9f, 20000}, {44, 0.5f, 30000}, {57, 0.8f, 40000}, {59, 0.7f, 70000}});
    return hits;
}

std::vector<float> renderModule(DrumKitModule& kit, const Beat& hits = beat()) {
    kit.prepareToPlay(kRate, kBlock);
    std::vector<float> out((size_t)kLength, 0.0f);
    for (int start = 0; start < kLength; start += kBlock) {
        const int n = std::min(kBlock, kLength - start);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& hit : hits)
            if (hit.atSample >= start && hit.atSample < start + n)
                midi.addEvent(juce::MidiMessage::noteOn(10, hit.note, hit.velocity), hit.atSample - start);
        kit.processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + start);
    }
    return out;
}

float maxDifference(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    return worst;
}

// Where two renders first part, so a failure names the hit that went wrong.
juce::String firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        if (std::fabs(a[i] - b[i]) >= 1.0e-4f)
            return "first at sample " + juce::String((int)i) + ": " + juce::String(a[i]) + " vs " + juce::String(b[i]);
    return "none";
}

DrumKitModule* findKit(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* kit = dynamic_cast<DrumKitModule*>(node->getProcessor()))
            return kit;
    return nullptr;
}

float valueOf(DrumKitModule& kit, const juce::String& id) {
    auto* p = static_cast<juce::RangedAudioParameter*>(findParameterByID(&kit, id));
    return p->convertFrom0to1(p->getValue());
}

// Saves a project holding one Drum Kit at savedValues(), then rewrites project.json the way a save from before pads
// looked: no pad parameters and no pad state.
juce::File saveProjectFromBeforePads(const juce::File& root) {
    juce::AudioProcessorGraph graph;
    auto kit = std::make_unique<DrumKitModule>();
    for (const auto& [id, value] : savedValues()) {
        auto* p = static_cast<juce::RangedAudioParameter*>(findParameterByID(kit.get(), id));
        p->setValueNotifyingHost(p->convertTo0to1(value));
    }
    graph.addNode(std::move(kit));
    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
    const auto dir = root.getChildFile(juce::String("old") + synth::ProjectBundle::kBundleExtension);
    EXPECT_TRUE(synth::ProjectBundle::save(dir, graph, timeline, patch, macros, remote).ok);

    const auto file = dir.getChildFile(synth::ProjectBundle::kProjectFileName);
    auto json = juce::JSON::parse(file);
    for (auto& node : *json.getProperty("nodes", {}).getArray()) {
        if (node.getProperty("type", {}).toString() != "Drum Kit")
            continue;
        auto* params = node.getProperty("params", {}).getDynamicObject();
        for (int i = params->getProperties().size(); --i >= 0;)
            if (DrumKitModule::isPadParamId(params->getProperties().getName(i).toString()))
                params->removeProperty(params->getProperties().getName(i));
        node.getDynamicObject()->removeProperty("state");
    }
    EXPECT_TRUE(file.replaceWithText(juce::JSON::toString(json)));
    return dir;
}

struct Loaded {
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
};

class DrumKitMigration : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("DrumKitMigration-" + juce::String(juce::Time::currentTimeMillis()) + "-" +
                                 juce::String(juce::Random::getSystemRandom().nextInt()));
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }
    juce::File root;
};

} // namespace

TEST_F(DrumKitMigration, AProjectFromBeforePadsOpensWithEightPadsCarryingItsGroupValues) {
    const auto dir = saveProjectFromBeforePads(root);
    Loaded loaded;
    ASSERT_TRUE(
        synth::ProjectBundle::load(dir, loaded.graph, loaded.timeline, loaded.patch, loaded.macros, loaded.remote).ok);
    auto* kit = findKit(loaded.graph);
    ASSERT_NE(kit, nullptr);
    EXPECT_EQ(kit->padCount(), 8);
    EXPECT_EQ(kit->pad(0).name, "Kick");
    EXPECT_EQ(kit->pad(7).note, 56);

    const auto expect = [&](int pad, const char* field, const char* group) {
        EXPECT_NEAR(valueOf(*kit, "pad" + juce::String(pad) + field), savedValues().at(group), 1.0e-4f)
            << "pad " << pad << " " << field << " from " << group;
    };
    expect(1, "Level", "kickLevel");
    expect(17, "Tune", "kickTune");
    expect(2, "Decay", "snareDecay");
    expect(9, "Level", "snareLevel"); // the rim is in the snare group
    expect(4, "Decay", "closedDecay");
    expect(10, "Decay", "closedDecay"); // the pedal hat takes the closed decay
    expect(5, "Decay", "openDecay");
    expect(11, "Tune", "tomTune");
    expect(20, "Decay", "cymbalDecay");
    expect(8, "Level", "cowbellLevel");
    EXPECT_NEAR(valueOf(*kit, "pad21Level"), 0.8f, 1.0e-6f) << "a free pad has no group";
}

TEST_F(DrumKitMigration, AProjectFromBeforePadsRendersTheSameSamplesAsTheOldKit) {
    const auto dir = saveProjectFromBeforePads(root);
    for (const auto* hits : {&beat(), &partnerBeat()}) {
        SCOPED_TRACE(hits == &beat() ? "beat" : "partner beat");
        Loaded loaded; // a fresh load per beat, so no voice carries over from the other
        ASSERT_TRUE(
            synth::ProjectBundle::load(dir, loaded.graph, loaded.timeline, loaded.patch, loaded.macros, loaded.remote)
                .ok);
        auto* kit = findKit(loaded.graph);
        ASSERT_NE(kit, nullptr);

        legacy_drums::Kit oldKit(savedValues(), kRate);
        const auto expected = oldKit.render(*hits, kLength);
        const auto actual = renderModule(*kit, *hits);
        float peak = 0.0f;
        for (float s : expected)
            peak = std::max(peak, std::fabs(s));
        ASSERT_GT(peak, 0.1f) << "the beat is audible";
        // The only allowed difference is each value's round trip through a parameter's normalised range.
        EXPECT_LT(maxDifference(expected, actual), 1.0e-4f) << firstDifference(expected, actual);
    }
}

TEST_F(DrumKitMigration, TheEqualityCheckCatchesAKitThatIgnoresTheSavedValues) {
    legacy_drums::Kit oldKit(savedValues(), kRate);
    const auto expected = oldKit.render(beat(), kLength);
    DrumKitModule fresh; // the defaults, as a migration that dropped the group values would leave it
    EXPECT_GT(maxDifference(expected, renderModule(fresh)), 1.0e-2f);
}

TEST_F(DrumKitMigration, AFreshKitRendersTheSameSamplesAsTheOldKitAtItsDefaults) {
    for (const auto* hits : {&beat(), &partnerBeat()}) {
        SCOPED_TRACE(hits == &beat() ? "beat" : "partner beat");
        legacy_drums::Kit oldKit({}, kRate);
        DrumKitModule fresh;
        const auto expected = oldKit.render(*hits, kLength);
        const auto actual = renderModule(fresh, *hits);
        EXPECT_LT(maxDifference(expected, actual), 1.0e-4f) << firstDifference(expected, actual);
    }
}

TEST_F(DrumKitMigration, ASavedKitWithChangedPadsReopensWithThem) {
    juce::AudioProcessorGraph graph;
    auto owned = std::make_unique<DrumKitModule>();
    auto* kit = owned.get();
    graph.addNode(std::move(owned));
    kit->setPadCount(16);
    auto pad = kit->pad(2);
    pad.note = 62;
    pad.name = "Big clap";
    ASSERT_TRUE(kit->setPad(2, pad));
    kit->padParameter(13, DrumKitModule::PadParam::Level)->setValueNotifyingHost(0.25f);
    kit->setPadCount(8); // hidden, never deleted

    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
    const auto dir = root.getChildFile(juce::String("new") + synth::ProjectBundle::kBundleExtension);
    ASSERT_TRUE(synth::ProjectBundle::save(dir, graph, timeline, patch, macros, remote).ok);

    Loaded loaded;
    ASSERT_TRUE(
        synth::ProjectBundle::load(dir, loaded.graph, loaded.timeline, loaded.patch, loaded.macros, loaded.remote).ok);
    auto* back = findKit(loaded.graph);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->padCount(), 8);
    EXPECT_EQ(back->pad(2).note, 62);
    EXPECT_EQ(back->pad(2).name, "Big clap");
    EXPECT_EQ(back->pad(12).sound, 47) << "a hidden pad keeps its sound";
    EXPECT_NEAR(back->padParameter(13, DrumKitModule::PadParam::Level)->get(), 0.25f, 1.0e-6f);
}
