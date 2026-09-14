// Concern: render pass — the device callback's audio-thread entry point, one-render-pass-at-a-time bookkeeping,
// slicing, the device-input snapshot and the feedback guard.

#include "AudioEngine.h"
#include "Timeline/MidiRecorder.h"
#include <algorithm>

namespace {

/** Counts a render pass in and out for the whole of renderNextBlock — the audio half of the
 *  teardown handshake in AudioEngine::drainAudioCallbacks(). Allocation-free and lock-free; the
 *  cost is two atomic RMWs per pass.
 *
 *  Two monotonic counters rather than one in-flight count, because a plugin's audio thread renders
 *  back-to-back: a drain polling "is anything in flight?" can miss the microscopic gap between one
 *  block and the next forever, where "have the passes that were already running finished?" always
 *  completes within one block. */
struct ScopedRenderPass {
    ScopedRenderPass(std::atomic<std::uint64_t>& started, std::atomic<std::uint64_t>& finished) noexcept
        : finished_(finished) {
        // seq_cst, not acq_rel: this increment must not be reordered after the borrowed-pointer
        // reads it protects, and a message thread that has already published a new pointer must
        // then see this pass. Anything weaker leaves the Dekker-style store/load pair open.
        started.fetch_add(1, std::memory_order_seq_cst);
    }
    ~ScopedRenderPass() { finished_.fetch_add(1, std::memory_order_release); }

    std::atomic<std::uint64_t>& finished_;
};

} // namespace

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                   float* const* outputChannelData, int numOutputChannels,
                                                   int numSamples, const juce::AudioIODeviceCallbackContext& context) {
    juce::ignoreUnused(context);

    // The channel-aliasing subtlety, which is why this is not simply a buffer wrapped
    // around outputChannelData:
    //
    // juce::AudioProcessor renders IN PLACE over ONE buffer of max(numIn, numOut) channels.
    // Channels [0, numInputChannels) are what the graph's "Audio Input" node reads and channels
    // [0, numOutputChannels) are what its "Audio Output" node writes — the SAME memory. So the
    // device's input has to be COPIED into that buffer before the graph runs: the device's input
    // block is const and the graph is going to overwrite these channels with its result. Any
    // channel the input doesn't cover starts silent, exactly as before.
    //
    // The channels the device's outputs don't cover (a device with more inputs than outputs) come
    // from deviceScratch_. Both it and the pointer array are sized in audioDeviceAboutToStart, so
    // nothing here allocates; if a block ever arrives wider or longer than that, we serve the
    // output channels alone rather than allocate. This is juce::AudioProcessorPlayer's pattern.
    const auto pointerCapacity = static_cast<int>(deviceChannelPointers_.size());
    const int scratchChannels = deviceScratch_.getNumChannels();
    const bool scratchFitsBlock = deviceScratch_.getNumSamples() >= numSamples;

    int totalChannels = std::min(std::max(numInputChannels, numOutputChannels), pointerCapacity);
    int nextScratchChannel = 0;

    for (int channel = 0; channel < totalChannels; ++channel) {
        float* dest =
            (channel < numOutputChannels && outputChannelData != nullptr) ? outputChannelData[channel] : nullptr;
        if (dest == nullptr) {
            // An input channel past the output count, or an output channel the device handed us as
            // null. Borrow a scratch channel; if there is none to borrow, stop here — the render
            // buffer keeps every channel it has already resolved, which always includes the ones
            // the speakers will actually read.
            if (!scratchFitsBlock || nextScratchChannel >= scratchChannels) {
                totalChannels = channel;
                break;
            }
            dest = deviceScratch_.getWritePointer(nextScratchChannel++);
        }

        const float* src =
            (channel < numInputChannels && inputChannelData != nullptr) ? inputChannelData[channel] : nullptr;

        // This copy stays unconditional — it is the CAPTURE path (it feeds the render
        // buffer's now-vestigial IO-node channels and, below, captureDeviceInput's snapshot), and
        // the engine always captures input, armed or monitored or not. What actually gates the mic
        // -> speaker loop lives downstream of here: AudioInputModule::processBlock silences its own
        // graph output while monitoring is disabled (TransportService::isInputMonitoringEnabledForBlock),
        // and AudioEngine::runFeedbackGuard (called from renderPass, post-graph) disables monitoring
        // and zeroes the block outright if the output stays near-clip too long. See
        // docs/architecture.md's "Input monitoring & feedback guard".
        if (src != nullptr)
            std::copy(src, src + numSamples, dest);
        else
            std::fill(dest, dest + numSamples, 0.0f);

        deviceChannelPointers_[static_cast<std::size_t>(channel)] = dest;
    }

    // Any output channel that did not make it into the render buffer (only reachable in the
    // degraded no-scratch path above, or if a block somehow arrived before a prepare) still has to
    // leave the speakers silent rather than replaying whatever the device left in it.
    for (int channel = totalChannels; channel < numOutputChannels; ++channel)
        if (outputChannelData != nullptr && outputChannelData[channel] != nullptr)
            std::fill(outputChannelData[channel], outputChannelData[channel] + numSamples, 0.0f);

    // A SECOND copy of the input, into storage the graph never renders over, taken before
    // the graph runs. The copy above put the input into the render buffer for the "Audio Input" IO
    // node's benefit; the graph then overwrites those very channels with its result, so a module
    // that reads them mid-graph sees output, not input. See captureDeviceInput.
    captureDeviceInput(inputChannelData, numInputChannels, numSamples);

    juce::AudioBuffer<float> buffer(deviceChannelPointers_.data(), totalChannels, numSamples);
    juce::MidiBuffer midiMessages;
    midiMessageCollector.removeNextBlockOfMessages(midiMessages, numSamples);

    // Collect MIDI from active ExternalMidiModules
    // The ExternalMidiModule is just a processor in the graph.
    // It should collect messages in handleIncomingMidiMessage,
    // and then processBlock should output those to its MIDI output port.
    // If we call processBlock on it, we might be clearing its buffer too early.
    // Let's rely on the graph to process all nodes.

    // MIDI messages from collector are already in midiMessages.

    renderNextBlock(buffer, midiMessages);
}

