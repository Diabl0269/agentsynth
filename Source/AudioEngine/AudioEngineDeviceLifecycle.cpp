// Concern: device lifecycle & stream format — construction, initialise/shutdown, the device-callback prepare path
// (audioDeviceAboutToStart/Stopped), and the scratch/format-change hooks they call.

#include "AudioEngine.h"
#include "Modules/ADSRModule.h"
#include "Modules/AudioInputModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/RecordTapModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include "Timeline/MidiRecorder.h"
#include <algorithm>

AudioEngine::AudioEngine(HostMode mode)
    : hostMode_(mode) {
    // Install the transport as the graph's playhead exactly once, here. JUCE's
    // AudioProcessorGraph re-applies graph.getPlayHead() to every node processor on every render
    // pass, so every module — including nodes re-created later by a preset load or an undo restore
    // — sees it through the standard getPlayHead() API. No per-node injection, and nothing to
    // re-wire when the graph's node set changes.
    mainProcessorGraph.setPlayHead(&transport);
}

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::initialise() {
    // Hosted mode: the plugin wrapper owns the audio clock and forwards the host's MIDI, so we
    // skip device/MIDI acquisition entirely and only build the initial patch. prepareForHost()
    // supplies the real sample rate/channel count later, since there is no device to ask.
    if (!isHosted()) {
        initialiseDevices(savedDeviceState_.get());
        // A machine with NO usable audio device (headless Linux, CI) never receives the
        // audioDeviceAboutToStart() that normally configures the graph before the patch is built
        // below. Unconfigured, the "Audio Output" IO node snapshots 0 channels and every
        // connection into it is rejected as out-of-range — silently gutting the default patch.
        // Mirror the hosted placeholder; a device appearing later reconfigures on start.
        if (deviceManager.getCurrentAudioDevice() == nullptr)
            mainProcessorGraph.setPlayConfigDetails(0, 2, 44100.0, 512);
    } else {
        // A default-constructed AudioProcessorGraph reports 0 output channels until something
        // sets its channel layout. The graph's "Audio Output" IO node snapshots that count once,
        // the moment it's added below (AudioGraphIOProcessor::setParentGraph) — so any patch
        // connection into it made before the host's first prepareToPlay() would otherwise be
        // rejected as out-of-range and silently dropped. Standalone gets this for free from
        // audioDeviceAboutToStart(), which always runs before the patch is built; mirror it here
        // with the same placeholder (0 in / 2 out) the plugin's BusesProperties declares.
        // prepareForHost() reconciles this with the host's real layout before playback starts.
        mainProcessorGraph.setPlayConfigDetails(0, 2, 44100.0, 512);
    }

    if (!synth::PresetManager::loadDefaultPreset(mainProcessorGraph)) {
        createDefaultPatch(); // Fallback
    }
}

void AudioEngine::setSavedDeviceState(std::unique_ptr<juce::XmlElement> state) { savedDeviceState_ = std::move(state); }

void AudioEngine::initialiseDevices(const juce::XmlElement* savedDeviceState) {
    if (savedDeviceState != nullptr) {
        // Restore the user's own device setup — which is the only way audio INPUT ever gets
        // enabled, since the Audio tab writes the channel mask into this XML the moment the user
        // ticks an input channel (juce::AudioDeviceSelectorComponent sets useDefaultInputChannels
        // = false, and juce::AudioDeviceManager then persists "audioDeviceInChans").
        //
        // The 0 input channels NEEDED is load-bearing, and deliberately not 2: it is the count JUCE
        // falls back to whenever the saved setup does NOT pin its input channels (a state saved
        // after the user changed only their output device or sample rate) and whenever the saved
        // device can't be opened at all (interface unplugged — selectDefaultDeviceOnFailure below
        // re-runs with these same counts). Asking for 2 there would silently switch a microphone on
        // for a user who never asked for one. It costs nothing when the saved state IS explicit:
        // juce::AudioDeviceManager::setAudioDeviceSetup re-derives the needed count from the mask.
        deviceManager.initialise(0, 2, savedDeviceState, /*selectDefaultDeviceOnFailure*/ true);
    } else {
        // No saved state — a fresh install, or an existing user who has never touched the Audio
        // tab. This is byte-identical to what initialise() has always done (output only, input
        // hard-off), which is what makes this need no migration step: absence of state IS the
        // legacy behaviour, and inputs stay opt-in.
        deviceManager.initialiseWithDefaultDevices(0, 2);
    }

    deviceManager.addAudioCallback(this);
    deviceCallbackAttached_ = true;

    // Persist-on-change: every device/rate/channel change the user makes broadcasts here,
    // and changeListenerCallback hands the new state to whoever installed onDeviceStateChanged.
    deviceManager.addChangeListener(this);

    // Initialise MIDI input collector
    // With no device the setup reports 0 Hz, which MidiMessageCollector::reset asserts on;
    // collected messages are only consumed from render passes, which a device-less engine
    // never runs, so the placeholder rate is inert.
    const double collectorRate = deviceManager.getAudioDeviceSetup().sampleRate;
    midiMessageCollector.reset(collectorRate > 0.0 ? collectorRate : 44100.0);

    // Enable all available MIDI inputs by default
    for (auto& info : juce::MidiInput::getAvailableDevices()) {
        auto input = juce::MidiInput::openDevice(info.identifier, this);
        if (input != nullptr) {
            input->start();
            midiInputs.push_back(std::move(input));
        }
    }
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // Only ever subscribed to our own device manager, and only in Standalone mode; both checks are
    // here so a future subscription can't silently start persisting something else's state.
    if (isHosted() || source != &deviceManager)
        return;

    if (onDeviceStateChanged)
        onDeviceStateChanged(deviceManager.createStateXml());
}

