// InstrumentLevelModCableTests.cpp
// The Level knob of Drum Kit, Pluck and Mallet takes a mod cable, and so do Pluck's Body and Mallet's Tremolo: each
// cable arrives on an input channel of its own (no gutter jack, not an audio input), moves its knob live within the
// knob's range, and never leaks into the output. A project saved before a channel existed opens with every cable where
// it was.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Mallet/MalletModule.h"
#include "Modules/Pluck/PluckModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <set>
#include <tuple>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

struct Instrument {
    const char* name;
    int note;       // a note that sounds for the whole measured span
    int cableCount; // the live cables: Level, plus Body (Pluck) or Tremolo (Mallet)
    int inputs;     // every raw input channel the module declares
    int targets;    // the modulation targets it lists
    std::function<std::unique_ptr<ModuleBase>()> make;
};

const std::vector<Instrument>& instruments() {
    static const std::vector<Instrument> all = {
        {"Drum Kit", 49, 1, DrumKitModule::kPadModSlotBase + DrumKitModule::kMaxPadModSlots,
         (int)DrumKitModule::kModTargets.size(),
         [] { return std::make_unique<DrumKitModule>(); }}, // the crash rings past 0.3 s
        {"Pluck", 48, 2, (int)PluckModule::kModTargets.size(), (int)PluckModule::kModTargets.size(),
         [] { return std::make_unique<PluckModule>(); }},
        {"Mallet", 60, 2, (int)MalletModule::kModTargets.size(), (int)MalletModule::kModTargets.size(),
         [] { return std::make_unique<MalletModule>(); }},
    };
    return all;
}

// Renders `seconds` of the left output with the cable on `channel` held at `cv` and, when `note` >= 0, a note at sample
// 0.
std::vector<float> renderWithCv(ModuleBase& module, int note, float cv, double seconds, int channel = 0) {
    const int total = (int)(seconds * kRate);
    std::vector<float> out((size_t)total, 0.0f);
    for (int start = 0; start < total; start += kBlock) {
        const int n = std::min(kBlock, total - start);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::FloatVectorOperations::fill(block.getWritePointer(channel), cv, n);
        juce::MidiBuffer midi;
        if (start == 0 && note >= 0)
            midi.addEvent(juce::MidiMessage::noteOn(10, note, (juce::uint8)100), 0);
        module.processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + start);
    }
    return out;
}

