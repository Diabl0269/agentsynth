#pragma once

#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <algorithm>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * Audio Input — the patch's tap on the audio device's (or the host's) input (TL6-2).
 *
 * It replaces the bare juce::AudioGraphIOProcessor(audioInputNode) this node used to be. That node
 * only ever worked in Standalone mode and only for the channels the render buffer happened to
 * have; this one is an ordinary ModuleBase, so it has a bypass, a port model, a module type, a
 * cable colour and a card the editor can lay out — and it behaves identically in a plugin.
 *
 * -- Where the audio comes from -----------------------------------------------------------------
 *
 * Not from an input bus: the graph feeds a node's inputs from other nodes, and the device's input
 * is not a node. Every block AudioEngine copies the input into a snapshot buffer of its own and
 * parks pointers to it on the transport (TransportService::setDeviceInputForBlock); this module
 * downcasts getPlayHead() and copies from there — the same "the playhead is the one handle every
 * module already has" route Track In uses for the timeline snapshot. The snapshot copy is what
 * makes reading the input safe mid-graph: the graph renders IN PLACE over the buffer that carried
 * the input in, so by the time a node in the middle of the chain runs, those channels hold
 * partly-rendered output.
 *
 * The pointers are valid for the CURRENT RENDER PASS ONLY and are never cached in a member.
 *
 * -- Channel layout: fixed maximum, varying visible count ---------------------------------------
 *
 * JUCE settles a node's bus layout in the ModuleBase constructor and renegotiating it would drop
 * every connection the node already has — so the module always has kMaxChannels output channels,
 * whatever the device offers, and only the number of VISIBLE jacks follows the device (the same
 * pattern as the Macro bank's knob count; see docs/modules.md). Channels at or above the visible
 * count are cleared every block, and the owner drops any routing left on a jack that disappeared
 * (GraphEditor::dropRoutingsOnHiddenJacks) — an invisible jack cannot be unplugged.
 *
 * At least one jack is always visible: a patch with no input device still has to show where the
 * input would arrive, and a zero-jack card is not something the editor can draw a cable to.
 *
 * -- Bypass -------------------------------------------------------------------------------------
 *
 * A pure source with no audio INPUT of its own, so bypass has no dry signal to pass through and
 * clears exactly like mute would — the documented exception to the two-branch bypass/mute contract
 * (Oscillator, LFO, Noise, Poly MIDI take the same branch). It has no mute parameter at all: there
 * is nothing to mute that bypass does not already silence.
 */
class AudioInputModule : public ModuleBase {
public:
    /** Every device channel the module can ever carry. Fixed for the node's lifetime. */
    static constexpr int kMaxChannels = synth::TransportService::kMaxDeviceInputChannels;

    AudioInputModule()
        : ModuleBase("Audio Input", 0, kMaxChannels) {} // no inputs; one output per device channel

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0)
            return;

        // Pure source: no audio input, so bypass clears rather than passing a dry signal through
        // (see the class comment and docs/architecture.md's bypass/mute contract).
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        int copied = 0;

        if (auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead())) {
            const int deviceChannels = transport->getNumDeviceInputChannels();
            visibleChannels_.store(visibleFor(deviceChannels), std::memory_order_relaxed);

            const int available = std::min({deviceChannels, kMaxChannels, numChannels});
            const int samples = std::min(numSamples, transport->getNumDeviceInputSamples());

            for (; copied < available; ++copied) {
                const float* src = transport->getDeviceInputChannel(copied);
                if (src == nullptr)
                    break;

                float* dest = buffer.getWritePointer(copied);
                std::copy(src, src + samples, dest);
                // A short publish can only come of a mismatch between the engine's capture and this
                // pass's length; silence the remainder rather than leave the graph's last block in it.
                if (samples < numSamples)
                    std::fill(dest + samples, dest + numSamples, 0.0f);
            }
        } else {
            // No transport on the playhead: a foreign host, or a bare module in a unit test. There
            // is no input to be had, so the card falls back to its single-jack resting state.
            visibleChannels_.store(1, std::memory_order_relaxed);
        }

        // Everything the device did not fill — including every hidden channel — is silenced every
        // block. This is the half of the max-channel/visible-port pattern that keeps a hidden jack
        // from carrying anything (the other half is the owner's routing drop).
        for (int channel = copied; channel < numChannels; ++channel)
            buffer.clear(channel, 0, numSamples);
    }

    //==============================================================================
    // Port model
    //==============================================================================

    int getVisibleInputPortCount() const override { return 0; }
    int getVisibleOutputPortCount() const override { return visibleChannels_.load(std::memory_order_relaxed); }

    juce::String getOutputPortLabel(int channelIndex) const override {
        // "Left"/"Right" for the stereo case the old IO node drew, numbered channels beyond it.
        if (channelIndex == 0)
            return "Left";
        if (channelIndex == 1)
            return "Right";
        return "In " + juce::String(channelIndex + 1);
    }

    LogicalPort mapOutputChannel(int rawChannel) const override {
        LogicalPort p;
        const int visible = getVisibleOutputPortCount();
        p.visibleJackIndex = (visible > 0) ? juce::jlimit(0, visible - 1, rawChannel) : 0;
        p.role = PortRole::Audio;
        p.isPolyGroupHead = (rawChannel < visible);
        p.polyVoiceSpan = 1;
        return p;
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }
    ModuleType getModuleType() const override { return ModuleType::AudioInput; }

    //==============================================================================
    // Device-driven jack count
    //==============================================================================

    /** MESSAGE THREAD. Pushes the input channel count the engine is prepared for straight into the
     *  jack count, so the card resizes the moment the user changes device rather than one rendered
     *  block later. Writes the same value the audio thread would derive from the next block's
     *  context, so the two writers can never disagree for longer than one block. */
    void setDeviceChannelCount(int deviceChannels) {
        visibleChannels_.store(visibleFor(deviceChannels), std::memory_order_relaxed);
    }

private:
    /** Jacks shown for a device with `deviceChannels` inputs: clamped to what the node can carry,
     *  floored at one so the module always has somewhere to plug into. */
    static int visibleFor(int deviceChannels) noexcept { return juce::jlimit(1, kMaxChannels, deviceChannels); }

    // Written by the audio thread (once per block, from the context) and by the message thread
    // (setDeviceChannelCount); read by the UI. Relaxed is enough: it carries no other data, and a
    // count read one block late costs a repaint, not correctness.
    std::atomic<int> visibleChannels_{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioInputModule)
};
