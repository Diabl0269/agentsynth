// WavetableOscillatorModuleTests.cpp
// Unit tests for WavetableOscillatorModule:
//   • factory / parameter surface and port layout
//   • audio generation, position scanning, table selection
//   • anti-aliasing (mip selection) at high notes
//   • CV modulation in mono and poly mode
//   • wavetable file loading, rejection of bad input, and state round-trip
//   • mute / bypass contract (pure source module: both clear the output)

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/WavetableOscillatorModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

float rms(const juce::AudioBuffer<float>& buffer, int channel) {
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples();
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / (float)n);
}

/** Magnitude of a single frequency in a buffer, via a one-bin Goertzel-style DFT. */
float magnitudeAt(const juce::AudioBuffer<float>& buffer, int channel, float hz, double sampleRate) {
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples();
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 2.0 * juce::MathConstants<double>::pi * (double)hz * (double)i / sampleRate;
        re += data[i] * std::cos(w);
        im -= data[i] * std::sin(w);
    }
    return (float)(std::sqrt(re * re + im * im) * 2.0 / (double)n);
}

/** Renders `blocks` blocks of the module into a single concatenated buffer. */
juce::AudioBuffer<float> render(WavetableOscillatorModule& module, int numChannels, int blocks,
                                int blockSize = kBlockSize) {
    juce::AudioBuffer<float> out(numChannels, blocks * blockSize);
    out.clear();
    juce::AudioBuffer<float> block(numChannels, blockSize);
    for (int b = 0; b < blocks; ++b) {
        block.clear();
        juce::MidiBuffer midi;
        module.processBlock(block, midi);
        for (int ch = 0; ch < numChannels; ++ch)
            out.copyFrom(ch, b * blockSize, block, ch, 0, blockSize);
    }
    return out;
}

void setChoice(juce::AudioProcessor& module, const juce::String& paramID, int index) {
    for (auto* param : module.getParameters()) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
            if (choice->paramID == paramID) {
                choice->setValueNotifyingHost((float)index / (float)(choice->choices.size() - 1));
                return;
            }
        }
    }
    FAIL() << "no choice parameter named " << paramID;
}

void setFloat(juce::AudioProcessor& module, const juce::String& paramID, float value) {
    for (auto* param : module.getParameters()) {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param)) {
            if (f->paramID == paramID) {
                *f = value;
                return;
            }
        }
    }
    FAIL() << "no float parameter named " << paramID;
}

void setBool(juce::AudioProcessor& module, const juce::String& paramID, bool value) {
    for (auto* param : module.getParameters()) {
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param)) {
            if (b->paramID == paramID) {
                *b = value;
                return;
            }
        }
    }
    FAIL() << "no bool parameter named " << paramID;
}

void setInt(juce::AudioProcessor& module, const juce::String& paramID, int value) {
    for (auto* param : module.getParameters()) {
        if (auto* i = dynamic_cast<juce::AudioParameterInt*>(param)) {
            if (i->paramID == paramID) {
                *i = value;
                return;
            }
        }
    }
    FAIL() << "no int parameter named " << paramID;
}

/** Writes a WAV holding `numFrames` single-cycle frames of kFrameSize samples each.
    Frame f is a sine at harmonic (f + 1) so frames are trivially distinguishable. */
juce::File writeWavetableFile(const juce::String& name, int numFrames) {
    const int frameSize = WavetableOscillatorModule::kFrameSize;
    juce::AudioBuffer<float> buffer(1, numFrames * frameSize);
    buffer.clear();
    float* data = buffer.getWritePointer(0);
    for (int f = 0; f < numFrames; ++f) {
        const int harmonic = f + 1;
        for (int i = 0; i < frameSize; ++i) {
            const float phase = (float)i / (float)frameSize;
            data[f * frameSize + i] = 0.8f * std::sin(phase * (float)harmonic * juce::MathConstants<float>::twoPi);
        }
    }

    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return {};
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), kSampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release(); // writer owns it now
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    return file;
}

} // namespace

class WavetableOscillatorModuleTest : public ::testing::Test {
protected:
    std::unique_ptr<WavetableOscillatorModule> module;

    void SetUp() override {
        module = std::make_unique<WavetableOscillatorModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
    }
};

