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
    using WT = WavetableOscillatorModule;
    EXPECT_EQ(module->getModuleType(), ModuleType::Wavetable);
    EXPECT_EQ(module->getName(), "Wavetable");
    // Bypassed, Table, Position, Octave, Coarse, Fine, Level, Poly, Unison, Detune, Warp,
    // Warp Amt, Phase, Rand Phase, Spread, Width, Blend, Stack, Sub, Sub Oct, Sub Wave, Pan,
    // Sync In, Import, Interp, Dual I/O (#219), Muted
    EXPECT_EQ(module->getParameters().size(), 27);
    EXPECT_EQ(module->getTotalNumInputChannels(), WT::kNumInputs);
    EXPECT_EQ(module->getTotalNumOutputChannels(), WT::kNumOutputs);
    EXPECT_EQ(module->getVisibleInputPortCount(), WT::kNumJacks);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2); // Audio L + Audio R
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::Oscillator);

    // The Audio R block has to clear the whole shared-CV block, or a stereo read would land
    // on a mod-CV input channel.
    EXPECT_GE(WT::kRightBase, WT::kNumInputs);
    EXPECT_GE(WT::kNumOutputs, WT::kRightBase + WT::kNumVoices);
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
    using WT = WavetableOscillatorModule;

    EXPECT_EQ(module->getInputPortLabel(WT::kJackPitch), "Pitch");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackPosition), "Position");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackLevel), "Level");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackWarp), "Warp");
    EXPECT_EQ(module->getInputPortLabel(WT::kJackSync), "Sync");
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio L");
    EXPECT_EQ(module->getOutputPortLabel(1), "Audio R");

    // Mono lists every jack including Pitch; poly drives pitch from the fan instead.
    const auto mono = module->getModulationTargets();
    ASSERT_EQ(mono.size(), (size_t)WT::kNumJacks);
    EXPECT_EQ(mono[WT::kJackPosition].name, "Position");
    EXPECT_EQ(mono[WT::kJackPosition].channelIndex, WT::kJackPosition);
    EXPECT_EQ(mono[WT::kJackWarp].name, "Warp");
    EXPECT_EQ(mono[WT::kJackWarp].channelIndex, WT::kJackWarp);

    setBool(*module, "poly", true);
    const auto poly = module->getModulationTargets();
    ASSERT_EQ(poly.size(), (size_t)WT::kNumModCV);
    EXPECT_EQ(poly[0].name, "Position");
    EXPECT_EQ(poly[0].channelIndex, WT::kPolyModCVBase);
    EXPECT_EQ(poly.back().name, "Sync");
    EXPECT_EQ(poly.back().channelIndex, WT::kNumInputs - 1);
}

// Channels that existed before issue #180 must not have moved: a patch saved against the old
// six-jack module routes by raw channel index, so shifting Octave/Coarse/Fine/Level would
// silently repoint every saved modulation.
TEST_F(WavetableOscillatorModuleTest, LegacyModCVChannelsKeepTheirIndices) {
    using WT = WavetableOscillatorModule;

    EXPECT_EQ(WT::kJackPitch, 0);
    EXPECT_EQ(WT::kJackPosition, 1);
    EXPECT_EQ(WT::kJackOctave, 2);
    EXPECT_EQ(WT::kJackCoarse, 3);
    EXPECT_EQ(WT::kJackFine, 4);
    EXPECT_EQ(WT::kJackLevel, 5);

    // Poly's shared block still starts at channel 8 with the same five entries in order.
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackPosition, true), 8);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackOctave, true), 9);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackCoarse, true), 10);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackFine, true), 11);
    EXPECT_EQ(WT::modCVChannelFor(WT::kJackLevel, true), 12);
}

