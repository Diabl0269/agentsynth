// WaveformTestSignals.h — synthetic audio for the waveform tests: a changing-note sine whose level decays inside each
// note, so every peak bucket differs from its neighbour (a steady tone would legitimately repeat one value).
#pragma once

#include "Timeline/PeaksFile.h"
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace waveform_test {

// 220 / 330 / 440 / 293 Hz notes of 0.3 s each, level 0.9 decaying towards ~0.3 inside each note.
inline std::vector<float> makeNotes(double sampleRate, double seconds = 1.2) {
    constexpr double kFreqs[] = {220.0, 330.0, 440.0, 293.0};
    constexpr double kNoteSeconds = 0.3;
    std::vector<float> out((std::size_t)(seconds * sampleRate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = (double)i / sampleRate;
        const int note = (int)(t / kNoteSeconds) % 4;
        const double local = t - std::floor(t / kNoteSeconds) * kNoteSeconds;
        out[i] =
            (float)(0.9 * std::exp(-local / 0.25) * std::sin(2.0 * juce::MathConstants<double>::pi * kFreqs[note] * t));
    }
    return out;
}

inline synth::PeaksFile::Data peaksOf(const std::vector<float>& mono, int bucketSize = 256) {
    synth::PeaksFile::Accumulator accumulator(bucketSize, 1);
    juce::AudioBuffer<float> buffer(1, (int)mono.size());
    buffer.copyFrom(0, 0, mono.data(), (int)mono.size());
    accumulator.addSamples(buffer, (int)mono.size());
    accumulator.flushPartial();
    return accumulator.getData();
}

inline bool writeWav(const juce::File& file, double sampleRate, const std::vector<float>& mono) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 1, 24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    juce::AudioBuffer<float> buffer(1, (int)mono.size());
    buffer.copyFrom(0, 0, mono.data(), (int)mono.size());
    return writer->writeFromAudioSampleBuffer(buffer, 0, (int)mono.size());
}

// The longest stretch of consecutive entries whose values are equal within `tolerance`.
template <typename T>
int longestRun(const std::vector<T>& values, double tolerance) {
    int best = values.empty() ? 0 : 1, run = 1;
    for (std::size_t i = 1; i < values.size(); ++i) {
        run = std::abs((double)values[i] - (double)values[i - 1]) <= tolerance ? run + 1 : 1;
        best = std::max(best, run);
    }
    return best;
}

} // namespace waveform_test
