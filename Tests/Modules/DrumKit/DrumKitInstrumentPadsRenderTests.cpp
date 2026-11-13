// DrumKitInstrumentPadsRenderTests.cpp
// Every module the pad menu offers (DrumKitModule::canPlayFromPad), struck from a pad and rendered on its own with
// nothing between it and the output: what a user hears from the pad alone. The list comes from the module factory, so
// a module that joins the menu is covered the day it does.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SamplerMidiWiring.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>

using synth::drums::PadKind;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;
constexpr int kStrikePad = 0; // the default kit's pad 0 answers MIDI note 36

struct Rendered {
    float peak = 0.0f;
    int zeroCrossings = 0; // sign changes of channel 0 over the whole render
    int samples = 0;
};

// A graph with a kit whose pad 0 plays `module` at `tune` semitones, linked the way a project is (the module's uuid,
// then publishSamplerMidiWiring), struck once at full velocity (or auditioned from the card) and then rendered:
// the module alone, as the engine would, draining the pad's note in its own processBlock.
Rendered strikeAndRender(std::unique_ptr<ModuleBase> moduleOwner, float tune, int blocks, bool audition = false) {
    AudioEngine engine{AudioEngine::HostMode::Standalone};
    auto& graph = engine.getGraph();
    auto kitNode = graph.addNode(std::make_unique<DrumKitModule>());
    auto* module = moduleOwner.get();
    auto moduleNode = graph.addNode(std::move(moduleOwner));
    auto* kit = dynamic_cast<DrumKitModule*>(kitNode->getProcessor());
    kit->prepareToPlay(kRate, kBlock);
    module->prepareToPlay(kRate, kBlock);
    auto pad = kit->pad(kStrikePad);
    pad.kind = PadKind::Instrument;
    pad.instrumentUuid = synth::AIStateMapper::ensureNodeUuid(moduleNode.get());
    pad.instrumentPitch = 60;
    EXPECT_TRUE(kit->setPad(kStrikePad, pad));
    synth::publishSamplerMidiWiring(graph);
    EXPECT_TRUE(kit->padInstrumentLinked(kStrikePad));
    auto* tuneParam = kit->padParameter(kStrikePad, DrumKitModule::PadParam::Tune);
    tuneParam->setValueNotifyingHost(tuneParam->convertTo0to1(tune));

    juce::MidiBuffer hit;
    if (audition)
        kit->auditionPad(kStrikePad);
    else
        hit.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 0);
    juce::AudioBuffer<float> kitBuffer(2, kBlock);
    kitBuffer.clear();
    kit->processBlock(kitBuffer, hit);

    Rendered out;
    float previous = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        if (b > 0) { // the kit keeps running so an audition's timed Note-Off is sent
            kitBuffer.clear();
            juce::MidiBuffer none;
            kit->processBlock(kitBuffer, none);
        }
        juce::AudioBuffer<float> buffer(std::max(2, module->getTotalNumOutputChannels()), kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);
        out.peak = std::max(out.peak, buffer.getMagnitude(0, 0, kBlock));
        const auto* data = buffer.getReadPointer(0);
        for (int i = 0; i < kBlock; ++i) {
            if (data[i] == 0.0f)
                continue;
            if (previous != 0.0f && (previous < 0.0f) != (data[i] < 0.0f))
                ++out.zeroCrossings;
            previous = data[i];
        }
        out.samples += kBlock;
    }
    return out;
}

// A half-second 440 Hz tone on disk, for the Sampler (silent with no sample loaded, by design).
juce::File writeTone(const juce::File& dir) {
    auto file = dir.getChildFile("pad-tone.wav");
    juce::AudioBuffer<float> tone(1, 22050);
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float)i / 44100.0f));
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), 44100.0, 1, 16, {}, 0));
    EXPECT_NE(writer, nullptr);
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
    return file;
}

std::unique_ptr<ModuleBase> makeModule(const juce::String& typeName) {
    auto processor = synth::AIStateMapper::createModule(typeName);
    return std::unique_ptr<ModuleBase>(dynamic_cast<ModuleBase*>(processor.release()));
}

} // namespace

TEST(DrumKitInstrumentPadsRender, EveryModuleThePadMenuOffersSoundsWhenStruckOrAuditioned) {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("pad-render-tests");
    dir.createDirectory();
    int checked = 0;
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto probe = makeModule(name);
        if (probe == nullptr || !DrumKitModule::canPlayFromPad(probe->getModuleType()))
            continue;
        // Poly MIDI turns notes into pitch and gate voltages: its outputs are control signals for other modules, not
        // audio, so what it makes is checked in its own tests.
        if (probe->getModuleType() == ModuleType::PolyMidi)
            continue;
        ++checked;
        for (const bool audition : {false, true}) {
            auto module = makeModule(name);
            if (auto* sampler = dynamic_cast<SamplerModule*>(module.get()))
                ASSERT_TRUE(sampler->loadSampleFile(writeTone(dir)));
            const auto r = strikeAndRender(std::move(module), 0.0f, 40, audition);
            RecordProperty(name.toStdString() + (audition ? "_audition_peak" : "_peak"), std::to_string(r.peak));
            EXPECT_GT(r.peak, 0.01f) << name << (audition ? " (card audition)" : "")
                                     << " is in the pad menu but a strike makes no sound (peak " << r.peak << ")";
        }
    }
    EXPECT_GE(checked, 4);
    dir.deleteRecursively();
}

