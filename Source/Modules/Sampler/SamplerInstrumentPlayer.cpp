#include "Modules/Sampler/SamplerInstrumentPlayer.h"
#include <cmath>

namespace synth::sampler {

namespace {

constexpr int kSustainPedalController = 64;
constexpr float kLimiterKnee = 0.9f;

// A chord of loud notes sums past full scale; only the excess above the knee is rounded, so one note stays as is.
float softLimit(float x) {
    const float mag = std::fabs(x);
    if (mag <= kLimiterKnee)
        return x;
    return std::copysign(kLimiterKnee + 0.08f * std::tanh((mag - kLimiterKnee) * 12.5f), x);
}

bool isInstrumentMessage(const juce::MidiMessage& m) {
    return m.isNoteOnOrOff() || m.isAllNotesOff() || m.isAllSoundOff() || m.isControllerOfType(kSustainPedalController);
}

} // namespace

float blockMean(const juce::AudioBuffer<float>& buffer, int channel) {
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return 0.0f;
    const float* data = buffer.getReadPointer(channel);
    float sum = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        sum += data[i];
    return sum / (float)buffer.getNumSamples();
}

void InstrumentPlayer::prepare(double sampleRate, float level) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    pool_.prepare(sampleRate_);
    level_.reset(sampleRate_, 0.01);
    level_.setCurrentAndTargetValue(level);
    running_ = false;
    publishSoundingNotes();
}

// Replaced instruments stay in `retained_` until the audio thread has let go of them (reference count 1 = only
// this array), so the audio thread never runs an Instrument destructor.
void InstrumentPlayer::publish(Instrument::Ptr instrument) {
    for (int i = retained_.size(); --i >= 0;)
        if (retained_[i]->getReferenceCount() == 1)
            retained_.remove(i);
    if (instrument != nullptr)
        retained_.add(instrument);
    {
        const juce::SpinLock::ScopedLockType lock(lock_);
        active_ = instrument;
    }
    name_ = instrument != nullptr ? instrument->name : juce::String();
    generation_.fetch_add(1, std::memory_order_relaxed);
}

Instrument::Ptr InstrumentPlayer::current() const {
    const juce::SpinLock::ScopedTryLockType lock(lock_);
    return lock.isLocked() ? active_ : Instrument::Ptr();
}

// Try-lock only: a block that races a publish renders silence rather than waiting.
bool InstrumentPlayer::acquireInstrument() {
    const juce::SpinLock::ScopedTryLockType lock(lock_);
    if (!lock.isLocked())
        return false;
    if (active_.get() != audioHeld_.get())
        audioHeld_ = active_;
    pool_.setInstrument(audioHeld_.get());
    return true;
}

void InstrumentPlayer::release() {
    if (!running_ && audioHeld_ == nullptr)
        return;
    pool_.setInstrument(nullptr);
    pool_.stopAll();
    audioHeld_ = nullptr;
    running_ = false;
    publishSoundingNotes();
}

void InstrumentPlayer::applyMessage(const juce::MidiMessage& m, const VoiceParams& voice) {
    if (m.isNoteOn())
        pool_.noteOn(m.getNoteNumber(), m.getFloatVelocity(), voice);
    else if (m.isNoteOff())
        pool_.noteOff(m.getNoteNumber(), voice);
    else if (m.isAllSoundOff())
        pool_.stopAll();
    else if (m.isAllNotesOff())
        pool_.allNotesOff();
    else if (m.isControllerOfType(kSustainPedalController))
        pool_.sustainPedal(m.getControllerValue() >= 64, voice);
}

void InstrumentPlayer::applyLevelAndLimit(float* left, float* right, int numSamples, const float* levelCv,
                                          float level) {
    level_.setTargetValue(level);
    for (int i = 0; i < numSamples; ++i) {
        const float gain = juce::jlimit(0.0f, 1.0f, level_.getNextValue() + (levelCv != nullptr ? levelCv[i] : 0.0f));
        left[i] = softLimit(left[i] * gain);
        right[i] = softLimit(right[i] * gain);
    }
}

void InstrumentPlayer::publishSoundingNotes() {
    const auto notes = pool_.soundingNotes();
    std::uint64_t low = 0, high = 0;
    for (size_t n = 0; n < 64; ++n) {
        low |= notes.test(n) ? (std::uint64_t{1} << n) : 0;
        high |= notes.test(n + 64) ? (std::uint64_t{1} << n) : 0;
    }
    soundingLow_.store(low, std::memory_order_relaxed);
    soundingHigh_.store(high, std::memory_order_relaxed);
}

std::bitset<128> InstrumentPlayer::getSoundingNotes() const {
    const std::uint64_t low = soundingLow_.load(std::memory_order_relaxed);
    const std::uint64_t high = soundingHigh_.load(std::memory_order_relaxed);
    std::bitset<128> notes;
    for (size_t n = 0; n < 64; ++n) {
        notes.set(n, ((low >> n) & 1u) != 0);
        notes.set(n + 64, ((high >> n) & 1u) != 0);
    }
    return notes;
}

// The CV channels are read before channels 0 and 1 (the Pitch CV shares ch1 with the right output) are written; the
// rest are cleared last, after the Level CV has been used.
void InstrumentPlayer::process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                               const PlayerBlockParams& params) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 2 || numSamples <= 0) {
        buffer.clear();
        return;
    }
    VoiceParams voice = params.voice;
    voice.pitchSemis +=
        24.0 * (double)blockMean(buffer, params.pitchCvChannel) + (double)blockMean(buffer, params.fineCvChannel);
    const bool levelCvValid = params.levelCvChannel > 1 && params.levelCvChannel < numChannels;
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    const float* levelCv = levelCvValid ? buffer.getReadPointer(params.levelCvChannel) : nullptr;
    buffer.clear(0, 0, numSamples);
    buffer.clear(1, 0, numSamples);

    if (acquireInstrument()) {
        int position = 0;
        const auto renderTo = [&](int end) {
            if (end > position)
                pool_.render(left, right, position, end, voice);
            position = end;
        };
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (!isInstrumentMessage(message))
                continue;
            renderTo(juce::jlimit(position, numSamples, metadata.samplePosition));
            applyMessage(message, voice);
        }
        renderTo(numSamples);
        running_ = true;
        applyLevelAndLimit(left, right, numSamples, levelCv, params.level);
        publishSoundingNotes();
    }
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);
}

} // namespace synth::sampler
