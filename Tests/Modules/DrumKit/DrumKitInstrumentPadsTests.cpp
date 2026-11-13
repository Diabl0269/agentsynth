// DrumKitInstrumentPadsTests.cpp
// A pad that plays another module: the strike's pitch (plus Tune), velocity (Level times the hit), the Note-Off at the
// hit's end or after a card audition, a retrigger, a target swapped while a note is held, a missing target staying
// silent, the graph-wide link (a deleted module, a reopened project) and the note reaching a real Pluck.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SamplerMidiWiring.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Pluck/PluckModule.h"
#include "Modules/SequencerModule.h"
#include <gtest/gtest.h>

using synth::drums::PadKind;
using PadParam = DrumKitModule::PadParam;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;

struct Note {
    int pitch;
    int velocity; // 0 = note-off
    int at;
};

std::vector<Note> drain(synth::NoteInbox& inbox, int numSamples = kBlock) {
    juce::MidiBuffer midi;
    inbox.drainInto(midi, numSamples);
    std::vector<Note> out;
    for (const auto m : midi) {
        const auto msg = m.getMessage();
        out.push_back({msg.getNoteNumber(), msg.isNoteOn() ? (int)msg.getVelocity() : 0, m.samplePosition});
    }
    return out;
}

void block(DrumKitModule& kit, juce::MidiBuffer midi = {}) {
    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    kit.processBlock(buffer, midi);
}

juce::MidiBuffer noteOn(int note, float velocity, int at = 0) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, note, velocity), at);
    return midi;
}

juce::MidiBuffer noteOff(int note, int at = 0) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOff(10, note), at);
    return midi;
}

void setActual(juce::AudioParameterFloat* p, float value) { p->setValueNotifyingHost(p->convertTo0to1(value)); }

// A kit whose pad 2 (the snare, note 38) plays `uuid` at `pitch`, linked to `inbox`.
struct Rig {
    std::unique_ptr<DrumKitModule> kit = std::make_unique<DrumKitModule>();
    synth::NoteInbox::Ptr inbox = new synth::NoteInbox();

    explicit Rig(int pitch = 60) {
        kit->prepareToPlay(kRate, kBlock);
        auto pad = kit->pad(1);
        pad.kind = PadKind::Instrument;
        pad.instrumentUuid = "target";
        pad.instrumentPitch = pitch;
        pad.name = "Pluck";
        EXPECT_TRUE(kit->setPad(1, pad));
        link();
    }
    void link() {
        kit->linkInstrumentPads([this](const juce::String& uuid) { return uuid == "target" ? inbox.get() : nullptr; });
    }
};

} // namespace

TEST(DrumKitInstrumentPads, AStrikeSendsThePitchPlusTuneAtLevelTimesVelocity) {
    Rig rig(60);
    setActual(rig.kit->padParameter(1, PadParam::Level), 0.5f);
    setActual(rig.kit->padParameter(1, PadParam::Tune), 2.6f); // rounds to 3 semitones
    block(*rig.kit, noteOn(38, 1.0f, 17));
    const auto notes = drain(*rig.inbox);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].pitch, 63);
    EXPECT_EQ(notes[0].velocity, 64); // 0.5 * 1.0 * 127, rounded
    EXPECT_EQ(notes[0].at, 17);
    EXPECT_EQ(rig.kit->lastPlayedPad(), 1);
}

TEST(DrumKitInstrumentPads, TheHitsNoteOffEndsTheNote) {
    Rig rig(48);
    block(*rig.kit, noteOn(38, 0.8f));
    drain(*rig.inbox);
    block(*rig.kit, noteOff(38, 40));
    const auto notes = drain(*rig.inbox);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].pitch, 48);
    EXPECT_EQ(notes[0].velocity, 0);
    EXPECT_EQ(notes[0].at, 40);
}

TEST(DrumKitInstrumentPads, AnInstrumentPadMakesNoSoundOfItsOwn) {
    Rig rig;
    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    auto midi = noteOn(38, 1.0f);
    rig.kit->processBlock(buffer, midi);
    EXPECT_EQ(buffer.getMagnitude(0, kBlock), 0.0f);
}

