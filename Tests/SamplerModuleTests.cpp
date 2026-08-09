// SamplerModuleTests.cpp
// Unit tests for SamplerModule (issue #146 — Granular / Sample Player):
//   • registration + port/parameter surface
//   • file loading (success, missing file, unreadable file)
//   • Sample mode: playback at unity rate, pitch ratio, loop vs one-shot
//   • Granular mode: produces bounded audio, respects the gate
//   • trigger-gate precedence (CV > MIDI > free-run) and the connected-latch
//   • bypass/mute, CV pass-through channels
//   • getExtraState/setExtraState round trip, including the trusted-only rule in AIStateMapper
//   • SampleWaveformComponent::computePeaks (headless static helper)

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/UI/SampleWaveformComponent.h"
#include "TestAudioHelpers.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

constexpr double kRate = 44100.0;

// Writes a 32-bit PCM WAV into the temp directory. 32 bits keeps the round trip accurate enough for
// exact-value assertions (quantisation is ~5e-10).
juce::File writeTestWav(const juce::String& fileName, int numFrames, int numChannels, double sampleRate,
                        const std::function<float(int channel, int index)>& generator) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(fileName);
    file.deleteFile();

    juce::AudioBuffer<float> buffer(numChannels, numFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numFrames; ++i)
            buffer.setSample(ch, i, generator(ch, i));

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return {};

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int)numChannels, 32, {}, 0));
    if (writer == nullptr)
        return {};

    stream.release(); // writer owns the stream now
    writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
    writer.reset(); // flush
    return file;
}

// A 0 -> 0.5 linear ramp; every frame has a distinct value, so a rate assertion can name the exact
// source index the output must have come from.
juce::File writeRampWav(const juce::String& fileName, int numFrames) {
    return writeTestWav(fileName, numFrames, 1, kRate,
                        [numFrames](int, int i) { return 0.5f * (float)i / (float)(numFrames - 1); });
}

float rampValue(int index, int numFrames) { return 0.5f * (float)index / (float)(numFrames - 1); }

// Renders `numBlocks` blocks of `blockSize` into one buffer, optionally holding a CV/gate value on
// `cvChannel` for every block.
juce::AudioBuffer<float> render(SamplerModule& module, int numBlocks, int blockSize, int cvChannel = -1,
                                float cvValue = 0.0f, juce::MidiBuffer firstBlockMidi = {}) {
    juce::AudioBuffer<float> result(SamplerModule::kNumChannels, numBlocks * blockSize);
    result.clear();

    for (int b = 0; b < numBlocks; ++b) {
        juce::AudioBuffer<float> block(SamplerModule::kNumChannels, blockSize);
        block.clear();
        if (cvChannel >= 0)
            for (int i = 0; i < blockSize; ++i)
                block.setSample(cvChannel, i, cvValue);

        juce::MidiBuffer midi = (b == 0) ? firstBlockMidi : juce::MidiBuffer();
        module.processBlock(block, midi);

        for (int ch = 0; ch < SamplerModule::kNumChannels; ++ch)
            result.copyFrom(ch, b * blockSize, block, ch, 0, blockSize);
    }
    return result;
}

class SamplerModuleTest : public ::testing::Test {
protected:
    std::unique_ptr<SamplerModule> module;

    void SetUp() override {
        module = std::make_unique<SamplerModule>();
        module->prepareToPlay(kRate, 512);
    }

    juce::AudioParameterChoice* playMode() {
        return dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[1]);
    }
    juce::AudioParameterFloat* pitch() { return dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[2]); }
    juce::AudioParameterInt* rootNote() { return dynamic_cast<juce::AudioParameterInt*>(module->getParameters()[3]); }
    juce::AudioParameterBool* loop() { return dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[4]); }
    juce::AudioParameterFloat* start() { return dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[5]); }
    juce::AudioParameterFloat* grainSize() {
        return dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[6]);
    }
    juce::AudioParameterFloat* density() {
        return dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[7]);
    }
    juce::AudioParameterFloat* level() { return dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[9]); }
};

} // namespace

