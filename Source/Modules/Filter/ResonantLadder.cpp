#include "Modules/Filter/ResonantLadder.h"
#include <cmath>

namespace synth {

namespace {

// Resonance knob -> ladder feedback, fitted to a sweep of a reference synth's 24 dB ladder low-pass (noise through
// it at 13 cutoffs x 17 knob steps, the transfer function read per cell; at each knob step the feedback that
// minimises the level-matched dB error over 0.15..6 x cutoff across the 130..2500 Hz cutoffs). The reference reads
// feedback = knob (zero feedback at 0, the edge of self-oscillation at 1); the least-squares line through the 17
// fitted points is 0.9908 x knob - 0.0023 (largest residual 0.008). The knob stops just short of 1 so the ladder
// keeps a finite peak (about +30 dB over the pass band at the top, as the reference does) and never self-sustains.
constexpr float kFeedbackSlope = 0.9908f;
constexpr float kFeedbackOffset = -0.0023f;

// Headroom at Drive 1. The reference ladder is linear at its default drive; this one tanh-saturated the Oscillator's
// +-1.2 saw, which flattened the resonant peak (at 90 % a saw at 1 kHz showed +5 dB over the band below, the
// reference +12.5 dB). Scaling the signal down before the saturation and up after it, the peak error against the
// reference stayed at 0.4 dB mean for a headroom of 0.1, 0.24 for 0.05 and 0.25 for 0.02 (fitted over 3 cutoffs x 6
// knob steps), so 0.05 is the largest headroom that has stopped improving the fit. Drive 10 uses no headroom, so
// the saturated range keeps its old character; the values between are linear in the Drive knob.
constexpr float kCleanHeadroom = 0.05f;

// Stability limit of the discrete ladder (the stage zero and the one-sample feedback delay), from the closed-loop
// poles of 1 + 4k z^-1 S(z)^4 at 44 cutoff/sample-rate ratios from 0.02 to 0.45: the largest feedback with every pole
// inside the unit circle is 1.008..1.012 below a ratio of 0.1 and falls to 0.932 at 0.45. A quadratic through the
// points from 0.13 up (largest residual 0.0008) is 1.05097 - 0.45276 x + 0.41869 x^2; the ceiling keeps 0.01 below it,
// so the ladder rings down at the top of the knob at every cutoff and sample rate instead of oscillating on its own.
constexpr float kStabilityConstant = 1.05097f;
constexpr float kStabilityLinear = -0.45276f;
constexpr float kStabilityQuadratic = 0.41869f;
constexpr float kStabilityMargin = 0.01f;

constexpr float kOutputGain = 1.2f;

const juce::dsp::LookupTableTransform<float>& tanhTable() {
    static const juce::dsp::LookupTableTransform<float> table{[](float x) { return std::tanh(x); }, -5.0f, 5.0f, 128};
    return table;
}

} // namespace

float ladderFeedbackForResonance(float resonanceKnob) noexcept {
    // `!(x > 0)` also catches NaN, which would otherwise stay in the ladder's state for good.
    if (!(resonanceKnob > 0.0f))
        return 0.0f;
    return juce::jmax(0.0f, kFeedbackSlope * juce::jmin(resonanceKnob, 1.0f) + kFeedbackOffset);
}

float ladderFeedbackCeiling(float x) noexcept {
    const float limit = kStabilityConstant + x * (kStabilityLinear + x * kStabilityQuadratic);
    return juce::jmin(1.0f, limit) - kStabilityMargin;
}

float ladderHeadroomForDrive(float drive) noexcept {
    const float t = juce::jlimit(0.0f, 1.0f, (drive - 1.0f) / 9.0f);
    return kCleanHeadroom + (1.0f - kCleanHeadroom) * t;
}

ResonantLadder::ResonantLadder()
    : state_(2) {
    setSampleRate(1000.0f);
    setResonance(0.0f);
    setDrive(1.0f);
    mode_ = Mode::LPF24;
    setMode(Mode::LPF12);
}

void ResonantLadder::setMode(Mode newMode) noexcept {
    if (newMode == mode_)
        return;
    // Output taps over the five stage outputs and the input-feedback compensation, as in the JUCE ladder.
    switch (newMode) {
    case Mode::LPF12:
        outputMix_ = {0, 0, 1, 0, 0};
        comp_ = 0.5f;
        break;
    case Mode::HPF12:
        outputMix_ = {1, -2, 1, 0, 0};
        comp_ = 0.0f;
        break;
    case Mode::BPF12:
        outputMix_ = {0, 1, -1, 0, 0};
        comp_ = 0.5f;
        break;
    case Mode::LPF24:
        outputMix_ = {0, 0, 0, 0, 1};
        comp_ = 0.5f;
        break;
    case Mode::HPF24:
        outputMix_ = {1, -4, 6, -4, 1};
        comp_ = 0.0f;
        break;
    case Mode::BPF24:
        outputMix_ = {0, 0, 1, -2, 1};
        comp_ = 0.5f;
        break;
    }
    for (auto& a : outputMix_)
        a *= kOutputGain;
    mode_ = newMode;
    reset();
}

void ResonantLadder::prepare(const juce::dsp::ProcessSpec& spec) {
    tanhTable(); // built here so the audio thread never constructs it
    setSampleRate((float)spec.sampleRate);
    state_.resize((size_t)juce::jmax(1u, spec.numChannels));
    reset();
}

void ResonantLadder::reset() noexcept {
    for (auto& s : state_)
        s.fill(0.0f);
    cutoffSmoother_.setCurrentAndTargetValue(cutoffSmoother_.getTargetValue());
    feedbackSmoother_.setCurrentAndTargetValue(feedbackSmoother_.getTargetValue());
}

void ResonantLadder::setCutoffFrequencyHz(float hz) noexcept {
    cutoffHz_ = hz;
    updateCutoff();
    updateFeedback();
}

void ResonantLadder::setResonance(float resonanceKnob) noexcept {
    knobFeedback_ = ladderFeedbackForResonance(resonanceKnob);
    updateFeedback();
}

void ResonantLadder::setDrive(float drive) noexcept {
    drive_ = juce::jlimit(1.0f, 10.0f, drive);
    gain_ = std::pow(drive_, -2.642f) * 0.6103f + 0.3903f;
    drive2_ = drive_ * 0.04f + 0.96f;
    gain2_ = std::pow(drive2_, -2.642f) * 0.6103f + 0.3903f;
    // The stages hold the signal at headroom scale, so a changed headroom rescales them with it: the output
    // (stage x 1 / headroom) stays continuous while Drive is swept.
    const float newHeadroom = ladderHeadroomForDrive(drive_);
    if (newHeadroom != headroom_ && headroom_ > 0.0f) {
        const float ratio = newHeadroom / headroom_;
        for (auto& stages : state_)
            for (auto& v : stages)
                v *= ratio;
    }
    headroom_ = newHeadroom;
    inverseHeadroom_ = 1.0f / headroom_;
}

float ResonantLadder::processSample(float input, size_t channel) noexcept {
    auto& s = state_[channel];
    const auto& tanhLut = tanhTable();

    const float a1 = cutoffCoefficient_;
    const float g = 1.0f - a1;
    const float b0 = g * 0.76923076923f;
    const float b1 = g * 0.23076923076f;

    const float dx = gain_ * tanhLut(drive_ * input * headroom_);
    const float a = dx - 4.0f * feedback_ * (gain2_ * tanhLut(drive2_ * s[4]) - dx * comp_);
    const float b = b1 * s[0] + a1 * s[1] + b0 * a;
    const float c = b1 * s[1] + a1 * s[2] + b0 * b;
    const float d = b1 * s[2] + a1 * s[3] + b0 * c;
    const float e = b1 * s[3] + a1 * s[4] + b0 * d;

    s = {a, b, c, d, e};
    const float mixed =
        a * outputMix_[0] + b * outputMix_[1] + c * outputMix_[2] + d * outputMix_[3] + e * outputMix_[4];
    return mixed * inverseHeadroom_;
}

void ResonantLadder::setSampleRate(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    cutoffScaler_ = -2.0f * juce::MathConstants<float>::pi / sampleRate;
    // The smoothing time is the JUCE ladder's, so an automated cutoff or resonance moves as it always did.
    constexpr double kSmoothingSeconds = 0.05;
    cutoffSmoother_.reset((double)sampleRate, kSmoothingSeconds);
    feedbackSmoother_.reset((double)sampleRate, kSmoothingSeconds);
    updateCutoff();
    updateFeedback();
}

void ResonantLadder::updateFeedback() noexcept {
    feedbackSmoother_.setTargetValue(juce::jmin(knobFeedback_, ladderFeedbackCeiling(cutoffHz_ / sampleRate_)));
}

void ResonantLadder::updateCutoff() noexcept { cutoffSmoother_.setTargetValue(std::exp(cutoffHz_ * cutoffScaler_)); }

} // namespace synth