// ============================================================================
// Factory / parameter surface
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, FactoryInitialisation) {
    EXPECT_EQ(module->getModuleType(), ModuleType::Wavetable);
    EXPECT_EQ(module->getName(), "Wavetable");
    // Bypassed, Table, Position, Octave, Coarse, Fine, Level, Poly, Unison, Detune, Muted
    EXPECT_EQ(module->getParameters().size(), 11);
    EXPECT_EQ(module->getTotalNumInputChannels(), 13);
    EXPECT_EQ(module->getTotalNumOutputChannels(), 13);
    EXPECT_EQ(module->getVisibleInputPortCount(), 6);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::Oscillator);
}

TEST_F(WavetableOscillatorModuleTest, DeclaresEnoughOutputsForEveryCVInput) {
    // Guards the buffer-aliasing invariant: JUCE only makes a private copy of an input
    // channel when inputChan < getTotalNumOutputChannels().
    int highestCVChannel = 0;
    setBool(*module, "poly", true);
    for (const auto& target : module->getModulationTargets())
        highestCVChannel = std::max(highestCVChannel, target.channelIndex);
    EXPECT_LT(highestCVChannel, module->getTotalNumOutputChannels());
}

TEST_F(WavetableOscillatorModuleTest, PortLabelsAndModulationTargets) {
    EXPECT_EQ(module->getInputPortLabel(0), "Pitch");
    EXPECT_EQ(module->getInputPortLabel(1), "Position");
    EXPECT_EQ(module->getInputPortLabel(5), "Level");
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio");

    const auto mono = module->getModulationTargets();
    ASSERT_EQ(mono.size(), 6u);
    EXPECT_EQ(mono[1].name, "Position");
    EXPECT_EQ(mono[1].channelIndex, 1);

    setBool(*module, "poly", true);
    const auto poly = module->getModulationTargets();
    ASSERT_EQ(poly.size(), 5u);
    EXPECT_EQ(poly[0].name, "Position");
    EXPECT_EQ(poly[0].channelIndex, 8);
}

TEST_F(WavetableOscillatorModuleTest, LogicalPortMappingMonoAndPoly) {
    // Mono: raw 0-5 map straight onto the six visible jacks.
    for (int raw = 0; raw <= 5; ++raw) {
        const auto port = module->mapInputChannel(raw);
        EXPECT_EQ(port.visibleJackIndex, raw);
        EXPECT_EQ(port.polyVoiceSpan, 1);
    }

    setBool(*module, "poly", true);
    const auto head = module->mapInputChannel(0);
    EXPECT_EQ(head.visibleJackIndex, 0);
    EXPECT_EQ(head.role, PortRole::Pitch);
    EXPECT_TRUE(head.isPolyGroupHead);
    EXPECT_EQ(head.polyVoiceSpan, 8);

    for (int raw = 1; raw <= 7; ++raw) {
        const auto voice = module->mapInputChannel(raw);
        EXPECT_EQ(voice.visibleJackIndex, 0);
        EXPECT_FALSE(voice.isPolyGroupHead);
        EXPECT_EQ(voice.polyVoiceSpan, 1);
    }

    for (int raw = 8; raw <= 12; ++raw) {
        const auto cv = module->mapInputChannel(raw);
        EXPECT_EQ(cv.visibleJackIndex, raw - 7);
        EXPECT_EQ(cv.role, PortRole::ModCV);
        EXPECT_TRUE(cv.isPolyGroupHead);
    }

    // Poly CV connections stay plain DirectCV routings (no auto attenuverter).
    EXPECT_FALSE(module->isAutoPromotableModTarget(8));
    setBool(*module, "poly", false);
    EXPECT_TRUE(module->isAutoPromotableModTarget(1));
}

// ============================================================================
// Mip geometry
// ============================================================================

TEST(WavetableMipGeometry, LimitsDecreaseMonotonicallyToTheFundamental) {
    using WT = WavetableOscillatorModule;
    EXPECT_EQ(WT::mipHarmonicLimit(0), 1023);
    EXPECT_EQ(WT::mipHarmonicLimit(WT::kNumMips - 1), 1);

    for (int m = 1; m < WT::kNumMips; ++m) {
        EXPECT_LT(WT::mipHarmonicLimit(m), WT::mipHarmonicLimit(m - 1)) << "mip " << m << " must be coarser";
        // Every mip must be able to store its own harmonics without aliasing.
        EXPECT_LE(WT::mipHarmonicLimit(m), WT::mipLength(m) / 2 - 1) << "mip " << m << " exceeds its own Nyquist";
        EXPECT_GE(WT::mipLength(m), 64);
    }
}

