#pragma once

#include <cstdint>
#include <type_traits>

namespace synth {

// Immutable description of one audio block's position on the timeline, computed
// once per callback by TransportService::tick() before the graph runs. Audio-thread
// consumers read it for the duration of that block only; it is POD so publishing a
// copy never allocates.
struct BlockTimeInfo {
    std::int64_t blockStartSample = 0; // transport sample position at sample 0 of the block
    int numSamples = 0;

    double startPpq = 0.0; // beat position at sample 0 of the block
    // Beat position the transport would reach after numSamples with no loop wrap
    // (== startPpq while stopped). When loopWrapSample >= 0 the block actually covers
    // [startPpq, loopEndPpq) for samples [0, loopWrapSample) and continues from
    // loopStartPpq for the remainder.
    double endPpq = 0.0;

    double bpm = 120.0;
    double sampleRate = 44100.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    bool playing = false;
    bool looping = false;
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;

    // Block-relative sample index at which the position wraps back to loopStartPpq,
    // or -1 if this block doesn't wrap. A wrap landing exactly on the block boundary
    // is reported as -1 (the next block simply starts at loopStartPpq).
    int loopWrapSample = -1;

    double beatsPerSample() const noexcept { return bpm / (60.0 * sampleRate); }
};

static_assert(std::is_trivially_copyable_v<BlockTimeInfo>,
              "BlockTimeInfo is published by copy on the audio path and must stay POD");

} // namespace synth
