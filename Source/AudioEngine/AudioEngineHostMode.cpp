// Concern: host mode — the plugin wrapper's prepareForHost/processHostBlock/releaseFromHost trio, mirroring the
// standalone device-callback path without touching hardware.

#include "AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"

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

    // MIDI Remote runs on the AUDIO thread here — HostMode::Hosted never opens hardware MIDI, so
    // the host's own forwarded buffer IS the MIDI path (docs/midi_remote.md §4.8's hostSourceKey).
    // RemoteMessageSink::handleMessage is lock-free and allocation-free by contract, which is what
    // makes this safe to call from processBlock. Guarded on remoteMessageSink_ so the cost is zero
    // while MIDI Remote is idle; remoteHostScratchMidi_ is a pre-allocated member so the clear()/
    // addEvent() below never allocate once warmed up. Consumed messages are dropped from the
    // buffer before renderNextBlock, same as the standalone path's "goes nowhere else" contract.
    //
    // This whole block runs BEFORE renderNextBlock(), so it is outside ScopedRenderPass too —
    // ScopedRemoteSinkCall covers it exactly like the standalone MIDI-thread call site, entered once
    // for the loop rather than per message since the sink is loaded once (FRO197).
    {
        const ScopedRemoteSinkCall remoteSinkGuard(remoteSinkCallsInFlight_);
        if (auto* sink = remoteMessageSink_.load(std::memory_order_seq_cst); sink != nullptr) {
            remoteHostScratchMidi_.clear();
            for (const auto metadata : midiMessages) {
                const auto message = metadata.getMessage();
                if (!sink->handleMessage(synth::midi::hostSourceKey(), message))
                    remoteHostScratchMidi_.addEvent(message, metadata.samplePosition);
            }
            midiMessages.swapWith(remoteHostScratchMidi_);
        }
    }

    renderNextBlock(buffer, midiMessages);
}

void AudioEngine::releaseFromHost() { mainProcessorGraph.releaseResources(); }