void AudioEngine::drainAudioCallbacks() noexcept {
    // MESSAGE THREAD. The other half of ScopedRenderPass's handshake. The caller has already
    // published its new pointer with a seq_cst store, so every pass that STARTS from here on reads
    // the new value; this only has to outlast the passes that had already started, which is exactly
    // what waiting for the finished count to catch up with the started count says. It does NOT wait
    // for the audio thread to go idle — an engine inside a host never does.
    //
    // Bounded on purpose. A device thread wedged inside a callback (or a host that never returns
    // from processBlock) would otherwise hang the message thread during teardown; giving up after
    // the timeout is strictly better than a deadlock. Returns immediately whenever nothing is
    // clocking the graph — a suspended device callback, a headless test, a hosted engine between
    // blocks.
    static constexpr int kDrainTimeoutMs = 2000;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)kDrainTimeoutMs;
    const auto target = renderPassesStarted_.load(std::memory_order_seq_cst);

    int spins = 0;
    while (renderPassesFinished_.load(std::memory_order_acquire) < target) {
        if (juce::Time::getMillisecondCounter() > deadline) {
            jassertfalse; // a render pass outlasted the drain: something is holding the audio thread
            return;
        }
        // A block is milliseconds at most, so spinning wins the common case; fall back to sleeping
        // rather than burning a core if the pass really is long (a huge buffer, a stalled host).
        if (++spins < 1000)
            juce::Thread::yield();
        else
            juce::Thread::sleep(1);
    }
}

