#pragma once

// The pieces of a TrackRenderPlan, shared by its builder and its renderer only.
#include "AudioEngine/TrackRenderPlan.h"
#include <vector>

namespace synth {

/** A latency delay exactly like JUCE's DelayChannelOp: a ring of delay + 1 samples, read one behind the write. */
struct TrackRenderPlan::DelayLine {
    explicit DelayLine(int delaySamples)
        : buffer((size_t)delaySamples + 1, 0.0f)
        , writeIndex(delaySamples) {}

    void process(float* data, int numSamples) noexcept {
        for (int i = numSamples; --i >= 0;) {
            buffer[(size_t)writeIndex] = *data;
            *data++ = buffer[(size_t)readIndex];
            if (++readIndex >= (int)buffer.size())
                readIndex = 0;
            if (++writeIndex >= (int)buffer.size())
                writeIndex = 0;
        }
    }

    std::vector<float> buffer;
    int readIndex = 0;
    int writeIndex;
};

/** One source of an input channel: another node's output (null for silence) and its delay line (-1 for none). */
struct StepFeed {
    const float* source = nullptr;
    int delay = -1;
};

/** What lands on one input channel before the node renders: `first` copied in, then each of `rest` added. */
struct StepInputMix {
    int channel = 0;
    StepFeed first;
    std::vector<StepFeed> rest;
};

enum class StepIo { None, AudioIn, AudioOut, MidiIn, MidiOut };

struct TrackRenderPlan::Step {
    juce::AudioProcessorGraph::Node::Ptr node;
    juce::AudioProcessor* processor = nullptr;
    StepIo io = StepIo::None;
    int numAudioChannels = 0;
    std::vector<float*> channels;  // the node's in-place channels: its own output slot, or its job's scratch
    juce::AudioBuffer<float> view; // refers to `channels`; re-pointed per block, never reallocated
    std::vector<StepInputMix> mixes;
    std::vector<int> clears; // output-only channels
    juce::MidiBuffer midi;
    const juce::MidiBuffer* midiFirst = nullptr;
    std::vector<const juce::MidiBuffer*> midiRest;
};

struct TrackRenderPlan::Job {
    std::vector<Step*> steps; // in the graph's render order
    std::vector<int> successors;
    int waits = 0;
    juce::AudioBuffer<float> scratch;
    float* delayScratch = nullptr;
};

} // namespace synth
