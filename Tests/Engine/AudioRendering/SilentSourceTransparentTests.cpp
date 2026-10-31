// The "a silent source adds nothing" half of the audio-unheard guard: a source at Level 0, bypassed or muted is
// cabled into every module that processes audio, next to the source already feeding it, and the chain's output must
// not change by a single bit. The cables are made by the canvas's own rule (GraphEditor::resolvePolyLink) and the
// chain is played from a timeline clip, so the graph, the render-time down-mix and the note path are the real ones.
//
// Every registered module must be classified here (EveryRegisteredModuleIsClassified), so a new effect cannot join
// the product without joining the guard.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioRenderingTestHelpers.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/OfflineTransportDriver.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using Node = juce::AudioProcessorGraph::Node;

constexpr double kSilentRate = 48000.0;
constexpr int kSilentBlock = 512;
constexpr int kSilentBlocks = 12;
constexpr int kSilentMidi = juce::AudioProcessorGraph::midiChannelIndex;
constexpr const char* kSilentTrackUuid = "c1c1c1c1-0000-0000-0000-000000000193";

enum class SilentHow { None, LevelZero, Bypassed, Muted };
enum class Legs { One, WholePair };

struct Processor {
    const char* type;
    int filterType = -1;
    bool poly = false;
};

const std::vector<const char*>& silentSources() {
    static const std::vector<const char*> sources = {"Oscillator", "Wavetable", "Noise", "Pluck"};
    return sources;
}

const std::vector<Processor>& processors() {
    static const std::vector<Processor> list = [] {
        std::vector<Processor> all;
        for (const bool poly : {false, true}) {
            for (int type = 0; type < 8; ++type)
                all.push_back({"Filter", type, poly});
            all.push_back({"VCA", -1, poly});
        }
        for (const char* fx :
             {"Bitcrusher", "Channel Strip", "Chorus", "Compressor", "Delay", "Distortion", "Flanger", "Gate",
              "Limiter", "Parametric EQ", "Phaser", "Pitch Shifter", "Reverb", "Ring Modulator"})
            all.push_back({fx});
        return all;
    }();
    return list;
}

// Registered modules that do not process a source's audio in place: sources, modulators, routing, envelopes.
const std::set<std::string>& notProcessors() {
    static const std::set<std::string> names = {
        "ADSR",          "Amp Env",       "Attenuverter",  "Audio Input",
        "Audio Output",  "Comparator",    "Drum Kit",      "Envelope Follower",
        "External MIDI", "Filter Env",    "Hosted Plugin", "LFO",
        "MIDI Keyboard", "Macro In",      "Macro MIDI In", "Macro MIDI Out",
        "Macro Out",     "Macros",        "Mallet",        "Master",
        "Math",          "Midi Input",    "Mod Slot",      "Noise",
        "Oscillator",    "Pluck",         "Poly MIDI",     "Poly Sequencer",
        "Rec Tap",       "Sample & Hold", "Sampler",       "Sequencer",
        "Track Audio",   "Track In",      "Voice Mixer",   "Wavetable",
    };
    return names;
}

void setIfPresent(juce::AudioProcessor& p, const juce::String& id, float value) { setParamValue(&p, id, value); }

void connectJacks(juce::AudioProcessorGraph& graph, Node& from, Node& to) {
    const auto link = GraphEditor::resolvePolyLink(dynamic_cast<ModuleBase*>(from.getProcessor()), 0,
                                                   dynamic_cast<ModuleBase*>(to.getProcessor()), 0);
    for (int v = 0; v < link.voiceCount; ++v)
        graph.addConnection({{from.nodeID, link.sourceChannel(v)}, {to.nodeID, link.destChannel(v)}});
}

// A source's cables onto `dest`'s first jack. WholePair cables its Left and Right legs both onto one input channel,
// voice by voice; a source with no right leg cannot, and answers false.
bool connectSource(juce::AudioProcessorGraph& graph, Node& source, Node& dest, Legs legs, bool poly) {
    if (legs == Legs::One) {
        connectJacks(graph, source, dest);
        return true;
    }
    const int right = dynamic_cast<ModuleBase*>(source.getProcessor())->rightAudioLegChannel();
    if (right <= 0)
        return false;
    for (int v = 0; v < (poly ? 8 : 1); ++v) {
        graph.addConnection({{source.nodeID, v}, {dest.nodeID, v}});
        graph.addConnection({{source.nodeID, right + v}, {dest.nodeID, v}});
    }
    return true;
}

