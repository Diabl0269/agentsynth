// LiveReconfigureTests.cpp
//
// The guard for the live-reconfigure bug kind: restarting the audio device (or a plugin host re-preparing the engine)
// while a song plays must sound like a short pause and one clean resume. The project loops; it has a MIDI track on a
// hosted plugin, a MIDI track on a Poly Oscillator and an audio clip, and it is restarted mid-note at every block size
// and rate below, in both host modes.
//
// The hosted plugin is the harsh case on purpose: its voices live through prepareToPlay, each note-on adds a voice,
// and only a note-off or a CC 120/123 ends one, which is how a third-party synth is free to behave.
//
// A second test walks every module in the catalog that takes MIDI: a source such as a Sequencer forgets its held notes
// when it is prepared, so a restart is only safe if every internal sink forgets its voices too.

#include "../FakeAudioIODevice.h"
#include "../Modules/Filter/AllocationCounter.h"
#include "../StubPluginInstance.h"
#include "../UniqueTempDir.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Timeline/AudioClipStreamer.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kPluginTrackUuid = "1ec00000-0000-0000-0000-000000000001";
constexpr const char* kPolyTrackUuid = "1ec00000-0000-0000-0000-000000000002";
constexpr const char* kAudioTrackUuid = "1ec00000-0000-0000-0000-000000000003";
constexpr const char* kClipAsset = "Audio/clip.wav";
constexpr double kLoopBeats = 8.0;
constexpr int kPadPitch = 48;
constexpr int kPulsePitch = 64;

// Beats where the restart lands: the pad and a pulse are both held, with no note edge within a block of 1024 at 44.1
// kHz.
const std::array<double, 3> kRestartBeats{2.1, 5.1, 10.1};

// What the plugin heard and what it is playing right now. A fixed table, so processBlock never allocates.
struct VoiceLog {
    std::array<int, 128> sounding{};
    std::array<int, 128> onsets{};
    int maxStack = 0;
    int resets = 0;

    int total() const {
        int n = 0;
        for (const int v : sounding)
            n += v;
        return n;
    }
};

// Plays one 220 Hz sine per sounding voice, and keeps its voices through prepareToPlay and releaseResources.
class StackingInstrument : public synth::test::StubPluginInstance {
public:
    explicit StackingInstrument(std::shared_ptr<VoiceLog> log)
        : StubPluginInstance(0, 2, "Stacking Instrument")
        , log_(std::move(log)) {}

    void prepareToPlay(double sampleRate, int) override {
        step_ = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
    }
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                auto& voices = log_->sounding[(std::size_t)message.getNoteNumber()];
                ++voices;
                ++log_->onsets[(std::size_t)message.getNoteNumber()];
                log_->maxStack = std::max(log_->maxStack, voices);
            } else if (message.isNoteOff()) {
                auto& voices = log_->sounding[(std::size_t)message.getNoteNumber()];
                voices = std::max(0, voices - 1);
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                log_->sounding.fill(0);
                ++log_->resets;
            }
        }
        const float gain = 0.05f * (float)log_->total();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float v = gain * (float)std::sin(phase_);
            phase_ += step_;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, v);
        }
    }

private:
    std::shared_ptr<VoiceLog> log_;
    double step_ = 0.0;
    double phase_ = 0.0;
};

void setParam(juce::AudioProcessor& processor, const char* id, float value) {
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (ranged->getParameterID() == id)
                ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
}

bool writeClip(const juce::File& file) {
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000.0, 2u, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    juce::AudioBuffer<float> audio(2, 48000 * 6);
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        const float v = 0.1f * std::sin((float)i * 0.02f);
        audio.setSample(0, i, v);
        audio.setSample(1, i, v);
    }
    return writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

struct Format {
    double rate;
    int block;
};

// The whole project, driven through the same entry points the device or the host uses.
class Song {
public:
    explicit Song(AudioEngine::HostMode hostMode)
        : mode(hostMode)
        , engine(hostMode)
        , log(std::make_shared<VoiceLog>())
        , backend([sharedLog = log] { return std::make_unique<StackingInstrument>(sharedLog); }) {}

