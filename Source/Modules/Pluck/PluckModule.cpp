// PluckModule: parameters, note handling, the body resonator and the per-sample mix.
// The string itself is in PluckVoices.h.

#include "PluckModule.h"
#include "Modules/ParameterText.h"

#include <cmath>

namespace {

// Peak of one default velocity-1 string before Level. Chosen so a default velocity-100 note peaks where a
// default Drum Kit kick does.
constexpr float kStringPeak = 0.62f;

struct BodyBandSpec {
    double hz, q, weight;
};
// A small guitar-like box: a low air resonance, the top plate, and a bright shimmer.
constexpr std::array<BodyBandSpec, 3> kBodyBands = {{{100.0, 4.0, 0.5}, {250.0, 3.0, 0.45}, {2500.0, 1.5, 0.3}}};

} // namespace

void PluckModule::BodyBand::set(double hz, double q, double sampleRate) noexcept {
    const double w = 2.0 * synth::drums::kPi * std::min(hz, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0;
    b2 = -alpha / a0;
    a1 = -2.0 * std::cos(w) / a0;
    a2 = (1.0 - alpha) / a0;
}

double PluckModule::BodyBand::process(double x) noexcept {
    const double y = b0 * x + z1; // transposed direct form II
    z1 = z2 - a1 * y;
    z2 = b2 * x - a2 * y;
    return y;
}

juce::AudioParameterFloat* PluckModule::addFloat(const char* id, const char* name, float lo, float hi, float def,
                                                 float skew) {
    auto* p = new juce::AudioParameterFloat(id, name, juce::NormalisableRange<float>(lo, hi, 0.0f, skew), def,
                                            (lo == 0.0f && hi == 1.0f) ? synth::fractionPercentAttributes()
                                                                       : juce::AudioParameterFloatAttributes());
    addParameter(p);
    return p;
}

PluckModule::PluckModule()
    : ModuleBase("Pluck", 0, kNumChannels, StereoAudio::Declared) {
    tuneParam = addFloat("tune", "Tune", -24.0f, 24.0f, 0.0f);
    fineParam = addFloat("fine", "Fine", -100.0f, 100.0f, 0.0f);
    decayParam = addFloat("decay", "Decay", 0.1f, 20.0f, 3.0f, 0.4f);
    decayTrackParam = addFloat("decayTrack", "Decay tracking", 0.0f, 1.0f, 0.7f);
    brightnessParam = addFloat("brightness", "Brightness", 0.0f, 1.0f, 0.5f);
    pickPositionParam = addFloat("pickPosition", "Pick position", 0.02f, 0.5f, 0.13f);
    pickHardnessParam = addFloat("pickHardness", "Pick hardness", 0.0f, 1.0f, 0.6f);
    bodyParam = addFloat("body", "Body", 0.0f, 1.0f, 0.3f);
    stiffnessParam = addFloat("stiffness", "Stiffness", 0.0f, 1.0f, 0.0f);
    releaseParam = addFloat("release", "Release", 0.01f, 5.0f, 0.3f, 0.4f);
    velBrightParam = addFloat("velBright", "Velocity brightness", 0.0f, 1.0f, 0.5f);
    levelParam = addFloat("level", "Level", 0.0f, 1.0f, 0.8f);
    addMuteParameter();
    enableVisualBuffer(true);
}

void PluckModule::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& v : voices_)
        v.prepare(sampleRate_); // the only allocation: one delay line per voice, sized for the lowest pitch
    for (std::size_t i = 0; i < body_.size(); ++i)
        body_[i].set(kBodyBands[i].hz, kBodyBands[i].q, sampleRate_);
    smoothedLevel_.reset(sampleRate_, 0.01);
    smoothedLevel_.setCurrentAndTargetValue(levelParam->get());
    smoothedBody_.reset(sampleRate_, 0.02);
    smoothedBody_.setCurrentAndTargetValue(bodyParam->get());
    stopAll();
}

void PluckModule::stopAll() noexcept {
    publishedVoices_.store(0, std::memory_order_relaxed);
    for (auto& v : voices_)
        v.stop();
    for (auto& band : body_)
        band.reset();
    bodyTail_ = 0;
}