// ============================================================================
// Registration / surface
// ============================================================================

TEST_F(SamplerModuleTest, FactoryInitialisation) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Sampler);
    EXPECT_EQ(module->getName(), "Sampler");
    // bypassed, playMode, pitch, rootNote, loop, start, grainSize, density, spray, level, muted
    EXPECT_EQ(module->getParameters().size(), 11);
    EXPECT_EQ(module->getTotalNumInputChannels(), SamplerModule::kNumChannels);
    EXPECT_EQ(module->getTotalNumOutputChannels(), SamplerModule::kNumChannels);
    EXPECT_EQ(module->getVisibleInputPortCount(), SamplerModule::kNumChannels);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
}

TEST_F(SamplerModuleTest, DefaultParameterValues) {
    EXPECT_EQ(playMode()->getIndex(), 0); // Sample
    EXPECT_FLOAT_EQ(pitch()->get(), 0.0f);
    EXPECT_EQ(rootNote()->get(), 60);
    EXPECT_TRUE(loop()->get());
    EXPECT_FLOAT_EQ(start()->get(), 0.0f);
    EXPECT_FLOAT_EQ(level()->get(), 0.8f);
}

TEST_F(SamplerModuleTest, ModulationTargetsExcludeTriggerJack) {
    auto targets = module->getModulationTargets();
    EXPECT_EQ(targets.size(), 6u);
    for (const auto& t : targets)
        EXPECT_NE(t.channelIndex, SamplerModule::kTriggerCh) << "the gate jack must never be auto-attenuverted";

    EXPECT_FALSE(module->isAutoPromotableModTarget(SamplerModule::kTriggerCh));
    EXPECT_TRUE(module->isAutoPromotableModTarget(SamplerModule::kPitchCVCh));
}

TEST_F(SamplerModuleTest, PortRolesAndLabels) {
    EXPECT_EQ(module->mapInputChannel(SamplerModule::kTriggerCh).role, PortRole::Gate);
    EXPECT_EQ(module->mapInputChannel(SamplerModule::kPitchCVCh).role, PortRole::ModCV);
    EXPECT_EQ(module->mapOutputChannel(0).role, PortRole::Audio);
    EXPECT_EQ(module->getInputPortLabel(SamplerModule::kTriggerCh), "Trig");
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio L");
    EXPECT_EQ(module->getOutputPortLabel(1), "Audio R");
}

TEST_F(SamplerModuleTest, RegisteredInModuleFactory) {
    auto created = synth::AIStateMapper::createModule("Sampler");
    ASSERT_NE(created, nullptr);
    EXPECT_NE(dynamic_cast<SamplerModule*>(created.get()), nullptr);
}

// ============================================================================
// File loading
// ============================================================================

TEST_F(SamplerModuleTest, NoSampleLoadedProducesSilence) {
    EXPECT_TRUE(module->getSampleFilePath().isEmpty());
    EXPECT_TRUE(module->getSampleName().isEmpty());
    EXPECT_EQ(module->getSample(), nullptr);

    auto out = render(*module, 2, 512);
    EXPECT_TRUE(TestAudioHelpers::isSilent(out, 0));
    EXPECT_TRUE(TestAudioHelpers::isSilent(out, 1));
    EXPECT_FALSE(module->isPlaying());
}

TEST_F(SamplerModuleTest, LoadRejectsMissingFile) {
    auto missing = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("no-such-sample-146.wav");
    missing.deleteFile();
    EXPECT_FALSE(module->loadSampleFile(missing));
    EXPECT_TRUE(module->getSampleFilePath().isEmpty());
}

TEST_F(SamplerModuleTest, LoadRejectsUnreadableFile) {
    auto notAudio = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-not-audio-146.wav");
    notAudio.replaceWithText("this is definitely not a wav file");
    EXPECT_FALSE(module->loadSampleFile(notAudio));
    EXPECT_TRUE(module->getSampleFilePath().isEmpty());
    notAudio.deleteFile();
}

