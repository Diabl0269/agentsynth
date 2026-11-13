#pragma once

// Shared fixtures for the multi-sample Sampler tests: temp WAV files and in-memory samples.

#include "Modules/Sampler/SamplerInstrument.h"
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>

namespace sampler_test {

using Generator = std::function<float(int channel, int frame)>;

// A scratch directory removed with the object.
class TempDir {
public:
    TempDir() {
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-sampler-" + juce::Uuid().toString());
        dir_.createDirectory();
    }
    ~TempDir() { dir_.deleteRecursively(); }
    juce::File file(const juce::String& name) const { return dir_.getChildFile(name); }
    const juce::File& path() const { return dir_; }

private:
    juce::File dir_;
};

inline bool writeWav(const juce::File& file, int frames, int channels, double rate, const Generator& gen) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, gen(c, i));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), rate, (unsigned)channels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return true;
}

inline synth::sampler::SampleData::Ptr makeSample(int frames, int channels, double rate, const Generator& gen) {
    synth::sampler::SampleData::Ptr s = new synth::sampler::SampleData();
    s->audio.setSize(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            s->audio.setSample(c, i, gen(c, i));
    s->sourceSampleRate = rate;
    return s;
}

inline synth::sampler::SampleData::Ptr dcSample(float level, int frames = 88200) {
    return makeSample(frames, 1, 44100.0, [level](int, int) { return level; });
}

inline synth::sampler::Region makeRegion(synth::sampler::SampleData::Ptr sample, int loKey = 0, int hiKey = 127,
                                         int loVel = 1, int hiVel = 127, int root = 60) {
    synth::sampler::Region r;
    r.loKey = loKey;
    r.hiKey = hiKey;
    r.loVel = loVel;
    r.hiVel = hiVel;
    r.rootKey = root;
    r.sample = std::move(sample);
    return r;
}

inline synth::sampler::Instrument::Ptr makeInstrument(std::vector<synth::sampler::Region> regions) {
    synth::sampler::Instrument::Ptr inst = new synth::sampler::Instrument();
    inst->name = "test";
    inst->regions = std::move(regions);
    return inst;
}

} // namespace sampler_test
