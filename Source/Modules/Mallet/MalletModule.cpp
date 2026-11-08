// MalletModule: parameters, note handling, the tremolo and the per-sample mix.
// The bar itself is in MalletVoices.h.

#include "MalletModule.h"
#include "Modules/ParameterText.h"

namespace {

// Peak of one default velocity-1 strike before Level. Chosen so a default velocity-100 note peaks where a
// default Drum Kit kick does.
constexpr float kBarPeak = 0.9f;

} // namespace

juce::AudioParameterFloat* MalletModule::addFloat(const char* id, const char* name, float lo, float hi, float def,
                                                  float skew) {
    auto* p = new juce::AudioParameterFloat(id, name, juce::NormalisableRange<float>(lo, hi, 0.0f, skew), def,
                                            (lo == 0.0f && hi == 1.0f) ? synth::fractionPercentAttributes()
                                                                       : juce::AudioParameterFloatAttributes());
    addParameter(p);
    return p;
}

MalletModule::MalletModule()
    : ModuleBase("Mallet", (int)kModTargets.size(), kNumChannels, StereoAudio::Declared) {
    addParameter(materialParam = new juce::AudioParameterChoice(
                     "material", "Material",
                     juce::StringArray{"Marimba", "Vibraphone", "Xylophone", "Glockenspiel", "Bell"}, 0));
    tuneParam = addFloat("tune", "Tune", -24.0f, 24.0f, 0.0f);
    fineParam = addFloat("fine", "Fine", -100.0f, 100.0f, 0.0f);
    decayParam = addFloat("decay", "Decay", 0.05f, 10.0f, 1.2f, 0.4f);
    dampingParam = addFloat("damping", "Damping", 0.0f, 1.0f, 0.5f);
    brightnessParam = addFloat("brightness", "Brightness", 0.0f, 1.0f, 0.5f);
    hardnessParam = addFloat("hardness", "Hardness", 0.0f, 1.0f, 0.5f);
    resonatorParam = addFloat("resonator", "Resonator", 0.0f, 1.0f, 0.5f);
    tremoloParam = addFloat("tremolo", "Tremolo", 0.0f, 1.0f, 0.0f);
    releaseParam = addFloat("release", "Release", 0.01f, 5.0f, 0.5f, 0.4f);
    velBrightParam = addFloat("velBright", "Velocity brightness", 0.0f, 1.0f, 0.5f);
    levelParam = addFloat("level", "Level", 0.0f, 1.0f, 0.8f);
    addMuteParameter();
    mods_.bind(*this);
    enableVisualBuffer(true);
}

void MalletModule::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothedLevel_.reset(sampleRate_, 0.01);
    smoothedLevel_.setCurrentAndTargetValue(levelParam->get());
    smoothedTremolo_.reset(sampleRate_, 0.02);
    smoothedTremolo_.setCurrentAndTargetValue(tremoloParam->get());
    tremoloPhase_ = 0.0;
    stopAll();
}

void MalletModule::stopAll() noexcept {
    publishedVoices_.store(0, std::memory_order_relaxed);
    for (auto& v : voices_)
        v.stop();
}

int MalletModule::activeVoiceCount() const noexcept {
    int n = 0;
    for (const auto& v : voices_)
        n += v.active ? 1 : 0;
    return n;
}

// Every setting is the knob moved by its cable on the note's own sample, so a cable shapes each new note and never a
// ringing one.
void MalletModule::noteOn(int note, float velocity, const juce::AudioBuffer<float>& cv, int sample) {
    const auto at = [&](const juce::AudioParameterFloat* p) { return mods_.valueAt(cv, *p, sample); };
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    // Velocity brings the mallet's hardness and the upper modes up, scaled by Velocity brightness.
    const float shift = at(velBrightParam) * (vel - 0.5f);
    synth::mallet::Strike s;
    s.material = materialParam->getIndex();
    s.hz = synth::voices::noteHz(note, at(tuneParam), at(fineParam));
    s.velocity = vel;
    s.decay = at(decayParam);
    s.damping = at(dampingParam);
    s.brightness = juce::jlimit(0.0f, 1.0f, at(brightnessParam) + shift);
    s.hardness = juce::jlimit(0.0f, 1.0f, at(hardnessParam) + shift);
    s.resonator = at(resonatorParam);
    s.sampleRate = sampleRate_;
    voices_[synth::voices::pickVoice(voices_)].trigger(s, note, ++noteCounter_);
}

void MalletModule::noteOff(int note, const juce::AudioBuffer<float>& cv, int sample) {
    const float release = mods_.valueAt(cv, *releaseParam, sample);
    for (auto& v : voices_)
        if (v.active && v.held && v.note == note)
            v.release(release, sampleRate_);
}

float MalletModule::renderSample() noexcept {
    float sum = 0.0f;
    for (auto& v : voices_)
        if (v.active)
            sum += v.tick(noise_);
    sum *= kBarPeak * smoothedLevel_.getNextValue();

    const float depth = smoothedTremolo_.getNextValue();
    if (depth > 0.0f) {
        // Gain swings between 1 and 1 - depth, once per 1 / 5.5 s.
        sum *= 1.0f - depth * 0.5f * (1.0f - static_cast<float>(std::cos(tremoloPhase_)));
        tremoloPhase_ += synth::drums::kTwoPi * MalletModule::kTremoloHz / sampleRate_;
        if (tremoloPhase_ > synth::drums::kTwoPi)
            tremoloPhase_ -= synth::drums::kTwoPi;
    }
    // Several bars at full velocity can sum past full scale: round only the excess above 0.9.
    const float mag = std::fabs(sum);
    if (mag > 0.9f)
        sum = std::copysign(0.9f + 0.08f * std::tanh((mag - 0.9f) * 12.5f), sum);
    return sum;
}

void MalletModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    // The cables arrive on the input channels, and the render overwrites channels 0 and 1 as it goes: the live cables
    // are read before anything writes the buffer, and a note's cables on its own sample, which the render has not
    // reached yet. blockCV reads 0 when a layout has no such channel.
    const float levelCv = blockCV(buffer, kLevelCvChannel);
    const float tremoloCv = blockCV(buffer, kTremoloCvChannel);

    // A pure source has no dry signal to pass through, so bypass and mute both clear. They stay two
    // branches, and either one cuts every bar that is still ringing.
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

    smoothedLevel_.setTargetValue(modulateNormalised(*levelParam, levelParam->get(), levelCv));
    smoothedTremolo_.setTargetValue(modulateNormalised(*tremoloParam, tremoloParam->get(), tremoloCv));
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
        const int at = juce::jlimit(position, numSamples, metadata.samplePosition);
        renderTo(at);
        if (on)
            noteOn(message.getNoteNumber(), message.getFloatVelocity(), buffer, at);
        else
            noteOff(message.getNoteNumber(), buffer, at);
    }
    renderTo(numSamples);
    // The cables are spent: none leaves the module on a channel beyond the audio pair.
    for (int ch = kNumChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    publishedVoices_.store(activeVoiceCount(), std::memory_order_relaxed);

    if (auto* vb = getVisualBuffer())
        vb->pushBlock(left, numSamples);
}
