#pragma once
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace TestAudioHelpers {

// Render audio through a prepared AudioProcessorGraph.
// The graph must already have nodes added, connected, and prepareToPlay called.
// Pass MIDI events in the midiBuffer (e.g., noteOn).
// Returns a stereo buffer with numSamples of rendered audio.
inline juce::AudioBuffer<float> renderGraph(juce::AudioProcessorGraph& graph, juce::MidiBuffer& midi, int numSamples,
                                            int numChannels = 2) {
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    buffer.clear();
    graph.processBlock(buffer, midi);
    return buffer;
}

// Render multiple blocks, accumulating output. Useful for rendering longer durations.
// MIDI is only injected in the first block.
inline juce::AudioBuffer<float> renderGraphMultiBlock(juce::AudioProcessorGraph& graph, juce::MidiBuffer& midi,
                                                      int totalSamples, int blockSize = 512, int numChannels = 2) {
    juce::AudioBuffer<float> result(numChannels, totalSamples);
    result.clear();

    int samplesRendered = 0;
    bool firstBlock = true;

    while (samplesRendered < totalSamples) {
        int samplesThisBlock = std::min(blockSize, totalSamples - samplesRendered);
        juce::AudioBuffer<float> blockBuffer(numChannels, samplesThisBlock);
        blockBuffer.clear();

        if (firstBlock) {
            graph.processBlock(blockBuffer, midi);
            firstBlock = false;
        } else {
            juce::MidiBuffer emptyMidi;
            graph.processBlock(blockBuffer, emptyMidi);
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            result.copyFrom(ch, samplesRendered, blockBuffer, ch, 0, samplesThisBlock);
        }
        samplesRendered += samplesThisBlock;
    }

    return result;
}

// Compute RMS level of a buffer on a given channel
inline float computeRMS(const juce::AudioBuffer<float>& buffer, int channel = 0) {
    float sumSquares = 0.0f;
    const float* data = buffer.getReadPointer(channel);
    int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
        sumSquares += data[i] * data[i];

    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

// Check if buffer is silent (all samples below threshold)
inline bool isSilent(const juce::AudioBuffer<float>& buffer, int channel = 0, float threshold = 1e-6f) {
    const float* data = buffer.getReadPointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs(data[i]) > threshold)
            return false;
    return true;
}

// Estimate dominant frequency using zero-crossing counting
inline float estimateFrequencyByZeroCrossings(const juce::AudioBuffer<float>& buffer, double sampleRate = 44100.0,
                                              int channel = 0) {
    const float* data = buffer.getReadPointer(channel);
    int numSamples = buffer.getNumSamples();
    int zeroCrossings = 0;

    for (int i = 1; i < numSamples; ++i) {
        if ((data[i] >= 0.0f && data[i - 1] < 0.0f) || (data[i] < 0.0f && data[i - 1] >= 0.0f))
            ++zeroCrossings;
    }

    // Each complete cycle has 2 zero crossings
    double durationSeconds = static_cast<double>(numSamples) / sampleRate;
    return static_cast<float>(zeroCrossings / (2.0 * durationSeconds));
}

// Compute RMS over a specific time window (in samples)
inline float computeRMSInRange(const juce::AudioBuffer<float>& buffer, int startSample, int endSample,
                               int channel = 0) {
    float sumSquares = 0.0f;
    const float* data = buffer.getReadPointer(channel);
    int count = endSample - startSample;

    for (int i = startSample; i < endSample && i < buffer.getNumSamples(); ++i)
        sumSquares += data[i] * data[i];

    return std::sqrt(sumSquares / static_cast<float>(count));
}

// Create a MidiBuffer with a single note-on event at sample 0
inline juce::MidiBuffer createNoteOnMidi(int noteNumber = 60, float velocity = 1.0f, int channel = 1) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, velocity), 0);
    return midi;
}

// Create a MidiBuffer with note-on at sample 0 and note-off at given sample
inline juce::MidiBuffer createNoteOnOffMidi(int noteNumber = 60, float velocity = 1.0f, int noteOffSample = 22050,
                                            int channel = 1) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, velocity), 0);
    midi.addEvent(juce::MidiMessage::noteOff(channel, noteNumber, 0.0f), noteOffSample);
    return midi;
}

// Workaround for JUCE graph MIDI routing: manually inject MIDI into audio input node
// This bypasses the graph's internal MIDI routing by directly setting MIDI on the buffer
// that gets passed to processBlock().
inline juce::AudioBuffer<float> renderGraphMultiBlockWithDirectMidi(juce::AudioProcessorGraph& graph,
                                                                    juce::MidiBuffer& midi, int totalSamples,
                                                                    int blockSize = 512, int numChannels = 2) {
    // For now, fallback to regular renderGraphMultiBlock
    // The real solution requires understanding JUCE's MIDI routing better
    return renderGraphMultiBlock(graph, midi, totalSamples, blockSize, numChannels);
}

// ===========================================================================
// Snapshot/Reference Testing Helpers
// ===========================================================================

// Save a buffer to a binary file (raw float32 samples, single channel)
inline bool saveReference(const juce::AudioBuffer<float>& buffer, const juce::String& filePath, int channel = 0) {
    juce::File file(filePath);
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(file);
    if (stream.failedToOpen())
        return false;
    stream.setPosition(0);
    stream.truncate();
    const float* data = buffer.getReadPointer(channel);
    stream.write(data, sizeof(float) * static_cast<size_t>(buffer.getNumSamples()));
    return true;
}

// Load a reference buffer from a binary file
inline juce::AudioBuffer<float> loadReference(const juce::String& filePath) {
    juce::File file(filePath);
    if (!file.existsAsFile())
        return {};
    juce::FileInputStream stream(file);
    if (stream.failedToOpen())
        return {};
    int numSamples = static_cast<int>(stream.getTotalLength() / sizeof(float));
    juce::AudioBuffer<float> buffer(1, numSamples);
    stream.read(buffer.getWritePointer(0), sizeof(float) * static_cast<size_t>(numSamples));
    return buffer;
}

// Compare two buffers with a per-sample tolerance. Returns max absolute difference.
inline float compareBuffers(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channel = 0) {
    int n = std::min(a.getNumSamples(), b.getNumSamples());
    const float* da = a.getReadPointer(channel);
    const float* db = b.getReadPointer(channel);
    float maxDiff = 0.0f;
    for (int i = 0; i < n; ++i)
        maxDiff = std::max(maxDiff, std::abs(da[i] - db[i]));
    return maxDiff;
}

// Check if a reference file exists
inline bool referenceExists(const juce::String& filePath) { return juce::File(filePath).existsAsFile(); }

} // namespace TestAudioHelpers
