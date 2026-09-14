// WavetableOscillatorModuleTestHelpers.h
// Shared render/parameter helpers and constants used by the WavetableOscillatorModule test suites.
#pragma once

#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
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