    ~Song() {
        stopStream();
        engine.shutdown();
        bundle.deleteRecursively();
    }

    bool build() {
        engine.getAudioClipStreamer().setPrefetchPausedForTest(true);
        if (mode == AudioEngine::HostMode::Standalone)
            engine.setAudioDeviceDisabled(true);
        engine.initialise();
        if (!writeClip(bundle.getChildFile(kClipAsset)))
            return false;
        engine.getAudioClipStreamer().setAssetRoots(bundle, juce::File());
        return buildGraph() && buildTimeline();
    }

    void startStream(Format format) {
        current = format;
        if (mode == AudioEngine::HostMode::Hosted) {
            engine.prepareForHost(format.rate, format.block, 0, 2);
        } else {
            device = std::make_unique<synth::test::FakeAudioIODevice>(0, 2, 0, 0, format.rate, format.block);
            engine.audioDeviceAboutToStart(device.get());
        }
        running = true;
        if (!published) {
            engine.publishTimeline(doc);
            published = true;
        }
        engine.getAudioClipStreamer().pumpForTest();
    }

    void stopStream() {
        if (!running)
            return;
        running = false;
        if (mode == AudioEngine::HostMode::Hosted)
            engine.releaseFromHost();
        else
            engine.audioDeviceStopped();
    }

    // One callback. Returns the block's peak.
    float block() {
        engine.getAudioClipStreamer().pumpForTest();
        juce::AudioBuffer<float> buffer(2, current.block);
        buffer.clear();
        if (mode == AudioEngine::HostMode::Hosted) {
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);
        } else {
            float* outputs[] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};
            synth::test::AllocationScope allocations;
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, current.block, {});
            allocationsInLastCallback = allocations.count();
        }
        const float peak = buffer.getMagnitude(0, current.block);
        highestPeak = std::max(highestPeak, peak);
        return peak;
    }

    void playUntilBeat(double beat) {
        while (engine.getTransport().getPositionSnapshot().ppq + passesPlayed * kLoopBeats < beat) {
            const double before = engine.getTransport().getPositionSnapshot().ppq;
            block();
            if (engine.getTransport().getPositionSnapshot().ppq < before)
                ++passesPlayed;
        }
    }

    int polyVoices() const {
        const auto mask = poly->getActiveVoiceMask();
        int n = 0;
        for (int i = 0; i < 8; ++i)
            n += (mask >> i) & 1;
        return n;
    }

    const AudioEngine::HostMode mode;
    AudioEngine engine;
    std::shared_ptr<VoiceLog> log;
    synth::test::StubBackend backend;
    synth::TimelineDoc doc;
    PolyMidiModule* poly = nullptr;
    float highestPeak = 0.0f;
    int passesPlayed = 0;
    int allocationsInLastCallback = 0;

