#include "Modules/Sampler/SamplerSampleData.h"

namespace synth::sampler {

SampleData::Ptr decodeSampleFile(const juce::File& file, double maxSeconds) {
    if (!file.existsAsFile())
        return nullptr;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return nullptr;

    const double readerRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    const juce::int64 maxFrames = (juce::int64)(maxSeconds * readerRate);
    const juce::int64 available = reader->lengthInSamples;
    const int frames = (int)juce::jmin(available, maxFrames);
    if (frames <= 1)
        return nullptr;

    SampleData::Ptr loaded = new SampleData();
    loaded->audio.setSize(juce::jlimit(1, 2, (int)reader->numChannels), frames);
    loaded->audio.clear();
    reader->read(&loaded->audio, 0, frames, 0, true, true);
    loaded->sourceSampleRate = readerRate;
    loaded->filePath = file.getFullPathName();
    loaded->fileName = file.getFileName();
    loaded->truncated = available > maxFrames;
    return loaded;
}

} // namespace synth::sampler