// ============================================================================
// Audio generation
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, ZeroChannelsDoesNotCrash) {
    juce::AudioBuffer<float> buffer(0, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(WavetableOscillatorModuleTest, ProducesAudioOnChannelZero) {
    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f) << "default table should produce audio";

    for (int ch = 1; ch < 13; ++ch)
        EXPECT_NEAR(rms(out, ch), 0.0f, 1.0e-6f) << "channel " << ch << " must stay silent in mono mode";
}

TEST_F(WavetableOscillatorModuleTest, DefaultTablePositionZeroIsASine) {
    // "Basic Shapes" at position 0 is a pure sine: nearly all energy at the fundamental.
    const auto out = render(*module, 13, 8);
    const float fundamental = magnitudeAt(out, 0, 440.0f, kSampleRate);
    const float secondHarmonic = magnitudeAt(out, 0, 880.0f, kSampleRate);
    const float thirdHarmonic = magnitudeAt(out, 0, 1320.0f, kSampleRate);

    EXPECT_GT(fundamental, 0.5f);
    EXPECT_LT(secondHarmonic, fundamental * 0.05f);
    EXPECT_LT(thirdHarmonic, fundamental * 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, ScanningPositionChangesTheSpectrum) {
    const auto atZero = render(*module, 13, 8);
    const float sineThird = magnitudeAt(atZero, 0, 1320.0f, kSampleRate);

    // Position 1.0 of "Basic Shapes" is a square wave — strong odd harmonics.
    module = std::make_unique<WavetableOscillatorModule>();
    module->prepareToPlay(kSampleRate, kBlockSize);
    setFloat(*module, "position", 1.0f);
    const auto atOne = render(*module, 13, 8);
    const float squareThird = magnitudeAt(atOne, 0, 1320.0f, kSampleRate);

    EXPECT_GT(squareThird, sineThird * 10.0f) << "scanning to the square end must add harmonics";
    EXPECT_GT(rms(atOne, 0), 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, EveryBuiltInTableProducesAudio) {
    for (int table = 0; table < WavetableOscillatorModule::kNumBuiltIns; ++table) {
        auto osc = std::make_unique<WavetableOscillatorModule>();
        osc->prepareToPlay(kSampleRate, kBlockSize);
        setChoice(*osc, "table", table);
        setFloat(*osc, "position", 0.5f);
        const auto out = render(*osc, 13, 4);
        EXPECT_GT(rms(out, 0), 0.01f) << "built-in table " << table << " produced no audio";
        EXPECT_EQ(osc->getNumFrames(), WavetableOscillatorModule::kBuiltInFrames);
    }
}

TEST_F(WavetableOscillatorModuleTest, LoadedFileChoiceFallsBackWhenNothingIsLoaded) {
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_FALSE(module->hasLoadedWavetable());
    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f) << "should fall back to the first built-in, not go silent";
}

TEST_F(WavetableOscillatorModuleTest, LowSampleRateWithExtremeTuningStaysFinite) {
    // At 8 kHz with octave +4 and coarse +12 the note is ~14 kHz, i.e. more than one full
    // cycle per sample. Guards the phase wrap (`while`, not `if`) in renderVoice.
    module->prepareToPlay(8000.0, kBlockSize);
    setInt(*module, "octave", 4);
    setInt(*module, "coarse", 12);
    setInt(*module, "unison", 4);
    setFloat(*module, "detune", 40.0f);

    const auto out = render(*module, 13, 4);
    const float* data = out.getReadPointer(0);
    for (int i = 0; i < out.getNumSamples(); ++i) {
        ASSERT_TRUE(std::isfinite(data[i])) << "non-finite sample at " << i;
        ASSERT_LT(std::abs(data[i]), 2.0f) << "sample out of range at " << i;
    }
}

TEST_F(WavetableOscillatorModuleTest, OutputStaysBounded) {
    setChoice(*module, "table", 2); // Pulse — the most peaky built-in
    setFloat(*module, "position", 0.0f);
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 50.0f);
    const auto out = render(*module, 13, 8);
    EXPECT_LT(out.getMagnitude(0, 0, out.getNumSamples()), 2.0f) << "output must not blow up";
}

// ============================================================================
// Anti-aliasing
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, HighNotesDoNotAlias) {
    // A square wave (position 1.0) at MIDI 108 (~4186 Hz) has only 2 harmonics below
    // Nyquist. Without mip selection its 3rd harmonic upwards would fold back down into
    // the audible band. Assert the band below the fundamental stays clean.
    setFloat(*module, "position", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(13, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 108, 1.0f), 0);
    block.clear();
    module->processBlock(block, midi);

    const auto out = render(*module, 13, 8);
    const float fundamental = magnitudeAt(out, 0, 4186.0f, kSampleRate);
    ASSERT_GT(fundamental, 0.1f) << "the note itself must be audible";

    // Sweep the band below the fundamental — nothing should be generated there.
    float worstAlias = 0.0f;
    for (float hz = 200.0f; hz < 3500.0f; hz += 100.0f)
        worstAlias = std::max(worstAlias, magnitudeAt(out, 0, hz, kSampleRate));

    EXPECT_LT(worstAlias, fundamental * 0.05f) << "aliased energy below the fundamental: " << worstAlias;
}

