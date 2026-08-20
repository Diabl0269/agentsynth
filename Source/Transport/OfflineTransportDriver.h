#pragma once

#include "BlockTimeInfo.h"
#include "TransportService.h"
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>

class AudioEngine;

namespace synth {

// Runs an AudioEngine's graph at a fixed sample rate / block size with no audio device: the
// headless render harness the timeline's engine-level tests are written against, and the render
// loop the user-facing bounce/export is built on. Message-thread / test-thread code —
// nothing here is audio-thread safe, and nothing here needs to be, because *this* is the thread
// that clocks the graph.
//
// PRECONDITION: the engine must not be receiving audio-device callbacks while a driver is alive on
// it. The driver clocks the graph itself through prepareForHost / processHostBlock (the same path a
// plugin host uses), so anything else clocking the same graph would tick the transport twice per
// block and race the device thread. A HostMode::Hosted engine satisfies this by construction — it
// never opens a device. A HostMode::Standalone engine satisfies it only once its device callback is
// detached (AudioEngine::suspendDeviceCallback), which is exactly what synth::BounceExporter does
// around an offline render; the constructor asserts on AudioEngine::isReceivingDeviceCallbacks(),
// which is the property that actually matters rather than the host mode that used to stand in for
// it.
//
// The one place a block is produced is renderOneBlock(); every public render call is a thin loop
// around it. The stream* calls are the same loops with nothing accumulated — bounce/export writes
// `scratch` straight to disk from the per-block callback, which is what keeps a ten-minute take off
// the heap.
class OfflineTransportDriver {
public:
    // Borrows `engineToUse` — it must outlive the driver — and puts it on the requested render
    // format via prepareForHost(sampleRate, blockSize, 0, numChannels). Releasing the engine
    // afterwards (releaseFromHost / shutdown) stays the caller's job; the driver never tears down
    // state it doesn't own.
    explicit OfflineTransportDriver(AudioEngine& engineToUse, double sampleRateToUse = 44100.0,
                                    int blockSizeToUse = 512, int numChannelsToUse = 2);

    // Optional per-block observer: the rendered block, and the BlockTimeInfo it was rendered
    // against. The buffer is the driver's reusable scratch — valid for the duration of the call
    // only, so a consumer that needs to keep it must copy or write it out.
    using BlockCallback = std::function<void(const juce::AudioBuffer<float>&, const BlockTimeInfo&)>;

    // Optional pre-block gate for the stream* loops: called immediately BEFORE each block, it ends
    // the render without rendering that block when it returns false. Two jobs, both of which have to
    // happen before the work rather than after it — stopping a cancelled render (rendering a tail
    // nobody is writing is pure waste) and giving a consumer somewhere to block, which is how the
    // bounce waits for AudioClipStreamer's prefetch thread.
    using BlockGate = std::function<bool()>;

    // Renders exactly numBlocks blocks and returns the concatenated audio
    // (numChannels x numBlocks * blockSize). Non-positive counts render nothing.
    juce::AudioBuffer<float> renderBlocks(int numBlocks, const BlockCallback& perBlock = {});

    // Renders whole blocks until the transport reaches `beat`, i.e. until the just-rendered
    // block's BlockTimeInfo::endPpq >= beat. Position is therefore checked AFTER each block and
    // the last one may overshoot by up to a block: starting from beat 0, the result is exactly
    // ceil(sampleFromBeat(beat) / blockSize) * blockSize samples.
    //
    // Returns an empty buffer, immediately and without spinning, when `beat` is not ahead of the
    // current position. A stopped transport never reaches any beat, so a block that reports
    // playing == false ends the render and is discarded — which is why a transport that was never
    // started yields 0 samples (it costs exactly one probe block, since play() is queued and only
    // takes effect on the first tick, so "is it playing?" is unanswerable before then).
    // maxBlocks bounds a runaway render; hitting it asserts and stops.
    juce::AudioBuffer<float> renderToBeat(double beat, const BlockCallback& perBlock = {}, int maxBlocks = 1 << 18);

    // Streaming twins of the two calls above: block-for-block identical, but nothing is
    // accumulated — the callback is the only way to see the audio, and the return value is the
    // number of blocks rendered (the same count the corresponding render* call would have divided
    // by blockSize). renderBlocks/renderToBeat are implemented ON TOP of these, so there is still
    // exactly one loop per shape.
    //
    // This is what bounce/export uses. renderToBeat would hand it a single contiguous buffer of the
    // whole take — ~230 MB for ten minutes of 48 kHz stereo — for no reason, since every block is
    // written to disk and never looked at again.
    //
    // `beforeBlock` is the only way out of either loop that does not cost a rendered block; see
    // BlockGate.
    int streamBlocks(int numBlocks, const BlockCallback& perBlock, const BlockGate& beforeBlock = {});
    int streamToBeat(double beat, const BlockCallback& perBlock, int maxBlocks = 1 << 18,
                     const BlockGate& beforeBlock = {});

    TransportService& getTransport();

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    int getNumChannels() const noexcept { return numChannels; }

private:
    // Renders one block into `scratch` (clocking the transport once, before the graph, exactly as
    // a host callback would) and returns the BlockTimeInfo that block was rendered against.
    const BlockTimeInfo& renderOneBlock();

    void copyBlockInto(juce::AudioBuffer<float>& destination, int destStartSample) const;

    AudioEngine& engine;
    double sampleRate;
    int blockSize;
    int numChannels;

    // Allocated once in the constructor: the per-block path must not allocate, even offline, so
    // that what these tests exercise is the same work the audio thread does.
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer scratchMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineTransportDriver)
};

} // namespace synth
