// InstrumentNoteTimeCableTests.cpp
// Pluck and Mallet take a cable on every knob. The note-time knobs read their cable on the sample a note starts and
// never again, so a cable shapes each new note and not a ringing one; Release reads its cable on the note-off's sample.
// Level and the second live knob (Body, Tremolo) are covered in InstrumentLevelModCableTests.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/Mallet/MalletModule.h"
#include "Modules/ModTargetList.h"
#include "Modules/Pluck/PluckModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <set>
#include <span>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

struct Instrument {
    const char* name;
    int note;
    std::span<const synth::ModTargetSpec> specs;
    std::function<std::unique_ptr<ModuleBase>()> make;
};

const std::vector<Instrument>& instruments() {
    static const std::vector<Instrument> all = {
        {"Pluck", 60, PluckModule::kModTargets, [] { return std::make_unique<PluckModule>(); }},
        {"Mallet", 60, MalletModule::kModTargets, [] { return std::make_unique<MalletModule>(); }},
    };
    return all;
}

struct Render {
    int channel = 0;                                         // the cable under test
    std::function<float(int)> cv = [](int) { return 0.0f; }; // its value at an absolute sample
    int noteAt = 0;
    int offAt = -1; // absolute sample of the note-off, none when negative
    double seconds = 0.4;
};

std::vector<float> render(const Instrument& instrument, const Render& plan) {
    auto module = instrument.make();
    module->prepareToPlay(kRate, kBlock);
    const int channels = std::max(2, module->getTotalNumInputChannels());
    const int total = (int)(plan.seconds * kRate);
    std::vector<float> out((size_t)total, 0.0f);
    for (int start = 0; start < total; start += kBlock) {
        const int n = std::min(kBlock, total - start);
        juce::AudioBuffer<float> block(channels, n);
        block.clear();
        for (int i = 0; i < n; ++i)
            block.setSample(plan.channel, i, plan.cv(start + i));
        juce::MidiBuffer midi;
        if (plan.noteAt >= start && plan.noteAt < start + n)
            midi.addEvent(juce::MidiMessage::noteOn(1, instrument.note, (juce::uint8)100), plan.noteAt - start);
        if (plan.offAt >= start && plan.offAt < start + n)
            midi.addEvent(juce::MidiMessage::noteOff(1, instrument.note), plan.offAt - start);
        module->processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + start);
    }
    return out;
}

double rms(const std::vector<float>& s, size_t from, size_t to) {
    to = std::min(to, s.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double)s[i] * s[i];
    return to > from ? std::sqrt(sum / (double)(to - from)) : 0.0;
}

// The RMS of (a - b) over [from, to), relative to a's own RMS there.
double relativeDifference(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to) {
    std::vector<float> diff(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        diff[i] = a[i] - b[i];
    const double reference = rms(a, from, to);
    return reference > 0.0 ? rms(diff, from, to) / reference : 0.0;
}

// The cable value that moves the knob furthest: +0.5, or -0.5 when the knob already sits in its upper half.
float roomyCable(const Instrument& instrument, const char* paramId) {
    auto module = instrument.make();
    return findParameterByID(module.get(), paramId)->getValue() < 0.5f ? 0.5f : -0.5f;
}

} // namespace

TEST(InstrumentNoteTimeCable, ACableOnEveryNoteTimeKnobChangesTheNote) {
    for (const auto& instrument : instruments())
        for (size_t k = 0; k < instrument.specs.size(); ++k) {
            const auto& spec = instrument.specs[k];
            if (spec.read != synth::ModRead::NoteOn)
                continue;
            SCOPED_TRACE(std::string(instrument.name) + "/" + spec.paramId);
            const float amount = roomyCable(instrument, spec.paramId);
            const auto plain = render(instrument, {(int)k, [](int) { return 0.0f; }});
            const auto moved = render(instrument, {(int)k, [amount](int) { return amount; }});
            ASSERT_GT(rms(plain, 2000, 8000), 1.0e-3) << "the note must sound";
            EXPECT_GT(relativeDifference(plain, moved, 0, plain.size()), 0.02) << "the cable must move the note";
        }
}

TEST(InstrumentNoteTimeCable, ANoteAlreadyRingingIgnoresACableThatChangesAfterItsNoteOn) {
    constexpr int kSwitch = 5000; // mid-block, mid-note
    for (const auto& instrument : instruments())
        for (size_t k = 0; k < instrument.specs.size(); ++k) {
            const auto& spec = instrument.specs[k];
            if (spec.read == synth::ModRead::Live)
                continue;
            SCOPED_TRACE(std::string(instrument.name) + "/" + spec.paramId);
            const int channel = (int)k;
            const auto steady = render(instrument, {channel, [](int) { return 0.0f; }});
            const auto switched = render(instrument, {channel, [](int i) { return i < kSwitch ? 0.0f : 1.0f; }});
            for (size_t i = 0; i < steady.size(); ++i)
                ASSERT_EQ(steady[i], switched[i]) << "sample " << i;
        }
}

