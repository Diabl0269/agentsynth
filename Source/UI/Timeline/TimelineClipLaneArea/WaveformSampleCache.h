// WaveformSampleCache.h
//
// The raw audio a zoomed-in committed clip's waveform is drawn from: a few padded blocks of samples, each read once
// through a host-supplied resolver and served to every repaint after that. Owned by TimelineClipLaneArea.
#pragma once

#include "WaveformDraw.h"
#include <cstdint>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <set>
#include <vector>

namespace synth::ui {

// The part of an asset's file a clip shows, in FILE samples: the asset's own rate, never the transport's.
struct SampleSpan {
    double startSample = 0.0;
    double numSamples = 0.0;
};
SampleSpan sampleSpanForClip(double fileRate, double sourceStartSeconds, double lengthBeats, double bpm);

// Which data the last waveform paint drew from: peaks, exact sample columns, or the sample line.
enum class WaveformSource : int { None, Peaks, SampleColumns, SampleLine };

class WaveformSampleCache {
public:
    // Fills `out` with `numSamples` frames of the asset from `startSample` (all channels, silence past the end).
    using Resolver = std::function<bool(const juce::String& assetRef, std::int64_t startSample, int numSamples,
                                        juce::AudioBuffer<float>& out)>;

    // The most frames (per channel) one block holds; a view needing more is drawn from the peaks instead.
    static constexpr int kMaxBlockFrames = 1 << 20;

    void setResolver(Resolver resolver);
    bool hasResolver() const noexcept { return static_cast<bool>(resolver_); }
    // Forgets every block and every remembered failure.
    void clear();
    // False once a read of this asset failed, until clear().
    bool canRead(const juce::String& assetRef) const { return failed_.count(assetRef) == 0; }
    // The block holding [start, start + num) of `assetRef`, reading a padded one on a miss; nullptr when the read
    // fails. `blockStart` is the file sample index of the block's first frame.
    const juce::AudioBuffer<float>* find(const juce::String& assetRef, std::int64_t start, int num,
                                         std::int64_t& blockStart);

private:
    struct Block {
        juce::String assetRef;
        std::int64_t start = 0;
        juce::AudioBuffer<float> samples;
    };
    Resolver resolver_;
    std::vector<Block> blocks_;
    std::set<juce::String> failed_;
};

} // namespace synth::ui
