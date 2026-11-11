// WaveformSampleCache.cpp

#include "WaveformSampleCache.h"
#include "TimelineClipLaneInternal.h"

#include <algorithm>

namespace synth::ui {

namespace {
// Blocks kept, most recent first: a handful of clips on screen, two of them possibly from the same file.
constexpr std::size_t kMaxBlocks = 6;
} // namespace

// Buckets and the sample span count FILE samples, so the rate is the audio file's own, never the transport's.
SampleSpan sampleSpanForClip(double fileRate, double sourceStartSeconds, double lengthBeats, double bpm) {
    const double secondsPerBeat = bpm > 0.0 ? 60.0 / bpm : 60.0 / detail::kFallbackBpm;
    SampleSpan span;
    span.startSample = std::max(0.0, sourceStartSeconds) * fileRate;
    span.numSamples = std::max(0.0, lengthBeats) * secondsPerBeat * fileRate;
    return span;
}

void WaveformSampleCache::setResolver(Resolver resolver) {
    resolver_ = std::move(resolver);
    clear();
}

void WaveformSampleCache::clear() {
    blocks_.clear();
    failed_.clear();
}

// A miss reads about twice the visible range (never past kMaxBlockFrames), so a scroll or a small zoom change is
// served without touching the disk again. A failed read is remembered per asset until clear(), so a file that cannot
// be read is not retried on every paint. Called on the message thread; a block is under a million frames.
const juce::AudioBuffer<float>* WaveformSampleCache::find(const juce::String& assetRef, std::int64_t start, int num,
                                                          std::int64_t& blockStart) {
    for (const auto& block : blocks_) {
        if (block.assetRef == assetRef && block.start <= start &&
            block.start + block.samples.getNumSamples() >= start + num) {
            blockStart = block.start;
            return &block.samples;
        }
    }
    if (!resolver_ || num <= 0 || num > kMaxBlockFrames)
        return nullptr;

    const std::int64_t pad = std::min<std::int64_t>(num / 2, (kMaxBlockFrames - num) / 2);
    const std::int64_t readStart = std::max<std::int64_t>(0, start - pad);
    const int readNum = (int)std::min<std::int64_t>(kMaxBlockFrames, (start + num + pad) - readStart);
    Block block;
    block.assetRef = assetRef;
    block.start = readStart;
    if (!resolver_(assetRef, readStart, readNum, block.samples) || block.samples.getNumChannels() <= 0 ||
        block.samples.getNumSamples() < readNum) {
        failed_.insert(assetRef);
        return nullptr;
    }
    blocks_.insert(blocks_.begin(), std::move(block));
    if (blocks_.size() > kMaxBlocks)
        blocks_.pop_back();
    blockStart = blocks_.front().start;
    return &blocks_.front().samples;
}

} // namespace synth::ui
