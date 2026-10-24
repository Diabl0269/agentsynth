// Concern: ModuleBase's output stage -- the click-free bypass/mute transition, the opt-in Mix and
// Pan, and the opt-in output level. The per-block order, in ModuleBase::renderThroughOutputStage:
//
//   1. startBlock   the effective bypass/mute state follows the parameters by one transition
//   2. captureDry   a copy of the audio legs' input, only when a bypass crossfade or Mix needs it
//   3. the module's own processModuleBlock, on its ACTIVE branch while a fade runs
//   4. applyMixPan  out = dry * (1 - mix) + wet * mix, then a balance pan (never while bypassed/muted)
//   5. applyFade    the crossfade/fade of the transition
//   6. finishBlock  advance the ramp; when it ends the effective state reaches the parameter
//
// A module never sees any of this: it keeps its two separate bypass/mute branches and asks
// isBypassed()/isMuted(), which answer with the lagging effective state on the render thread.

#include "ModuleOutputStage.h"
#include "Modules/ParameterText.h"

#include "ModuleBase.h"
#include <algorithm>

namespace synth {

namespace {
constexpr double kFadeSeconds = 0.005;
constexpr int kMinFadeSamples = 64;
constexpr double kSmoothSeconds = 0.010;
constexpr int kDefaultCaptureSamples = 1024;
constexpr double kFallbackRate = 48000.0;
} // namespace

void OutputLegs::add(int raw, int dryRaw, bool right) noexcept {
    if (count >= kMax)
        return;
    const bool dry = dryRaw >= 0;
    channel[count] = raw;
    dryChannel[count] = dryRaw;
    hasDry[count] = dry;
    isRight[count] = right;
    ++count;
    anyDry = anyDry || dry;
    hasRight = hasRight || right;
}

// The dry-capture storage is owned by the base, never grown on the audio thread: the constructor
// reserves two legs of 1024 samples so a module that never calls prepare still crossfades at normal
// block sizes, and prepare() grows it to the module's real leg count and block size. A block (or leg
// count) past the capacity falls back to the old hard switch for that block.
void ModuleOutputStage::reserveDefault(int legs) {
    if (legs > 0)
        dry_.setSize(legs, kDefaultCaptureSamples);
}

void ModuleOutputStage::prepare(double sampleRate, int maxBlock, int legs) {
    const int channels = std::max(legs, dry_.getNumChannels());
    const int samples = std::max({maxBlock, kDefaultCaptureSamples, dry_.getNumSamples()});
    if (channels > 0 && (channels != dry_.getNumChannels() || samples != dry_.getNumSamples()))
        dry_.setSize(channels, samples);
    rate_ = 0.0;
    setRate(sampleRate);
    mixSm_.setCurrentAndTargetValue(mix_ != nullptr ? mix_->get() : 1.0f);
    panSm_.setCurrentAndTargetValue(pan_ != nullptr ? pan_->get() : 0.0f);
    synced_ = false; // the next block adopts the parameters without a fade
    fade_ = Fade::None;
    rampPos_ = 0;
}

void ModuleOutputStage::addMix(juce::AudioProcessor& owner) {
    if (mix_ == nullptr)
        owner.addParameter(mix_ =
                               new juce::AudioParameterFloat("mix", "Mix", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                             1.0f, synth::fractionPercentAttributes()));
}

void ModuleOutputStage::addPan(juce::AudioProcessor& owner) {
    if (pan_ == nullptr)
        owner.addParameter(pan_ = new juce::AudioParameterFloat("pan", "Pan", -1.0f, 1.0f, 0.0f));
}

void ModuleOutputStage::setRate(double sampleRate) noexcept {
    const double rate = sampleRate > 0.0 ? sampleRate : kFallbackRate;
    if (rate == rate_)
        return;
    rate_ = rate;
    rampLen_ = std::max(kMinFadeSamples, juce::roundToInt(rate * kFadeSeconds));
    // reset() snaps the smoother to its target, so keep the value it was at.
    const float mixNow = mixSm_.getCurrentValue(), panNow = panSm_.getCurrentValue();
    mixSm_.reset(rate, kSmoothSeconds);
    panSm_.reset(rate, kSmoothSeconds);
    mixSm_.setCurrentAndTargetValue(mixNow);
    panSm_.setCurrentAndTargetValue(panNow);
}

bool ModuleOutputStage::canCapture(const OutputLegs& legs, int numSamples) const noexcept {
    return numSamples <= dry_.getNumSamples() && legs.count <= dry_.getNumChannels();
}

void ModuleOutputStage::startBlock(bool paramBypass, bool paramMute, const OutputLegs& legs, bool canCaptureDry,
                                   bool bypassIsInPlace) noexcept {
    dryValid_ = false;
    // Nothing audible to fade (CV and MIDI modules), so the state switches at once as it always did.
    if (!synced_ || legs.count == 0) {
        effBypass_ = paramBypass;
        effMute_ = paramMute;
        synced_ = true;
        fade_ = Fade::None;
        rampPos_ = 0;
        return;
    }
    if (fade_ != Fade::None)
        return; // a ramp longer than the previous block carries on
    rampPos_ = 0;
    if (paramBypass != effBypass_) {
        if (paramBypass) {
            fade_ = Fade::BypassToDry; // keeps rendering wet until the ramp has ended
        } else {
            effBypass_ = false; // the wet branch must run to be faded in
            fade_ = Fade::BypassToWet;
        }
        // Nowhere to keep the dry signal, or a bypass that is not an in-place pass-through: hard switch.
        if (!bypassIsInPlace || (legs.anyDry && !canCaptureDry)) {
            effBypass_ = paramBypass;
            fade_ = Fade::None;
        }
    } else if (paramMute != effMute_) {
        if (paramMute) {
            fade_ = Fade::MuteOut;
        } else {
            effMute_ = false;
            fade_ = Fade::MuteIn;
        }
    }
}

bool ModuleOutputStage::needsDry(const OutputLegs& legs) const noexcept {
    if (!legs.anyDry)
        return false;
    if (fade_ == Fade::BypassToDry || fade_ == Fade::BypassToWet)
        return true;
    const bool resting = fade_ == Fade::None && (effBypass_ || effMute_);
    return !resting && mix_ != nullptr &&
           (mixSm_.isSmoothing() || mix_->get() != 1.0f || mixSm_.getCurrentValue() != 1.0f);
}

void ModuleOutputStage::captureDry(const juce::AudioBuffer<float>& buffer, const OutputLegs& legs) noexcept {
    const int n = buffer.getNumSamples();
    for (int i = 0; i < legs.count; ++i)
        if (legs.hasDry[i] && legs.dryChannel[i] < buffer.getNumChannels())
            dry_.copyFrom(i, 0, buffer, legs.dryChannel[i], 0, n);
    dryValid_ = true;
}

void ModuleOutputStage::beginRender() noexcept {
    bypassQueried_ = false;
    muteQueried_ = false;
    renderThread_.store(threadKey(), std::memory_order_relaxed);
}

// A module that never asked for the flag it is being faded on does not implement it (it renders the
// same either way), so a fade would only dip its output; drop the fade and switch at once.
void ModuleOutputStage::endRender(bool paramBypass, bool paramMute) noexcept {
    renderThread_.store(0, std::memory_order_relaxed);
    const bool bypassFade = fade_ == Fade::BypassToDry || fade_ == Fade::BypassToWet;
    const bool muteFade = fade_ == Fade::MuteOut || fade_ == Fade::MuteIn;
    if ((bypassFade && !bypassQueried_) || (muteFade && !muteQueried_)) {
        effBypass_ = paramBypass;
        effMute_ = paramMute;
        fade_ = Fade::None;
        rampPos_ = 0;
    }
}

void ModuleOutputStage::syncTargets() noexcept {
    if (mix_ != nullptr)
        mixSm_.setTargetValue(mix_->get());
    if (pan_ != nullptr)
        panSm_.setTargetValue(pan_->get());
}

void ModuleOutputStage::skipMixPan(int numSamples) noexcept {
    syncTargets();
    mixSm_.skip(numSamples);
    panSm_.skip(numSamples);
}

// A Left-only patch leaves the right input leg empty, so a hard-right balance (left leg to zero) would
// leave nothing to hear. While the right input is silent and the left is not, the left leg is lent to
// the right in proportion to |pan|: nothing changes at centre, the far leg grows from zero as the knob
// moves (no step when it leaves centre), and full pan lands the whole signal on one side. `span` is
// the voice count of a poly split-block module; voice v's right leg borrows voice v's left.
void ModuleOutputStage::fillRightForPan(juce::AudioBuffer<float>& buffer, int rightBase, int span) noexcept {
    const float target = pan_ != nullptr ? std::min(1.0f, std::abs(pan_->get())) : 0.0f;
    const float from = panFill_;
    panFill_ = 0.0f;
    if (target == 0.0f && from == 0.0f)
        return;
    const int n = buffer.getNumSamples();
    for (int v = 0; v < span; ++v) {
        const int l = v;
        const int r = rightBase + v;
        if (r >= buffer.getNumChannels() || l >= buffer.getNumChannels())
            break;
        if (buffer.getMagnitude(r, 0, n) != 0.0f || buffer.getMagnitude(l, 0, n) == 0.0f)
            continue;
        const float* src = buffer.getReadPointer(l);
        float* dst = buffer.getWritePointer(r);
        for (int s = 0; s < n; ++s)
            dst[s] = src[s] * (from + (target - from) * (float)(s + 1) / (float)n);
        panFill_ = target;
    }
}

void ModuleOutputStage::applyMixPan(juce::AudioBuffer<float>& buffer, const OutputLegs& legs,
                                    bool panApplies) noexcept {
    syncTargets();
    const int n = buffer.getNumSamples();
    const bool doMix =
        mix_ != nullptr && dryValid_ && legs.anyDry && (mixSm_.isSmoothing() || mixSm_.getCurrentValue() != 1.0f);
    const bool doPan =
        pan_ != nullptr && panApplies && legs.hasRight && (panSm_.isSmoothing() || panSm_.getCurrentValue() != 0.0f);
    if (!doMix && !doPan) {
        mixSm_.skip(n);
        panSm_.skip(n);
        return;
    }
    if (!doMix)
        mixSm_.skip(n);
    if (!doPan)
        panSm_.skip(n);

    float* wet[OutputLegs::kMax] = {};
    const float* dry[OutputLegs::kMax] = {};
    for (int i = 0; i < legs.count; ++i) {
        wet[i] = buffer.getWritePointer(legs.channel[i]);
        dry[i] = doMix && legs.hasDry[i] ? dry_.getReadPointer(i) : nullptr;
    }
    for (int s = 0; s < n; ++s) {
        const float mix = doMix ? mixSm_.getNextValue() : 1.0f;
        float gainL = 1.0f, gainR = 1.0f;
        if (doPan)
            ModuleBase::panGains(panSm_.getNextValue(), gainL, gainR);
        for (int i = 0; i < legs.count; ++i) {
            float v = wet[i][s];
            if (dry[i] != nullptr)
                v = dry[i][s] * (1.0f - mix) + v * mix;
            wet[i][s] = doPan ? v * (legs.isRight[i] ? gainR : gainL) : v;
        }
    }
}

void ModuleOutputStage::applyFade(juce::AudioBuffer<float>& buffer, const OutputLegs& legs) noexcept {
    if (fade_ == Fade::None)
        return;
    const int n = buffer.getNumSamples();
    const bool rising = fade_ == Fade::MuteIn || fade_ == Fade::BypassToWet;
    const bool crossfade = fade_ == Fade::BypassToDry || fade_ == Fade::BypassToWet;
    const int rampSamples = std::min(n, rampLen_ - rampPos_);
    const float step = 1.0f / (float)rampLen_;

    for (int i = 0; i < legs.count; ++i) {
        float* out = buffer.getWritePointer(legs.channel[i]);
        const float* dry = crossfade && dryValid_ && legs.hasDry[i] ? dry_.getReadPointer(i) : nullptr;
        for (int s = 0; s < n; ++s) {
            const float t = s < rampSamples ? (float)(rampPos_ + s + 1) * step : 1.0f;
            const float wetGain = rising ? t : 1.0f - t;
            out[s] = dry != nullptr ? out[s] * wetGain + dry[s] * (1.0f - wetGain) : out[s] * wetGain;
        }
    }
}

void ModuleOutputStage::finishBlock(int numSamples) noexcept {
    if (fade_ == Fade::None)
        return;
    rampPos_ += numSamples;
    if (rampPos_ < rampLen_)
        return;
    if (fade_ == Fade::MuteOut)
        effMute_ = true;
    else if (fade_ == Fade::BypassToDry)
        effBypass_ = true;
    fade_ = Fade::None;
    rampPos_ = 0;
}

} // namespace synth

