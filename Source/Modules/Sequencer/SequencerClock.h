#pragma once

#include "Transport/BlockTimeInfo.h"
#include <array>

namespace synth {

/** A stretch of one audio block measured in step units (1 unit = one step = `rate` beats). Sample `sampleOffset`
 *  of the block is unit `uStart`; the segment ends at `uEnd`. */
struct SequencerSegment {
    double uStart = 0.0;
    double uEnd = 0.0;
    int sampleOffset = 0;
    int sampleCount = 0;
    double samplesPerUnit = 1.0;
    /** The position jumped (first block, locate, loop wrap, clock source change): forget the last fired hit. */
    bool discontinuity = false;
};

/** What a block covers: one segment, or two when the transport loop wraps inside it. */
struct SequencerClockBlock {
    std::array<SequencerSegment, 2> segments{};
    int count = 0;
};

/** The sequencer's position in step units, either free-running from the BPM parameter or locked to the transport.
 *  Audio thread only; allocation-free. */
class SequencerClock {
public:
    /** Next run starts at unit 0 and counts as a discontinuity. */
    void reset() noexcept { mode_ = Mode::None; }

    /** Advances by one block at `bpm`; the first block after reset() starts at unit 0. */
    SequencerClockBlock freeRunning(int numSamples, double bpm, double sampleRate, double rateBeats) noexcept;

    /** The block's ppq range (with the loop-wrap split) converted to step units; empty at an unusable tempo. */
    SequencerClockBlock synced(const BlockTimeInfo& info, double rateBeats) noexcept;

private:
    enum class Mode { None, Free, Synced };
    Mode mode_ = Mode::None;
    double unit_ = 0.0;
    double lastEndPpq_ = 0.0;
};

} // namespace synth
