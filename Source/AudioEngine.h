#pragma once

#include "Modules/ModuleBase.h"
#include "Timeline/AutomationApplier.h"
#include "Timeline/EpochExchange.h"
#include "Timeline/TimelineSnapshotExchange.h"
#include "Transport/TransportService.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {
class MidiRecorder; // Forward declaration (Source/Timeline/MidiRecorder.h)
class TimelineDoc;  // Forward declaration (Source/Timeline/TimelineDoc.h)
} // namespace synth

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

    // ---- Device-callback attachment (Standalone only) ----
    // True exactly while this engine is registered as an AudioIODeviceCallback on its own
    // AudioDeviceManager — i.e. between initialise() and shutdown() in Standalone mode, minus any
    // window a caller has explicitly suspended. Always false in Hosted mode (the host owns the
    // clock and this engine never registers anywhere).
    //
    // The reason it exists: "is something else clocking this graph right now?" is the precondition
    // every offline renderer needs, and HostMode alone can't answer it — a Standalone engine whose
    // callback has been detached is just as quiescent as a Hosted one. See
    // synth::OfflineTransportDriver and synth::BounceExporter.
    bool isReceivingDeviceCallbacks() const noexcept { return deviceCallbackAttached_; }

    // MESSAGE THREAD. Detaches this engine from the device callback so nothing clocks the graph,
    // and returns true if it actually detached (false when it wasn't attached in the first place —
    // Hosted mode, before initialise(), after shutdown(), or already suspended). Mirrors exactly
    // how initialise() attached it. juce::AudioDeviceManager calls audioDeviceStopped() on the way
    // out, which releases the graph's resources, so whoever suspends owns re-preparing the graph
    // for whatever it renders next.
    bool suspendDeviceCallback();

    // MESSAGE THREAD. Undoes suspendDeviceCallback(). juce::AudioDeviceManager::addAudioCallback
    // calls audioDeviceAboutToStart() on the callback it is adding whenever a device is open, so
    // this also re-applies the DEVICE's sample rate / block size to the transport and re-prepares
    // the graph — callers must not do that by hand. A no-op in Hosted mode or if already attached.
    void resumeDeviceCallback();

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

    // The timeline's message-thread -> audio-thread hand-off (TL2-2). The message thread publishes
    // snapshots here (and may reap on a timer); the engine opens exactly one audio block on it per
    // callback, alongside the transport tick, so the reclamation epoch advances in lockstep with
    // the clock. TL3's Track In modules read the published snapshot through this accessor.
    synth::TimelineSnapshotExchange& getTimelineSnapshots() noexcept { return timelineSnapshots; }
    const synth::TimelineSnapshotExchange& getTimelineSnapshots() const noexcept { return timelineSnapshots; }

    // TL4-2: the automation binding table's hand-off — the resolved "lane -> live parameter" list
    // the applier walks each block. Published by publishTimeline() below; exposed mainly so tests
    // can inspect what resolved.
    synth::EpochExchange<synth::AutomationBindingTable>& getAutomationBindings() noexcept {
        return automationBindings_;
    }
    const synth::EpochExchange<synth::AutomationBindingTable>& getAutomationBindings() const noexcept {
        return automationBindings_;
    }

    // TL4-5: the audio -> UI reflection ring. AutomationApplier::applyBlock (renderPass, audio
    // thread) pushes into it; GraphEditor's 30 Hz timer drains it on the message thread to move
    // sliders without looping back through a parameter write. Always present (even in a
    // SYNTH_ENABLE_TIMELINE=0 build) so callers never need to null-check it — nothing pushes to it
    // in that build, so it simply stays empty.
    synth::AutomationUiFeed& getAutomationUiFeed() noexcept { return automationUiFeed_; }
    const synth::AutomationUiFeed& getAutomationUiFeed() const noexcept { return automationUiFeed_; }

    // TL4-2, MESSAGE THREAD: the one call that hands a TimelineDoc to the audio thread. Builds the
    // snapshot, resolves every automation lane against the CURRENT graph, and publishes the
    // snapshot FIRST and the binding table SECOND — that order is what makes a table's snapshot
    // pointer at most one publish behind the snapshot exchange (see AutomationApplier.h's lifetime
    // argument).
    //
    // Callers MUST re-call this after any graph change that adds, removes or replaces nodes (undo /
    // redo, patch apply, preset load, a module delete): bindings are resolved once, here, and a
    // stale table keeps automating the nodes it already resolved — safe, because each binding holds
    // a refcounted Node::Ptr, but a node added since the last publish is not automated until the
    // next one. Re-calling it with an unchanged doc is cheap and always correct.
    //
    // A no-op in a SYNTH_ENABLE_TIMELINE=0 build.
    void publishTimeline(const synth::TimelineDoc& doc);

    // TL4-2 stage 2: run the whole per-block sequence (transport tick, snapshot open, MIDI capture,
    // automation apply, graph render) once per 64-sample slice instead of once per callback, so
    // block-rate automation becomes control-rate automation.
    //
    // Default OFF, and it must stay that way until measured per patch: slicing is not audio-neutral.
    // Time-invariant processing doesn't care about block size, but anything with a per-block LFO
    // update or an FFT hop (Chorus, Phaser, PitchShifter …) renders audibly differently at 64
    // samples than at 512 — see AutomationSlicingTest.SliceParityTimeInvariantChain, which measures
    // exactly that difference. It also multiplies the per-block overhead (graph traversal, playhead
    // re-application, transport tick) by blockSize/64.
    void setAutomationSlicingEnabled(bool enabled) noexcept;
    bool isAutomationSlicingEnabled() const noexcept;

    // TL3-3: registers the sink that records external MIDI into timeline clips. Null by default —
    // capture is then a no-op. The recorder is called from exactly one site, renderNextBlock's
    // SYNTH_ENABLE_TIMELINE block, against the SAME buffer the graph itself renders — the collector-
    // drained (standalone) or host-delivered (hosted) stream, never the ExternalMidiModule push-path
    // copies handleIncomingMidiMessage also makes. See docs/architecture.md's "MIDI recording" note.
    void setMidiCaptureSink(synth::MidiRecorder* sink) noexcept {
        midiCaptureSink_.store(sink, std::memory_order_relaxed);
    }

    // TL4-4: registers the automation recorder whose gesture claims and global record arm the
    // applier consults each block (see AutomationApplier::applyBlock). Borrowed, never owned — the
    // same contract as setMidiCaptureSink: the owner must null this before destroying the recorder.
    // Null by default, in which case every lane plays back by its mode alone with no claims to
    // yield to. Only the AUDIO-VISIBLE half of the recorder is stored, so the audio thread can
    // never reach its doc, its undo manager or its capture buffers.
    void setAutomationRecorder(synth::AutomationRecorder* recorder) noexcept {
        automationRecordState_.store(recorder != nullptr ? &recorder->getAudioState() : nullptr,
                                     std::memory_order_relaxed);
    }

    // Total latency the graph reports for itself, in samples. This is report-only latency
    // aggregation: juce::AudioProcessorGraph already compensates parallel paths internally, so we
    // only surface the total (for the UI / host to display or pass on) — we do NOT compensate.
    int getGraphLatencySamples() const noexcept { return mainProcessorGraph.getLatencySamples(); }

    // The OUTPUT DEVICE's latency, in samples — the buffering between the graph writing a block and
    // that block leaving the speakers. Report-only, exactly like getGraphLatencySamples above: we
    // never compensate for it, we only surface it (TL5-4 offsets the drawn playhead by it so the
    // line matches what is being HEARD).
    //
    // 0 in Hosted mode (the host owns the device; asking our own idle AudioDeviceManager would be a
    // lie) and 0 whenever no device is open — e.g. every headless test.
    int getOutputLatencySamples() const noexcept {
        if (isHosted())
            return 0;
        if (auto* device = deviceManager.getCurrentAudioDevice())
            return device->getOutputLatencyInSamples();
        return 0;
    }

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
    // Declared before the graph for the same reason as the transport: TL3's timeline modules read
    // the exchange from the engine, and reverse-order destruction must take the graph's nodes down
    // first. Its own destructor reclaims everything published.
    synth::TimelineSnapshotExchange timelineSnapshots;
    // TL4-2. Declared before the graph for the same reverse-destruction-order reason, and with one
    // extra consequence worth naming: a published binding table holds refcounted Node::Ptrs, so a
    // table outliving the graph would keep those nodes' processors alive until it is freed. That is
    // safe (a juce::AudioProcessorGraph::Node references nothing back), and shutdown() reclaims the
    // exchange explicitly anyway, so it never happens in practice.
    synth::EpochExchange<synth::AutomationBindingTable> automationBindings_;
    synth::AutomationApplier automationApplier_;
    // TL4-5. Pre-allocated at construction (see AutomationUiFeed::kCapacity) — never grows, never
    // allocates from the audio thread.
    synth::AutomationUiFeed automationUiFeed_;
    juce::AudioProcessorGraph mainProcessorGraph;
    juce::AudioProcessorPlayer processorPlayer;

    // Message-thread only (initialise / shutdown / suspendDeviceCallback / resumeDeviceCallback),
    // read on the message thread by offline renderers. Never touched from the audio thread, so a
    // plain bool is the whole story.
    bool deviceCallbackAttached_ = false;

    std::atomic<bool> masterMuted_{false};
    std::atomic<bool> transportEnabled_{true};
    // TL4-2 stage 2. Off by default — see setAutomationSlicingEnabled().
    std::atomic<bool> automationSlicingEnabled_{false};
    // TL3-3: borrowed, never owned. Set by setMidiCaptureSink(); read once per callback in
    // renderNextBlock. Null default means capture is a no-op with no caller having to check.
    std::atomic<synth::MidiRecorder*> midiCaptureSink_{nullptr};
    // TL4-4: borrowed, never owned. Set by setAutomationRecorder(); read once per render pass and
    // handed straight to the applier. Null default means "no recorder", not "no automation".
    std::atomic<const synth::AutomationRecordState*> automationRecordState_{nullptr};

    void createDefaultPatch();

    // The one place the graph is actually clocked. Both the standalone device callback and the
    // hosted processBlock funnel through here so master-mute semantics (zero-fill AFTER the graph
    // runs, so sequencers / LFOs / envelopes keep advancing) can never drift between the two.
    void renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    // One complete render pass over `buffer`: transport tick, timeline snapshot open, MIDI capture,
    // automation apply, then the graph. With slicing off this runs once per callback over the whole
    // buffer; with slicing on it runs once per 64-sample slice. Everything the "once per callback"
    // contracts used to say is really "once per render pass" — that is what this function is.
    void renderPass(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    // TL4-2 stage 2: renderPass() per kAutomationSliceSamples-sample slice, over views into
    // `buffer` and per-slice MIDI re-based to slice-relative sample positions. Allocation-free —
    // the channel-pointer array and the MIDI scratch are sized in prepare (see prepareSliceScratch).
    void renderSliced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    // Sizes the slicing scratch for a given channel count / block size. Called from both prepare
    // paths (audioDeviceAboutToStart, prepareForHost) — never from the audio callback.
    void prepareSliceScratch(int numChannels, int blockSize);

    // Control-rate slice length. 64 samples is ~1.45 ms at 44.1 kHz — finer than any knob gesture
    // and coarse enough that the per-slice graph-traversal overhead stays bounded.
    static constexpr int kAutomationSliceSamples = 64;

    // Preallocated slicing scratch. `sliceChannelPointers_` backs the juce::AudioBuffer view
    // constructed per slice (the channel-pointer ctor takes no ownership and allocates nothing);
    // `sliceMidi_` is refilled per slice with ensureSize()'d storage that clear() keeps.
    std::vector<float*> sliceChannelPointers_;
    juce::MidiBuffer sliceMidi_;

    juce::MidiMessageCollector midiMessageCollector;
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