TEST_F(SamplerModuleTest, LoadWavPopulatesSample) {
    auto file = writeTestWav("sampler-stereo-146.wav", 2048, 2, kRate,
                             [](int ch, int i) { return (ch == 0 ? 0.4f : -0.4f) * (i % 2 == 0 ? 1.0f : -1.0f); });
    ASSERT_TRUE(file.existsAsFile());

    const int generationBefore = module->getSampleGeneration();
    ASSERT_TRUE(module->loadSampleFile(file));

    EXPECT_EQ(module->getSampleFilePath(), file.getFullPathName());
    EXPECT_EQ(module->getSampleName(), file.getFileName());
    EXPECT_GT(module->getSampleGeneration(), generationBefore);

    auto sample = module->getSample();
    ASSERT_NE(sample, nullptr);
    EXPECT_EQ(sample->audio.getNumSamples(), 2048);
    EXPECT_EQ(sample->audio.getNumChannels(), 2);
    EXPECT_DOUBLE_EQ(sample->sourceSampleRate, kRate);
    EXPECT_FALSE(sample->truncated);

    file.deleteFile();
}

TEST_F(SamplerModuleTest, FailedLoadKeepsPreviousSample) {
    auto good = writeRampWav("sampler-keep-146.wav", 1024);
    ASSERT_TRUE(module->loadSampleFile(good));

    auto missing = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gone-146.wav");
    missing.deleteFile();
    EXPECT_FALSE(module->loadSampleFile(missing));

    EXPECT_EQ(module->getSampleFilePath(), good.getFullPathName());
    ASSERT_NE(module->getSample(), nullptr);
    good.deleteFile();
}

TEST_F(SamplerModuleTest, ClearSampleReturnsToSilence) {
    auto file = writeRampWav("sampler-clear-146.wav", 1024);
    ASSERT_TRUE(module->loadSampleFile(file));
    module->clearSample();

    EXPECT_TRUE(module->getSampleFilePath().isEmpty());
    EXPECT_EQ(module->getSample(), nullptr);
    EXPECT_TRUE(TestAudioHelpers::isSilent(render(*module, 1, 512), 0));
    file.deleteFile();
}

// ============================================================================
// Sample mode playback
// ============================================================================

TEST_F(SamplerModuleTest, SampleModePlaysAtUnityRateOnBothChannels) {
    constexpr int kFrames = 4096;
    auto file = writeRampWav("sampler-unity-146.wav", kFrames);
    ASSERT_TRUE(module->loadSampleFile(file));
    level()->setValueNotifyingHost(1.0f); // remove the level scaling from the comparison

    auto out = render(*module, 1, 512);

    // The first kFadeSamples are the anti-click ramp; past it the output is the source verbatim.
    for (int i = SamplerModule::kFadeSamples; i < 512; ++i) {
        EXPECT_NEAR(out.getSample(0, i), rampValue(i, kFrames), 1e-4f) << "frame " << i;
        EXPECT_NEAR(out.getSample(1, i), rampValue(i, kFrames), 1e-4f) << "mono source must fan to both outputs";
    }
    file.deleteFile();
}

TEST_F(SamplerModuleTest, FadeInRampsRatherThanClicking) {
    auto file = writeTestWav("sampler-dc-146.wav", 4096, 1, kRate, [](int, int) { return 1.0f; });
    ASSERT_TRUE(module->loadSampleFile(file));
    level()->setValueNotifyingHost(1.0f);

    auto out = render(*module, 1, 512);

    EXPECT_LT(out.getSample(0, 0), 0.1f) << "a full-scale DC sample must not jump straight to 1.0";
    for (int i = 1; i < SamplerModule::kFadeSamples; ++i)
        EXPECT_GE(out.getSample(0, i), out.getSample(0, i - 1)) << "fade-in must be monotonic";
    EXPECT_NEAR(out.getSample(0, SamplerModule::kFadeSamples + 8), 1.0f, 1e-4f);
    file.deleteFile();
}