// Notes for a source: its MIDI, and in poly each voice's pitch from Poly MIDI.
void feedNotes(juce::AudioProcessorGraph& graph, Node& trackIn, Node* polyMidi, Node& source) {
    graph.addConnection({{trackIn.nodeID, kSilentMidi}, {source.nodeID, kSilentMidi}});
    if (polyMidi != nullptr)
        connectJacks(graph, *polyMidi, source);
}

juce::Array<float> renderChain(const Processor& proc, const char* silentType, SilentHow how, Legs mainLegs,
                               Legs silentLegs, bool& wired) {
    auto engine = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
    engine->initialise();
    auto& graph = engine->getGraph();
    const auto add = [&](const char* type) { return graph.addNode(synth::AIStateMapper::createModule(type)); };

    auto trackIn = add("Track In");
    trackIn->properties.set("uuid", kSilentTrackUuid);
    dynamic_cast<ModuleBase*>(trackIn->getProcessor())->setNodeUuid(kSilentTrackUuid);
    auto polyMidi = proc.poly ? add("Poly MIDI") : nullptr;
    if (polyMidi != nullptr)
        graph.addConnection({{trackIn->nodeID, kSilentMidi}, {polyMidi->nodeID, kSilentMidi}});

    const auto makeSource = [&](const char* type, Legs legs) {
        auto node = add(type);
        auto& p = *node->getProcessor();
        setIfPresent(p, "poly", proc.poly ? 1.0f : 0.0f);
        setIfPresent(p, "dualIO", legs == Legs::WholePair ? 1.0f : 0.0f);
        feedNotes(graph, *trackIn, polyMidi.get(), *node);
        return node;
    };

    auto main = makeSource("Oscillator", mainLegs);
    setIfPresent(*main->getProcessor(), "unison", 3.0f);
    setIfPresent(*main->getProcessor(), "width", 1.0f);
    setChoiceParam(main->getProcessor(), "waveform", 2);

    auto dut = add(proc.type);
    setIfPresent(*dut->getProcessor(), "poly", proc.poly ? 1.0f : 0.0f);
    if (proc.filterType >= 0)
        setChoiceParam(dut->getProcessor(), "filterType", proc.filterType);
    wired = connectSource(graph, *main, *dut, mainLegs, proc.poly);
    Node::Ptr envelope;
    if (std::string(proc.type) == "VCA") { // a VCA with nothing on its CV is shut
        envelope = add("ADSR");
        setIfPresent(*envelope->getProcessor(), "poly", proc.poly ? 1.0f : 0.0f);
        graph.addConnection({{trackIn->nodeID, kSilentMidi}, {envelope->nodeID, kSilentMidi}});
        for (int v = 0; v < (proc.poly ? 8 : 1); ++v) {
            if (polyMidi != nullptr)
                graph.addConnection({{polyMidi->nodeID, 8 + v}, {envelope->nodeID, v}});
            graph.addConnection({{envelope->nodeID, v}, {dut->nodeID, proc.poly ? 8 + v : 1}});
        }
    }

    if (how != SilentHow::None) {
        auto silent = makeSource(silentType, silentLegs);
        auto* module = dynamic_cast<ModuleBase*>(silent->getProcessor());
        if (how == SilentHow::LevelZero)
            setIfPresent(*module, "level", 0.0f);
        else if (how == SilentHow::Bypassed)
            module->setBypassed(true);
        else
            module->setMuted(true);
        wired = wired && connectSource(graph, *silent, *dut, silentLegs, proc.poly);
    }

    auto out = add("Audio Output");
    auto* dutModule = dynamic_cast<ModuleBase*>(dut->getProcessor());
    const int right = dutModule->rightAudioLegChannel();
    for (int v = 0; v < (proc.poly ? 8 : 1); ++v) {
        graph.addConnection({{dut->nodeID, v}, {out->nodeID, 0}});
        if (right > 0)
            graph.addConnection({{dut->nodeID, right + v}, {out->nodeID, 1}});
    }

    synth::TimelineDoc doc;
    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    doc.setTrackBinding(trackId, kSilentTrackUuid);
    const auto clip = doc.addClip(trackId, 0.0, 4.0, "Clip");
    for (const int i : {0, 1}) {
        synth::MidiNote note;
        note.startBeat = 0.05 * i;
        note.lengthBeats = 0.04;
        note.pitch = 57 + 7 * i;
        note.velocity = 100;
        doc.addNote(clip, note);
    }
    synth::OfflineTransportDriver driver(*engine, kSilentRate, kSilentBlock, 2);
    engine->publishTimeline(doc);
    driver.getTransport().play();
    const auto rendered = driver.renderBlocks(kSilentBlocks);
    juce::Array<float> samples;
    for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
        samples.addArray(rendered.getReadPointer(ch), rendered.getNumSamples());
    engine->releaseFromHost();
    engine->shutdown();
    return samples;
}