// Tune moves the pitch: +12 semitones doubles the cycles in the same render.
TEST(DrumKitInstrumentPadsRender, PadTuneMovesThePitchOfTheOscillatorAndWavetable) {
    for (const char* name : {"Oscillator", "Wavetable"}) {
        auto low = makeModule(name);
        auto high = makeModule(name);
        ASSERT_NE(low, nullptr) << name;
        ASSERT_TRUE(DrumKitModule::canPlayFromPad(low->getModuleType())) << name;
        const auto a = strikeAndRender(std::move(low), 0.0f, 40);
        const auto b = strikeAndRender(std::move(high), 12.0f, 40);
        RecordProperty(std::string(name) + "_crossings_tune0", std::to_string(a.zeroCrossings));
        RecordProperty(std::string(name) + "_crossings_tune12", std::to_string(b.zeroCrossings));
        ASSERT_GT(a.zeroCrossings, 10) << name;
        const double ratio = (double)b.zeroCrossings / (double)a.zeroCrossings;
        EXPECT_NEAR(ratio, 2.0, 0.2) << name << " crossings " << a.zeroCrossings << " -> " << b.zeroCrossings;
    }
}

namespace {

// The instrument a track builds: a MIDI source feeds the oscillator and the ADSR that gates it, and the oscillator
// runs through a VCA whose gain is the ADSR. A pad names the oscillator; what is heard is the VCA's output.
Rendered strikeGatedChain(const juce::String& oscillatorType, float tune, int blocks) {
    AudioEngine engine{AudioEngine::HostMode::Standalone};
    auto& graph = engine.getGraph();
    auto kitNode = graph.addNode(std::make_unique<DrumKitModule>());
    auto sourceNode = graph.addNode(synth::AIStateMapper::createModule("Sequencer"));
    auto oscNode = graph.addNode(synth::AIStateMapper::createModule(oscillatorType));
    auto adsrNode = graph.addNode(synth::AIStateMapper::createModule("ADSR"));
    auto* kit = dynamic_cast<DrumKitModule*>(kitNode->getProcessor());
    auto* osc = dynamic_cast<ModuleBase*>(oscNode->getProcessor());
    auto* adsr = dynamic_cast<ModuleBase*>(adsrNode->getProcessor());
    VCAModule vca;
    for (ModuleBase* m : {static_cast<ModuleBase*>(kit), osc, adsr, static_cast<ModuleBase*>(&vca)})
        m->prepareToPlay(kRate, kBlock);
    for (auto* node : {oscNode.get(), adsrNode.get()})
        EXPECT_TRUE(graph.addConnection({{sourceNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                         {node->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    auto pad = kit->pad(kStrikePad);
    pad.kind = PadKind::Instrument;
    pad.instrumentUuid = synth::AIStateMapper::ensureNodeUuid(oscNode.get());
    pad.instrumentPitch = 60;
    EXPECT_TRUE(kit->setPad(kStrikePad, pad));
    synth::publishSamplerMidiWiring(graph);
    EXPECT_TRUE(kit->padInstrumentLinked(kStrikePad));
    auto* tuneParam = kit->padParameter(kStrikePad, DrumKitModule::PadParam::Tune);
    tuneParam->setValueNotifyingHost(tuneParam->convertTo0to1(tune));

    juce::MidiBuffer hit;
    hit.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 0);
    juce::AudioBuffer<float> kitBuffer(2, kBlock);
    kitBuffer.clear();
    kit->processBlock(kitBuffer, hit);

    Rendered out;
    float previous = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> oscBuffer(std::max(2, osc->getTotalNumOutputChannels()), kBlock);
        juce::AudioBuffer<float> envBuffer(std::max(2, adsr->getTotalNumOutputChannels()), kBlock);
        oscBuffer.clear();
        envBuffer.clear();
        juce::MidiBuffer oscMidi, envMidi;
        osc->processBlock(oscBuffer, oscMidi);
        adsr->processBlock(envBuffer, envMidi);
        juce::AudioBuffer<float> vcaBuffer(VCAModule::kNumChannels, kBlock);
        vcaBuffer.clear();
        vcaBuffer.copyFrom(0, 0, oscBuffer, 0, 0, kBlock);
        vcaBuffer.copyFrom(1, 0, envBuffer, 0, 0, kBlock);
        juce::MidiBuffer none;
        vca.processBlock(vcaBuffer, none);
        out.peak = std::max(out.peak, vcaBuffer.getMagnitude(0, 0, kBlock));
        const auto* data = vcaBuffer.getReadPointer(0);
        for (int i = 0; i < kBlock; ++i) {
            if (data[i] == 0.0f)
                continue;
            if (previous != 0.0f && (previous < 0.0f) != (data[i] < 0.0f))
                ++out.zeroCrossings;
            previous = data[i];
        }
        out.samples += kBlock;
    }
    return out;
}

} // namespace

// The pad names the oscillator, but the ADSR that gates it is fed by the same MIDI source and has to hear the note
// too, or the VCA stays shut.
TEST(DrumKitInstrumentPadsRender, APadOnAnOscillatorOpensTheEnvelopeThatGatesItAtTheTunedPitch) {
    for (const char* type : {"Oscillator", "Wavetable"}) {
        const auto base = strikeGatedChain(type, 0.0f, 80);
        const auto up = strikeGatedChain(type, 12.0f, 80);
        RecordProperty(std::string(type) + "_chain_peak", std::to_string(base.peak));
        RecordProperty(std::string(type) + "_chain_crossings_tune0", std::to_string(base.zeroCrossings));
        RecordProperty(std::string(type) + "_chain_crossings_tune12", std::to_string(up.zeroCrossings));
        EXPECT_GT(base.peak, 0.01f) << type << ": the gated chain is silent when the pad is struck";
        ASSERT_GT(base.zeroCrossings, 10) << type;
        EXPECT_NEAR((double)up.zeroCrossings / (double)base.zeroCrossings, 2.0, 0.25) << type;
    }
}