TEST(InstrumentNoteTimeCable, ANoteReadsItsCableOnItsOwnSampleNotTheBlocksFirst) {
    constexpr int kNoteAt = 300;
    for (const auto& instrument : instruments())
        for (size_t k = 0; k < instrument.specs.size(); ++k) {
            const auto& spec = instrument.specs[k];
            if (spec.read != synth::ModRead::NoteOn)
                continue;
            SCOPED_TRACE(std::string(instrument.name) + "/" + spec.paramId);
            const int channel = (int)k;
            const auto ramp = [](int i) { return i < kBlock ? (float)i / (float)(kBlock - 1) : 0.0f; };
            const float atNote = ramp(kNoteAt);
            const auto rising = render(instrument, {channel, ramp, kNoteAt});
            const auto held =
                render(instrument, {channel, [atNote](int i) { return i < kBlock ? atNote : 0.0f; }, kNoteAt});
            const auto atBlockStart = render(instrument, {channel, [](int) { return 0.0f; }, kNoteAt});
            for (size_t i = 0; i < rising.size(); ++i)
                ASSERT_EQ(rising[i], held[i]) << "sample " << i << ": the ramp's value at the note-on, not elsewhere";
            EXPECT_GT(relativeDifference(held, atBlockStart, 0, held.size()), 0.01)
                << "the ramp's value at sample 300 differs from its value at sample 0";
        }
}

TEST(InstrumentNoteTimeCable, TheReleaseCableShapesTheTailAfterTheNoteOffAndOnlyThen) {
    constexpr int kOffAt = 4000;
    for (const auto& instrument : instruments()) {
        SCOPED_TRACE(instrument.name);
        size_t release = 0;
        for (size_t k = 0; k < instrument.specs.size(); ++k)
            if (instrument.specs[k].read == synth::ModRead::NoteOff)
                release = k;
        const int channel = (int)release;
        const float amount = roomyCable(instrument, instrument.specs[release].paramId);
        Render plan{channel, [](int) { return 0.0f; }, 0, kOffAt, 0.8};
        const auto plain = render(instrument, plan);
        plan.cv = [amount](int) { return amount; };
        const auto moved = render(instrument, plan);
        for (size_t i = 0; i < (size_t)kOffAt; ++i)
            ASSERT_EQ(plain[i], moved[i]) << "before the note-off nothing reads Release; sample " << i;
        EXPECT_GT(relativeDifference(plain, moved, (size_t)kOffAt, plain.size()), 0.02)
            << "the tail after the note-off follows the cable";

        // The cable is read on the note-off's own sample, so one that moves exactly there counts.
        plan.cv = [amount](int i) { return i < kOffAt ? 0.0f : amount; };
        const auto late = render(instrument, plan);
        EXPECT_GT(relativeDifference(plain, late, (size_t)kOffAt, plain.size()), 0.02)
            << "a cable moving at the note-off's sample counts";
        plan.cv = [amount](int i) { return i <= kOffAt ? 0.0f : amount; };
        const auto after = render(instrument, plan);
        for (size_t i = 0; i < plain.size(); ++i)
            ASSERT_EQ(plain[i], after[i]) << "a cable that moves after the note-off changes nothing; sample " << i;
    }
}

TEST(InstrumentNoteTimeCable, EveryCableComesBackOnItsChannelAfterSaveAndLoad) {
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("InstrumentNoteTimeCable-" + juce::Uuid().toString());
    root.createDirectory();
    const auto dir = root.getChildFile(juce::String("cable") + synth::ProjectBundle::kBundleExtension);
    {
        juce::AudioProcessorGraph graph;
        auto lfo = graph.addNode(synth::AIStateMapper::createModule("LFO"));
        for (const auto& instrument : instruments()) {
            const auto node = graph.addNode(instrument.make());
            for (size_t k = 0; k < instrument.specs.size(); ++k)
                graph.addConnection({{lfo->nodeID, 0}, {node->nodeID, (int)k}});
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
    const auto nameOf = [&graph](juce::AudioProcessorGraph::NodeID id) {
        return graph.getNodeForId(id)->getProcessor()->getName();
    };
    std::set<std::pair<juce::String, int>> cabled, expected;
    for (const auto& c : graph.getConnections())
        if (const auto from = nameOf(c.source.nodeID);
            (from == "LFO" || from == "Attenuverter") && nameOf(c.destination.nodeID) != "Attenuverter")
            cabled.insert({nameOf(c.destination.nodeID), c.destination.channelIndex});
    for (const auto& instrument : instruments())
        for (size_t k = 0; k < instrument.specs.size(); ++k)
            expected.insert({instrument.name, (int)k});
    EXPECT_EQ(cabled, expected) << "Level on channel 0, Body or Tremolo on 1, and every other knob after them";
    root.deleteRecursively();
}
