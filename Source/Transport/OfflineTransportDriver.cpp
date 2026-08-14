#include "OfflineTransportDriver.h"

#include "../AudioEngine.h"
#include <cmath>

namespace synth {

namespace {
// Cap on the up-front accumulation allocation (~4.2 M samples/channel). A render longer than this
// grows the buffer as it goes rather than reserving whatever a wildly out-of-range target beat
// would imply.
constexpr int kMaxPreallocBlocks = 8192;
} // namespace

OfflineTransportDriver::OfflineTransportDriver(AudioEngine& engineToUse, double sampleRateToUse, int blockSizeToUse,
                                               int numChannelsToUse)
    : engine(engineToUse)
    , sampleRate(sampleRateToUse)
    , blockSize(blockSizeToUse)
    , numChannels(numChannelsToUse) {
    // A Standalone engine owns an audio device and clocks itself; driving its graph from here as
    // well would tick the transport twice per block and race the device thread.
    jassert(engine.isHosted());
    jassert(sampleRate > 0.0);
    jassert(blockSize > 0);
    jassert(numChannels > 0);

    scratch.setSize(numChannels, blockSize);
    scratch.clear();

    engine.prepareForHost(sampleRate, blockSize, 0, numChannels);
}

TransportService& OfflineTransportDriver::getTransport() { return engine.getTransport(); }

const BlockTimeInfo& OfflineTransportDriver::renderOneBlock() {
    // Mirrors a host callback: a zeroed buffer and an empty MIDI buffer in, the engine's own
    // renderNextBlock (transport tick, then the graph) doing the work. Neither clear() allocates.
    scratch.clear();
    scratchMidi.clear();
    engine.processHostBlock(scratch, scratchMidi);
    return engine.getTransport().getCurrentBlockInfo();
}

void OfflineTransportDriver::copyBlockInto(juce::AudioBuffer<float>& destination, int destStartSample) const {
    const int channelsToCopy = juce::jmin(numChannels, destination.getNumChannels());
    for (int ch = 0; ch < channelsToCopy; ++ch)
        destination.copyFrom(ch, destStartSample, scratch, ch, 0, blockSize);
}

juce::AudioBuffer<float> OfflineTransportDriver::renderBlocks(int numBlocks, const BlockCallback& perBlock) {
    juce::AudioBuffer<float> out;
    if (numBlocks <= 0)
        return out;

    out.setSize(numChannels, numBlocks * blockSize);
    out.clear();

    for (int b = 0; b < numBlocks; ++b) {
        const auto& info = renderOneBlock();
        copyBlockInto(out, b * blockSize);
        if (perBlock)
            perBlock(scratch, info);
    }

    return out;
}

juce::AudioBuffer<float> OfflineTransportDriver::renderToBeat(double beat, const BlockCallback& perBlock,
                                                              int maxBlocks) {
    juce::AudioBuffer<float> out;

    // Read the position before rendering anything: a target that is not ahead of us can never be
    // reached by rendering forwards, so there is nothing to do and nothing to spin on.
    const auto snapshot = getTransport().getPositionSnapshot();
    if (maxBlocks <= 0 || !(beat > snapshot.ppq))
        return out;

    // Up-front capacity. Exact under a constant tempo — the growth path below only comes into play
    // if the tempo changes mid-render (or the target is absurdly far away).
    const double bpm = snapshot.bpm > 0.0 ? snapshot.bpm : 120.0;
    const double samplesToTarget = (beat - snapshot.ppq) * 60.0 * sampleRate / bpm;
    const int estimatedBlocks = (int)std::ceil(samplesToTarget / (double)blockSize);
    int capacityBlocks = juce::jlimit(1, juce::jmin(maxBlocks, kMaxPreallocBlocks), estimatedBlocks);
    out.setSize(numChannels, capacityBlocks * blockSize);
    out.clear();

    int blocksKept = 0;
    bool hitSafetyCap = true; // cleared by every non-cap exit below
    for (int b = 0; b < maxBlocks; ++b) {
        const auto& info = renderOneBlock();

        // A stopped transport never reaches any beat. Drop the block that discovered it (so a
        // transport that was never started yields an empty buffer) and stop.
        if (!info.playing) {
            hitSafetyCap = false;
            break;
        }

        if (blocksKept + 1 > capacityBlocks) {
            capacityBlocks = juce::jmin(maxBlocks, juce::jmax(blocksKept * 2, blocksKept + 1));
            out.setSize(numChannels, capacityBlocks * blockSize, true, true, false);
        }

        copyBlockInto(out, blocksKept * blockSize);
        ++blocksKept;

        if (perBlock)
            perBlock(scratch, info);

        if (info.endPpq >= beat) {
            hitSafetyCap = false;
            break;
        }
    }

    // Reaching the cap is a bug backstop, not a mode: the target beat was never going to arrive.
    jassert(!hitSafetyCap);
    juce::ignoreUnused(hitSafetyCap);

    out.setSize(numChannels, blocksKept * blockSize, true, true, false);
    return out;
}

} // namespace synth
