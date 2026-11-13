#include "SequencerClock.h"
#include <cmath>

namespace synth {

SequencerClockBlock SequencerClock::freeRunning(int numSamples, double bpm, double sampleRate,
                                                double rateBeats) noexcept {
    SequencerClockBlock block;
    const double samplesPerUnit = 60.0 / bpm * sampleRate * rateBeats;
    if (!(samplesPerUnit > 0.0) || numSamples <= 0)
        return block;

    const bool restart = mode_ != Mode::Free;
    if (restart)
        unit_ = 0.0;
    mode_ = Mode::Free;

    const double advance = (double)numSamples / samplesPerUnit;
    block.segments[0] = {unit_, unit_ + advance, 0, numSamples, samplesPerUnit, restart};
    block.count = 1;
    unit_ += advance;
    return block;
}

SequencerClockBlock SequencerClock::synced(const BlockTimeInfo& info, double rateBeats) noexcept {
    SequencerClockBlock block;
    const double beatsPerSample = info.beatsPerSample();
    if (!(beatsPerSample > 0.0) || !(rateBeats > 0.0))
        return block;

    const double samplesPerUnit = rateBeats / beatsPerSample;
    const bool wraps = info.loopWrapSample >= 0;
    const bool continuous = mode_ == Mode::Synced && std::abs(info.startPpq - lastEndPpq_) < 1.0e-6;

    // Before a loop wrap the block only reaches the loop end, not the raw endPpq (see BlockTimeInfo).
    const double firstEndPpq = wraps ? info.loopEndPpq : info.endPpq;
    const int firstCount = wraps ? info.loopWrapSample : info.numSamples;
    block.segments[0] = {
        info.startPpq / rateBeats, firstEndPpq / rateBeats, 0, firstCount, samplesPerUnit, !continuous};
    block.count = 1;
    double endPpq = info.endPpq;

    if (wraps) {
        endPpq = info.loopStartPpq + (info.endPpq - info.loopEndPpq);
        block.segments[1] = {info.loopStartPpq / rateBeats,         endPpq / rateBeats, info.loopWrapSample,
                             info.numSamples - info.loopWrapSample, samplesPerUnit,     true};
        block.count = 2;
    }
    mode_ = Mode::Synced;
    lastEndPpq_ = endPpq;
    return block;
}

} // namespace synth