TEST(DrumKitInstrumentPads, ACardAuditionHoldsTheNoteForAQuarterSecond) {
    Rig rig(60);
    rig.kit->auditionPad(1);
    block(*rig.kit);
    auto notes = drain(*rig.inbox);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].velocity, juce::roundToInt(0.8f * rig.kit->padParameter(1, PadParam::Level)->get() * 127.0f));
    const int hold = (int)(DrumKitModule::kAuditionHoldSeconds * kRate);
    int sent = 0;
    for (int b = 1; b * kBlock <= hold + kBlock; ++b) {
        block(*rig.kit);
        for (const auto& n : drain(*rig.inbox)) {
            EXPECT_EQ(n.velocity, 0);
            EXPECT_EQ(b * kBlock + n.at, hold);
            ++sent;
        }
    }
    EXPECT_EQ(sent, 1);
}

TEST(DrumKitInstrumentPads, ARetriggerEndsTheHeldNoteFirst) {
    Rig rig(60);
    block(*rig.kit, noteOn(38, 1.0f));
    drain(*rig.inbox);
    block(*rig.kit, noteOn(38, 1.0f, 5));
    const auto notes = drain(*rig.inbox);
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_EQ(notes[0].velocity, 0);
    EXPECT_GT(notes[1].velocity, 0);
}

TEST(DrumKitInstrumentPads, ZeroLevelSendsNothing) {
    Rig rig;
    setActual(rig.kit->padParameter(1, PadParam::Level), 0.0f);
    block(*rig.kit, noteOn(38, 1.0f));
    EXPECT_TRUE(drain(*rig.inbox).empty());
}

TEST(DrumKitInstrumentPads, AMissingTargetIsSilentAndSaysSo) {
    Rig rig;
    rig.kit->linkInstrumentPads([](const juce::String&) { return nullptr; });
    EXPECT_FALSE(rig.kit->padInstrumentLinked(1));
    block(*rig.kit, noteOn(38, 1.0f));
    EXPECT_TRUE(drain(*rig.inbox).empty());
}

TEST(DrumKitInstrumentPads, AHeldNoteEndsWhenThePadStopsReachingItsModule) {
    Rig rig(60);
    block(*rig.kit, noteOn(38, 1.0f));
    drain(*rig.inbox);
    auto pad = rig.kit->pad(1);
    pad.instrumentUuid = "elsewhere"; // not linked yet: the old inbox must still hear the Note-Off
    ASSERT_TRUE(rig.kit->setPad(1, pad));
    block(*rig.kit);
    const auto notes = drain(*rig.inbox);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].velocity, 0);
}

TEST(DrumKitInstrumentPads, BypassEndsHeldNotes) {
    Rig rig;
    block(*rig.kit, noteOn(38, 1.0f));
    drain(*rig.inbox);
    rig.kit->setBypassed(true);
    block(*rig.kit);
    block(*rig.kit);
    const auto notes = drain(*rig.inbox);
    ASSERT_FALSE(notes.empty());
    EXPECT_EQ(notes.back().velocity, 0);
}

TEST(DrumKitInstrumentPads, TheLinkSurvivesASaveAndRestoreOfTheSameUuid) {
    Rig rig;
    const auto state = rig.kit->getExtraState();
    rig.kit->setExtraState(state);
    EXPECT_TRUE(rig.kit->padInstrumentLinked(1));
    block(*rig.kit, noteOn(38, 1.0f));
    EXPECT_EQ(drain(*rig.inbox).size(), 1u);
}

TEST(DrumKitInstrumentPads, ResetPadGoesBackToTheSlotsKitSoundKeepingItsNote) {
    Rig rig;
    auto pad = rig.kit->pad(1);
    pad.note = 70;
    ASSERT_TRUE(rig.kit->setPad(1, pad));
    rig.kit->resetPad(1);
    EXPECT_EQ(rig.kit->pad(1).kind, synth::drums::defaultPad(1).kind);
    EXPECT_EQ(rig.kit->pad(1).sound, 38);
    EXPECT_EQ(rig.kit->pad(1).name, "Snare");
    EXPECT_EQ(rig.kit->pad(1).note, 70);
    EXPECT_TRUE(rig.kit->pad(1).instrumentUuid.isEmpty());
}