// ============================================================================
// CV modulation
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, PositionCVScansTheTableInMonoMode) {
    // Baseline at position 0 (sine).
    const auto dry = render(*module, 13, 8);
    const float dryThird = magnitudeAt(dry, 0, 1320.0f, kSampleRate);

    // Full-scale CV on channel 1 pushes the scan to the square end of the table.
    auto osc = std::make_unique<WavetableOscillatorModule>();
    osc->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(13, 8 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i)
            block.getWritePointer(1)[i] = 1.0f; // Position CV
        juce::MidiBuffer midi;
        osc->processBlock(block, midi);
        out.copyFrom(0, b * kBlockSize, block, 0, 0, kBlockSize);
    }

    const float wetThird = magnitudeAt(out, 0, 1320.0f, kSampleRate);
    EXPECT_GT(wetThird, dryThird * 10.0f) << "position CV must scan the table";
}

TEST_F(WavetableOscillatorModuleTest, LevelCVAttenuatesInMonoMode) {
    const auto dry = render(*module, 13, 4);

    auto osc = std::make_unique<WavetableOscillatorModule>();
    osc->prepareToPlay(kSampleRate, kBlockSize);
    setFloat(*osc, "level", 1.0f);

    juce::AudioBuffer<float> block(13, kBlockSize);
    float wetRms = 0.0f;
    for (int b = 0; b < 4; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i)
            block.getWritePointer(5)[i] = -0.8f; // Level CV, negative
        juce::MidiBuffer midi;
        osc->processBlock(block, midi);
        wetRms = rms(block, 0);
    }

    EXPECT_LT(wetRms, rms(dry, 0) * 0.6f) << "negative level CV must attenuate";
}

TEST_F(WavetableOscillatorModuleTest, PolyModeRendersOneVoicePerPitchCVChannel) {
    setBool(*module, "poly", true);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(13, kBlockSize);
    const float voiceHz[3] = {220.0f, 330.0f, 440.0f};

    for (int b = 0; b < 4; ++b) {
        block.clear();
        for (int v = 0; v < 3; ++v)
            for (int i = 0; i < kBlockSize; ++i)
                block.getWritePointer(v)[i] = voiceHz[v];
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
    }

    for (int v = 0; v < 3; ++v)
        EXPECT_GT(rms(block, v), 0.05f) << "voice " << v << " should sound";
    for (int v = 3; v < 8; ++v)
        EXPECT_NEAR(rms(block, v), 0.0f, 1.0e-6f) << "voice " << v << " has no pitch CV and must stay silent";
    for (int ch = 8; ch < 13; ++ch)
        EXPECT_NEAR(rms(block, ch), 0.0f, 1.0e-6f) << "CV channel " << ch << " must not leak audio downstream";
}

TEST_F(WavetableOscillatorModuleTest, PolyModePositionCVScansAllVoices) {
    setBool(*module, "poly", true);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(13, 8 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i) {
            block.getWritePointer(0)[i] = 440.0f; // voice 0 pitch, Hz
            block.getWritePointer(8)[i] = 1.0f;   // shared Position CV
        }
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        out.copyFrom(0, b * kBlockSize, block, 0, 0, kBlockSize);
    }

    // Scanned to the square end: the third harmonic must be present.
    const float third = magnitudeAt(out, 0, 1320.0f, kSampleRate);
    const float fundamental = magnitudeAt(out, 0, 440.0f, kSampleRate);
    EXPECT_GT(third, fundamental * 0.1f);
}

