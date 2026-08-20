#pragma once

#include <cmath>
#include <cstdint>

namespace synth {

// The seam every musical-time conversion goes through. Exactly two methods, so a
// real tempo map (ramps, changes) can replace ConstantTempoMap later without
// touching clips, lanes, or the transport itself. Beats are the canonical unit.
class TempoMap {
public:
    virtual ~TempoMap() = default;

    virtual double beatFromSample(std::int64_t samplePosition) const noexcept = 0;
    virtual std::int64_t sampleFromBeat(double beat) const noexcept = 0;
};

// v1 implementation: a single constant BPM at a fixed sample rate. Mutable only
// from the audio thread (TransportService applies SetBpm commands while ticking).
class ConstantTempoMap : public TempoMap {
public:
    ConstantTempoMap() = default;
    ConstantTempoMap(double bpmToUse, double sampleRateToUse) noexcept
        : bpm(bpmToUse)
        , sampleRate(sampleRateToUse) {}

    void setBpm(double newBpm) noexcept { bpm = newBpm; }
    void setSampleRate(double newSampleRate) noexcept { sampleRate = newSampleRate; }
    double getBpm() const noexcept { return bpm; }
    double getSampleRate() const noexcept { return sampleRate; }

    double beatFromSample(std::int64_t samplePosition) const noexcept override {
        return (double)samplePosition * bpm / (60.0 * sampleRate);
    }

    std::int64_t sampleFromBeat(double beat) const noexcept override {
        return (std::int64_t)std::llround(beat * 60.0 * sampleRate / bpm);
    }

private:
    double bpm = 120.0;
    double sampleRate = 44100.0;
};

} // namespace synth