void AudioEngine::renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // The one place a render pass is declared in flight. Both entry points (the standalone device
    // callback and the hosted processBlock) funnel through here, so this single guard covers every
    // read of the borrowed midiCaptureSink_ / automationRecordState_ pointers below — see
    // drainAudioCallbacks(), which is what an owner destroying either of them waits on.
    const ScopedRenderPass renderPassGuard(renderPassesStarted_, renderPassesFinished_);

    // Off by default, in which case this is one pass over the whole buffer and the
    // behaviour is byte-identical to what it was before slicing existed. The scratch check is a
    // belt-and-braces fallback: a callback arriving with more channels than prepare() sized for
    // would otherwise have to allocate, and allocating here is not allowed.
    const int numChannels = buffer.getNumChannels();
    const bool canSlice = numChannels > 0 && buffer.getNumSamples() > kAutomationSliceSamples &&
                          static_cast<std::size_t>(numChannels) <= sliceChannelPointers_.size();

    if (automationSlicingEnabled_.load(std::memory_order_relaxed) && canSlice)
        renderSliced(buffer, midiMessages);
    else
        renderPass(buffer, midiMessages);

    // Zero-fill AFTER the graph has run (and after every slice, not per slice) so sequencers /
    // LFOs / envelopes keep advancing.
    if (masterMuted_.load(std::memory_order_relaxed))
        buffer.clear();
}

void AudioEngine::renderPass(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, int inputSampleOffset) {
    // The one clock site: both the standalone device callback and the hosted processBlock funnel
    // through here, so the transport advances exactly once per render pass in either mode. Must run
    // before the graph so every node renders against this pass's position. Gated on the runtime
    // setting too: disabling it mid-session simply freezes the transport in place.
    if (transportEnabled_.load(std::memory_order_relaxed))
        transport.tick(buffer.getNumSamples());

    // Open this pass's timeline snapshot, exactly like the tick, and park it on the
    // transport so every node can reach it through the playhead it already has (see
    // TransportService::setCurrentTimelineSnapshot). Exactly one beginAudioBlock() per RENDER PASS
    // is the epoch-reclamation contract (it used to read "per callback"; with slicing on, a
    // callback is several passes — the contract is unchanged, only the unit is named more
    // precisely, and slicing only makes the epoch advance faster). The borrowed reference must not
    // outlive this pass, which is why the transport's copy is overwritten at the top of the next
    // one. Deliberately NOT gated on transportEnabled_: freezing the transport is a musical
    // decision, and stalling reclamation with it would let retired snapshots pile up for as long as
    // the setting is off.
    transport.setCurrentTimelineSnapshot(&timelineSnapshots.beginAudioBlock());

    // The second passenger on the same carrier. Unlike the snapshot this pointer is not
    // per-block — the streamer lives as long as the engine does — but it is installed here, beside
    // the snapshot, so the two always arrive together and a node never sees one without the other.
    transport.setAudioClipStreamer(&clipStreamer_);

    // The single MIDI-recording capture point. `midiMessages` here is already the buffer
    // that BOTH standalone (collector drain, see audioDeviceIOCallbackWithContext) and hosted
    // (delivered directly by the host into processHostBlock) modes converge on before the graph
    // ever sees it — the one place every external MIDI message is guaranteed to appear exactly
    // once. This must never read the ExternalMidiModule pushMidiMessage() copies
    // handleIncomingMidiMessage also makes (those flow only inside that module's own processing,
    // never through this buffer) — recording from both paths would double-record any note whose
    // source also has an ExternalMidi node bound to it.
    // Loaded ONCE into a local, here, and used only within this pass — the pointer is borrowed, and
    // the in-flight guard in renderNextBlock is what makes that borrow safe against an owner
    // destroying the recorder (acquire pairs with the setter's publish).
    if (auto* recorder = midiCaptureSink_.load(std::memory_order_acquire))
        recorder->captureBlock(midiMessages, transport.getCurrentBlockInfo());

    // Push this pass's automation values into their bound parameters, after the tick (so the
    // beat position is this pass's) and before the graph (so every node reads the automated value
    // in the same pass it was written).
    // The recorder's audio-visible half rides along so per-lane record modes and in-flight
    // gesture claims are honoured. Null unless an owner installed a recorder.
    // The UI reflection feed rides along too, so a slider can follow automation without a
    // notifying write. Always passed (see getAutomationUiFeed()) — nothing drains it without a
    // GraphEditor around to do so, so a headless render pass just fills a ring nobody reads.
    automationApplier_.applyBlock(automationBindings_.beginAudioBlock(), transport.getCurrentBlockInfo(),
                                  automationRecordState_.load(std::memory_order_acquire), &automationUiFeed_);

    // Deliberately OUTSIDE the timeline flag: device input is not a timeline feature.
    // Point the playhead at this pass's slice of the captured input before the graph runs, and
    // take it away immediately after — nothing outside a render pass may read those pointers.
    publishDeviceInputForPass(inputSampleOffset, buffer.getNumSamples());

    // Deliberately OUTSIDE the timeline flag for the same reason as the device-input
    // publish above: whether Audio Input's graph output is gated is an input-path property, not a
    // timeline one. Read once per render pass and handed to the carrier BEFORE the graph runs, so
    // every module — and the feedback guard below — agree on the same answer this pass.
    const bool monitoringEnabledThisPass = inputMonitoringEnabled_.load(std::memory_order_relaxed);
    transport.setInputMonitoringEnabledForBlock(monitoringEnabledThisPass);

    // The mixer solo gate, same carrier and same once-per-pass rule: every strip and Master's
    // Direct input read one answer for the whole pass. See refreshSoloGate().
    transport.setMixerSoloActiveForBlock(soloedStripCount_.load(std::memory_order_relaxed) > 0);

    mainProcessorGraph.processBlock(buffer, midiMessages);

    transport.setDeviceInputForBlock(nullptr, 0, 0);

    // The metronome click, generated from the transport and summed POST-graph — after the
    // graph has produced its own output (so the click can never appear in anything the graph itself
    // taps or that a bounce renders from inside the graph — see BounceExporter's force-off guard)
    // and BEFORE renderNextBlock's master-mute zero-fill, which runs after renderPass/renderSliced
    // return. That ordering is deliberate: master mute clears the WHOLE buffer, so it silences the
    // click along with everything else the engine produces, exactly as it silences the graph.
    metronome_.renderClicks(buffer, transport.getCurrentBlockInfo());

    // The feedback guard. Post-graph (so it sees exactly what would reach the speakers,
    // metronome click included) and pre-master-mute (renderNextBlock's zero-fill runs after this
    // returns) — ungated, like the monitoring flag above.
    runFeedbackGuard(buffer, monitoringEnabledThisPass);
}

