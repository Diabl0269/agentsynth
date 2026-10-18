#pragma once

// The Sampler's Instrument mode: hands a loaded Instrument to the audio thread and renders MIDI through the voice pool.

#include "Modules/Sampler/SamplerVoicePool.h"
#include <atomic>
#include <bitset>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth::sampler {

struct PlayerBlockParams {
    VoiceParams voice; // pitchSemis is the knob part (Pitch + Fine); the CV is added per block
    float level = 0.8f;
    int pitchCvChannel = -1; // CV channels of the buffer; -1 = none. Must not be channel 0 or 1.
    int fineCvChannel = -1;
    int levelCvChannel = -1;
};

/** The mean of one CV channel over the block; 0 for a channel the buffer does not have. */
float blockMean(const juce::AudioBuffer<float>& buffer, int channel);

class InstrumentPlayer {
public:
    /** Stops every voice and snaps the level smoother to `level`. */
    void prepare(double sampleRate, float level);

    /** Message thread. Replaces the instrument (null clears it). */
    void publish(Instrument::Ptr instrument);

    /** Message thread. The published instrument, or null when none is loaded or a publish is in flight. */
    Instrument::Ptr current() const;

    /** Audio thread. Points `held` at the published instrument with a try-lock; false (leaving `held`) while a publish
        is in flight. The caller keeps `held` until it next acquires, so a replaced instrument is never freed here. */
    bool tryAcquire(Instrument::Ptr& held) const;

    /** Message thread. */
    juce::String name() const { return name_; }

    /** Bumped by every publish. */
    int generation() const noexcept { return generation_.load(std::memory_order_relaxed); }

    /** Audio thread. Renders one block into channels 0 and 1 and clears every other channel (after reading the CV
        channels named in `params`). Silence while a publish is in flight. */
    void process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const PlayerBlockParams& params);

    /** Audio thread. Cuts every voice and lets go of the instrument; cheap when already idle. */
    void release();

    /** Any thread. The notes sounding at the end of the last block. */
    std::bitset<128> getSoundingNotes() const;

    /** Any thread. The voices sounding (held or releasing) at the end of the last block. */
    int activeVoiceCount() const noexcept { return activeVoices_.load(std::memory_order_relaxed); }

private:
    bool acquireInstrument();
    void applyMessage(const juce::MidiMessage& message, const VoiceParams& voice);
    void applyLevelAndLimit(float* left, float* right, int numSamples, const float* levelCv, float level);
    void publishSoundingNotes();

    mutable juce::SpinLock lock_;
    Instrument::Ptr active_;                           // guarded by lock_
    juce::ReferenceCountedArray<Instrument> retained_; // message thread: keeps replaced instruments alive
    juce::String name_;                                // message thread
    std::atomic<int> generation_{0};

    VoicePool pool_;
    Instrument::Ptr audioHeld_; // audio thread: the instrument the pool points at
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> level_;
    double sampleRate_ = 44100.0;
    bool running_ = false;
    std::atomic<std::uint64_t> soundingLow_{0};
    std::atomic<std::uint64_t> soundingHigh_{0};
    std::atomic<int> activeVoices_{0};
};

} // namespace synth::sampler
