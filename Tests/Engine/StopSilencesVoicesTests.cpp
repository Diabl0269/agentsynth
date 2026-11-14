// StopSilencesVoicesTests.cpp
//
// Guard for the live-reconfigure bug kind, transport-stop flavour: pressing Stop while a note is held must leave every
// instrument silent once its release is over. A timeline MIDI track holds one very long note into each instrument
// type below; the transport is stopped mid-note; the output after the release must be under -80 dB.
//
// Kinds: live-reconfigure

#include "../FakeAudioIODevice.h"
#include "../StubPluginInstance.h"
#include "../UniqueTempDir.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kTrackUuid = "5700c000-0000-0000-0000-000000000001";
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
constexpr double kNoteBeats = 32.0;
constexpr float kMinus80Db = 1.0e-4f;
constexpr float kAudible = 1.0e-3f;
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

// A synth that holds each note until its note-off (or CC 120/123), like a third-party instrument would.
class HoldingInstrument : public synth::test::StubPluginInstance {
public:
    HoldingInstrument()
        : StubPluginInstance(0, 2, "Holding Instrument") {}
    void prepareToPlay(double sampleRate, int) override {
        step_ = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        for (const auto metadata : midi) {
            const auto m = metadata.getMessage();
            if (m.isNoteOn())
                ++held_;
            else if (m.isNoteOff())
                held_ = std::max(0, held_ - 1);
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                held_ = 0;
        }
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float v = held_ > 0 ? 0.1f * (float)std::sin(phase_) : 0.0f;
            phase_ += step_;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, v);
        }
    }

private:
    int held_ = 0;
    double step_ = 0.0;
    double phase_ = 0.0;
};

struct Chain {
    std::string name;
    // Adds the instrument chain between the track's MIDI and the Audio Output; returns false when it cannot be built.
    std::function<bool(juce::AudioProcessorGraph&, juce::AudioProcessorGraph::Node::Ptr trackIn,
                       juce::AudioProcessorGraph::Node::Ptr out)>
        build;
    bool mustBeAudible = true;
};

void setParam(juce::AudioProcessor& processor, const char* id, float value) {
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (ranged->getParameterID() == id)
                ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
}

struct StopResult {
    bool built = false;
    float peakBeforeStop = 0.0f;
    float peakAfterRelease = 0.0f;
};

StopResult playThenStop(const Chain& chain) {
    StopResult result;
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    engine.getAudioClipStreamer().setPrefetchPausedForTest(true);
    engine.setAudioDeviceDisabled(true);
    engine.initialise();

    auto& graph = engine.getGraph();
    // initialise() loads the saved default preset, which may sound by itself; the chain under test must be alone.
    for (const auto& existing : graph.getNodes())
        graph.removeNode(existing->nodeID);
    auto trackIn = graph.addNode(synth::AIStateMapper::createModule("Track In"));
    auto out = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
    if (trackIn == nullptr || out == nullptr)
        return result;
    trackIn->properties.set("uuid", kTrackUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(trackIn->getProcessor()))
        module->setNodeUuid(kTrackUuid);
    if (!chain.build(graph, trackIn, out))
        return result;

    synth::TimelineDoc doc;
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Held");
    if (!doc.setTrackBinding(track, kTrackUuid))
        return result;
    const auto clip = doc.addClip(track, 0.0, kNoteBeats, "Clip");
    synth::MidiNote note;
    note.startBeat = 0.0;
    note.lengthBeats = kNoteBeats;
    note.pitch = 60;
    note.velocity = 100;
    if (!doc.addNote(clip, note).isValid())
        return result;

    synth::test::FakeAudioIODevice device(0, 2, 0, 0, kRate, kBlock);
    engine.audioDeviceAboutToStart(&device);
    engine.publishTimeline(doc);
    result.built = true;

    const auto block = [&] {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        float* outputs[] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, kBlock, {});
        return std::max(buffer.getMagnitude(0, kBlock), buffer.getMagnitude(1, kBlock));
    };

    auto& transport = engine.getTransport();
    transport.setLoop(0.0, kNoteBeats, true);
    transport.play();
    const int half_second = (int)(0.5 * kRate) / kBlock;
    for (int i = 0; i < half_second; ++i)
        result.peakBeforeStop = std::max(result.peakBeforeStop, block());

    transport.stop();
    // Give every release 1 s to finish, then listen to the next 200 ms.
    for (int i = 0; i < (int)(1.0 * kRate) / kBlock; ++i)
        block();
    for (int i = 0; i < (int)(0.2 * kRate) / kBlock; ++i)
        result.peakAfterRelease = std::max(result.peakAfterRelease, block());

    engine.audioDeviceStopped();
    engine.shutdown();
    return result;
}