void AudioEngine::runFeedbackGuard(juce::AudioBuffer<float>& buffer, bool monitoringEnabledThisPass) noexcept {
    // Never evaluates while monitoring is disabled — a loud synth alone can never trip it. Reset the
    // run rather than merely skipping: a run that was building up before monitoring was switched off
    // must not survive into whenever it's switched back on.
    if (!monitoringEnabledThisPass) {
        feedbackGuardConsecutiveSamples_ = 0;
        return;
    }

    // Scope the peak scan to the graph's OUTPUT channels (mainProcessorGraph.getTotalNumOutputChannels(),
    // set by setPlayConfigDetails in both host modes) rather than every channel `buffer` carries: a
    // device with more inputs than outputs parks the extra input channels past the output count in
    // this same render buffer (see audioDeviceIOCallbackWithContext), and a hot mic sitting there
    // unconnected to anything is not feedback — it never reaches the speakers.
    const int numChannels = std::min(buffer.getNumChannels(), mainProcessorGraph.getTotalNumOutputChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) {
        feedbackGuardConsecutiveSamples_ = 0;
        return;
    }

    float peak = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, numSamples));

    if (peak < kFeedbackPeakThreshold) {
        feedbackGuardConsecutiveSamples_ = 0;
        return;
    }

    feedbackGuardConsecutiveSamples_ += numSamples;

    const double sampleRate = transport.getSampleRate();
    const double consecutiveSeconds = sampleRate > 0.0 ? (double)feedbackGuardConsecutiveSamples_ / sampleRate : 0.0;
    if (consecutiveSeconds < kFeedbackSustainSeconds)
        return;

    // Tripped. Disable monitoring — the SAME flag AudioInputModule reads next render pass and the
    // one MainComponent's poll will see and stop re-enabling — latch the one-shot report for the UI,
    // and zero THIS block's output immediately rather than waiting for the disabled flag to take
    // effect next render pass (which could be a full device block away). The whole block, not just
    // from some in-block sample: the peak was measured over the whole block, so there is no single
    // sample position to call "the" trip point.
    inputMonitoringEnabled_.store(false, std::memory_order_relaxed);
    feedbackGuardTripped_.store(true, std::memory_order_relaxed);
    buffer.clear();
    feedbackGuardConsecutiveSamples_ = 0;
}