// ---------------------------------------------------------------------------------------------
// ModuleBase glue
// ---------------------------------------------------------------------------------------------

// Bypass leaves the input on the output channel of the same index, so a module whose first input is
// audio has a dry path on every leg that also exists as an input. The first input is the test (not the
// leg's own) because a collapsed split-block module hides its right input yet still passes it through.
int ModuleBase::bypassDryChannel(int outputChannel) const {
    const bool dryPath = getTotalNumInputChannels() > 0 && mapInputChannel(0).role == PortRole::Audio;
    return dryPath ? outputChannel : -1;
}

void ModuleBase::buildOutputLegs(synth::OutputLegs& legs, int bufferChannels) const {
    const int numOut = std::min(getTotalNumOutputChannels(), bufferChannels);
    const int numIn = getTotalNumInputChannels();
    const int right = rightAudioLegChannel();
    std::uint64_t seen = 0;
    int rightSpan = 1;
    const auto add = [&](int raw) {
        if (raw < 0 || raw >= numOut || raw >= 64 || ((seen >> raw) & 1u) != 0)
            return;
        seen |= std::uint64_t{1} << raw;
        const int dryRaw = bypassDryChannel(raw);
        legs.add(raw, dryRaw >= 0 && dryRaw < numIn ? dryRaw : -1,
                 right > 0 && raw >= right && raw < right + rightSpan);
    };
    // A contiguous FX pair has a one-channel right leg; a split-block module's right block is as wide as its left.
    if (right > 1) {
        const auto head = mapOutputChannel(0);
        rightSpan = head.role == PortRole::Audio ? std::max(1, head.polyVoiceSpan) : 1;
    }
    for (int c = 0; c < numOut && legs.count < synth::OutputLegs::kMax; ++c) {
        const auto port = mapOutputChannel(c);
        if (port.role != PortRole::Audio || !port.isPolyGroupHead)
            continue;
        for (int v = 0; v < std::max(1, port.polyVoiceSpan); ++v)
            add(c + v);
    }
    // A collapsed jack owns both raw legs without marking the second one a head.
    for (int v = 0; right > 0 && v < rightSpan; ++v)
        add(right + v);
    add(duplicateOutputChannel());
}