// The project's own osc chain: Track In -> Poly MIDI -> poly Oscillator (unison 6) -> poly VCA <- poly ADSR.
bool buildPolyChain(juce::AudioProcessorGraph& graph, const char* oscillatorType, float release, float gateThreshold,
                    float velocity, juce::AudioProcessorGraph::Node::Ptr trackIn,
                    juce::AudioProcessorGraph::Node::Ptr out) {
    auto poly = graph.addNode(synth::AIStateMapper::createModule("Poly MIDI"));
    auto osc = graph.addNode(synth::AIStateMapper::createModule(oscillatorType));
    auto adsr = graph.addNode(synth::AIStateMapper::createModule("ADSR"));
    auto vca = graph.addNode(synth::AIStateMapper::createModule("VCA"));
    if (!poly || !osc || !adsr || !vca)
        return false;
    setParam(*osc->getProcessor(), "poly", 1.0f);
    setParam(*osc->getProcessor(), "unison", 6.0f);
    setParam(*adsr->getProcessor(), "poly", 1.0f);
    setParam(*adsr->getProcessor(), "release", release);
    setParam(*adsr->getProcessor(), "gateThreshold", gateThreshold);
    setParam(*adsr->getProcessor(), "velocity", velocity);
    setParam(*vca->getProcessor(), "poly", 1.0f);
    graph.addConnection({{trackIn->nodeID, kMidi}, {poly->nodeID, kMidi}});
    graph.addConnection({{trackIn->nodeID, kMidi}, {osc->nodeID, kMidi}});
    for (int v = 0; v < 8; ++v) {
        graph.addConnection({{poly->nodeID, v}, {osc->nodeID, v}});
        graph.addConnection({{poly->nodeID, 8 + v}, {osc->nodeID, OscillatorModule::kGateBase + v}});
        graph.addConnection({{poly->nodeID, 8 + v}, {adsr->nodeID, v}});
        graph.addConnection({{osc->nodeID, v}, {vca->nodeID, v}});
        graph.addConnection({{adsr->nodeID, v}, {vca->nodeID, 8 + v}});
        graph.addConnection({{vca->nodeID, v}, {out->nodeID, 0}});
    }
    return true;
}

// One MIDI-taking module straight to the output, mono wiring.
bool buildDirect(juce::AudioProcessorGraph& graph, const juce::String& type,
                 juce::AudioProcessorGraph::Node::Ptr trackIn, juce::AudioProcessorGraph::Node::Ptr out) {
    auto module = graph.addNode(synth::AIStateMapper::createModule(type));
    if (!module)
        return false;
    graph.addConnection({{trackIn->nodeID, kMidi}, {module->nodeID, kMidi}});
    for (int channel = 0; channel < 2; ++channel)
        graph.addConnection({{module->nodeID, channel}, {out->nodeID, channel}});
    return true;
}

