#pragma once

#include "Modules/ModuleBase.h"
#include "Timeline/AudioClipStreamer.h"
#include "Timeline/AutomationApplier.h"
#include "Timeline/EpochExchange.h"
#include "Timeline/TimelineSnapshotExchange.h"
#include "Transport/Metronome.h"
#include "Transport/TransportService.h"
#include <array>
#include <atomic>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace synth {
class MidiRecorder; // Forward declaration (Source/Timeline/MidiRecorder.h)
class TimelineDoc;  // Forward declaration (Source/Timeline/TimelineDoc.h)
} // namespace synth

class AudioEngine
    : public juce::AudioIODeviceCallback
    , public juce::MidiInputCallback
    , public juce::ChangeListener {
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

    // ---- Persisted device state (TL6-1, Standalone only) ----
    // MESSAGE THREAD, before initialise(). Hands the engine the device setup an earlier session
    // persisted (a juce::AudioDeviceManager "DEVICESETUP" element, as produced by
    // getDeviceManager().createStateXml() and handed to the owner through onDeviceStateChanged
    // below). With a state set, initialise() restores it; with none — a fresh install, or any
    // install whose user has never touched the Audio tab — initialise() takes exactly the path it
    // always took, so audio INPUT stays off until the user opts in. There is no migration step for
    // existing users: "no saved state" IS the legacy behaviour.
    //
    // A setter rather than an initialise(const XmlElement*) overload because both of
    // MainComponent's initialise() call sites (the runtime-permission callback and the direct one)
    // would otherwise have to carry the argument, and because the engine keeps the state for any
    // later re-initialise.
    void setSavedDeviceState(std::unique_ptr<juce::XmlElement> state);
    bool hasSavedDeviceState() const noexcept { return savedDeviceState_ != nullptr; }

    // MESSAGE THREAD, installed by the OWNER (MainComponent on the standalone path) before
    // initialise(). Called whenever the AudioDeviceManager reports a change — the user picking a
    // device, a sample rate, or ticking an input channel in the Audio tab — with the manager's
    // fresh state to persist. The engine deliberately knows nothing about ApplicationProperties:
    // Core never touches settings, so the owner does the storing.
    //
    // The payload may be null: juce::AudioDeviceManager only produces a DEVICESETUP element once a
    // setup has been chosen EXPLICITLY (its startup defaults are not "explicit"), which is exactly
    // what keeps an untouched install on the legacy defaults path above. Owners must null-check.
    std::function<void(std::unique_ptr<juce::XmlElement>)> onDeviceStateChanged;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

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

    // ---- Input monitoring gate + feedback guard (TL6-7) ----
    // ANY THREAD (message-thread writes from MainComponent's poll; the audio thread also writes it,
    // on a guard trip). Published to the transport carrier once per render pass — see
    // TransportService::setInputMonitoringEnabledForBlock — which is what AudioInputModule actually
    // reads; nothing downstream of the graph ever reads this atomic directly. See
    // docs/architecture.md's "Input monitoring & feedback guard (TL6-7)".
    void setInputMonitoringEnabled(bool enabled) noexcept;
    bool isInputMonitoringEnabled() const noexcept;

    // The feedback guard's one-shot report: true if the guard tripped since the last call, false
    // otherwise — and an atomic exchange back to false in the same call, so a caller that polls
    // (MainComponent's 10 Hz timer) consumes a trip exactly once however many ticks pass before it
    // reads it.
    bool consumeFeedbackGuardTripped() noexcept;

    // TL6-9: the one-shot report that a device/sample-rate change happened while a take (audio or
    // MIDI) was rolling — same exchange-back-to-false contract as consumeFeedbackGuardTripped(), so
    // MainComponent's 10 Hz poll consumes it exactly once and finalizes whatever take was in flight
    // through the SAME commit choke points a manual Record-off or a transport stop already use (a
    // take must never span a format change — see handleStreamFormatChange()).
    bool consumeFormatChangedDuringCapture() noexcept {
        return formatChangedDuringCapture_.exchange(false, std::memory_order_relaxed);
    }

    // Thresholds the feedback guard trips on. A block's peak sample magnitude (over the graph's
    // OUTPUT channels only, post-graph) staying at or above kFeedbackPeakThreshold for a running
    // total of kFeedbackSustainSeconds — measured in elapsed SAMPLES, not block count, so the timing
    // is block-size-agnostic — disables monitoring, latches the tripped flag above, and zeroes that
    // block's output immediately. Never evaluated while monitoring is disabled. See renderPass /
    // runFeedbackGuard and docs/architecture.md.
    static constexpr float kFeedbackPeakThreshold = 0.97f;
    static constexpr double kFeedbackSustainSeconds = 0.25;

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

    // TL5-6: the click generator, summed POST-graph in renderPass (see the class comment there and
    // docs/architecture.md's Metronome subsection). Message-thread callers use setEnabled/
    // setForcedOn/isEnabled/isForcedOn; the audio thread only ever calls renderClicks(), from inside
    // renderPass.
    synth::Metronome& getMetronome() noexcept { return metronome_; }
    const synth::Metronome& getMetronome() const noexcept { return metronome_; }

    // The timeline's message-thread -> audio-thread hand-off (TL2-2). The message thread publishes
    // snapshots here (and may reap on a timer); the engine opens exactly one audio block on it per
    // callback, alongside the transport tick, so the reclamation epoch advances in lockstep with
    // the clock. TL3's Track In modules read the published snapshot through this accessor.
    synth::TimelineSnapshotExchange& getTimelineSnapshots() noexcept { return timelineSnapshots; }
    const synth::TimelineSnapshotExchange& getTimelineSnapshots() const noexcept { return timelineSnapshots; }

    // TL6-4: the disk-streaming service behind "Track Audio" clip playback. Owned by the engine (it
    // outlives any one snapshot, unlike the exchange's payloads), kept in step with the published
    // timeline by publishTimeline(), and handed to the modules through the playhead in renderPass.
    // Exposed mainly so MainComponent can set its asset roots and tests can drive its pump.
    synth::AudioClipStreamer& getAudioClipStreamer() noexcept { return clipStreamer_; }
    const synth::AudioClipStreamer& getAudioClipStreamer() const noexcept { return clipStreamer_; }

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
    // Like getInputLatencySamples() below, this reads a value CACHED at audioDeviceAboutToStart
    // (TL6-8 made the two symmetric): it is the latency of the stream that is actually RUNNING —
    // an engine whose callback is detached is not being clocked by any device, so reporting the
    // device manager's idle number would be a lie — and caching is what makes it assertable
    // headlessly against a fake device.
    //
    // 0 in Hosted mode (the host owns the device) and 0 whenever no device has started — e.g. every
    // headless test that never calls audioDeviceAboutToStart.
    int getOutputLatencySamples() const noexcept {
        if (isHosted())
            return 0;
        return deviceOutputLatencySamples_.load(std::memory_order_relaxed);
    }

    // The INPUT DEVICE's latency, in samples — the buffering between a sample arriving at the
    // hardware input and the graph seeing it. Report-only, exactly like the two above; TL6-8
    // consumes it when aligning recorded input against the timeline.
    //
    // Cached at audioDeviceAboutToStart, cleared at audioDeviceStopped — see the output sibling
    // above for the argument. 0 in Hosted mode and 0 whenever no device has started.
    int getInputLatencySamples() const noexcept {
        if (isHosted())
            return 0;
        return deviceInputLatencySamples_.load(std::memory_order_relaxed);
    }

    // TL6-8: the ROUND-TRIP latency an audio take has to be shifted back by, in samples —
    //
    //     input device + graph + output device
    //
    // and the reason it is that sum rather than just the input's, which is the whole argument
    // behind recording alignment:
    //
    //   a musician plays against what they HEAR. A click (or a backing track) sitting at timeline
    //   sample G leaves the speakers `output` samples after the graph rendered it, so the performer
    //   hears grid-position G at wall-clock G + output and plays their note THEN. That note travels
    //   back through the input device and reaches our callback a further `input` samples later, plus
    //   whatever latency the graph itself reports between the input node and the tap. So a note
    //   MEANT for timeline sample G is captured at timeline sample G + input + graph + output, and
    //   the take must be shifted back by exactly that to land where it was played.
    //
    // Report-only aggregation, like each of its three terms: nothing here compensates anything. The
    // one consumer is the take-commit math (synth::computeTakePlacement), plus the status bar's
    // "RT" readout. Note that this is deliberately NOT what TL5-4's drawn playhead uses — that one
    // offsets by the OUTPUT latency alone, because it answers a different question ("where is the
    // audio the user is hearing right now?"). See docs/architecture.md.
    int getRecordingLatencySamples() const noexcept {
        return getInputLatencySamples() + getGraphLatencySamples() + getOutputLatencySamples();
    }

    // TL6-1 introspection for tests: the dimensions audioDeviceAboutToStart sized the device
    // callback's scratch to. The callback must never allocate, so the scratch has to cover the
    // widest/longest block the device can deliver BEFORE the first one arrives — see
    // Tests/AudioInputTests.cpp.
    struct DeviceScratchInfo {
        int numChannelPointers = 0; // channel-pointer array the render buffer is built from
        int numScratchChannels = 0; // spare writable channels (inputs past the output count)
        int numScratchSamples = 0;
    };
    DeviceScratchInfo getDeviceScratchInfo() const noexcept {
        return {static_cast<int>(deviceChannelPointers_.size()), deviceScratch_.getNumChannels(),
                deviceScratch_.getNumSamples()};
    }

    // TL6-2: how many INPUT channels the graph is currently prepared for — the device's active
    // input count in Standalone mode, the host's declared input count in Hosted mode, and 0 before
    // either prepare path has run (every headless test, and any install whose user has not opted
    // into an input device). Cached at prepare time on whichever thread prepares, read from the
    // message thread, so it is atomic exactly like getInputLatencySamples()'s cache.
    //
    // The audio thread does NOT consult this — it publishes the real per-block channel count
    // through the playhead (TransportService::setDeviceInputForBlock). This is the message-thread
    // half: it is what lets the UI resize an Audio Input card the moment the device changes,
    // instead of waiting for the audio thread to render a block with the new layout.
    int getDeviceInputChannelCount() const noexcept { return deviceInputChannelCount_.load(std::memory_order_relaxed); }

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

protected:
    // TL6-1 TEST SEAM, and the ONE place initialise() touches real hardware in Standalone mode:
    // it opens the audio device (from `savedDeviceState` when there is one, from JUCE's defaults
    // when there isn't), attaches this engine as the device callback, subscribes to device-state
    // changes and opens every available MIDI input. A test subclass overrides it to assert WHICH
    // branch initialise() chose without any of the side effects — no device is opened, no MIDI
    // input is grabbed, and the engine is left unattached (isReceivingDeviceCallbacks() stays
    // false), which is precisely the state a headless test wants.
    virtual void initialiseDevices(const juce::XmlElement* savedDeviceState);

    // TL6-9 TEST SEAM: called once per step inside handleStreamFormatChange(), in the order those
    // steps actually run (1 = after transport.prepare(), 2 = after metronome_.resetVoices(), 3 =
    // after clipStreamer_.invalidateAllStreams(), 4 = after the in-flight-take check). A no-op in
    // production — nothing calls it, so the optimizer removes it entirely — a test subclass
    // overrides it to record what it observes (e.g. the transport's rate) at each step, which is
    // what pins the ORDER those steps run in without instrumenting the production logic itself. See
    // Tests/DeviceChangeTests.cpp's HookOrderPinned.
    virtual void onFormatChangeStepForTest(int step) { juce::ignoreUnused(step); }

private:
    const HostMode hostMode_;

    juce::AudioDeviceManager deviceManager;
    // Declared before the graph on purpose: the graph holds a raw AudioPlayHead pointer to the
    // transport, and members are destroyed in reverse declaration order, so the graph goes first.
    synth::TransportService transport;
    // TL5-6: the click generator. Independent of the graph (it only reads a BlockTimeInfo and writes
    // into the caller's buffer), so its declaration order relative to the graph carries no lifetime
    // constraint — placed here because it is conceptually paired with the transport it clicks from.
    synth::Metronome metronome_;
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
    // TL6-4. Declared before the graph for the same reverse-destruction-order reason as the
    // transport and the exchanges: nodes read it through the playhead while they render, so the
    // graph's nodes must be gone before it is. Its own destructor stops the prefetch thread and
    // frees every reader.
    synth::AudioClipStreamer clipStreamer_;
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
    // TL6-7. Message-thread writes (MainComponent's poll) and audio-thread writes (the guard, on a
    // trip); read on the audio thread each render pass to publish to the transport carrier, and on
    // the message thread by isInputMonitoringEnabled(). Default false: with nothing ever calling
    // setInputMonitoringEnabled(true) — a SYNTH_ENABLE_TIMELINE=OFF build, or a build with the flag
    // on but nothing armed — this is exactly today's silent-input behaviour.
    std::atomic<bool> inputMonitoringEnabled_{false};
    // TL6-7. Set true by the guard (audio thread) on a trip; consumed (and reset) by
    // consumeFeedbackGuardTripped() (message thread poll).
    std::atomic<bool> feedbackGuardTripped_{false};
    // TL6-9. Set true by handleStreamFormatChange() (the prepare path) when a take was capturing at
    // the moment of the change; consumed (and reset) by consumeFormatChangedDuringCapture() (message
    // thread poll).
    std::atomic<bool> formatChangedDuringCapture_{false};
    // TL6-7, AUDIO THREAD ONLY: the guard's running "how many consecutive samples has the block
    // peak stayed >= kFeedbackPeakThreshold" counter, in samples so its timing is block-size-
    // agnostic. Reset to 0 whenever a block falls under the threshold, monitoring is disabled, or
    // the guard trips.
    std::int64_t feedbackGuardConsecutiveSamples_ = 0;
    // TL4-2 stage 2. Off by default — see setAutomationSlicingEnabled().
    std::atomic<bool> automationSlicingEnabled_{false};
    // TL3-3: borrowed, never owned. Set by setMidiCaptureSink(); read once per callback in
    // renderNextBlock. Null default means capture is a no-op with no caller having to check.
    std::atomic<synth::MidiRecorder*> midiCaptureSink_{nullptr};
    // TL4-4: borrowed, never owned. Set by setAutomationRecorder(); read once per render pass and
    // handed straight to the applier. Null default means "no recorder", not "no automation".
    std::atomic<const synth::AutomationRecordState*> automationRecordState_{nullptr};

    void createDefaultPatch();

    // TL6-9: the one consolidated prepare-path hook, called from BOTH audioDeviceAboutToStart and
    // prepareForHost whenever the engine's sample rate or block size changes. See
    // docs/architecture.md's "Device & sample-rate changes (TL6-9)" for the full order argument;
    // summary: transport FIRST (every other consumer, and every module's next processBlock, must
    // see the new rate consistently), then the metronome's voice pool, then the clip streamer's
    // ring invalidation, then the in-flight-take check that sets formatChangedDuringCapture_. Does
    // NOT touch the channel-count-dependent scratch/latency-cache/input-snapshot prep — those stay
    // separate calls in each of the two call sites because their sizes come from arguments this
    // function does not take (numInputChannels/numOutputChannels).
    void handleStreamFormatChange(double newRate, int newBlockSize);

    // The one place the graph is actually clocked. Both the standalone device callback and the
    // hosted processBlock funnel through here so master-mute semantics (zero-fill AFTER the graph
    // runs, so sequencers / LFOs / envelopes keep advancing) can never drift between the two.
    void renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    // One complete render pass over `buffer`: transport tick, timeline snapshot open, MIDI capture,
    // automation apply, then the graph. With slicing off this runs once per callback over the whole
    // buffer; with slicing on it runs once per 64-sample slice. Everything the "once per callback"
    // contracts used to say is really "once per render pass" — that is what this function is.
    //
    // `inputSampleOffset` is where this pass starts inside the callback's captured device input
    // (0 unless slicing is on), so an Audio Input module reads THIS slice's samples rather than
    // replaying the first slice for the whole block.
    void renderPass(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, int inputSampleOffset = 0);

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

    // TL6-1: the device setup restored by initialise(), or null for "use JUCE's defaults, inputs
    // off". Message thread only (setSavedDeviceState / initialise).
    std::unique_ptr<juce::XmlElement> savedDeviceState_;

    // TL6-1 device-callback scratch, both sized in audioDeviceAboutToStart and never resized from
    // the audio thread. `deviceChannelPointers_` backs the juce::AudioBuffer the callback renders
    // through (max(in, out) channels — see audioDeviceIOCallbackWithContext for why that buffer is
    // not simply the device's output pointers); `deviceScratch_` provides writable storage for the
    // channels the device's outputs don't cover, i.e. a device with more inputs than outputs.
    std::vector<float*> deviceChannelPointers_;
    juce::AudioBuffer<float> deviceScratch_;

    // TL6-1: cached at audioDeviceAboutToStart, cleared at audioDeviceStopped. Written from
    // whichever thread JUCE prepares the device on, read by anyone — hence atomic. See
    // getInputLatencySamples().
    std::atomic<int> deviceInputLatencySamples_{0};
    // TL6-8: the output sibling of the above, cached at the same two sites for the same reasons.
    // See getOutputLatencySamples().
    std::atomic<int> deviceOutputLatencySamples_{0};

    // Sizes the device-callback scratch above. Called from audioDeviceAboutToStart only — the
    // hosted path renders into the host's own buffer and needs none of this.
    void prepareDeviceScratch(int numInputChannels, int numOutputChannels, int blockSize);

    // ---- TL6-2: the block's device-input snapshot ----
    // The device's (or host's) input, copied ONCE per callback into storage the graph never
    // touches, and handed to the modules through the playhead. It cannot be the render buffer:
    // the graph renders in place over exactly those channels, so a module reading them mid-graph
    // would see partly-rendered output. Both host modes take the same copy, so "Audio Input"
    // behaves identically standalone and in a plugin.
    //
    // Sized in both prepare paths, never from the audio thread. A block longer than the snapshot
    // (a device or host exceeding the block size it declared) publishes NO input for that block
    // rather than allocating — the module then renders silence, which is the same degraded-but-safe
    // posture as the device scratch above.
    juce::AudioBuffer<float> deviceInputSnapshot_;
    // Refilled per render pass (offset into the snapshot when slicing is on) and handed to the
    // transport. Fixed size, so publishing allocates nothing.
    std::array<const float*, synth::TransportService::kMaxDeviceInputChannels> deviceInputPointers_{};
    // Audio thread only: how many snapshot channels this callback actually captured.
    int deviceInputChannelsThisBlock_ = 0;
    // Message-thread mirror of the prepared input channel count. See getDeviceInputChannelCount().
    std::atomic<int> deviceInputChannelCount_{0};

    void prepareDeviceInputSnapshot(int numInputChannels, int blockSize);
    // AUDIO THREAD, once per callback, BEFORE the graph runs. Copies the caller's input channels
    // into deviceInputSnapshot_ and records how many landed there.
    void captureDeviceInput(const float* const* inputChannelData, int numInputChannels, int numSamples) noexcept;
    // AUDIO THREAD, once per render pass. Points the transport at this pass's slice of the capture.
    void publishDeviceInputForPass(int sampleOffset, int numSamples) noexcept;

    // TL6-7: the feedback guard itself. Called from renderPass, post-graph and pre-master-mute
    // (beside the metronome), ungated like the monitoring flag — an input-path safety feature, not a
    // timeline one. `monitoringEnabledThisPass` is the SAME value renderPass just published to the
    // transport, so the guard and the module it's gating agree within one render pass.
    void runFeedbackGuard(juce::AudioBuffer<float>& buffer, bool monitoringEnabledThisPass) noexcept;

    // Preallocated slicing scratch. `sliceChannelPointers_` backs the juce::AudioBuffer view
    // constructed per slice (the channel-pointer ctor takes no ownership and allocates nothing);
    // `sliceMidi_` is refilled per slice with ensureSize()'d storage that clear() keeps.
    std::vector<float*> sliceChannelPointers_;
    juce::MidiBuffer sliceMidi_;

    juce::MidiMessageCollector midiMessageCollector;
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
