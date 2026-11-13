#pragma once

// One decoded audio file shared by the Sampler's recording and instrument modes.

#include <juce_audio_formats/juce_audio_formats.h>

namespace synth::sampler {

struct SampleData : public juce::ReferenceCountedObject {
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;
    juce::AudioBuffer<float> audio;
    double sourceSampleRate = 44100.0;
    juce::String filePath;
    juce::String fileName;
    bool truncated = false;
};

/** Reads `file` into memory (1 or 2 channels, at most `maxSeconds`); null when it is missing, unreadable or too
    short. Any thread. */
SampleData::Ptr decodeSampleFile(const juce::File& file, double maxSeconds);

/** 4-point Catmull-Rom read of `data` at fractional `pos`, clamped to the buffer. Audio thread safe. */
inline float interpolateCatmullRom(const float* data, int numFrames, double pos) {
    const int i1 = juce::jlimit(0, numFrames - 1, (int)pos);
    const float t = (float)(pos - (double)(int)pos);
    const int i0 = juce::jmax(0, i1 - 1);
    const int i2 = juce::jmin(numFrames - 1, i1 + 1);
    const int i3 = juce::jmin(numFrames - 1, i1 + 2);

    const float y0 = data[i0], y1 = data[i1], y2 = data[i2], y3 = data[i3];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

} // namespace synth::sampler