TEST_F(SamplerModuleTest, PitchParameterSetsPlaybackRate) {
    constexpr int kFrames = 8192;
    auto file = writeRampWav("sampler-pitch-146.wav", kFrames);
    ASSERT_TRUE(module->loadSampleFile(file));
    level()->setValueNotifyingHost(1.0f);
    pitch()->setValueNotifyingHost(pitch()->getNormalisableRange().convertTo0to1(12.0f)); // +1 octave == 2x
    ASSERT_FLOAT_EQ(pitch()->get(), 12.0f);

    auto out = render(*module, 1, 512);

    for (int i = SamplerModule::kFadeSamples; i < 512; ++i)
        EXPECT_NEAR(out.getSample(0, i), rampValue(2 * i, kFrames), 1e-4f) << "frame " << i;
    file.deleteFile();
}

TEST_F(SamplerModuleTest, MidiNoteTransposesRelativeToRootNote) {
    constexpr int kFrames = 8192;
    auto file = writeRampWav("sampler-midi-146.wav", kFrames);
    ASSERT_TRUE(module->loadSampleFile(file));
    level()->setValueNotifyingHost(1.0f);

    auto midi = TestAudioHelpers::createNoteOnMidi(72); // rootNote default 60 -> +12 semitones
    auto out = render(*module, 1, 512, /*cvChannel*/ -1, 0.0f, midi);

    for (int i = SamplerModule::kFadeSamples; i < 512; ++i)
        EXPECT_NEAR(out.getSample(0, i), rampValue(2 * i, kFrames), 1e-4f) << "frame " << i;
    file.deleteFile();
}

TEST_F(SamplerModuleTest, StartParameterOffsetsPlayhead) {
    constexpr int kFrames = 4096;
    auto file = writeRampWav("sampler-start-146.wav", kFrames);
    ASSERT_TRUE(module->loadSampleFile(file));
    level()->setValueNotifyingHost(1.0f);
    start()->setValueNotifyingHost(0.5f);
    ASSERT_FLOAT_EQ(start()->get(), 0.5f);

    auto out = render(*module, 1, 512);

    const int offset = (int)(0.5f * (float)(kFrames - 1));
    for (int i = SamplerModule::kFadeSamples; i < 512; ++i)
        EXPECT_NEAR(out.getSample(0, i), rampValue(offset + i, kFrames), 1e-4f) << "frame " << i;
    file.deleteFile();
}

TEST_F(SamplerModuleTest, OneShotStopsAtEndOfSample) {
    constexpr int kFrames = 512;
    auto file = writeTestWav("sampler-oneshot-146.wav", kFrames, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));
    loop()->setValueNotifyingHost(0.0f);
    ASSERT_FALSE(loop()->get());

    auto out = render(*module, 4, 512); // 2048 samples for a 512-frame one-shot

    EXPECT_GT(TestAudioHelpers::computeRMSInRange(out, 128, 400, 0), 0.1f) << "the one-shot must actually play";
    EXPECT_NEAR(TestAudioHelpers::computeRMSInRange(out, 1024, 2048, 0), 0.0f, 1e-6f)
        << "a non-looping sample must fall silent after its last frame";
    EXPECT_FALSE(module->isPlaying());
    file.deleteFile();
}

TEST_F(SamplerModuleTest, LoopKeepsPlayingPastEndOfSample) {
    constexpr int kFrames = 512;
    auto file = writeTestWav("sampler-loop-146.wav", kFrames, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));
    ASSERT_TRUE(loop()->get()); // default

    auto out = render(*module, 4, 512);

    EXPECT_GT(TestAudioHelpers::computeRMSInRange(out, 1024, 2048, 0), 0.1f)
        << "a looping sample must keep producing audio past its length";
    EXPECT_TRUE(module->isPlaying());
    file.deleteFile();
}

// ============================================================================
// Granular mode
// ============================================================================