private:
    bool buildGraph() {
        auto& graph = engine.getGraph();
        const auto add = [&](const char* type) { return graph.addNode(synth::AIStateMapper::createModule(type)); };
        constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

        const auto addTrack = [&](const char* type, const char* uuid) {
            auto node = add(type);
            if (node != nullptr) {
                node->properties.set("uuid", uuid);
                if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                    module->setNodeUuid(uuid);
            }
            return node;
        };

        auto pluginIn = addTrack("Track In", kPluginTrackUuid);
        auto polyIn = addTrack("Track In", kPolyTrackUuid);
        auto clip = addTrack("Track Audio", kAudioTrackUuid);
        auto polyNode = add("Poly MIDI");
        auto osc = add("Oscillator");
        auto adsr = add("ADSR");
        auto vca = add("VCA");
        auto out = add("Audio Output");
        auto hosted = std::make_unique<synth::HostedPluginModule>();
        hosted->prepareToPlay(48000.0, 512);
        juce::PluginDescription description;
        description.name = "Stacking Instrument";
        description.pluginFormatName = "VST3";
        description.uniqueId = 0x51A;
        description.deprecatedUid = 0x51A;
        description.fileOrIdentifier = "/nonexistent/test/path/Stacking.vst3";
        hosted->loadPlugin(description, backend);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!hosted->hasInstance() && std::chrono::steady_clock::now() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        if (!hosted->hasInstance() || !pluginIn || !polyIn || !clip || !polyNode || !osc || !adsr || !vca || !out)
            return false;
        auto plugin = graph.addNode(std::move(hosted));

        poly = dynamic_cast<PolyMidiModule*>(polyNode->getProcessor());
        setParam(*osc->getProcessor(), "poly", 1.0f);
        setParam(*adsr->getProcessor(), "poly", 1.0f);
        setParam(*adsr->getProcessor(), "release", 0.015f);
        setParam(*vca->getProcessor(), "poly", 1.0f);
        setParam(*vca->getProcessor(), "gain", 0.5f);

        graph.addConnection({{pluginIn->nodeID, kMidi}, {plugin->nodeID, kMidi}});
        graph.addConnection({{polyIn->nodeID, kMidi}, {polyNode->nodeID, kMidi}});
        graph.addConnection({{polyIn->nodeID, kMidi}, {osc->nodeID, kMidi}});
        for (int v = 0; v < 8; ++v) {
            graph.addConnection({{polyNode->nodeID, v}, {osc->nodeID, v}});
            graph.addConnection({{polyNode->nodeID, 8 + v}, {osc->nodeID, OscillatorModule::kGateBase + v}});
            graph.addConnection({{polyNode->nodeID, 8 + v}, {adsr->nodeID, v}});
            graph.addConnection({{osc->nodeID, v}, {vca->nodeID, v}});
            graph.addConnection({{adsr->nodeID, v}, {vca->nodeID, 8 + v}});
            graph.addConnection({{vca->nodeID, v}, {out->nodeID, 0}});
        }
        for (int channel = 0; channel < 2; ++channel) {
            graph.addConnection({{plugin->nodeID, channel}, {out->nodeID, channel}});
            graph.addConnection({{clip->nodeID, channel}, {out->nodeID, channel}});
        }
        return true;
    }

    bool addNotes(const char* name, const char* uuid) {
        const auto track = doc.addTrack(synth::TrackKind::Midi, name);
        if (!doc.setTrackBinding(track, uuid))
            return false;
        const auto clip = doc.addClip(track, 0.0, kLoopBeats, "Clip");
        const auto addNote = [&](double start, double length, int pitch) {
            synth::MidiNote note;
            note.startBeat = start;
            note.lengthBeats = length;
            note.pitch = pitch;
            note.velocity = 100;
            return doc.addNote(clip, note).isValid();
        };
        if (!addNote(0.0, kLoopBeats - 0.5, kPadPitch))
            return false;
        for (int beat = 0; beat < (int)kLoopBeats; ++beat)
            if (!addNote((double)beat, 0.5, kPulsePitch))
                return false;
        return true;
    }

    bool buildTimeline() {
        if (!addNotes("Plugin", kPluginTrackUuid) || !addNotes("Poly", kPolyTrackUuid))
            return false;
        const auto audio = doc.addTrack(synth::TrackKind::Audio, "Audio");
        if (!doc.setTrackBinding(audio, kAudioTrackUuid))
            return false;
        const auto clip = doc.addClip(audio, 0.0, kLoopBeats, "Clip");
        return clip.isValid() && doc.setClipAsset(clip, kClipAsset, 0.0);
    }

    juce::File bundle = synth::test::makeUniqueTempDir("LiveReconfigureTest");
    std::unique_ptr<synth::test::FakeAudioIODevice> device;
    Format current{48000.0, 512};
    bool running = false;
    bool published = false;
};