void ModuleBase::renderThroughOutputStage(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    const int n = buffer.getNumSamples();
    if (n == 0) {
        processModuleBlock(buffer, midi);
        return;
    }
    auto& stage = outputStage_;
    const bool paramBypass = bypassedParam->get();
    const bool paramMute = mutedParam != nullptr && mutedParam->get();

    synth::OutputLegs legs;
    buildOutputLegs(legs, buffer.getNumChannels());
    stage.setRate(getSampleRate());
    const bool canCapture = stage.canCapture(legs, n);
    stage.startBlock(paramBypass, paramMute, legs, canCapture, bypassPassesInputThrough());
    if (canCapture && stage.needsDry(legs))
        stage.captureDry(buffer, legs);

    // Pan balances a pair of legs. A split-block module collapsed to one jack has no right jack, so
    // the balance would only fade its one audible leg out: there the pan rests.
    const int right = rightAudioLegChannel();
    const bool panApplies = right > 0 && (isDualIO() || !hasSplitBlockStereo());
    if (panApplies && !stage.bypassedNow() && !stage.mutedNow() && !isNormalLeftToRight() &&
        mapInputChannel(right).role == PortRole::Audio)
        stage.fillRightForPan(buffer, right, right > 1 ? std::max(1, mapInputChannel(0).polyVoiceSpan) : 1);

    stage.beginRender();
    processModuleBlock(buffer, midi);
    stage.endRender(paramBypass, paramMute);

    if (stage.bypassedNow() || stage.mutedNow())
        stage.skipMixPan(n);
    else
        stage.applyMixPan(buffer, legs, panApplies);
    stage.applyFade(buffer, legs);
    stage.finishBlock(n);
}