TEST_F(SamplerModuleTest, GranularModeProducesBoundedAudio) {
    auto file = writeTestWav("sampler-granular-146.wav", 44100, 1, kRate, [](int, int i) {
        return 0.6f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * (float)i / (float)kRate);
    });
    ASSERT_TRUE(module->loadSampleFile(file));
    playMode()->setValueNotifyingHost(1.0f);
    ASSERT_EQ(playMode()->getIndex(), 1); // Granular
    density()->setValueNotifyingHost(density()->getNormalisableRange().convertTo0to1(40.0f));
    grainSize()->setValueNotifyingHost(grainSize()->getNormalisableRange().convertTo0to1(60.0f));

    auto out = render(*module, 8, 512);

    EXPECT_GT(TestAudioHelpers::computeRMS(out, 0), 0.01f) << "the grain cloud must make sound";
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            ASSERT_LE(std::abs(out.getSample(ch, i)), 1.0f) << "grain sum must stay inside [-1, 1]";
    file.deleteFile();
}

TEST_F(SamplerModuleTest, GranularModeStaysSilentWithoutASample) {
    playMode()->setValueNotifyingHost(1.0f);
    auto out = render(*module, 4, 512);
    EXPECT_TRUE(TestAudioHelpers::isSilent(out, 0));
}

TEST_F(SamplerModuleTest, GranularHighDensityDoesNotClipOrBlowUp) {
    auto file = writeTestWav("sampler-dense-146.wav", 22050, 1, kRate, [](int, int) { return 0.9f; });
    ASSERT_TRUE(module->loadSampleFile(file));
    playMode()->setValueNotifyingHost(1.0f);
    density()->setValueNotifyingHost(1.0f);   // 100 grains/sec
    grainSize()->setValueNotifyingHost(1.0f); // 500 ms grains -> maximum overlap

    auto out = render(*module, 8, 512);

    for (int i = 0; i < out.getNumSamples(); ++i) {
        ASSERT_TRUE(std::isfinite(out.getSample(0, i))) << "frame " << i;
        ASSERT_LE(std::abs(out.getSample(0, i)), 1.0f) << "frame " << i;
    }
    file.deleteFile();
}

// ============================================================================
// Gate / trigger behaviour
// ============================================================================

TEST_F(SamplerModuleTest, FreeRunsWhenNothingIsPatchedIn) {
    auto file = writeTestWav("sampler-freerun-146.wav", 4096, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));

    auto out = render(*module, 1, 512);
    EXPECT_GT(TestAudioHelpers::computeRMSInRange(out, 128, 512, 0), 0.1f)
        << "dropping a Sampler in and loading a file should make sound with no wiring";
    file.deleteFile();
}

TEST_F(SamplerModuleTest, TriggerCVGatesPlaybackOnceConnected) {
    auto file = writeTestWav("sampler-gate-146.wav", 44100, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));

    // Block 1: gate high on the trigger channel -> plays, and latches "a cable is connected".
    auto high = render(*module, 1, 512, SamplerModule::kTriggerCh, 1.0f);
    EXPECT_GT(TestAudioHelpers::computeRMSInRange(high, 128, 512, 0), 0.1f);

    // Block 2: gate low. Now that a cable is known to be there, a low gate must silence the module
    // rather than falling back to free-running.
    auto low = render(*module, 1, 512, SamplerModule::kTriggerCh, 0.0f);
    EXPECT_NEAR(TestAudioHelpers::computeRMSInRange(low, 128, 512, 0), 0.0f, 1e-6f);

    // Block 3: gate high again -> retriggers.
    auto again = render(*module, 1, 512, SamplerModule::kTriggerCh, 1.0f);
    EXPECT_GT(TestAudioHelpers::computeRMSInRange(again, 128, 512, 0), 0.1f);
    file.deleteFile();
}

