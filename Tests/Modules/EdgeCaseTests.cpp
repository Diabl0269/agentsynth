#include "Modules/ADSRModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>

// Test zero-length buffers for each module
TEST(EdgeCaseTests, OscillatorZeroLengthBuffer) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0); // 0 samples
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(osc.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, FilterZeroLengthBuffer) {
    FilterModule filter;
    filter.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(filter.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, VCAZeroLengthBuffer) {
    VCAModule vca;
    vca.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(vca.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, LFOZeroLengthBuffer) {
    LFOModule lfo;
    lfo.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(lfo.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, ADSRZeroLengthBuffer) {
    ADSRModule adsr;
    adsr.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(adsr.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, DelayZeroLengthBuffer) {
    DelayModule delay;
    delay.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(delay.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, DistortionZeroLengthBuffer) {
    DistortionModule distortion;
    distortion.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(distortion.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, ReverbZeroLengthBuffer) {
    ReverbModule reverb;
    reverb.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(reverb.processBlock(buffer, midi));
}

// Test sample rate changes
TEST(EdgeCaseTests, OscillatorSampleRateChange) {
    OscillatorModule osc;
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Prepare at 44100
    osc.prepareToPlay(44100.0, 512);
    buffer.clear();
    osc.processBlock(buffer, midi);

    float rms1 = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    // Change to 96000
    osc.prepareToPlay(96000.0, 512);
    buffer.clear();
    osc.processBlock(buffer, midi);

    float rms2 = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    // Should produce signal at both sample rates
    EXPECT_GT(rms1, 0.0f);
    EXPECT_GT(rms2, 0.0f);
}

// Test extreme parameters
TEST(EdgeCaseTests, OscillatorExtremeParameters) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    // Set extreme octave, coarse, fine values
    if (auto octaveParam = findParameterByID(&osc, "octave")) {
        octaveParam->setValueNotifyingHost(-4.0f); // Extreme low
    }
    if (auto coarseParam = findParameterByID(&osc, "coarse")) {
        coarseParam->setValueNotifyingHost(-12.0f / 12.0f); // Normalize to [0,1]
    }
    if (auto fineParam = findParameterByID(&osc, "fine")) {
        fineParam->setValueNotifyingHost(-100.0f / 100.0f); // Normalize to [0,1]
    }

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    buffer.clear();

    osc.processBlock(buffer, midi);

    // Verify output is in valid range [-1, 1]
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* samples = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            EXPECT_GE(samples[i], -1.1f); // Small tolerance for numerical errors
            EXPECT_LE(samples[i], 1.1f);
        }
    }
}

// Test maximum resonance filter
TEST(EdgeCaseTests, FilterMaxResonance) {
    FilterModule filter;
    filter.prepareToPlay(44100.0, 512);

    // Set resonance to maximum
    if (auto resonanceParam = findParameterByID(&filter, "resonance")) {
        resonanceParam->setValueNotifyingHost(1.0f);
    }

    // Create a buffer with test signal
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    buffer.clear();

    // Fill with a signal
    auto* samples = buffer.getWritePointer(0);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        samples[i] = std::sin(2.0f * juce::MathConstants<float>::pi * i / 512.0f);
    }

    filter.processBlock(buffer, midi);

    // Verify no NaN or inf in output
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            EXPECT_FALSE(std::isnan(data[i]));
            EXPECT_FALSE(std::isinf(data[i]));
        }
    }
}

// Test rapid parameter changes
TEST(EdgeCaseTests, OscillatorRapidParameterChanges) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Change waveform parameter rapidly for 100 blocks
    auto waveformParam = findParameterByID(&osc, "waveform");

    for (int block = 0; block < 100; ++block) {
        // Cycle through waveforms
        float waveform = static_cast<float>(block % 4) / 3.0f;
        if (waveformParam) {
            waveformParam->setValueNotifyingHost(waveform);
        }

        buffer.clear();
        EXPECT_NO_THROW(osc.processBlock(buffer, midi));
    }
}

// Test single sample buffer
TEST(EdgeCaseTests, OscillatorSingleSampleBuffer) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 1);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(osc.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, FilterSingleSampleBuffer) {
    FilterModule filter;
    filter.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 1);
    juce::MidiBuffer midi;

    // Fill with a test sample
    buffer.setSample(0, 0, 0.5f);

    EXPECT_NO_THROW(filter.processBlock(buffer, midi));
}

// Test large buffer
TEST(EdgeCaseTests, OscillatorLargeBuffer) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 16384);

    juce::AudioBuffer<float> buffer(2, 16384);
    juce::MidiBuffer midi;
    buffer.clear();

    osc.processBlock(buffer, midi);

    // Verify output exists
    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rms, 0.0f);
}

// Test all waveforms
TEST(EdgeCaseTests, OscillatorAllWaveforms) {
    OscillatorModule osc;
    osc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    auto waveformParam = findParameterByID(&osc, "waveform");

    // Test each waveform (0-3: Sine, Saw, Square, Triangle)
    for (int wf = 0; wf < 4; ++wf) {
        if (waveformParam) {
            waveformParam->setValueNotifyingHost(static_cast<float>(wf) / 3.0f);
        }

        buffer.clear();
        osc.processBlock(buffer, midi);

        float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
        EXPECT_GT(rms, 0.0f) << "Waveform " << wf << " should produce non-zero output";
    }
}

TEST(EdgeCaseTests, ChorusZeroLengthBuffer) {
    ChorusModule chorus;
    chorus.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(chorus.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, PhaserZeroLengthBuffer) {
    PhaserModule phaser;
    phaser.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(phaser.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, CompressorZeroLengthBuffer) {
    CompressorModule compressor;
    compressor.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(compressor.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, FlangerZeroLengthBuffer) {
    FlangerModule flanger;
    flanger.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(flanger.processBlock(buffer, midi));
}

TEST(EdgeCaseTests, LimiterZeroLengthBuffer) {
    LimiterModule limiter;
    limiter.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 0);
    juce::MidiBuffer midi;

    EXPECT_NO_THROW(limiter.processBlock(buffer, midi));
}
