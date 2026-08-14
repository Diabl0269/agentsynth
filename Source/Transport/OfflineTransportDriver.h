#pragma once

#include "BlockTimeInfo.h"
#include "TransportService.h"
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>

class AudioEngine;

namespace synth {

// Runs an AudioEngine's graph at a fixed sample rate / block size with no audio device: the
// headless render harness the timeline's engine-level tests are written against, and the render
// loop the user-facing bounce/export (TL4-6) is built on. Message-thread / test-thread code —
// nothing here is audio-thread safe, and nothing here needs to be, because *this* is the thread
// that clocks the graph.
//
// The engine must be HostMode::Hosted: the driver drives the graph through
// prepareForHost / processHostBlock, the same path a plugin host uses, so a Standalone engine
// (which owns a real device and clocks itself) would be fighting for the same graph.
//
// The one place a block is produced is renderOneBlock(); both public render calls are thin loops
// around it, and bounce/export streams `scratch` to disk from the same seam instead of
// accumulating in RAM.
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