void AudioEngine::renderSliced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numChannels = buffer.getNumChannels();
    const int totalSamples = buffer.getNumSamples();

    for (int offset = 0; offset < totalSamples; offset += kAutomationSliceSamples) {
        const int sliceLength = std::min(kAutomationSliceSamples, totalSamples - offset);

        for (int channel = 0; channel < numChannels; ++channel)
            sliceChannelPointers_[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel) + offset;

        // A view, not a copy: this ctor wraps the caller's channel pointers and allocates nothing.
        juce::AudioBuffer<float> sliceView(sliceChannelPointers_.data(), numChannels, sliceLength);

        // Re-base this slice's MIDI to slice-relative positions. clear() keeps the storage
        // ensureSize() reserved in prepare, and the (data, numBytes, position) overload of addEvent
        // avoids constructing a juce::MidiMessage (which would allocate for a sysex).
        sliceMidi_.clear();
        for (const auto metadata : midiMessages) {
            if (metadata.samplePosition >= offset && metadata.samplePosition < offset + sliceLength)
                sliceMidi_.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition - offset);
        }

        renderPass(sliceView, sliceMidi_, offset);
    }
}

void AudioEngine::captureDeviceInput(const float* const* inputChannelData, int numInputChannels,
                                     int numSamples) noexcept {
    deviceInputChannelsThisBlock_ = 0;

    if (inputChannelData == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    // Longer than the snapshot was sized for: publish nothing rather than allocate or copy a
    // truncated block that would make the module render this block's tail as silence.
    if (numSamples > deviceInputSnapshot_.getNumSamples())
        return;

    const int channels =
        std::min({numInputChannels, deviceInputSnapshot_.getNumChannels(), (int)deviceInputPointers_.size()});

    for (int channel = 0; channel < channels; ++channel) {
        const float* src = inputChannelData[channel];
        float* dest = deviceInputSnapshot_.getWritePointer(channel);
        if (src != nullptr)
            std::copy(src, src + numSamples, dest);
        else
            std::fill(dest, dest + numSamples, 0.0f);
    }

    deviceInputChannelsThisBlock_ = channels;
}

void AudioEngine::publishDeviceInputForPass(int sampleOffset, int numSamples) noexcept {
    const int offset = std::max(0, sampleOffset);
    if (deviceInputChannelsThisBlock_ <= 0 || numSamples <= 0 ||
        offset + numSamples > deviceInputSnapshot_.getNumSamples()) {
        transport.setDeviceInputForBlock(nullptr, 0, 0);
        return;
    }

    for (int channel = 0; channel < deviceInputChannelsThisBlock_; ++channel)
        deviceInputPointers_[(std::size_t)channel] = deviceInputSnapshot_.getReadPointer(channel) + offset;

    transport.setDeviceInputForBlock(deviceInputPointers_.data(), deviceInputChannelsThisBlock_, numSamples);
}