TEST_F(WavetableOscillatorModuleTest, StereoOutputPortsMapToSeparateChannelBlocks) {
    using WT = WavetableOscillatorModule;

    // Mono: one channel per leg, neither of which is a poly head spanning voices.
    const auto monoL = module->mapOutputChannel(0);
    EXPECT_EQ(monoL.visibleJackIndex, 0);
    EXPECT_EQ(monoL.role, PortRole::Audio);
    EXPECT_EQ(monoL.polyVoiceSpan, 1);

    const auto monoR = module->mapOutputChannel(WT::kRightBase);
    EXPECT_EQ(monoR.visibleJackIndex, 1);
    EXPECT_EQ(monoR.role, PortRole::Audio);

    setBool(*module, "poly", true);

    // Poly: both legs fan eight wide, so the poly-bus collapse in AudioEngine can carry a
    // stereo unison stack into the Voice Mixer as two cables rather than sixteen.
    const auto polyL = module->mapOutputChannel(0);
    EXPECT_TRUE(polyL.isPolyGroupHead);
    EXPECT_EQ(polyL.polyVoiceSpan, WT::kNumVoices);

    const auto polyR = module->mapOutputChannel(WT::kRightBase);
    EXPECT_EQ(polyR.visibleJackIndex, 1);
    EXPECT_TRUE(polyR.isPolyGroupHead);
    EXPECT_EQ(polyR.polyVoiceSpan, WT::kNumVoices);

    for (int v = 1; v < WT::kNumVoices; ++v) {
        EXPECT_FALSE(module->mapOutputChannel(v).isPolyGroupHead);
        EXPECT_FALSE(module->mapOutputChannel(WT::kRightBase + v).isPolyGroupHead);
        EXPECT_EQ(module->mapOutputChannel(WT::kRightBase + v).visibleJackIndex, 1);
    }

    // A channel between the two audio blocks is a silent pass-through, never a bus head.
    EXPECT_FALSE(module->mapOutputChannel(WT::kPolyModCVBase).isPolyGroupHead);
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

// ============================================================================
// Warp modes (issue #180 phase 2 / 4)
// ============================================================================

namespace {

constexpr int kCh = WavetableOscillatorModule::kNumOutputs;

/** Names in the same order as the "warp" choice parameter, for readable failures. */
const char* const kWarpNames[] = {"Off",  "Sync",   "Bend +",   "Bend -", "PWM",    "Asym",
                                  "Flip", "Mirror", "Quantize", "Remap",  "Formant"};

/** Sends a note-on, then renders `blocks` further blocks and returns those. The note-on block
    itself is discarded — it carries the parameter smoothers' ramp-in. */
juce::AudioBuffer<float> renderNote(WavetableOscillatorModule& module, int midiNote, int blocks = 8) {
    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 1.0f), 0);
    block.clear();
    module.processBlock(block, midi);
    return render(module, kCh, blocks);
}

/** Renders exactly the note-on block, so sample 0 is the first sample after the retrigger. */
juce::AudioBuffer<float> renderAttack(WavetableOscillatorModule& module, int midiNote) {
    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 1.0f), 0);
    block.clear();
    module.processBlock(block, midi);
    return block;
}

/** Loudest bin in a swept band. */
float peakInBand(const juce::AudioBuffer<float>& buffer, int channel, float fromHz, float toHz, float stepHz) {
    float peak = 0.0f;
    for (float hz = fromHz; hz <= toHz; hz += stepHz)
        peak = std::max(peak, magnitudeAt(buffer, channel, hz, kSampleRate));
    return peak;
}

} // namespace

class WavetableWarpAliasTest
    : public WavetableOscillatorModuleTest
    , public ::testing::WithParamInterface<int> {};

// The anti-aliasing guarantee is the module's contract, and a warp is exactly the thing that can
// break it: warps run AFTER mip selection, so they can put back the harmonics the pyramid exists
// to remove. Every mode is swept at full warp on a high note. Nothing legitimate lives below the
// fundamental for any of these modes — they all reweight or multiply harmonics of f0 — so any
// energy down there is folded.
TEST_P(WavetableWarpAliasTest, EveryWarpModeStaysCleanBelowTheFundamental) {
    constexpr float kNoteHz = 4186.0f; // MIDI 108
    const int warpIndex = GetParam();

    setFloat(*module, "position", 1.0f); // squarest frame — the most harmonics to fold
    setChoice(*module, "warp", warpIndex);
    setFloat(*module, "warpAmount", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 108);

    // Legitimate content: the fundamental and its harmonics, all at or above kNoteHz.
    const float signal = peakInBand(out, 0, kNoteHz * 0.95f, 20000.0f, 100.0f);
    ASSERT_GT(signal, 0.01f) << kWarpNames[warpIndex] << " produced no audible signal";

    // Folded content lands below the fundamental.
    const float alias = peakInBand(out, 0, 300.0f, kNoteHz * 0.85f, 100.0f);

    EXPECT_LT(alias, signal * 0.05f) << "warp \"" << kWarpNames[warpIndex] << "\" aliases: " << alias << " vs signal "
                                     << signal;
}