TEST(DrumKitInstrumentPads, OnlySoundingInstrumentsOtherThanAKitOrSequencerArePlayable) {
    EXPECT_TRUE(DrumKitModule::canPlayFromPad(ModuleType::Pluck));
    EXPECT_TRUE(DrumKitModule::canPlayFromPad(ModuleType::Sampler));
    EXPECT_FALSE(DrumKitModule::canPlayFromPad(ModuleType::DrumKit));
    EXPECT_FALSE(DrumKitModule::canPlayFromPad(ModuleType::Sequencer));
    EXPECT_FALSE(DrumKitModule::canPlayFromPad(ModuleType::Filter));
}

// ---- In a graph ----

namespace {

struct GraphRig {
    AudioEngine engine{AudioEngine::HostMode::Standalone};
    juce::AudioProcessorGraph::Node::Ptr kitNode, pluckNode;
    DrumKitModule* kit = nullptr;
    PluckModule* pluck = nullptr;

    GraphRig() {
        auto& graph = engine.getGraph();
        kitNode = graph.addNode(std::make_unique<DrumKitModule>());
        pluckNode = graph.addNode(std::make_unique<PluckModule>());
        kit = dynamic_cast<DrumKitModule*>(kitNode->getProcessor());
        pluck = dynamic_cast<PluckModule*>(pluckNode->getProcessor());
        kit->prepareToPlay(kRate, kBlock);
        pluck->prepareToPlay(kRate, kBlock);
        auto pad = kit->pad(0);
        pad.kind = PadKind::Instrument;
        pad.instrumentUuid = synth::AIStateMapper::ensureNodeUuid(pluckNode.get());
        EXPECT_TRUE(kit->setPad(0, pad));
        synth::publishSamplerMidiWiring(graph);
    }
};

float pluckPeak(PluckModule& pluck, int blocks) {
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(pluck.getTotalNumOutputChannels(), kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        pluck.processBlock(buffer, midi);
        peak = std::max(peak, buffer.getMagnitude(0, 0, kBlock));
    }
    return peak;
}

} // namespace

TEST(DrumKitInstrumentPads, AStruckPadPlaysTheLinkedPluck) {
    GraphRig rig;
    ASSERT_TRUE(rig.kit->padInstrumentLinked(0));
    EXPECT_EQ(pluckPeak(*rig.pluck, 4), 0.0f) << "nothing struck yet";
    block(*rig.kit, noteOn(36, 1.0f));
    EXPECT_GT(pluckPeak(*rig.pluck, 4), 0.01f);
}

TEST(DrumKitInstrumentPads, DeletingTheModuleUnlinksThePad) {
    GraphRig rig;
    rig.pluck = nullptr;
    rig.engine.getGraph().removeNode(rig.pluckNode->nodeID);
    rig.pluckNode = nullptr;
    rig.engine.refreshMidiWiring();
    EXPECT_FALSE(rig.kit->padInstrumentLinked(0));
    block(*rig.kit, noteOn(36, 1.0f)); // nothing to reach, nothing crashes
}

TEST(DrumKitInstrumentPads, AReopenedProjectLinksThePadToTheRestoredModule) {
    GraphRig rig;
    const auto json = synth::AIStateMapper::graphToJSON(rig.engine.getGraph());
    AudioEngine target(AudioEngine::HostMode::Standalone);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, target.getGraph(), true, true));
    DrumKitModule* restored = nullptr;
    for (auto* node : target.getGraph().getNodes())
        if (auto* k = dynamic_cast<DrumKitModule*>(node->getProcessor()))
            restored = k;
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->pad(0).kind, PadKind::Instrument);
    EXPECT_TRUE(restored->padInstrumentLinked(0));
}