TEST_F(SamplerModuleTest, GateRisingLateInABlockIsNotMistakenForAnUnpatchedJack) {
    auto file = writeTestWav("sampler-lategate-146.wav", 44100, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));

    // Gate stays low for the first 200 samples, then rises. A 64-sample probe would see only zeros,
    // conclude nothing is patched, and free-run the whole block from sample 0.
    juce::AudioBuffer<float> block(SamplerModule::kNumChannels, 512);
    block.clear();
    for (int i = 200; i < 512; ++i)
        block.setSample(SamplerModule::kTriggerCh, i, 1.0f);

    juce::MidiBuffer midi;
    module->processBlock(block, midi);

    EXPECT_NEAR(TestAudioHelpers::computeRMSInRange(block, 0, 190, 0), 0.0f, 1e-6f)
        << "nothing should sound before the gate rises";
    EXPECT_GT(TestAudioHelpers::computeRMSInRange(block, 300, 512, 0), 0.1f)
        << "the late rising edge must still trigger playback";
    file.deleteFile();
}

// ============================================================================
// Bypass / mute / channel hygiene
// ============================================================================

TEST_F(SamplerModuleTest, BypassAndMuteSilenceOutput) {
    auto file = writeTestWav("sampler-bypass-146.wav", 4096, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));

    module->setBypassed(true);
    EXPECT_TRUE(TestAudioHelpers::isSilent(render(*module, 1, 512), 0)) << "a source module clears on bypass";
    module->setBypassed(false);

    module->setMuted(true);
    EXPECT_TRUE(TestAudioHelpers::isSilent(render(*module, 1, 512), 0));
    module->setMuted(false);
    file.deleteFile();
}

TEST_F(SamplerModuleTest, CVInputChannelsDoNotLeakToOutput) {
    auto file = writeTestWav("sampler-cvleak-146.wav", 4096, 1, kRate, [](int, int) { return 0.5f; });
    ASSERT_TRUE(module->loadSampleFile(file));

    juce::AudioBuffer<float> block(SamplerModule::kNumChannels, 512);
    block.clear();
    for (int ch = 2; ch < SamplerModule::kNumChannels; ++ch)
        for (int i = 0; i < 512; ++i)
            block.setSample(ch, i, 0.75f);

    juce::MidiBuffer midi;
    module->processBlock(block, midi);

    for (int ch = 2; ch < SamplerModule::kNumChannels; ++ch)
        EXPECT_TRUE(TestAudioHelpers::isSilent(block, ch))
            << "CV channel " << ch << " must be a silent pass-through, not audio";
    file.deleteFile();
}

TEST_F(SamplerModuleTest, LevelCVScalesOutput) {
    auto file = writeTestWav("sampler-levelcv-146.wav", 4096, 1, kRate, [](int, int) { return 1.0f; });
    ASSERT_TRUE(module->loadSampleFile(file));
    level()->setValueNotifyingHost(0.0f);
    ASSERT_FLOAT_EQ(level()->get(), 0.0f);

    auto quiet = render(*module, 1, 512);
    EXPECT_NEAR(TestAudioHelpers::computeRMSInRange(quiet, 128, 512, 0), 0.0f, 1e-6f);

    auto loud = render(*module, 1, 512, SamplerModule::kLevelCVCh, 0.5f);
    EXPECT_NEAR(TestAudioHelpers::computeRMSInRange(loud, 128, 512, 0), 0.5f, 0.02f)
        << "level CV must add on top of the parameter";
    file.deleteFile();
}

TEST_F(SamplerModuleTest, ZeroChannelBufferIsSafe) {
    juce::AudioBuffer<float> empty(0, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(empty, midi));
}

// ============================================================================
// Non-parameter state round trip
// ============================================================================

TEST_F(SamplerModuleTest, ExtraStateCarriesTheLoadedFile) {
    EXPECT_TRUE(module->getExtraState().isVoid()) << "no sample loaded -> no JSON noise";

    auto file = writeRampWav("sampler-state-146.wav", 1024);
    ASSERT_TRUE(module->loadSampleFile(file));

    auto state = module->getExtraState();
    ASSERT_FALSE(state.isVoid());
    ASSERT_NE(state.getDynamicObject(), nullptr);
    EXPECT_EQ(state.getDynamicObject()->getProperty("sampleFile").toString(), file.getFullPathName());

    SamplerModule restored;
    restored.prepareToPlay(kRate, 512);
    restored.setExtraState(state);
    EXPECT_EQ(restored.getSampleFilePath(), file.getFullPathName());
    ASSERT_NE(restored.getSample(), nullptr);
    EXPECT_EQ(restored.getSample()->audio.getNumSamples(), 1024);

    file.deleteFile();
}