INSTANTIATE_TEST_SUITE_P(AllWarpModes, WavetableWarpAliasTest,
                         ::testing::Range(0, (int)WavetableOscillatorModule::Warp::Count));

class WavetableWarpBoundsTest
    : public WavetableOscillatorModuleTest
    , public ::testing::WithParamInterface<int> {};

TEST_P(WavetableWarpBoundsTest, EveryWarpModeStaysBounded) {
    setChoice(*module, "warp", GetParam());
    setFloat(*module, "warpAmount", 1.0f);
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 50.0f);
    setFloat(*module, "subLevel", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 60);
    for (int ch : {0, WavetableOscillatorModule::kRightBase}) {
        const float peak = out.getMagnitude(ch, 0, out.getNumSamples());
        EXPECT_LT(peak, 4.0f) << kWarpNames[GetParam()] << " channel " << ch << " blew up";
        EXPECT_TRUE(std::isfinite(peak)) << kWarpNames[GetParam()] << " produced a non-finite sample";
    }
}

INSTANTIATE_TEST_SUITE_P(AllWarpModes, WavetableWarpBoundsTest,
                         ::testing::Range(0, (int)WavetableOscillatorModule::Warp::Count));

TEST_F(WavetableOscillatorModuleTest, WarpAmountZeroLeavesTheWaveAlone) {
    setFloat(*module, "position", 0.6f);
    setChoice(*module, "warp", (int)WavetableOscillatorModule::Warp::Asym);
    setFloat(*module, "warpAmount", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto warped = renderNote(*module, 60, 4);

    auto plain = std::make_unique<WavetableOscillatorModule>();
    setFloat(*plain, "position", 0.6f);
    plain->prepareToPlay(kSampleRate, kBlockSize);
    const auto reference = renderNote(*plain, 60, 4);

    for (int i = 0; i < reference.getNumSamples(); i += 37)
        ASSERT_NEAR(warped.getReadPointer(0)[i], reference.getReadPointer(0)[i], 1.0e-5f) << "sample " << i;
}

TEST_F(WavetableOscillatorModuleTest, WarpChangesTheSpectrum) {
    // A sine frame warped by Bend gains harmonics it did not have.
    setFloat(*module, "position", 0.0f); // pure sine
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto clean = renderNote(*module, 60, 4);
    const float cleanSecond = magnitudeAt(clean, 0, 523.3f, kSampleRate); // 2nd harmonic of C4

    setChoice(*module, "warp", (int)WavetableOscillatorModule::Warp::BendPlus);
    setFloat(*module, "warpAmount", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto bent = renderNote(*module, 60, 4);
    const float bentSecond = magnitudeAt(bent, 0, 523.3f, kSampleRate);

    EXPECT_GT(bentSecond, cleanSecond * 4.0f) << "Bend + must add harmonics to a sine";
}

TEST_F(WavetableOscillatorModuleTest, WarpAmountCVDrivesTheWarp) {
    using WT = WavetableOscillatorModule;
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "warp", (int)WT::Warp::BendPlus);
    setFloat(*module, "warpAmount", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    // Warm up on a note, then feed full-scale CV into the Warp jack.
    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    block.clear();
    module->processBlock(block, midi);

    juce::AudioBuffer<float> out(kCh, 4 * kBlockSize);
    out.clear();
    for (int b = 0; b < 4; ++b) {
        block.clear();
        juce::FloatVectorOperations::fill(block.getWritePointer(WT::kJackWarp), 1.0f, kBlockSize);
        juce::MidiBuffer empty;
        module->processBlock(block, empty);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    EXPECT_GT(magnitudeAt(out, 0, 523.3f, kSampleRate), 0.02f)
        << "CV on the Warp jack must warp the read even with the knob at zero";
}

// ============================================================================
// Phase control (issue #180 phase 1)
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, RetriggerPhaseStartsTheWaveWhereAsked) {
    setFloat(*module, "position", 0.0f); // sine: phase is directly readable from sample 0

    // Phase 0 starts at the sine's zero crossing, rising.
    setFloat(*module, "phase", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_NEAR(renderAttack(*module, 60).getReadPointer(0)[0], 0.0f, 0.02f);

    // A quarter turn starts at the positive peak instead.
    setFloat(*module, "phase", 90.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_GT(renderAttack(*module, 60).getReadPointer(0)[0], 0.8f);

    // Three quarters starts at the negative peak.
    setFloat(*module, "phase", 270.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_LT(renderAttack(*module, 60).getReadPointer(0)[0], -0.8f);
}

TEST_F(WavetableOscillatorModuleTest, RandomPhaseDecorrelatesRepeatedNotes) {
    setFloat(*module, "position", 0.0f);
    setFloat(*module, "randomPhase", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    // Without randomisation every note-on would start at exactly the same sample value.
    std::vector<float> firstSamples;
    for (int n = 0; n < 6; ++n)
        firstSamples.push_back(renderAttack(*module, 60).getReadPointer(0)[0]);

    float spread = 0.0f;
    for (float v : firstSamples)
        spread = std::max(spread, std::abs(v - firstSamples[0]));

    EXPECT_GT(spread, 0.1f) << "random phase must vary the attack between notes";
}

TEST_F(WavetableOscillatorModuleTest, UnisonSpreadDecorrelatesTheStack) {
    // Eight phase-correlated sines at the same pitch sum to 8x one sine; spreading their start
    // phases around the cycle makes them cancel instead. Detune stays at 0 so the ONLY
    // difference between the two renders is the start phase.
    setFloat(*module, "position", 0.0f);
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 0.0f);
    setFloat(*module, "spread", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const float correlated = rms(renderNote(*module, 60, 2), 0);

    setFloat(*module, "spread", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const float spread = rms(renderNote(*module, 60, 2), 0);

    EXPECT_LT(spread, correlated * 0.5f) << "spreading unison start phases must break the pile-up";
}

// ============================================================================
// Stereo, pan and voicing (issue #180 phase 3)
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, DefaultsKeepAudioLIdenticalToAudioR) {
    // Width 0 / pan 0 is mono-compatible: both jacks carry the same signal at full level, so a
    // patch that only cables Audio L sounds exactly as it did before the module went stereo.
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto out = renderNote(*module, 60, 2);

    const float left = rms(out, 0);
    ASSERT_GT(left, 0.05f);
    EXPECT_NEAR(rms(out, WavetableOscillatorModule::kRightBase), left, 1.0e-6f);
}

TEST_F(WavetableOscillatorModuleTest, UnisonWidthSeparatesTheStereoLegs) {
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 30.0f);
    setFloat(*module, "width", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto centred = renderNote(*module, 60, 2);
    const int R = WavetableOscillatorModule::kRightBase;

    // At width 0 the legs are identical, so their difference is silence.
    float centredDiff = 0.0f;
    for (int i = 0; i < centred.getNumSamples(); ++i)
        centredDiff = std::max(centredDiff, std::abs(centred.getReadPointer(0)[i] - centred.getReadPointer(R)[i]));
    EXPECT_LT(centredDiff, 1.0e-5f);

    setFloat(*module, "width", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto wide = renderNote(*module, 60, 2);

    float wideDiff = 0.0f;
    for (int i = 0; i < wide.getNumSamples(); ++i)
        wideDiff = std::max(wideDiff, std::abs(wide.getReadPointer(0)[i] - wide.getReadPointer(R)[i]));
    EXPECT_GT(wideDiff, 0.05f) << "width must place the detuned unison voices differently in L and R";
}

TEST_F(WavetableOscillatorModuleTest, PanShiftsEnergyBetweenTheLegs) {
    const int R = WavetableOscillatorModule::kRightBase;

    setFloat(*module, "pan", -1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto left = renderNote(*module, 60, 2);
    EXPECT_GT(rms(left, 0), 0.05f);
    EXPECT_NEAR(rms(left, R), 0.0f, 1.0e-6f);

    setFloat(*module, "pan", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto right = renderNote(*module, 60, 2);
    EXPECT_NEAR(rms(right, 0), 0.0f, 1.0e-6f);
    EXPECT_GT(rms(right, R), 0.05f);
}

TEST_F(WavetableOscillatorModuleTest, PolyModeWritesBothAudioBlocks) {
    using WT = WavetableOscillatorModule;
    setBool(*module, "poly", true);
    setFloat(*module, "pan", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> out(kCh, 2 * kBlockSize);
    out.clear();
    juce::AudioBuffer<float> block(kCh, kBlockSize);
    for (int b = 0; b < 2; ++b) {
        block.clear();
        // Three sounding voices at distinct pitches.
        juce::FloatVectorOperations::fill(block.getWritePointer(0), 220.0f, kBlockSize);
        juce::FloatVectorOperations::fill(block.getWritePointer(1), 330.0f, kBlockSize);
        juce::FloatVectorOperations::fill(block.getWritePointer(2), 440.0f, kBlockSize);
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    for (int v = 0; v < 3; ++v) {
        EXPECT_GT(rms(out, v), 0.05f) << "voice " << v << " left leg";
        EXPECT_GT(rms(out, WT::kRightBase + v), 0.05f) << "voice " << v << " right leg";
    }
    // Silent voices stay silent on both legs.
    EXPECT_NEAR(rms(out, 3), 0.0f, 1.0e-6f);
    EXPECT_NEAR(rms(out, WT::kRightBase + 3), 0.0f, 1.0e-6f);
    // The shared CV block between the two audio blocks must not leak audio.
    EXPECT_NEAR(rms(out, WT::kPolyModCVBase), 0.0f, 1.0e-6f);
}

TEST_F(WavetableOscillatorModuleTest, SubOscillatorAddsASubOctavePartial) {
    setFloat(*module, "position", 0.0f);
    setFloat(*module, "subLevel", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto dry = renderNote(*module, 69, 4); // A4 = 440 Hz
    const float drySub = magnitudeAt(dry, 0, 220.0f, kSampleRate);

    setFloat(*module, "subLevel", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto wet = renderNote(*module, 69, 4);
    EXPECT_GT(magnitudeAt(wet, 0, 220.0f, kSampleRate), drySub + 0.2f) << "sub must appear an octave down";

    // Two octaves down puts it at 110 Hz instead.
    setChoice(*module, "subOctave", 1);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto twoDown = renderNote(*module, 69, 4);
    EXPECT_GT(magnitudeAt(twoDown, 0, 110.0f, kSampleRate), 0.2f);
}

TEST_F(WavetableOscillatorModuleTest, StackModesTransposeUnisonVoices) {
    setFloat(*module, "position", 0.0f);
    setInt(*module, "unison", 4);
    setFloat(*module, "detune", 0.0f);
    setChoice(*module, "stack", (int)WavetableOscillatorModule::Stack::PowerChord);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 69, 4); // A4 = 440
    // Power chord: root, fifth (+7 = 659.3), octave (+12 = 880).
    EXPECT_GT(magnitudeAt(out, 0, 440.0f, kSampleRate), 0.05f) << "root missing";
    EXPECT_GT(magnitudeAt(out, 0, 659.3f, kSampleRate), 0.05f) << "fifth missing";
    EXPECT_GT(magnitudeAt(out, 0, 880.0f, kSampleRate), 0.05f) << "octave missing";
}

TEST_F(WavetableOscillatorModuleTest, BlendFadesTheStackAgainstTheCentreVoice) {
    setInt(*module, "unison", 8);
    setFloat(*module, "detune", 40.0f);

    setFloat(*module, "blend", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto full = renderNote(*module, 60, 2);

    setFloat(*module, "blend", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto centreOnly = renderNote(*module, 60, 2);

    // With the stack muted only the centre voice remains, so the two renders differ.
    float diff = 0.0f;
    for (int i = 0; i < full.getNumSamples(); ++i)
        diff = std::max(diff, std::abs(full.getReadPointer(0)[i] - centreOnly.getReadPointer(0)[i]));
    EXPECT_GT(diff, 0.02f);
    EXPECT_GT(rms(centreOnly, 0), 0.05f) << "the centre voice must survive blend 0";
}

TEST_F(WavetableOscillatorModuleTest, RingModMultipliesBySyncInput) {
    using WT = WavetableOscillatorModule;
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "syncMode", (int)WT::SyncMode::RingMod);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
    block.clear();
    module->processBlock(block, midi);

    // Ring-modulating a 440 Hz carrier with a 110 Hz sine gives sidebands at 330 and 550,
    // and suppresses the carrier itself.
    juce::AudioBuffer<float> out(kCh, 8 * kBlockSize);
    out.clear();
    double phase = 0.0;
    for (int b = 0; b < 8; ++b) {
        block.clear();
        float* sync = block.getWritePointer(WT::kJackSync);
        for (int i = 0; i < kBlockSize; ++i) {
            sync[i] = (float)std::sin(phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / kSampleRate;
        }
        juce::MidiBuffer empty;
        module->processBlock(block, empty);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    const float carrier = magnitudeAt(out, 0, 440.0f, kSampleRate);
    const float lower = magnitudeAt(out, 0, 330.0f, kSampleRate);
    const float upper = magnitudeAt(out, 0, 550.0f, kSampleRate);
    EXPECT_GT(lower, 0.1f) << "lower sideband missing";
    EXPECT_GT(upper, 0.1f) << "upper sideband missing";
    EXPECT_LT(carrier, lower * 0.5f) << "ring mod must suppress the carrier";
}

TEST_F(WavetableOscillatorModuleTest, HardSyncResetsThePhaseOnTheMasterEdge) {
    using WT = WavetableOscillatorModule;
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "syncMode", (int)WT::SyncMode::HardSync);
    // The slave must sit at a NON-integer multiple of the master: at an exact multiple it
    // completes a whole number of cycles per master period and the reset is a no-op.
    setInt(*module, "octave", 2);
    setInt(*module, "coarse", 5); // 110 Hz * 4 * 2^(5/12) ~= 587 Hz, ratio 5.34
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> block(kCh, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0); // A2 = 110 Hz -> slave at 440
    block.clear();
    module->processBlock(block, midi);

    juce::AudioBuffer<float> out(kCh, 8 * kBlockSize);
    out.clear();
    double phase = 0.0;
    for (int b = 0; b < 8; ++b) {
        block.clear();
        float* sync = block.getWritePointer(WT::kJackSync);
        for (int i = 0; i < kBlockSize; ++i) {
            sync[i] = (float)std::sin(phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / kSampleRate;
        }
        juce::MidiBuffer empty;
        module->processBlock(block, empty);
        for (int ch = 0; ch < kCh; ++ch)
            out.copyFrom(ch, b * kBlockSize, block, ch, 0, kBlockSize);
    }

    // Retriggering a sine at 110 Hz imposes that period on the output, so energy appears at the
    // master's fundamental where a free-running 440 Hz sine would have none.
    EXPECT_GT(magnitudeAt(out, 0, 110.0f, kSampleRate), 0.02f) << "hard sync must imprint the master period";
}

// ============================================================================
// Import modes and interpolation (issue #180 phases 1 and 4)
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, FixedImportSizeSplitsOnThatBoundary) {
    // Eight 2048-sample frames = 64 frames of 256 samples.
    const juce::File file = writeWavetableFile("wt180-fixed.wav", 8);
    ASSERT_TRUE(file.existsAsFile());

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::Fixed256);
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), 64);

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::Fixed2048);
    ASSERT_TRUE(module->loadWavetableFile(file));
    EXPECT_EQ(module->getNumFrames(), 8);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, SingleCycleImportMakesExactlyOneFrame) {
    const juce::File file = writeWavetableFile("wt180-single.wav", 8);
    ASSERT_TRUE(file.existsAsFile());

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::SingleCycle);
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);
    EXPECT_EQ(module->getNumFrames(), 1);

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, PitchDetectImportFindsTheSourcePeriod) {
    // A file whose period is 512 samples, written as 2048-sample blocks of the 4th harmonic.
    const int frameSize = WavetableOscillatorModule::kFrameSize;
    const int numBlocks = 8;
    juce::AudioBuffer<float> buffer(1, numBlocks * frameSize);
    float* data = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] = 0.8f * std::sin((float)i / 512.0f * juce::MathConstants<float>::twoPi);

    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-pitch.wav");
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    ASSERT_NE(stream, nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), kSampleRate, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::PitchDetect);
    ASSERT_TRUE(module->loadWavetableFile(file));
    setChoice(*module, "table", WavetableOscillatorModule::kLoadedTableChoice);

    // 8 * 2048 / 512 = 32 detected cycles, all of which fit under the 64-frame cap.
    EXPECT_EQ(module->getNumFrames(), 32);

    // Each detected cycle is one period, so the table plays back as a plain sine.
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto out = renderNote(*module, 69, 4);
    EXPECT_GT(magnitudeAt(out, 0, 440.0f, kSampleRate), 0.3f);
    EXPECT_LT(magnitudeAt(out, 0, 880.0f, kSampleRate), 0.05f) << "a detected single cycle must have no 2nd harmonic";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, SpectralImportKeepsMagnitudesAndDropsPhase) {
    // Two frames of the same harmonic in opposite phase. A normal import keeps them opposed,
    // so scanning between them cancels; a spectral import collapses both to sine phase, so the
    // midpoint keeps its level.
    const int frameSize = WavetableOscillatorModule::kFrameSize;
    juce::AudioBuffer<float> buffer(1, 2 * frameSize);
    float* data = buffer.getWritePointer(0);
    for (int i = 0; i < frameSize; ++i) {
        const float p = (float)i / (float)frameSize * juce::MathConstants<float>::twoPi;
        data[i] = 0.8f * std::sin(p);
        data[frameSize + i] = -0.8f * std::sin(p);
    }

    const juce::File file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-spectral.wav");
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    ASSERT_NE(stream, nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), kSampleRate, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    const auto midpointLevel = [&](WavetableOscillatorModule::ImportMode mode) {
        auto mod = std::make_unique<WavetableOscillatorModule>();
        setChoice(*mod, "importMode", (int)mode);
        EXPECT_TRUE(mod->loadWavetableFile(file));
        setChoice(*mod, "table", WavetableOscillatorModule::kLoadedTableChoice);
        setFloat(*mod, "position", 0.5f);
        mod->prepareToPlay(kSampleRate, kBlockSize);
        return rms(renderNote(*mod, 60, 4), 0);
    };

    const float plain = midpointLevel(WavetableOscillatorModule::ImportMode::Auto);
    const float spectral = midpointLevel(WavetableOscillatorModule::ImportMode::Spectral);

    EXPECT_LT(plain, 0.05f) << "opposed frames must cancel at the midpoint on a normal import";
    EXPECT_GT(spectral, 0.2f) << "a spectral import must make the frames phase-coherent";

    file.deleteFile();
}

TEST_F(WavetableOscillatorModuleTest, HermiteInterpolationTracksLinearButStaysBounded) {
    setFloat(*module, "position", 0.0f);
    setChoice(*module, "interpolation", (int)WavetableOscillatorModule::Interpolation::Hermite);
    module->prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderNote(*module, 60, 4);
    EXPECT_GT(magnitudeAt(out, 0, 261.6f, kSampleRate), 0.5f) << "a sine must still be a sine";
    EXPECT_LT(out.getMagnitude(0, 0, out.getNumSamples()), 1.5f);

    // The cubic fit must not ring on the coarse mips a high note selects.
    setChoice(*module, "table", 5); // Digital — the busiest built-in
    setFloat(*module, "position", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);
    const auto high = renderNote(*module, 108, 4);
    EXPECT_LT(high.getMagnitude(0, 0, high.getNumSamples()), 2.0f);
}

// ============================================================================
// Folder browser (issue #180 phase 1)
// ============================================================================

TEST_F(WavetableOscillatorModuleTest, FolderBrowserScansStepsAndWraps) {
    const juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-browser");
    folder.deleteRecursively();
    ASSERT_TRUE(folder.createDirectory());

    // Three readable tables plus a file the loader must ignore.
    for (const char* name : {"a-table.wav", "b-table.wav", "c-table.wav"}) {
        const juce::File src = writeWavetableFile(juce::String("tmp-") + name, 4);
        ASSERT_TRUE(src.existsAsFile());
        ASSERT_TRUE(src.moveFileTo(folder.getChildFile(name)));
    }
    folder.getChildFile("notes.txt").replaceWithText("not a wavetable");

    module->setWavetableFolder(folder);
    EXPECT_EQ(module->getWavetableFolder(), folder);
    ASSERT_EQ(module->getFolderWavetableCount(), 3) << "only audio files belong in the browser list";
    EXPECT_EQ(module->getFolderIndex(), -1) << "scanning must not load anything on its own";

    // Next from a fresh scan lands on the first entry, sorted by name.
    ASSERT_TRUE(module->nextWavetable());
    EXPECT_EQ(module->getFolderIndex(), 0);
    EXPECT_EQ(module->getWavetableFile().getFileName(), "a-table.wav");

    ASSERT_TRUE(module->nextWavetable());
    EXPECT_EQ(module->getWavetableFile().getFileName(), "b-table.wav");

    // Wrapping at both ends.
    ASSERT_TRUE(module->nextWavetable());
    ASSERT_TRUE(module->nextWavetable());
    EXPECT_EQ(module->getFolderIndex(), 0) << "next past the end must wrap to the start";

    ASSERT_TRUE(module->previousWavetable());
    EXPECT_EQ(module->getFolderIndex(), 2) << "previous past the start must wrap to the end";

    // An empty folder leaves the browser inert rather than failing.
    const juce::File empty = folder.getChildFile("empty");
    ASSERT_TRUE(empty.createDirectory());
    module->setWavetableFolder(empty);
    EXPECT_EQ(module->getFolderWavetableCount(), 0);
    EXPECT_FALSE(module->nextWavetable());

    folder.deleteRecursively();
}

TEST_F(WavetableOscillatorModuleTest, StateRoundTripRestoresNewParametersAndFolder) {
    const juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wt180-state");
    folder.deleteRecursively();
    ASSERT_TRUE(folder.createDirectory());

    setChoice(*module, "warp", (int)WavetableOscillatorModule::Warp::Mirror);
    setFloat(*module, "warpAmount", 0.75f);
    setFloat(*module, "phase", 180.0f);
    setFloat(*module, "spread", 0.5f);
    setFloat(*module, "width", 0.8f);
    setFloat(*module, "pan", -0.4f);
    setFloat(*module, "subLevel", 0.6f);
    setChoice(*module, "stack", (int)WavetableOscillatorModule::Stack::Minor);
    setChoice(*module, "importMode", (int)WavetableOscillatorModule::ImportMode::Fixed512);
    setChoice(*module, "interpolation", (int)WavetableOscillatorModule::Interpolation::Hermite);
    module->setWavetableFolder(folder);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto restored = std::make_unique<WavetableOscillatorModule>();
    restored->setStateInformation(state.getData(), (int)state.getSize());

    const auto readFloat = [](juce::AudioProcessor& m, const juce::String& id) {
        for (auto* p : m.getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
                if (f->paramID == id)
                    return f->get();
        return -999.0f;
    };
    const auto readChoice = [](juce::AudioProcessor& m, const juce::String& id) {
        for (auto* p : m.getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
                if (c->paramID == id)
                    return c->getIndex();
        return -1;
    };

    EXPECT_EQ(readChoice(*restored, "warp"), (int)WavetableOscillatorModule::Warp::Mirror);
    EXPECT_NEAR(readFloat(*restored, "warpAmount"), 0.75f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "phase"), 180.0f, 0.5f);
    EXPECT_NEAR(readFloat(*restored, "spread"), 0.5f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "width"), 0.8f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "pan"), -0.4f, 0.01f);
    EXPECT_NEAR(readFloat(*restored, "subLevel"), 0.6f, 0.01f);
    EXPECT_EQ(readChoice(*restored, "stack"), (int)WavetableOscillatorModule::Stack::Minor);
    EXPECT_EQ(readChoice(*restored, "importMode"), (int)WavetableOscillatorModule::ImportMode::Fixed512);
    EXPECT_EQ(readChoice(*restored, "interpolation"), (int)WavetableOscillatorModule::Interpolation::Hermite);
    EXPECT_EQ(restored->getWavetableFolder(), folder);

    // The same settings must survive the AIStateMapper extra-state path presets use.
    auto viaExtra = std::make_unique<WavetableOscillatorModule>();
    viaExtra->setExtraState(module->getExtraState());
    EXPECT_EQ(viaExtra->getWavetableFolder(), folder);

    folder.deleteRecursively();
}

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
