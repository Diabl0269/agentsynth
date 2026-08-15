#pragma once

#include "Modules/ModuleBase.h"
#include "Transport/TransportService.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>

class AudioEngine
    : public juce::AudioIODeviceCallback
    , public juce::MidiInputCallback {
public:
    // How the graph is driven.
    //   Standalone — AudioEngine owns an AudioDeviceManager, opens the default output device and
    //                every available MIDI input, and clocks the graph from the device callback.
    //   Hosted     — a plugin wrapper (AgentSynthAudioProcessor) owns the clock. initialise()
    //                touches neither the device manager nor MIDI inputs; the host calls
    //                prepareForHost / processHostBlock / releaseFromHost instead. Opening an audio
    //                device from inside a plugin would fight the host for the hardware, and opening
    //                MIDI inputs directly would double-trigger notes the host already forwards.
    enum class HostMode { Standalone, Hosted };

    explicit AudioEngine(HostMode mode = HostMode::Standalone);
    ~AudioEngine() override;

    void initialise();
    void shutdown();

    HostMode getHostMode() const noexcept { return hostMode_; }
    bool isHosted() const noexcept { return hostMode_ == HostMode::Hosted; }

    // ---- Hosted-mode driving API (no-ops / unused in Standalone mode) ----
    // Mirror of the AudioIODeviceCallback trio, but fed by the plugin wrapper's
    // prepareToPlay / processBlock / releaseResources.
    void prepareForHost(double sampleRate, int blockSize, int numInputChannels, int numOutputChannels);
    void processHostBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    void releaseFromHost();

    // Voice count / mute API (§4.2)
    struct VoiceInfo {
        int activeVoices = 0;
        int maxVoices = 0;
    };
    VoiceInfo getActiveVoiceInfo() const;
    int getDisplayVoiceCount() const;

    void setMasterMute(bool muted) noexcept;
    bool isMasterMuted() const noexcept;

    // TL1-9 runtime companion to SYNTH_ENABLE_TIMELINE: lets the transport be frozen/resumed without
    // a rebuild. Only meaningful when the flag is compiled in — see renderNextBlock(). Default true
    // (today's ticking behaviour) so a build that never touches this setting is unaffected.
    void setTransportEnabled(bool enabled) noexcept;
    bool isTransportEnabled() const noexcept;

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    juce::AudioProcessorGraph& getGraph() { return mainProcessorGraph; }
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // The one clock. Installed as the graph's AudioPlayHead in the constructor and ticked once per
    // callback in renderNextBlock; message-thread callers use play()/stop()/locateBeat()/... and
    // any thread may read getPositionSnapshot().
    synth::TransportService& getTransport() noexcept { return transport; }
    const synth::TransportService& getTransport() const noexcept { return transport; }

    // Total latency the graph reports for itself, in samples. This is report-only latency
    // aggregation: juce::AudioProcessorGraph already compensates parallel paths internally, so we
    // only surface the total (for the UI / host to display or pass on) — we do NOT compensate.
    int getGraphLatencySamples() const noexcept { return mainProcessorGraph.getLatencySamples(); }

    struct ModRoutingInfo {
        juce::AudioProcessorGraph::NodeID attenuverterNodeID;
        juce::AudioProcessorGraph::NodeID sourceNodeID;
        int sourceChannelIndex;
        juce::AudioProcessorGraph::NodeID destNodeID;
        int destChannelIndex;
        bool isBypassed;
    };

    struct ModulationDisplayInfo {
        juce::AudioProcessorGraph::NodeID attenuverterNodeID;
        juce::AudioProcessorGraph::NodeID destNodeID;
        int destChannelIndex;
        float modSignalValue;
        float modSignalPeak;
        bool isBypassed;
    };

    enum class RoutingKind { AttenuverterChain, DirectCV, PolyBus };

    struct ModulationRouting {
        RoutingKind kind = RoutingKind::AttenuverterChain;
        juce::AudioProcessorGraph::NodeID sourceNodeID;
        int sourceChannelIndex = 0;
        int sourceVisibleJack = 0;
        juce::AudioProcessorGraph::NodeID destNodeID;
        int destChannelIndex = 0;
        int destVisibleJack = 0;
        juce::AudioProcessorGraph::NodeID attenuverterNodeID; // valid only for AttenuverterChain
        int voiceCount = 1;
        float amount = 1.0f;
        bool isBypassed = false;
        bool hasSource = false;
        bool hasDest = false;
        float modSignalValue = 0.0f;
        float modSignalPeak = 0.0f;
        PortRole role = PortRole::ModCV;
    };

    std::vector<ModulationRouting> getModulationRoutings() const;
    std::vector<ModRoutingInfo> getActiveModRoutings() const;
    std::vector<ModulationDisplayInfo> getModulationDisplayInfo() const;
    std::vector<ModulationDisplayInfo> getModulationDisplayInfo(const std::vector<ModulationRouting>& routings) const;
    /** Inserts an attenuverter between source and destination. Returns the new attenuverter's
     *  NodeID (invalid on failure) so callers that rebuild a routing can carry the old
     *  attenuverter's amount across; callers that just want the wire can ignore it. */
    juce::AudioProcessorGraph::NodeID addModRouting(juce::AudioProcessorGraph::NodeID sourceNodeID,
                                                    int sourceChannelIndex,
                                                    juce::AudioProcessorGraph::NodeID destNodeID, int destChannelIndex);
    void addEmptyModRouting();
    void removeModRouting(juce::AudioProcessorGraph::NodeID attenuverterNodeID);
    void toggleModBypass(juce::AudioProcessorGraph::NodeID attenuverterNodeID);
    bool isModBypassed(juce::AudioProcessorGraph::NodeID attenuverterNodeID) const;
    void updateModuleNames();
    void ensureMidiDeviceOpen(const juce::String& deviceName);

private:
    const HostMode hostMode_;

    juce::AudioDeviceManager deviceManager;
    // Declared before the graph on purpose: the graph holds a raw AudioPlayHead pointer to the
    // transport, and members are destroyed in reverse declaration order, so the graph goes first.
    synth::TransportService transport;
    juce::AudioProcessorGraph mainProcessorGraph;
    juce::AudioProcessorPlayer processorPlayer;

    std::atomic<bool> masterMuted_{false};
    std::atomic<bool> transportEnabled_{true};

    void createDefaultPatch();

    // The one place the graph is actually clocked. Both the standalone device callback and the
    // hosted processBlock funnel through here so master-mute semantics (zero-fill AFTER the graph
    // runs, so sequencers / LFOs / envelopes keep advancing) can never drift between the two.
    void renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    juce::MidiMessageCollector midiMessageCollector;
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