TEST_F(SamplerModuleTest, TrustedGraphJSONRestoresTheSample) {
    auto file = writeRampWav("sampler-graphstate-146.wav", 1024);

    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<SamplerModule>());
    ASSERT_NE(node, nullptr);
    ASSERT_TRUE(dynamic_cast<SamplerModule*>(node->getProcessor())->loadSampleFile(file));

    auto json = synth::AIStateMapper::graphToJSON(source);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));

    SamplerModule* restoredSampler = nullptr;
    for (auto* n : restored.getNodes())
        if (auto* s = dynamic_cast<SamplerModule*>(n->getProcessor()))
            restoredSampler = s;

    ASSERT_NE(restoredSampler, nullptr);
    EXPECT_EQ(restoredSampler->getSampleFilePath(), file.getFullPathName())
        << "undo/redo and preset load must not drop the loaded sample";
    file.deleteFile();
}

TEST_F(SamplerModuleTest, UntrustedPatchCannotNameAFileToOpen) {
    auto file = writeRampWav("sampler-untrusted-146.wav", 1024);

    // Exactly the shape a model could emit: a Sampler node carrying a "state" object.
    auto json = juce::JSON::parse(R"({
      "nodes": [{"id": 1, "type": "Sampler", "state": {"sampleFile": "PLACEHOLDER"}}],
      "connections": []
    })");
    ASSERT_TRUE(json.isObject());
    json.getDynamicObject()
        ->getProperty("nodes")
        .getArray()
        ->getReference(0)
        .getDynamicObject()
        ->getProperty("state")
        .getDynamicObject()
        ->setProperty("sampleFile", file.getFullPathName());

    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));

    SamplerModule* sampler = nullptr;
    for (auto* n : graph.getNodes())
        if (auto* s = dynamic_cast<SamplerModule*>(n->getProcessor()))
            sampler = s;

    ASSERT_NE(sampler, nullptr) << "the node itself is still legal to author";
    EXPECT_TRUE(sampler->getSampleFilePath().isEmpty())
        << "untrusted JSON must never make the app read a file it named";
    file.deleteFile();
}

// ============================================================================
// SampleWaveformComponent::computePeaks — pure static helper
// ============================================================================

TEST(SampleWaveformPaint, EmptyStateAndLoadedSampleBothRender) {
    SamplerModule sampler;
    sampler.prepareToPlay(kRate, 512);

    SampleWaveformComponent view(sampler);
    view.setSize(260, 80);

    juce::Image image(juce::Image::ARGB, 260, 80, true);
    {
        juce::Graphics g(image);
        EXPECT_NO_THROW(view.paint(g)) << "empty state (\"No sample loaded\") must render";
    }

    auto file = writeRampWav("sampler-paint-146.wav", 2048);
    ASSERT_TRUE(sampler.loadSampleFile(file));
    render(sampler, 1, 512); // advance the playhead so the position line is drawn
    ASSERT_TRUE(sampler.isPlaying());

    view.resized(); // rebuilds the peak cache at the current width
    EXPECT_NO_THROW(view.timerCallback());
    {
        juce::Graphics g(image);
        EXPECT_NO_THROW(view.paint(g));
    }

    // A repeat tick with nothing changed must be a no-op, not a repaint storm.
    EXPECT_NO_THROW(view.timerCallback());
    file.deleteFile();
}

TEST(SampleWaveformPaint, ZeroWidthComponentIsSafe) {
    SamplerModule sampler;
    sampler.prepareToPlay(kRate, 512);
    SampleWaveformComponent view(sampler);
    view.setSize(0, 0);

    juce::Image image(juce::Image::ARGB, 1, 1, true);
    juce::Graphics g(image);
    EXPECT_NO_THROW(view.paint(g));
    EXPECT_NO_THROW(view.timerCallback());
}