float peakWithoutRestart(AudioEngine::HostMode mode, Format format) {
    auto song = std::make_unique<Song>(mode);
    EXPECT_TRUE(song->build());
    song->startStream(format);
    auto& transport = song->engine.getTransport();
    EXPECT_TRUE(transport.setLoop(0.0, kLoopBeats, true));
    EXPECT_TRUE(transport.play());
    song->playUntilBeat(2.0 * kLoopBeats);
    return song->highestPeak;
}

struct RestartCase {
    AudioEngine::HostMode mode;
    Format format;
};

std::string caseName(const RestartCase& c) {
    return std::string(c.mode == AudioEngine::HostMode::Hosted ? "Host" : "Device") + "_" +
           std::to_string((int)c.format.rate) + "_" + std::to_string(c.format.block);
}

std::vector<RestartCase> restartCases() {
    std::vector<RestartCase> cases;
    for (const auto mode : {AudioEngine::HostMode::Standalone, AudioEngine::HostMode::Hosted})
        for (const double rate : {44100.0, 48000.0})
            for (const int block : {64, 256, 441, 512, 1024})
                cases.push_back({mode, {rate, block}});
    return cases;
}

} // namespace

class LiveReconfigure : public ::testing::TestWithParam<RestartCase> {};

// Regression test for FRO1222: a buffer size change while playing left the held notes sounding in the plugin with no
// note-off to come, so each loop pass stacked another copy of the track and Stop could not silence them.
TEST_P(LiveReconfigure, RestartMidSongResumesOnceAtTheRightPlace) {
    const auto c = GetParam();
    const Format startFormat{48000.0, 512};

    auto song = std::make_unique<Song>(c.mode);
    ASSERT_TRUE(song->build());
    song->startStream(startFormat);
    auto& transport = song->engine.getTransport();
    ASSERT_TRUE(transport.setLoop(0.0, kLoopBeats, true));
    ASSERT_TRUE(transport.play());

    const Format formats[]{c.format, startFormat};
    int nextFormat = 0;
    for (const double beat : kRestartBeats) {
        song->playUntilBeat(beat);

        // The published position is the start of the last block; the song resumes where that block ended.
        const double beatBefore = transport.getCurrentBlockInfo().endPpq;
        const int pluginVoicesBefore = song->log->total();
        const int polyVoicesBefore = song->polyVoices();
        const int padOnsets = song->log->onsets[kPadPitch];
        const int pulseOnsets = song->log->onsets[kPulsePitch];
        EXPECT_EQ(pluginVoicesBefore, 2) << "at beat " << beat << ": the plugin should hold the pad and one pulse";
        EXPECT_EQ(polyVoicesBefore, 2) << "at beat " << beat;

        song->stopStream();
        song->startStream(formats[nextFormat++ % 2]);

        EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, beatBefore) << "the song must resume where it paused";
        EXPECT_TRUE(transport.getPositionSnapshot().playing);

        song->block();
        EXPECT_EQ(song->allocationsInLastCallback, 0) << "the callback that resumes the song must not allocate";
        EXPECT_DOUBLE_EQ(transport.getCurrentBlockInfo().startPpq, beatBefore);
        EXPECT_EQ(song->log->total(), pluginVoicesBefore)
            << "at beat " << beat << ": the plugin must play what it played before the restart, once";
        EXPECT_EQ(song->polyVoices(), polyVoicesBefore) << "at beat " << beat;
        EXPECT_EQ(song->log->onsets[kPadPitch], padOnsets + 1) << "a held note resumes with exactly one note-on";
        EXPECT_EQ(song->log->onsets[kPulsePitch], pulseOnsets + 1);
    }

    song->playUntilBeat(3.0 * kLoopBeats);
    EXPECT_LE(song->log->maxStack, 1) << "a pitch sounded more than once at a time: the track doubled";
    EXPECT_LE(song->highestPeak, 1.05f * peakWithoutRestart(c.mode, startFormat))
        << "a restart must never be louder than the same song played straight through";

    ASSERT_TRUE(transport.stop());
    song->block();
    EXPECT_EQ(song->log->total(), 0) << "Stop must release everything in its first block";
    const int blocksIn200Ms = (int)(0.2 * song->engine.getGraph().getSampleRate()) / startFormat.block + 1;
    for (int i = 0; i < blocksIn200Ms; ++i)
        song->block();
    EXPECT_LT(song->block(), 1.0e-3f) << "the whole project must be silent shortly after Stop";
}