float rmsOf(const std::vector<float>& s, double fromSec, double toSec) {
    const auto a = (size_t)(fromSec * kRate);
    const auto b = std::min(s.size(), (size_t)(toSec * kRate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum += (double)s[i] * s[i];
    return b > a ? (float)std::sqrt(sum / (double)(b - a)) : 0.0f;
}

std::unique_ptr<ModuleBase> prepared(const Instrument& instrument) {
    auto module = instrument.make();
    module->prepareToPlay(kRate, kBlock);
    return module;
}

float levelOf(ModuleBase& module) {
    auto* level = findParameterByID(&module, "level");
    return level->convertFrom0to1(level->getValue());
}

} // namespace

TEST(InstrumentLevelModCable, LevelIsAKnobOnlyTargetOnTheFirstInputChannel) {
    for (const auto& instrument : instruments()) {
        SCOPED_TRACE(instrument.name);
        auto module = instrument.make();
        EXPECT_EQ(module->getTotalNumInputChannels(), instrument.inputs);
        EXPECT_EQ(module->getVisibleInputPortCount(), 0) << "no gutter jack";
        EXPECT_EQ(module->mapInputChannel(0).role, PortRole::ModCV);
        EXPECT_EQ(module->bypassDryChannel(0), -1) << "bypass passes no cable through as audio";
        const auto targets = module->getModulationTargets();
        ASSERT_EQ(targets.size(), (size_t)instrument.targets);
        EXPECT_EQ(targets[0].paramId, "level");
        EXPECT_EQ(targets[0].channelIndex, 0);
        EXPECT_TRUE(targets[0].knobOnly);
        EXPECT_EQ(module->modulationChannelForParam("level"), 0);
        EXPECT_TRUE(module->isAutoPromotableModTarget(0)) << "a cable into it goes through an attenuverter";
    }
}

// Regression test for FRO1359: the instruments' Level knob took no mod cable.
TEST(InstrumentLevelModCable, ACableOnLevelScalesTheOutputWithinTheKnobsRange) {
    for (const auto& instrument : instruments()) {
        SCOPED_TRACE(instrument.name);
        auto plain = prepared(instrument);
        const float base = levelOf(*plain);
        const auto unmodulated = renderWithCv(*plain, instrument.note, 0.0f, 0.3);
        auto lowered = prepared(instrument);
        const auto down = renderWithCv(*lowered, instrument.note, -0.4f, 0.3);
        auto closed = prepared(instrument);
        const auto silent = renderWithCv(*closed, instrument.note, -1.0f, 0.3);

        // Past the 10 ms level ramp. Level is a linear gain, so -0.4 of the 0..1 knob scales by (base - 0.4) / base.
        const float reference = rmsOf(unmodulated, 0.05, 0.3);
        ASSERT_GT(reference, 1.0e-3f) << "the note must sound";
        EXPECT_NEAR(rmsOf(down, 0.05, 0.3) / reference, (base - 0.4f) / base, 0.03f);
        EXPECT_LT(rmsOf(silent, 0.05, 0.3), reference * 1.0e-3f) << "the cable can close the level";
        EXPECT_FLOAT_EQ(levelOf(*closed), base) << "the cable never writes the knob";
    }
}

TEST(InstrumentLevelModCable, TheCableNeverReachesTheOutput) {
    for (const auto& instrument : instruments()) {
        SCOPED_TRACE(instrument.name);
        auto module = prepared(instrument);
        const auto out = renderWithCv(*module, -1, 0.7f, 0.1);
        EXPECT_EQ(rmsOf(out, 0.0, 0.1), 0.0f) << "no note: silence, whatever the cable carries";

        auto bypassed = prepared(instrument);
        bypassed->setBypassed(true);
        const auto dry = renderWithCv(*bypassed, instrument.note, 0.7f, 0.1);
        EXPECT_LT(rmsOf(dry, 0.05, 0.1), 1.0e-6f) << "bypass clears; the cable is not a dry signal";
    }
}

// Tools/ui-verify/fixtures/two-tracks-folded.agsproj was written by the app before the Level channel existed (and must
// never be regenerated): its Drum Kit takes MIDI from its Track In and feeds the track's Gate on both legs.
TEST(InstrumentLevelModCable, AProjectFromBeforeTheLevelCableOpensWithItsCablesInPlace) {
    const auto dir = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("two-tracks-folded.agsproj");
    ASSERT_TRUE(dir.getChildFile("project.json").existsAsFile());
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
    const auto loaded = synth::ProjectBundle::load(dir, graph, timeline, patch, macros, remote);
    ASSERT_TRUE(loaded.ok) << loaded.message;

    juce::AudioProcessorGraph::Node* kit = nullptr;
    for (auto* node : graph.getNodes())
        if (node->getProcessor()->getName() == "Drum Kit")
            kit = node;
    ASSERT_NE(kit, nullptr);
    EXPECT_EQ(kit->getProcessor()->getTotalNumInputChannels(),
              DrumKitModule::kPadModSlotBase + DrumKitModule::kMaxPadModSlots);

    using Edge = std::tuple<juce::String, int, juce::String, int>;
    std::set<Edge> touching;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == kit->nodeID || c.destination.nodeID == kit->nodeID)
            touching.insert({graph.getNodeForId(c.source.nodeID)->getProcessor()->getName(), c.source.channelIndex,
                             graph.getNodeForId(c.destination.nodeID)->getProcessor()->getName(),
                             c.destination.channelIndex});
    const int midi = juce::AudioProcessorGraph::midiChannelIndex;
    EXPECT_EQ(
        touching,
        (std::set<Edge>{{"Track In", midi, "Drum Kit", midi}, {"Drum Kit", 0, "Gate", 0}, {"Drum Kit", 1, "Gate", 1}}))
        << "every saved cable on its raw channel, and nothing on the new Level channel";
}

