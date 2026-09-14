// Concern: host mode — the plugin wrapper's prepareForHost/processHostBlock/releaseFromHost trio, mirroring the
// standalone device-callback path without touching hardware.

#include "AudioEngine.h"

void AudioEngine::prepareForHost(double sampleRate, int blockSize, int numInputChannels, int numOutputChannels) {
    // The collector is still used in hosted mode: ExternalMidiModule-bound messages and any
    // future UI-generated MIDI go through it, and it must be reset to the host's rate or its
    // timestamps land in the wrong block.
    midiMessageCollector.reset(sampleRate);
    mainProcessorGraph.setPlayConfigDetails(numInputChannels, numOutputChannels, sampleRate, blockSize);
    // Before the graph, for the same reason as audioDeviceAboutToStart: the musical position is
    // preserved across the rate change, the sample position is re-derived. This also resets
    // the metronome, invalidates the clip streamer and flags any in-flight take.
    handleStreamFormatChange(sampleRate, blockSize);
    prepareSliceScratch(std::max(numInputChannels, numOutputChannels), blockSize);
    // Hosted mode takes the same input snapshot as the device callback. The host's buffer is
    // one in/out buffer the graph renders over in place, so the input has to be copied out of it
    // before the graph runs or "Audio Input" would tap the mix instead of the input.
    prepareDeviceInputSnapshot(numInputChannels, blockSize);
    mainProcessorGraph.prepareToPlay(sampleRate, blockSize);
}

void AudioEngine::processHostBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // The host's input lives in the low channels of the same buffer it wants the output in, so the
    // capture has to happen HERE, before the graph gets a chance to overwrite them.
    const int hostInputChannels =
        std::min(deviceInputChannelCount_.load(std::memory_order_relaxed), buffer.getNumChannels());
    if (hostInputChannels > 0)
        captureDeviceInput(buffer.getArrayOfReadPointers(), hostInputChannels, buffer.getNumSamples());
    else
        captureDeviceInput(nullptr, 0, 0);

    renderNextBlock(buffer, midiMessages);
}

void AudioEngine::releaseFromHost() { mainProcessorGraph.releaseResources(); }
