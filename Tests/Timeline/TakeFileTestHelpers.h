#pragma once

// Shared by the take-capture tests (TakeCaptureWriterTests, InputTakeRecorderTests, the MainComponent audio-take
// tests): a temp file that cleans up after itself, and a WAV read back through JUCE's own reader, so nothing
// here shares code with the writer under test. Header-only.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

namespace synth::test {

struct ScopedTakeFile {
    explicit ScopedTakeFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTakeFile() { file.deleteFile(); }
    juce::File file;
};

struct TakeWav {
    bool ok = false;
    double sampleRate = 0.0;
    int numChannels = 0;
    juce::int64 lengthInSamples = 0;
    juce::AudioBuffer<float> audio;
};

inline TakeWav readTakeWav(const juce::File& file) {
    TakeWav out;
    if (!file.existsAsFile())
        return out;
    auto input = file.createInputStream();
    if (input == nullptr)
        return out;
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(wavFormat.createReaderFor(input.release(), true));
    if (reader == nullptr)
        return out;
    out.sampleRate = reader->sampleRate;
    out.numChannels = (int)reader->numChannels;
    out.lengthInSamples = reader->lengthInSamples;
    out.audio.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    out.audio.clear();
    if (reader->lengthInSamples > 0)
        reader->read(&out.audio, 0, (int)reader->lengthInSamples, 0, true, true);
    out.ok = true;
    return out;
}

} // namespace synth::test