TEST_F(WavetableOscillatorModuleTest, OctaveParameterTransposesInPolyMode) {
    setBool(*module, "poly", true);
    setInt(*module, "octave", 1);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(13, 8 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlockSize; ++i)
            block.getWritePointer(0)[i] = 440.0f;
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        out.copyFrom(0, b * kBlockSize, block, 0, 0, kBlockSize);
    }

    EXPECT_GT(magnitudeAt(out, 0, 880.0f, kSampleRate), 0.4f) << "one octave up should sound at 880 Hz";
    EXPECT_LT(magnitudeAt(out, 0, 440.0f, kSampleRate), 0.1f);
}

// ============================================================================
// File loading
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, LoadWavetableFileSplitsFrames) {
    const juce::File file = writeWavetableFile("agentsynth_wt_test_4frames.wav", 4);
    ASSERT_TRUE(file.existsAsFile());

    EXPECT_TRUE(module->loadWavetableFile(file));
    EXPECT_TRUE(module->hasLoadedWavetable());
    EXPECT_EQ(module->getWavetableFile(), file);

    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), 4);
    EXPECT_EQ(module->getWavetableName(), file.getFileNameWithoutExtension());

    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f) << "the loaded table should sound";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, LoadedFramesKeepTheirDistinctHarmonics) {
    // Frame 0 is the fundamental, frame 3 is the 4th harmonic.
    const juce::File file = writeWavetableFile("agentsynth_wt_test_harmonics.wav", 4);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);

    setFloat(*module, "position", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto first = render(*module, 13, 8);
    EXPECT_GT(magnitudeAt(first, 0, 440.0f, kSampleRate), 0.5f);

    auto osc = std::make_unique<WavetableOscillatorModule>();
    ASSERT_TRUE(osc->loadWavetableFile(file));
    setChoice(*osc, "table", WavetableOscillatorModule::kLoadedTableChoice);
    setFloat(*osc, "position", 1.0f);
    osc->prepareToPlay(kSampleRate, kBlockSize);
    const auto last = render(*osc, 13, 8);
    EXPECT_GT(magnitudeAt(last, 0, 1760.0f, kSampleRate), 0.5f) << "last frame is the 4th harmonic";
    EXPECT_LT(magnitudeAt(last, 0, 440.0f, kSampleRate), 0.1f);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, LoadWavetableFileRejectsMissingAndInvalidFiles) {
    const juce::File missing =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_wt_missing.wav");
    missing.deleteFile();
    EXPECT_FALSE(module->loadWavetableFile(missing));
    EXPECT_FALSE(module->hasLoadedWavetable());

    const juce::File garbage =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_wt_garbage.wav");
    garbage.deleteFile();
    garbage.replaceWithText("this is not audio data");
    EXPECT_FALSE(module->loadWavetableFile(garbage));
    EXPECT_FALSE(module->hasLoadedWavetable());

    // The module still plays its built-in table.
    const auto out = render(*module, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f);

    garbage.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, LoadWavetableFileCapsFrameCount) {
    // A file with more frames than kMaxFrames is decimated, not truncated.
    const juce::File file =
        writeWavetableFile("agentsynth_wt_test_many.wav", WavetableOscillatorModule::kMaxFrames + 8);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), WavetableOscillatorModule::kMaxFrames);
    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, ReloadingWhileRenderingStaysStable) {
    const juce::File a = writeWavetableFile("agentsynth_wt_reload_a.wav", 4);
    const juce::File b = writeWavetableFile("agentsynth_wt_reload_b.wav", 8);
    ASSERT_TRUE(a.existsAsFile());
    ASSERT_TRUE(b.existsAsFile());

    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    juce::AudioBuffer<float> block(13, kBlockSize);
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(module->loadWavetableFile((i % 2 == 0) ? a : b));
        block.clear();
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        EXPECT_GT(rms(block, 0), 0.01f) << "iteration " << i;
    }
    EXPECT_EQ(module->getNumFrames(), 8);

    a.deleteFile();
    b.deleteFile();
}

