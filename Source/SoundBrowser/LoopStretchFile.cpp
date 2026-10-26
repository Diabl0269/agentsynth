// LoopStretchFile.cpp -- reads a loop, runs stretchLoop on it at the file's own rate and writes a 24-bit WAV copy.
#include "SoundBrowser/LoopStretchFile.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>

namespace synth::soundbrowser {

namespace {

constexpr int kBitsPerSample = 24;

// The name carries the song tempo for the eye and a hash of everything the copy depends on, so two files with one stem
// or one file at two tempos or shifts never share a copy, and an edited source is rendered again.
juce::String cacheName(const juce::File& source, const LoopTarget& target) {
    const auto key = source.getFullPathName() + "|" + juce::String(source.getSize()) + "|" +
                     juce::String(source.getLastModificationTime().toMilliseconds()) + "|" +
                     juce::String(target.fileBpm, 3) + "|" + juce::String(target.songBpm, 3) + "|" +
                     juce::String(target.semitones);
    auto name = source.getFileNameWithoutExtension() + "-" + juce::String(juce::roundToInt(target.songBpm)) + "bpm";
    if (target.semitones != 0)
        name += (target.semitones > 0 ? "+" : "") + juce::String(target.semitones) + "st";
    return name + "-" + juce::String::toHexString(key.hashCode64()).paddedLeft('0', 8).substring(0, 8) + ".wav";
}

bool readWhole(const juce::File& source, juce::AudioBuffer<float>& data, double& rate) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 ||
        (double)reader->lengthInSamples > SoundPreviewPlayer::kMaxSeconds * reader->sampleRate)
        return false;
    const int channels = (int)juce::jmin<unsigned int>(2, reader->numChannels);
    data.setSize(channels, (int)reader->lengthInSamples);
    rate = reader->sampleRate;
    return reader->read(&data, 0, data.getNumSamples(), 0, true, channels > 1);
}

// The window [startSeconds, startSeconds + lengthSeconds) at the file's own rate, at most two channels; the reader
// fills whatever lies past the file's end with silence.
bool readWindow(const juce::File& source, double startSeconds, double lengthSeconds, juce::AudioBuffer<float>& data,
                double& rate) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;
    const auto first = (juce::int64)std::llround(startSeconds * reader->sampleRate);
    const auto count = (int)std::llround(lengthSeconds * reader->sampleRate);
    if (count <= 0)
        return false;
    const int channels = (int)juce::jmin<unsigned int>(2, reader->numChannels);
    data.setSize(channels, count);
    rate = reader->sampleRate;
    return reader->read(&data, 0, count, first, true, channels > 1);
}

bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& data, double rate) {
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), rate, (unsigned int)data.getNumChannels(), kBitsPerSample, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns it now
    return writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples());
}

} // namespace

juce::File stretchedLoopCacheFolder() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Agent Synth Stretched Loops");
}

juce::File stretchedLoopFile(const juce::File& source, const LoopTarget& target, const juce::File& cacheFolder) {
    if (!target.needsStretch() || !source.existsAsFile())
        return source;
    const auto copy = cacheFolder.getChildFile(cacheName(source, target));
    if (copy.existsAsFile() && copy.getSize() > 0)
        return copy;
    if (!cacheFolder.createDirectory().wasOk())
        return source;

    juce::AudioBuffer<float> data;
    double rate = 0.0;
    if (!readWhole(source, data, rate))
        return source;
    const auto stretched = stretchLoop(data, rate, target.tempoRatio(), (double)target.semitones);

    // Written beside the copy and renamed, so a half-written file is never taken for a finished one.
    const auto partial = copy.withFileExtension("partial");
    const bool written = writeWav(partial, stretched, rate); // the writer closes the file when writeWav returns
    if (!written || !partial.moveFileTo(copy)) {
        partial.deleteFile();
        return source;
    }
    return copy;
}

// The clip-stretch path: unlike stretchedLoopFile it reads only a window (a clip may already be trimmed) and writes
// where the caller says, which then imports the copy into the project. Same write-then-rename as above.
bool renderStretchedWindow(const juce::File& source, double startSeconds, double lengthSeconds, double tempoRatio,
                           const juce::File& dest) {
    if (!(tempoRatio > 0.0) || !std::isfinite(tempoRatio) || !(lengthSeconds > 0.0) ||
        lengthSeconds > kMaxStretchWindowSeconds || !(startSeconds >= 0.0) || !source.existsAsFile())
        return false;
    juce::AudioBuffer<float> data;
    double rate = 0.0;
    if (!readWindow(source, startSeconds, lengthSeconds, data, rate))
        return false;
    const auto stretched = stretchLoop(data, rate, tempoRatio, 0.0);
    if (!dest.getParentDirectory().createDirectory().wasOk())
        return false;
    const auto partial = dest.withFileExtension("partial");
    const bool written = writeWav(partial, stretched, rate);
    if (!written || !partial.moveFileTo(dest)) {
        partial.deleteFile();
        return false;
    }
    return true;
}

} // namespace synth::soundbrowser