std::vector<Chain> chains() {
    std::vector<Chain> all;
    // The reported case: a poly Oscillator with unison 6 behind a poly ADSR. Release 0.015 s is the 'fro1118 test'
    // value.
    all.push_back({"PolyOscillatorUnison6ShortRelease", [](auto& g, auto in, auto out) {
                       return buildPolyChain(g, "Oscillator", 0.015f, 0.5f, 0.0f, in, out);
                   }});
    all.push_back({"PolyOscillatorLongRelease", [](auto& g, auto in, auto out) {
                       return buildPolyChain(g, "Oscillator", 0.3f, 0.5f, 0.0f, in, out);
                   }});
    all.push_back({"PolyWavetable", [](auto& g, auto in, auto out) {
                       return buildPolyChain(g, "Wavetable", 0.015f, 0.5f, 0.0f, in, out);
                   }});
    all.push_back({"HostedPlugin", [](auto& g, auto in, auto out) {
                       auto hosted = std::make_unique<synth::HostedPluginModule>();
                       hosted->prepareToPlay(kRate, kBlock);
                       juce::PluginDescription description;
                       description.name = "Holding Instrument";
                       description.pluginFormatName = "VST3";
                       description.uniqueId = 0x57A;
                       description.deprecatedUid = 0x57A;
                       description.fileOrIdentifier = "/nonexistent/test/path/Holding.vst3";
                       static synth::test::StubBackend backend([] { return std::make_unique<HoldingInstrument>(); });
                       hosted->loadPlugin(description, backend);
                       const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                       while (!hosted->hasInstance() && std::chrono::steady_clock::now() < deadline)
                           juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
                       if (!hosted->hasInstance())
                           return false;
                       auto node = g.addNode(std::move(hosted));
                       g.addConnection({{in->nodeID, kMidi}, {node->nodeID, kMidi}});
                       for (int c = 0; c < 2; ++c)
                           g.addConnection({{node->nodeID, c}, {out->nodeID, c}});
                       return true;
                   }});
    // Every other MIDI-taking module in the catalog (Oscillator, Wavetable, Sampler, drum machine, Pluck, Mallet, ...).
    const juce::var catalog = juce::JSON::parse(synth::AIStateMapper::getModuleCatalogJson());
    if (const auto* modules = catalog.getProperty("modules", {}).getDynamicObject()) {
        for (const auto& entry : modules->getProperties()) {
            const juce::String type = entry.name.toString();
            auto probe = synth::AIStateMapper::createModule(type);
            // Poly MIDI and Hosted Plugin have their own chains above. A bare Oscillator, Wavetable or LFO has no gate:
            // it sounds until the VCA and envelope after it close, so only its chain above can fall silent.
            if (probe == nullptr || !probe->acceptsMidi() || type == "Poly MIDI" || type == "Hosted Plugin" ||
                type == "Oscillator" || type == "Wavetable" || type == "LFO")
                continue;
            all.push_back({"Catalog_" + type.removeCharacters(" ").toStdString(),
                           [type](auto& g, auto in, auto out) { return buildDirect(g, type, in, out); }, false});
        }
    }
    return all;
}

} // namespace

class StopSilencesVoices : public ::testing::TestWithParam<Chain> {};

// Regression test for FRO1427: Stop with a note held must leave every instrument silent after its release.
TEST_P(StopSilencesVoices, OutputIsBelowMinus80DbAfterStopMidNote) {
    const auto& chain = GetParam();
    const auto result = playThenStop(chain);
    ASSERT_TRUE(result.built) << chain.name;
    if (chain.mustBeAudible)
        EXPECT_GT(result.peakBeforeStop, kAudible)
            << chain.name << ": the held note was never heard, so the test proves nothing";
    EXPECT_LT(result.peakAfterRelease, kMinus80Db) << chain.name << " is still sounding after Stop";
}

INSTANTIATE_TEST_SUITE_P(EveryInstrument, StopSilencesVoices, ::testing::ValuesIn(chains()),
                         [](const ::testing::TestParamInfo<Chain>& info) { return info.param.name; });