double rmsOf(const juce::Array<float>& v) {
    double sum = 0.0;
    for (const float x : v)
        sum += (double)x * (double)x;
    return v.isEmpty() ? 0.0 : std::sqrt(sum / v.size());
}

double maxDifference(const juce::Array<float>& a, const juce::Array<float>& b) {
    double most = 0.0;
    for (int i = 0; i < std::min(a.size(), b.size()); ++i)
        most = std::max(most, std::abs((double)a[i] - (double)b[i]));
    return most;
}

const char* howName(SilentHow how) {
    return how == SilentHow::LevelZero ? "Level 0" : how == SilentHow::Bypassed ? "bypassed" : "muted";
}

std::string processorName(const Processor& p) {
    std::string name = p.type;
    if (p.filterType >= 0)
        name += " type " + std::to_string(p.filterType);
    return name + (p.poly ? " poly" : "");
}

} // namespace

TEST(SilentSourceIsTransparent, EveryRegisteredModuleIsClassified) {
    std::set<std::string> classified = notProcessors();
    for (const auto& p : processors())
        classified.insert(p.type);
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames())
        EXPECT_EQ(classified.count(name.toStdString()), 1u)
            << name
            << " is a registered module this guard does not know: if it processes audio add it to "
               "processors(), otherwise to notProcessors()";
}

// Regression test for FRO1193: cabling a silent source next to a stereo pair lifted what came out of the module.
TEST(SilentSourceIsTransparent, ASilentCableChangesNothingDownstream) {
    struct Wiring {
        Legs main, silent;
        const char* name;
    };
    const std::vector<Wiring> wirings = {{Legs::One, Legs::One, "mono main, one silent leg"},
                                         {Legs::WholePair, Legs::One, "stereo pair main, one silent leg"},
                                         {Legs::One, Legs::WholePair, "mono main, silent pair"}};
    int mismatches = 0;
    for (const auto& proc : processors()) {
        for (const auto& wiring : wirings) {
            bool wired = true;
            const auto without = renderChain(proc, "Oscillator", SilentHow::None, wiring.main, wiring.silent, wired);
            for (const char* source : silentSources()) {
                for (const auto how : {SilentHow::LevelZero, SilentHow::Bypassed, SilentHow::Muted}) {
                    const auto with = renderChain(proc, source, how, wiring.main, wiring.silent, wired);
                    if (!wired)
                        continue; // this source has no right leg to cable as a pair
                    const double diff = maxDifference(without, with);
                    EXPECT_EQ(with.size(), without.size());
                    if (diff == 0.0)
                        continue;
                    ++mismatches;
                    ADD_FAILURE() << processorName(proc) << " + " << source << " " << howName(how) << " ("
                                  << wiring.name << "): RMS " << rmsOf(without) << " -> " << rmsOf(with)
                                  << ", max difference " << diff;
                }
            }
        }
    }
    std::cout << "[silent source] " << mismatches << " cases were not transparent" << std::endl;
}

// The guard above would pass on a chain that plays nothing, so each processor must actually carry the main source.
TEST(SilentSourceIsTransparent, TheChainsItComparesAreAudible) {
    for (const auto& proc : processors()) {
        bool wired = true;
        const auto out = renderChain(proc, "Oscillator", SilentHow::None, Legs::One, Legs::One, wired);
        EXPECT_GT(rmsOf(out), 1.0e-4) << processorName(proc);
    }
}