void AudioEngine::shutdown() {
    // FRO87: fire first, before anything below is touched -- this is what makes shutdown() safe
    // to call directly from ANY caller (a test, a future/hosted code path) without the caller
    // having to separately remember to detach UI-owned parameter attachments first. See the
    // member's own doc comment in AudioEngine.h.
    if (onBeforeShutdown)
        onBeforeShutdown();

    if (!isHosted()) {
        deviceManager.removeChangeListener(this);
        deviceManager.removeAudioCallback(this);
        deviceCallbackAttached_ = false;
#if JUCE_LINUX || JUCE_BSD || JUCE_MAC || JUCE_IOS
        for (auto& input : midiInputs) {
            input->stop();
        }
        midiInputs.clear();
#endif
    }
    mainProcessorGraph.clear();

    // Last, after the device callback is gone and the graph is empty: nothing can call
    // beginAudioBlock() any more, which is the precondition reclaimAllUnsafe() demands. The
    // binding tables go too: each holds refcounted Node::Ptrs, so leaving them until the
    // destructor would keep the just-cleared graph's processors alive for no reason.
    timelineSnapshots.reclaimAllUnsafe();
    automationBindings_.reclaimAllUnsafe();
    // Same precondition and same place: nothing can render any more, so the streamer's
    // prefetch thread can be stopped and every open reader closed.
    clipStreamer_.releaseAll();
}