INSTANTIATE_TEST_SUITE_P(EveryFormat, LiveReconfigure, ::testing::ValuesIn(restartCases()),
                         [](const ::testing::TestParamInfo<RestartCase>& info) { return caseName(info.param); });

// Regression test for FRO1222: a host that re-prepares without releasing first must behave like a device restart.
TEST(LiveReconfigureHost, RepreparingWithoutReleaseDoesNotDoubleTheTrack) {
    auto song = std::make_unique<Song>(AudioEngine::HostMode::Hosted);
    ASSERT_TRUE(song->build());
    song->startStream({48000.0, 512});
    auto& transport = song->engine.getTransport();
    ASSERT_TRUE(transport.setLoop(0.0, kLoopBeats, true));
    ASSERT_TRUE(transport.play());
    song->playUntilBeat(2.1);

    song->startStream({44100.0, 256});
    song->block();
    EXPECT_EQ(song->log->total(), 2);

    song->playUntilBeat(2.0 * kLoopBeats);
    EXPECT_LE(song->log->maxStack, 1);
    ASSERT_TRUE(transport.stop());
    song->block();
    EXPECT_EQ(song->log->total(), 0);
}

namespace {

float renderPeakAfterRestart(juce::AudioProcessor& module, bool holdNote, int numChannels) {
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;
    juce::AudioBuffer<float> buffer(numChannels, kBlock);

    module.prepareToPlay(kRate, kBlock);
    for (int i = 0; i < 20; ++i) {
        buffer.clear();
        juce::MidiBuffer midi;
        if (i == 0 && holdNote)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        module.processBlock(buffer, midi);
    }

    module.releaseResources();
    module.prepareToPlay(kRate, kBlock / 2);

    juce::AudioBuffer<float> after(numChannels, kBlock / 2);
    float peak = 0.0f;
    for (int i = 0; i < 40; ++i) {
        after.clear();
        juce::MidiBuffer none;
        module.processBlock(after, none);
        if (i >= 20) // past any restart ramp a module owns
            for (int channel = 0; channel < numChannels; ++channel)
                peak = std::max(peak, after.getMagnitude(channel, 0, after.getNumSamples()));
    }
    return peak;
}

} // namespace

// Regression test for FRO1222: a Sequencer forgets its held notes when prepared, so every internal MIDI sink has to
// forget its voices too, or the note it was holding has nobody left to release it. Walks the catalog, so a new
// instrument is covered the day it is added.
TEST(LiveReconfigureGuard, EveryMidiSinkInTheCatalogForgetsItsVoicesOnRestart) {
    const juce::var catalog = juce::JSON::parse(synth::AIStateMapper::getModuleCatalogJson());
    const auto* modules = catalog.getProperty("modules", {}).getDynamicObject();
    ASSERT_NE(modules, nullptr);

    int sinks = 0;
    for (const auto& entry : modules->getProperties()) {
        const juce::String type = entry.name.toString();
        auto held = synth::AIStateMapper::createModule(type);
        auto fresh = synth::AIStateMapper::createModule(type);
        if (held == nullptr || fresh == nullptr || !held->acceptsMidi())
            continue;
        ++sinks;
        const int channels = std::max({held->getTotalNumInputChannels(), held->getTotalNumOutputChannels(), 2});
        const float withNote = renderPeakAfterRestart(*held, true, channels);
        const float withoutNote = renderPeakAfterRestart(*fresh, false, channels);
        EXPECT_LE(withNote, withoutNote + 1.0e-3f)
            << type << " is still playing a note from before the restart, and no note-off will ever reach it";
    }
    EXPECT_GE(sinks, 3) << "the catalog walk found too few instruments to be testing anything";
}
