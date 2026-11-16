// CustomFilterDrive.cpp -- the Custom filter's Drive stage; see CustomFilterDrive.h for the law.
#include "CustomFilterDrive.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace customfilterdrive {

// ln(gain) as a cubic in the knob's normalised travel k = (drive - 1) / 9, through the origin:
// fitted to the measured small-signal gains 2.43 / 5.05 / 9.3 / 15.9 at k = 0.25 / 0.5 / 0.75 / 1.
constexpr float kLnGainA = 3.92599f;
constexpr float kLnGainB = -1.59670f;
constexpr float kLnGainC = 0.43687f;

float smallSignalGain(float drive) noexcept {
    const float k = (std::clamp(drive, kMinDrive, kMaxDrive) - kMinDrive) / (kMaxDrive - kMinDrive);
    return std::exp(k * (kLnGainA + k * (kLnGainB + k * kLnGainC)));
}

float wetWeight(float drive) noexcept {
    const float t = std::clamp((drive - kMinDrive) / (kFadeInDrive - kMinDrive), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float saturate(float x, float gain) noexcept { return kCeiling * std::tanh(gain * x / kCeiling); }

} // namespace customfilterdrive

/** Polyphase half-band IIR 2x oversampler (about a sample of latency, no linear-phase pre-ring). */
struct CustomFilterDrive::Channel {
    Channel()
        : oversampler(1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false) {}
    juce::dsp::Oversampling<float> oversampler;
};

CustomFilterDrive::CustomFilterDrive() = default;
CustomFilterDrive::~CustomFilterDrive() = default;

void CustomFilterDrive::prepare(int maxBlock) {
    maxBlock_ = std::max(maxBlock, 1);
    gain_.assign((size_t)maxBlock_, 1.0f);
    wet_.assign((size_t)maxBlock_, 0.0f);
    dry_.assign((size_t)maxBlock_, 0.0f);
    for (auto& ch : channels_) {
        ch = std::make_unique<Channel>();
        ch->oversampler.initProcessing((size_t)maxBlock_);
        ch->oversampler.reset();
    }
}

void CustomFilterDrive::reset() noexcept {
    for (auto& ch : channels_)
        if (ch != nullptr)
            ch->oversampler.reset();
}

void CustomFilterDrive::processChunk(Channel& ch, float* data, int count) noexcept {
    std::copy_n(data, count, dry_.data()); // the down-sampler overwrites `data`
    float* ptr[1] = {data};
    juce::dsp::AudioBlock<float> block(ptr, 1, (size_t)count);
    auto up = ch.oversampler.processSamplesUp(block);
    float* u = up.getChannelPointer(0);
    for (int i = 0; i < count; ++i) {
        const float g = gain_[(size_t)i];
        u[2 * i] = customfilterdrive::saturate(u[2 * i], g);
        u[2 * i + 1] = customfilterdrive::saturate(u[2 * i + 1], g);
    }
    ch.oversampler.processSamplesDown(block);
    for (int i = 0; i < count; ++i) {
        const float w = wet_[(size_t)i];
        data[i] = dry_[(size_t)i] + w * (data[i] - dry_[(size_t)i]);
    }
}

void CustomFilterDrive::process(float* const* channels, int numChannels, int numSamples, float driveStart,
                                float driveEnd) noexcept {
    using namespace customfilterdrive;
    if (channels_[0] == nullptr || numSamples <= 0 || (driveStart <= kMinDrive && driveEnd <= kMinDrive))
        return;
    for (int chunk = 0; chunk < numSamples; chunk += maxBlock_) {
        const int count = std::min(maxBlock_, numSamples - chunk);
        for (int i = 0; i < count; ++i) {
            const float t = (float)(chunk + i + 1) / (float)numSamples;
            const float d = driveStart + (driveEnd - driveStart) * t;
            gain_[(size_t)i] = smallSignalGain(d);
            wet_[(size_t)i] = wetWeight(d);
        }
        for (int c = 0; c < std::min(numChannels, kMaxChannels); ++c) {
            if (channels[c] == nullptr)
                continue;
            float* data = channels[c] + chunk;
            if (juce::FloatVectorOperations::findMaximum(data, count) < 1.0e-6f &&
                juce::FloatVectorOperations::findMinimum(data, count) > -1.0e-6f) {
                channels_[(size_t)c]->oversampler.reset(); // silence: nothing to saturate, no tail to keep
                continue;
            }
            processChunk(*channels_[(size_t)c], data, count);
        }
    }
}

} // namespace synth