void AudioEngine::createDefaultPatch() {
    mainProcessorGraph.clear();
    using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    // The input side is a real module (max channels fixed, visible jacks following the
    // device) reading the block's captured input off the playhead; the OUTPUT side stays a JUCE IO
    // node, because the graph's output channel count is tied to it.
    auto inputNode = mainProcessorGraph.addNode(std::make_unique<AudioInputModule>());
    auto outputNode =
        mainProcessorGraph.addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));

    auto sequencerNode = mainProcessorGraph.addNode(std::make_unique<SequencerModule>());
    auto oscillatorNode = mainProcessorGraph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = mainProcessorGraph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = mainProcessorGraph.addNode(std::make_unique<VCAModule>());
    auto adsrNode = mainProcessorGraph.addNode(std::make_unique<ADSRModule>("Amp Env"));
    auto filterAdsrNode = mainProcessorGraph.addNode(std::make_unique<ADSRModule>("Filter Env"));
    auto lfoNode = mainProcessorGraph.addNode(std::make_unique<LFOModule>());
    auto distortionNode = mainProcessorGraph.addNode(std::make_unique<DistortionModule>());
    auto delayNode = mainProcessorGraph.addNode(std::make_unique<DelayModule>());
    auto reverbNode = mainProcessorGraph.addNode(std::make_unique<ReverbModule>());

    inputNode->properties.set("x", 10.0f);
    inputNode->properties.set("y", 10.0f);
    sequencerNode->properties.set("x", 10.0f);
    sequencerNode->properties.set("y", 80.0f);
    oscillatorNode->properties.set("x", 540.0f);
    oscillatorNode->properties.set("y", 50.0f);
    filterNode->properties.set("x", 830.0f);
    filterNode->properties.set("y", 50.0f);
    vcaNode->properties.set("x", 1120.0f);
    vcaNode->properties.set("y", 50.0f);
    adsrNode->properties.set("x", 540.0f);
    adsrNode->properties.set("y", 450.0f);
    filterAdsrNode->properties.set("x", 830.0f);
    filterAdsrNode->properties.set("y", 450.0f);
    lfoNode->properties.set("x", 10.0f);
    lfoNode->properties.set("y", 500.0f);
    distortionNode->properties.set("x", 1410.0f);
    distortionNode->properties.set("y", 50.0f);
    delayNode->properties.set("x", 1690.0f);
    delayNode->properties.set("y", 50.0f);
    reverbNode->properties.set("x", 1970.0f);
    reverbNode->properties.set("y", 50.0f);
    outputNode->properties.set("x", 2250.0f);
    outputNode->properties.set("y", 300.0f);

    addModRouting(adsrNode->nodeID, 0, vcaNode->nodeID, 1);
    addModRouting(filterAdsrNode->nodeID, 0, filterNode->nodeID, 1);
    for (int i = 0; i < 4; ++i)
        addEmptyModRouting();

    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {oscillatorNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {adsrNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {filterAdsrNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{oscillatorNode->nodeID, 0}, {filterNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{sequencerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                      {filterNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
    mainProcessorGraph.addConnection({{vcaNode->nodeID, 0}, {distortionNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{vcaNode->nodeID, 0}, {distortionNode->nodeID, 1}});
    mainProcessorGraph.addConnection({{distortionNode->nodeID, 0}, {delayNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{distortionNode->nodeID, 1}, {delayNode->nodeID, 1}});
    mainProcessorGraph.addConnection({{delayNode->nodeID, 0}, {reverbNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{delayNode->nodeID, 1}, {reverbNode->nodeID, 1}});
    mainProcessorGraph.addConnection({{reverbNode->nodeID, 0}, {outputNode->nodeID, 0}});
    mainProcessorGraph.addConnection({{reverbNode->nodeID, 1}, {outputNode->nodeID, 1}});
}

void AudioEngine::prepareSliceScratch(int numChannels, int blockSize) {
    // Message thread (both prepare paths). Sized with headroom so a host that hands us a wider
    // buffer than it declared still takes the sliced path instead of silently falling back.
    const int channels = std::max(numChannels, 2) + 2;
    sliceChannelPointers_.assign(static_cast<std::size_t>(channels), nullptr);

    // Worst case for one slice is every event in the block landing inside it. juce::MidiBuffer
    // stores 4 bytes of header + the message bytes per event; 16 bytes per event is generous for
    // the note/CC traffic this path sees, and ensureSize is a no-op once the storage is big enough.
    sliceMidi_.clear();
    sliceMidi_.ensureSize(static_cast<std::size_t>(std::max(blockSize, kAutomationSliceSamples)) * 16u);
}

void AudioEngine::handleStreamFormatChange(double newRate, int newBlockSize) {
    // Order matters — see docs/architecture.md's "Device & sample-rate changes".
    //
    // 1. TRANSPORT FIRST: every other consumer below, and every module's NEXT processBlock, must see
    //    the new rate consistently once this call returns. TransportService::prepare() keeps the
    //    beat (not the sample) canonical, so the musical position survives untouched; only
    //    the sample-domain mirror of it moves.
    transport.prepare(newRate, newBlockSize);
    onFormatChangeStepForTest(1);

    // 2. METRONOME: a voice ringing across the boundary was computed for the OLD rate (see
    //    Metronome::startClick) and would otherwise continue at the wrong pitch and the wrong
    //    length. See Metronome::resetVoices().
    metronome_.resetVoices();
    onFormatChangeStepForTest(2);

    // 3. STREAMER: force every Track Audio ring to miss until the prefetch thread has refilled it at
    //    the NEW mapping, rather than risk a coincidental hit on content filled under the OLD one.
    //    See AudioClipStreamer::invalidateAllStreams().
    clipStreamer_.invalidateAllStreams();
    onFormatChangeStepForTest(3);

    // 4. IN-FLIGHT TAKES: neither an audio take's WAV (fixed header rate, written at whatever rate
    //    was active at startCapture()) nor a MIDI take's beat-domain events can honestly span a
    //    format change — an audio take that kept recording across the boundary would mix two actual
    //    sample rates under one header, and there is no clean way to splice that. Rather than try,
    //    flag it here (at the exact moment of the change) so MainComponent's 10 Hz poll finalizes it
    //    through the SAME commit choke points a manual Record-off or a transport stop already use —
    //    see consumeFormatChangedDuringCapture().
    bool anyCapturing = false;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor())) {
            if (tap->isCapturing()) {
                anyCapturing = true;
                break;
            }
        }
    }
    if (!anyCapturing) {
        if (auto* recorder = midiCaptureSink_.load(std::memory_order_relaxed))
            anyCapturing = recorder->isRecording();
    }
    if (anyCapturing)
        formatChangedDuringCapture_.store(true, std::memory_order_relaxed);
    onFormatChangeStepForTest(4);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device) {
        const int numInputChannels = device->getActiveInputChannels().countNumberOfSetBits();
        const int numOutputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
        const double sampleRate = device->getCurrentSampleRate();
        const int blockSize = device->getCurrentBufferSizeSamples();

        mainProcessorGraph.setPlayConfigDetails(numInputChannels, numOutputChannels, sampleRate, blockSize);
        // Before the graph: nodes read the playhead from their first prepared block onwards, so the
        // transport must already be on the device's sample rate when they do. This also
        // resets the metronome, invalidates the clip streamer and flags any in-flight take.
        handleStreamFormatChange(sampleRate, blockSize);
        // Both scratches are sized against max(in, out) — that is the channel count the
        // render buffer has once the device's input is copied into it, so sizing either on the
        // output count alone would make a 2-in/1-out device fall out of the sliced path (and, for
        // the device scratch, force an allocation in the callback).
        prepareDeviceScratch(numInputChannels, numOutputChannels, blockSize);
        prepareSliceScratch(std::max(numInputChannels, numOutputChannels), blockSize);
        prepareDeviceInputSnapshot(numInputChannels, blockSize);
        deviceInputLatencySamples_.store(device->getInputLatencyInSamples(), std::memory_order_relaxed);
        deviceOutputLatencySamples_.store(device->getOutputLatencyInSamples(), std::memory_order_relaxed);
        mainProcessorGraph.prepareToPlay(sampleRate, blockSize);
    }
}

void AudioEngine::prepareDeviceScratch(int numInputChannels, int numOutputChannels, int blockSize) {
    // Message thread (or whichever thread JUCE prepares the device on) — never the callback. Two
    // spare channels of headroom for the same reason prepareSliceScratch keeps them: a device that
    // hands us a wider block than it declared must not push the callback into allocating.
    const int channels = std::max({numInputChannels, numOutputChannels, 2}) + 2;
    deviceChannelPointers_.assign(static_cast<std::size_t>(channels), nullptr);

    // Sized to hold EVERY channel, not just the ones past the output count: a device may also hand
    // the callback a null pointer for an output channel, and that channel then needs scratch too.
    deviceScratch_.setSize(channels, std::max(blockSize, 1), /*keepExistingContent*/ false,
                           /*clearExtraSpace*/ true, /*avoidReallocating*/ false);
    deviceScratch_.clear();
}

void AudioEngine::prepareDeviceInputSnapshot(int numInputChannels, int blockSize) {
    // Message thread (both prepare paths) — never the callback. Always the full channel width, so
    // a device change that ADDS inputs never has to resize anything from the audio thread; only
    // the per-block "how many did we capture" count varies. Two blocks' worth of length is the same
    // headroom the other scratches keep, for a device or host that overruns what it declared.
    deviceInputSnapshot_.setSize(synth::TransportService::kMaxDeviceInputChannels, std::max(blockSize, 1) * 2,
                                 /*keepExistingContent*/ false, /*clearExtraSpace*/ true,
                                 /*avoidReallocating*/ false);
    deviceInputSnapshot_.clear();
    deviceInputChannelsThisBlock_ = 0;
    deviceInputPointers_.fill(nullptr);
    deviceInputChannelCount_.store(std::max(0, numInputChannels), std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped() {
    deviceInputLatencySamples_.store(0, std::memory_order_relaxed);
    deviceOutputLatencySamples_.store(0, std::memory_order_relaxed);
    deviceInputChannelCount_.store(0, std::memory_order_relaxed);
    deviceInputChannelsThisBlock_ = 0;
    mainProcessorGraph.releaseResources();
}