TEST(InstrumentLevelModCable, ALevelCableSurvivesSaveAndLoad) {
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("InstrumentLevelModCable-" + juce::Uuid().toString());
    root.createDirectory();
    const auto dir = root.getChildFile(juce::String("cable") + synth::ProjectBundle::kBundleExtension);
    {
        juce::AudioProcessorGraph graph;
        auto lfo = graph.addNode(synth::AIStateMapper::createModule("LFO"));
        for (const auto& instrument : instruments()) {
            const auto node = graph.addNode(instrument.make());
            for (int channel = 0; channel < instrument.cableCount; ++channel)
                graph.addConnection({{lfo->nodeID, 0}, {node->nodeID, channel}});
        }
        synth::TimelineDoc timeline;
        synth::PatchDocument patch;
        synth::MacroSet macros;
        synth::MidiRemoteProjectDoc remote;
        ASSERT_TRUE(synth::ProjectBundle::save(dir, graph, timeline, patch, macros, remote).ok);
    }
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
    ASSERT_TRUE(synth::ProjectBundle::load(dir, graph, timeline, patch, macros, remote).ok);
    // A saved mod cable comes back as a modulation: the LFO feeds an attenuverter that feeds the knob's channel.
    const auto nameOf = [&graph](juce::AudioProcessorGraph::NodeID id) {
        return graph.getNodeForId(id)->getProcessor()->getName();
    };
    std::set<std::pair<juce::String, int>> cabled;
    for (const auto& c : graph.getConnections())
        if (const auto from = nameOf(c.source.nodeID);
            (from == "LFO" || from == "Attenuverter") && nameOf(c.destination.nodeID) != "Attenuverter")
            cabled.insert({nameOf(c.destination.nodeID), c.destination.channelIndex});
    EXPECT_EQ(cabled, (std::set<std::pair<juce::String, int>>{
                          {"Drum Kit", 0}, {"Pluck", 0}, {"Pluck", 1}, {"Mallet", 0}, {"Mallet", 1}}))
        << "Level, Body and Tremolo cables each come back on their own channel";
    root.deleteRecursively();
}

TEST(InstrumentLevelModCable, ABodyOrTremoloCableChangesTheSoundWithinTheKnobsRange) {
    // Pluck's Body mixes in the resonator, so a full Body cable renders a different waveform than none.
    {
        PluckModule none, full;
        for (auto* m : {&none, &full})
            m->prepareToPlay(kRate, kBlock);
        const auto a = renderWithCv(none, 48, 0.0f, 0.3, PluckModule::kBodyCvChannel);
        const auto b = renderWithCv(full, 48, 1.0f, 0.3, PluckModule::kBodyCvChannel);
        double diff = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            diff += (double)(a[i] - b[i]) * (a[i] - b[i]);
        ASSERT_GT(rmsOf(a, 0.05, 0.3), 1.0e-3f) << "the note must sound";
        EXPECT_GT(std::sqrt(diff / (double)a.size()), 0.05 * rmsOf(a, 0.05, 0.3)) << "Body cable changes the tone";
        auto* body = findParameterByID(&full, "body");
        EXPECT_FLOAT_EQ(body->convertFrom0to1(body->getValue()), 0.3f) << "the cable never writes the knob";
    }
    // Mallet's Tremolo is 0 by default; a full cable swings the level at kTremoloHz, down to silence at the trough.
    {
        MalletModule none, full;
        for (auto* m : {&none, &full})
            m->prepareToPlay(kRate, kBlock);
        const auto steady = renderWithCv(none, 60, 0.0f, 0.5, MalletModule::kTremoloCvChannel);
        const auto swung = renderWithCv(full, 60, 1.0f, 0.5, MalletModule::kTremoloCvChannel);
        float lowest = 1.0f;
        for (double t = 0.1; t < 0.45; t += 0.01) {
            const float reference = rmsOf(steady, t, t + 0.01);
            ASSERT_GT(reference, 1.0e-4f);
            lowest = std::min(lowest, rmsOf(swung, t, t + 0.01) / reference);
        }
        EXPECT_LT(lowest, 0.3f) << "the Tremolo cable modulates the amplitude";
        auto* tremolo = findParameterByID(&full, "tremolo");
        EXPECT_FLOAT_EQ(tremolo->convertFrom0to1(tremolo->getValue()), 0.0f) << "the cable never writes the knob";
    }
}

