// CustomFilterDrive.h -- the Filter's Drive knob for the Custom type: a soft-clip stage in front of the
// drawn response, so the knob does something there as it does on the ladder types.
//
// The law is a smooth tanh-family saturator, gain * tanh(g * x / gain) with a fixed output ceiling, run at
// twice the sample rate so the harmonics it makes do not fold back as aliasing. The small-signal gain g
// rises with the knob (about +7.7 / +14 / +19 / +24 dB at a quarter, half, three quarters and full travel);
// the ceiling and the gain curve were fitted to level and harmonic measurements of a commercial reference
// synth's low-pass drive. Drive 1 (the default) is the input untouched, sample for sample, and the first
// stretch above it fades the stage in, so existing patches do not change and sweeping through 1 never steps.
#pragma once

#include <array>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace synth {

namespace customfilterdrive {

constexpr float kMinDrive = 1.0f;
constexpr float kMaxDrive = 10.0f;
/** Output ceiling of the saturator (linear): a full-scale input is squashed to at most this. */
constexpr float kCeiling = 1.1f;
/** The saturator is faded in over the first part of the knob: weight 0 at kMinDrive, 1 from here up. */
constexpr float kFadeInDrive = 1.2f;

/** Small-signal (linear) gain of the stage at knob value `drive` (1..10): 1 at 1, about 15.9 at 10. */
float smallSignalGain(float drive) noexcept;
/** How much of the saturated signal is heard at `drive`: 0 at 1, 1 from kFadeInDrive up, smooth between. */
float wetWeight(float drive) noexcept;
/** The saturator itself: ceiling * tanh(gain * x / ceiling). Pure; the stage applies it at 2x rate. */
float saturate(float x, float gain) noexcept;

} // namespace customfilterdrive

/** One saturator per channel (the Custom engine's channels: both legs of every voice). Every call to process()
    takes the Drive value at the start and end of the block and ramps linearly between them, per sample. */
class CustomFilterDrive {
public:
    static constexpr int kMaxChannels = 16;

    CustomFilterDrive();
    ~CustomFilterDrive();
    CustomFilterDrive(const CustomFilterDrive&) = delete;
    CustomFilterDrive& operator=(const CustomFilterDrive&) = delete;

    /** Allocates every buffer for blocks up to `maxBlock` samples. Call from prepareToPlay, never with process(). */
    void prepare(int maxBlock);
    /** Audio thread. Clears every channel's oversampler history (a mode change). */
    void reset() noexcept;
    /** Audio thread. Saturates each non-null `channels[c]` (c < kMaxChannels) in place. A block with Drive at its
        minimum at both ends costs nothing and leaves the samples bit for bit alone. No allocation, no locks. */
    void process(float* const* channels, int numChannels, int numSamples, float driveStart, float driveEnd) noexcept;

private:
    struct Channel;
    void processChunk(Channel& ch, float* data, int count) noexcept;

    std::array<std::unique_ptr<Channel>, kMaxChannels> channels_;
    std::vector<float> gain_; // per sample of the chunk: small-signal gain
    std::vector<float> wet_;  // per sample of the chunk: wet weight
    std::vector<float> dry_;  // the chunk as it came in, kept for the fade-in mix
    int maxBlock_ = 0;
};

} // namespace synth