int PluckModule::activeVoiceCount() const noexcept {
    int n = 0;
    for (const auto& v : voices_)
        n += v.active ? 1 : 0;
    return n;
}

void PluckModule::noteOn(int note, float velocity) {
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    // Velocity opens both the pick and the loop damping, scaled by Velocity brightness (neutral at 0.5).
    const float shift = velBrightParam->get() * (vel - 0.5f);
    synth::pluck::Strike s;
    s.hz = synth::voices::noteHz(note, tuneParam->get(), fineParam->get());
    s.velocity = vel;
    // Real strings ring shorter the higher they are: Decay is the T60 at C3, scaled by (f / C3)^-tracking.
    s.decay = juce::jmax(0.05f, decayParam->get() *
                                    static_cast<float>(std::pow(s.hz / kDecayReferenceHz, -decayTrackParam->get())));
    s.brightness = juce::jlimit(0.0f, 1.0f, brightnessParam->get() + shift);
    s.pickPosition = pickPositionParam->get();
    s.hardness = juce::jlimit(0.0f, 1.0f, pickHardnessParam->get() + shift);
    s.stiffness = stiffnessParam->get();
    s.sampleRate = sampleRate_;
    voices_[synth::voices::pickVoice(voices_)].trigger(s, noise_, note, ++noteCounter_);
}

void PluckModule::noteOff(int note) {
    for (auto& v : voices_)
        if (v.active && v.held && v.note == note)
            v.release(releaseParam->get(), sampleRate_);
}

float PluckModule::renderSample() noexcept {
    float string = 0.0f;
    for (auto& v : voices_)
        if (v.active)
            string += v.tick();
    string *= kStringPeak;

    const float bodyMix = smoothedBody_.getNextValue();
    float sum = string;
    if (bodyTail_ > 0) {
        double resonated = 0.0;
        for (std::size_t i = 0; i < body_.size(); ++i)
            resonated += body_[i].process(string) * kBodyBands[i].weight;
        sum = string * (1.0f - 0.35f * bodyMix) + static_cast<float>(resonated) * bodyMix;
        --bodyTail_;
    }
    sum *= smoothedLevel_.getNextValue();
    // Several strings at full velocity can sum past full scale: round only the excess above 0.9.
    const float mag = std::fabs(sum);
    if (mag > 0.9f)
        sum = std::copysign(0.9f + 0.08f * std::tanh((mag - 0.9f) * 12.5f), sum);
    return sum;
}

void PluckModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    // A pure source has no dry signal to pass through, so bypass and mute both clear. They stay two
    // branches, and either one cuts every string that is still ringing.
    if (isBypassed()) {
        buffer.clear();
        stopAll();
        return;
    }
    if (isMuted()) {
        buffer.clear();
        stopAll();
        return;
    }

    smoothedLevel_.setTargetValue(levelParam->get());
    smoothedBody_.setTargetValue(bodyParam->get());
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    int position = 0;
    const auto renderTo = [&](int end) {
        for (int i = position; i < end; ++i) {
            const float s = renderSample();
            left[i] = s;
            if (right != nullptr)
                right[i] = s;
        }
        position = end;
    };
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        const bool on = message.isNoteOn();
        if (!on && !message.isNoteOff())
            continue;
        renderTo(juce::jlimit(position, numSamples, metadata.samplePosition));
        if (on) {
            noteOn(message.getNoteNumber(), message.getFloatVelocity());
            bodyTail_ = static_cast<int>(sampleRate_); // keep the box ringing one second past the last string
        } else {
            noteOff(message.getNoteNumber());
        }
    }
    renderTo(numSamples);
    if (activeVoiceCount() > 0)
        bodyTail_ = std::max(bodyTail_, static_cast<int>(sampleRate_));

    publishedVoices_.store(activeVoiceCount(), std::memory_order_relaxed);

    if (auto* vb = getVisualBuffer())
        vb->pushBlock(left, numSamples);
}