TEST(InstrumentLevelModCable, BodyAndTremoloAreKnobOnlyTargetsOnTheSecondChannel) {
    for (const auto& [instrument, id, label] :
         {std::tuple<const Instrument*, const char*, const char*>{&instruments()[1], "body", "Body"},
          {&instruments()[2], "tremolo", "Tremolo"}}) {
        SCOPED_TRACE(id);
        auto module = instrument->make();
        const auto targets = module->getModulationTargets();
        ASSERT_EQ(targets.size(), (size_t)module->getTotalNumInputChannels());
        EXPECT_EQ(targets[1].paramId, id);
        EXPECT_EQ(targets[1].name, label);
        EXPECT_EQ(targets[1].channelIndex, 1);
        EXPECT_TRUE(targets[1].knobOnly);
        EXPECT_EQ(module->modulationChannelForParam(id), 1);
        EXPECT_EQ(module->getInputPortLabel(1), label);
        EXPECT_EQ(module->mapInputChannel(1).role, PortRole::ModCV);
        EXPECT_EQ(module->bypassDryChannel(1), -1);
    }
}

TEST(InstrumentLevelModCable, ABodyOrTremoloCableNeverReachesTheOutput) {
    for (const auto& instrument : instruments()) {
        if (instrument.cableCount < 2)
            continue;
        SCOPED_TRACE(instrument.name);
        auto module = prepared(instrument);
        const auto out = renderWithCv(*module, -1, 0.7f, 0.1, 1);
        EXPECT_EQ(rmsOf(out, 0.0, 0.1), 0.0f) << "no note: silence, whatever the cable carries";
        auto bypassed = prepared(instrument);
        bypassed->setBypassed(true);
        EXPECT_LT(rmsOf(renderWithCv(*bypassed, instrument.note, 0.7f, 0.1, 1), 0.05, 0.1), 1.0e-6f);
    }
}

// drum-kit-instrument-pad.agsproj holds a Pluck saved before Body had a channel: it opens with no cable on either.
TEST(InstrumentLevelModCable, APluckFromBeforeTheBodyCableOpensWithNoCables) {
    const auto dir = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("drum-kit-instrument-pad.agsproj");
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc remote;
    const auto loaded = synth::ProjectBundle::load(dir, graph, timeline, patch, macros, remote);
    ASSERT_TRUE(loaded.ok) << loaded.message;
    juce::AudioProcessorGraph::Node* pluck = nullptr;
    for (auto* node : graph.getNodes())
        if (node->getProcessor()->getName() == "Pluck")
            pluck = node;
    ASSERT_NE(pluck, nullptr);
    EXPECT_EQ(pluck->getProcessor()->getTotalNumInputChannels(), (int)PluckModule::kModTargets.size());
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == pluck->nodeID)
            EXPECT_EQ(c.destination.channelIndex, juce::AudioProcessorGraph::midiChannelIndex) << "only MIDI feeds it";
}