// ============================================================================
// State round-trip
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, StateRoundTripRestoresParametersAndWavetable) {
    const juce::File file = writeWavetableFile("agentsynth_wt_state.wav", 6);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));

    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    setFloat(*module, "position", 0.75f);
    setInt(*module, "unison", 4);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto restored = std::make_unique<WavetableOscillatorModule>();
    restored->setStateInformation(state.getData(), (int)state.getSize());
    restored->prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_TRUE(restored->hasLoadedWavetable());
    EXPECT_EQ(restored->getWavetableFile(), file);
    EXPECT_EQ(restored->getNumFrames(), 6);
    EXPECT_NEAR(restored->getScanPosition(), 0.75f, 1.0e-4f);

    const auto out = render(*restored, 13, 4);
    EXPECT_GT(rms(out, 0), 0.01f);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, StateRoundTripSurvivesAMissingWavetableFile) {
    const juce::File file = writeWavetableFile("agentsynth_wt_gone.wav", 4);
    ASSERT_TRUE(file.existsAsFile());
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);

    juce::MemoryBlock state;
    module->getStateInformation(state);
    file.deleteFile();

    auto restored = std::make_unique<WavetableOscillatorModule>();
    EXPECT_NO_THROW(restored->setStateInformation(state.getData(), (int)state.getSize()));
    restored->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_FALSE(restored->hasLoadedWavetable());

    // Falls back to a built-in rather than going silent.
    const auto out = render(*restored, 13, 4);
    EXPECT_GT(rms(out, 0), 0.05f);
}

// ============================================================================
// Graph-JSON round trip — the path presets and undo actually use
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, TrustedGraphJSONRestoresTheWavetable) {
    // getStateInformation is NOT what presets or undo use — AIStateMapper::graphToJSON
    // persists parameters plus getExtraState(). A wavetable path that only lived in the
    // binary ModuleState blob would be silently dropped on every preset load.
    const juce::File file = writeWavetableFile("agentsynth_wt_graphstate.wav", 4);
    ASSERT_TRUE(file.existsAsFile());

    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<WavetableOscillatorModule>());
    ASSERT_NE(node, nullptr);
    auto* sourceModule = dynamic_cast<WavetableOscillatorModule*>(node->getProcessor());
    ASSERT_NE(sourceModule, nullptr);
    ASSERT_TRUE(sourceModule->loadWavetableFile(file));
    setChoice(*sourceModule, "table", WavetableOscillatorModule::kLoadedTableChoice);

    const juce::var json = synth::AIStateMapper::graphToJSON(source);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));

    WavetableOscillatorModule* restoredModule = nullptr;
    for (auto* n : restored.getNodes())
        if (auto* w = dynamic_cast<WavetableOscillatorModule*>(n->getProcessor()))
            restoredModule = w;

    ASSERT_NE(restoredModule, nullptr);
    EXPECT_EQ(restoredModule->getWavetableFile(), file) << "undo/redo and preset load must not drop the wavetable";
    EXPECT_EQ(restoredModule->getNumFrames(), 4);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, UntrustedPatchCannotNameAWavetableFileToOpen) {
    // Same guard as the Sampler: a model-authored patch must never make the app read a
    // file it chose, so setExtraState is only reachable on the trusted path.
    const juce::File file = writeWavetableFile("agentsynth_wt_untrusted.wav", 4);
    ASSERT_TRUE(file.existsAsFile());

    juce::var json = juce::JSON::parse(R"({
      "nodes": [{"id": 1, "type": "Wavetable", "state": {"wavetableFile": "PLACEHOLDER"}}],
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
        ->setProperty("wavetableFile", file.getFullPathName());

    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));

    WavetableOscillatorModule* module = nullptr;
    for (auto* n : graph.getNodes())
        if (auto* w = dynamic_cast<WavetableOscillatorModule*>(n->getProcessor()))
            module = w;

    ASSERT_NE(module, nullptr) << "the node itself is still legal to author";
    EXPECT_FALSE(module->hasLoadedWavetable()) << "untrusted JSON must never make the app read a file it named";

    file.deleteFile();
}

// ============================================================================
// Mute / bypass contract
// ============================================================================

class WavetableMuteBypassTest
    : public WavetableOscillatorModuleTest
    , public ::testing::WithParamInterface<bool> {};

TEST_P(WavetableMuteBypassTest, OutputIsSilentWhenMutedOrBypassed) {
    // Warm up so there is real signal to silence.
    render(*module, 13, 2);

    if (GetParam())
        module->setMuted(true);
    else
        module->setBypassed(true);

    const auto out = render(*module, 13, 2);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        EXPECT_NEAR(rms(out, ch), 0.0f, 1.0e-9f) << "channel " << ch;
}

INSTANTIATE_TEST_SUITE_P(MuteAndBypass, WavetableMuteBypassTest, ::testing::Values(true, false));