TEST(SamplerFormats, IsSupportedAudioFileAcceptsAudioAndRejectsEverythingElse) {
    // Extension check only — the file need not exist, since drag-hover must stay cheap.
    juce::File base = juce::File::getSpecialLocation(juce::File::tempDirectory);
    EXPECT_TRUE(SamplerModule::isSupportedAudioFile(base.getChildFile("kick.wav")));
    EXPECT_TRUE(SamplerModule::isSupportedAudioFile(base.getChildFile("KICK.WAV"))) << "must be case-insensitive";
    EXPECT_TRUE(SamplerModule::isSupportedAudioFile(base.getChildFile("loop.aiff")));

    EXPECT_FALSE(SamplerModule::isSupportedAudioFile(base.getChildFile("patch.json")));
    EXPECT_FALSE(SamplerModule::isSupportedAudioFile(base.getChildFile("notes.txt")));
    EXPECT_FALSE(SamplerModule::isSupportedAudioFile(base.getChildFile("no-extension")));
    EXPECT_FALSE(SamplerModule::isSupportedAudioFile(base));
}

TEST(SamplerFormats, WildcardCoversWav) {
    auto wildcard = SamplerModule::getSupportedFormatWildcard();
    EXPECT_FALSE(wildcard.isEmpty());
    EXPECT_TRUE(wildcard.containsIgnoreCase("*.wav")) << "the issue asks for .wav at minimum; got " << wildcard;
}

TEST(SampleWaveformPeaks, EmptyInputsReturnNoColumns) {
    juce::AudioBuffer<float> empty(1, 0);
    EXPECT_TRUE(SampleWaveformComponent::computePeaks(empty, 32).empty());

    juce::AudioBuffer<float> audio(1, 100);
    audio.clear();
    EXPECT_TRUE(SampleWaveformComponent::computePeaks(audio, 0).empty());
}

TEST(SampleWaveformPeaks, ColumnsCoverTheWholeBufferAndTrackExtremes) {
    juce::AudioBuffer<float> audio(1, 100);
    audio.clear();
    audio.setSample(0, 0, 1.0f);   // first column
    audio.setSample(0, 99, -1.0f); // last column

    auto peaks = SampleWaveformComponent::computePeaks(audio, 10);
    ASSERT_EQ(peaks.size(), 10u);
    EXPECT_FLOAT_EQ(peaks.front().getEnd(), 1.0f);
    EXPECT_FLOAT_EQ(peaks.back().getStart(), -1.0f);
    EXPECT_FLOAT_EQ(peaks[5].getStart(), 0.0f);
    EXPECT_FLOAT_EQ(peaks[5].getEnd(), 0.0f);
}

TEST(SampleWaveformPeaks, ChannelsAreAveraged) {
    juce::AudioBuffer<float> audio(2, 8);
    audio.clear();
    for (int i = 0; i < 8; ++i) {
        audio.setSample(0, i, 1.0f);
        audio.setSample(1, i, -1.0f); // opposite phase cancels to zero
    }

    auto peaks = SampleWaveformComponent::computePeaks(audio, 4);
    ASSERT_EQ(peaks.size(), 4u);
    for (const auto& p : peaks) {
        EXPECT_NEAR(p.getStart(), 0.0f, 1e-6f);
        EXPECT_NEAR(p.getEnd(), 0.0f, 1e-6f);
    }
}

TEST(SampleWaveformPeaks, MoreColumnsThanFramesStillProducesOneColumnEach) {
    juce::AudioBuffer<float> audio(1, 4);
    audio.clear();
    audio.setSample(0, 2, 0.5f);

    auto peaks = SampleWaveformComponent::computePeaks(audio, 16);
    ASSERT_EQ(peaks.size(), 16u);
    float maxSeen = 0.0f;
    for (const auto& p : peaks)
        maxSeen = juce::jmax(maxSeen, p.getEnd());
    EXPECT_FLOAT_EQ(maxSeen, 0.5f);
}