void ModuleBase::prepareOutputStage(double sampleRate, int samplesPerBlock) {
    const int legs = std::min(getTotalNumOutputChannels(), synth::OutputLegs::kMax);
    outputStage_.prepare(sampleRate, std::max(samplesPerBlock, getBlockSize()), legs);
}

void ModuleBase::prepareOutputLevel(double sampleRate) {
    smoothedOutputLevel.reset(sampleRate, 0.01); // 10 ms ramp: anti-click on knob moves
    smoothedOutputLevel.setCurrentAndTargetValue(getOutputLevel());
    prepareOutputStage(sampleRate);
}

void ModuleBase::applyOutputLevel(juce::AudioBuffer<float>& buffer, int numAudioChannels) {
    if (outputLevelParam == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(numAudioChannels, buffer.getNumChannels());
    if (numSamples == 0 || numChannels <= 0)
        return;

    smoothedOutputLevel.setTargetValue(outputLevelParam->get());

    if (!smoothedOutputLevel.isSmoothing()) {
        const float gain = smoothedOutputLevel.getCurrentValue();
        if (gain != 1.0f)
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.applyGain(ch, 0, numSamples, gain);
        return;
    }

    // The per-sample walk keeps the ramp exact even when it completes mid-block.
    auto* const* channels = buffer.getArrayOfWritePointers();
    for (int i = 0; i < numSamples; ++i) {
        const float gain = smoothedOutputLevel.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }
}

// Both legs scale from ONE ramp: calling applyOutputLevel() twice would advance the smoother twice
// and leave the right leg a block behind the left, which reads as the image drifting while Level moves.
void ModuleBase::applyOutputLevelSplit(juce::AudioBuffer<float>& buffer, int span, int rightBase) {
    if (outputLevelParam == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    const int bufChannels = buffer.getNumChannels();
    const int leftCount = juce::jmin(span, bufChannels);
    if (numSamples == 0 || leftCount <= 0)
        return;

    const int rightCount = juce::jlimit(0, span, bufChannels - rightBase);

    smoothedOutputLevel.setTargetValue(outputLevelParam->get());

    if (!smoothedOutputLevel.isSmoothing()) {
        const float gain = smoothedOutputLevel.getCurrentValue();
        if (gain != 1.0f) {
            for (int ch = 0; ch < leftCount; ++ch)
                buffer.applyGain(ch, 0, numSamples, gain);
            for (int ch = 0; ch < rightCount; ++ch)
                buffer.applyGain(rightBase + ch, 0, numSamples, gain);
        }
        return;
    }

    auto* const* channels = buffer.getArrayOfWritePointers();
    for (int i = 0; i < numSamples; ++i) {
        const float gain = smoothedOutputLevel.getNextValue();
        for (int ch = 0; ch < leftCount; ++ch)
            channels[ch][i] *= gain;
        for (int ch = 0; ch < rightCount; ++ch)
            channels[rightBase + ch][i] *= gain;
    }
}
