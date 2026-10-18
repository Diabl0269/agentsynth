#pragma once

// The polyphonic renderer of an Instrument: one voice per sounding region, with envelope, loop, tone and pedal.

#include "Modules/InstrumentVoiceUtil.h"
#include "Modules/Sampler/SamplerInstrument.h"
#include <array>
#include <bitset>
#include <cstdint>

namespace synth::sampler {

struct VoiceParams {
    float attackSeconds = 0.001f;
    float releaseSeconds = 0.3f;
    float tone = 1.0f; // 0..1, 1 = fully open (no filter)
    float feel = 0.0f; // 0..1, how much velocity shapes level and brightness
    int maxVoices = 16;
    bool releaseSound = false; // fire the instrument's release-trigger regions on note-off
    double pitchSemis = 0.0;   // transpose + fine + CV, applied to every voice live
};

class VoicePool {
public:
    static constexpr int kMaxVoices = 32;

    /** Stops every voice. */
    void prepare(double sampleRate);

    /** Audio thread, once per block before render. Null = silence; a different instrument stops all voices. */
    void setInstrument(const Instrument* inst);

    /** Audio thread. `velocity01` is mapped to MIDI velocity 1..127. */
    void noteOn(int note, float velocity01, const VoiceParams& params);
    void noteOff(int note, const VoiceParams& params);
    void sustainPedal(bool down, const VoiceParams& params);

    /** Releases every voice (pedal ignored) with the release time of the latest call that carried params. */
    void allNotesOff();

    /** Silences every voice at once and forgets held keys and the pedal. */
    void stopAll();

    /** Adds frames [start, end) of every voice into `left` and `right`; the caller clears them first. */
    void render(float* left, float* right, int start, int end, const VoiceParams& params);

    int activeVoiceCount() const;
    std::bitset<128> soundingNotes() const;

private:
    enum class Stage { Attack, Sustain, Release };

    struct Voice {
        bool active = false;
        bool held = true;
        bool keyDown = false;
        bool pedalHeld = false;
        bool releaseVoice = false;
        std::uint32_t serial = 0;
        int note = 0;
        int regionIndex = 0;
        Stage stage = Stage::Attack;
        double position = 0.0;
        double semis = 0.0;
        double rateRatio = 1.0;
        float gain = 1.0f;
        float velocity01 = 1.0f;
        float env = 0.0f;
        float attackStep = 1.0f;
        float releaseCoef = 0.999f;
        float lowpassL = 0.0f, lowpassR = 0.0f;
        synth::voices::PeakWatch watch;
    };

    struct BlockContext;

    void startVoices(int note, float velocity01, int velocity127, bool releaseTrigger, const VoiceParams& p);
    Voice* allocateVoice(const VoiceParams& p, bool allowSteal);
    void beginRelease(Voice& v, float seconds);
    void releaseKey(Voice& v, const VoiceParams& p);
    float releaseSecondsFor(const Voice& v, const VoiceParams& p) const;
    bool renderVoice(Voice& v, const BlockContext& ctx, float* left, float* right, int start, int end);

    const Instrument* instrument_ = nullptr;
    double sampleRate_ = 44100.0;
    std::array<Voice, kMaxVoices> voices_{};
    std::array<int, 128> pressedVelocity_{};
    std::bitset<128> noteDown_;
    bool pedalDown_ = false;
    std::uint32_t nextSerial_ = 1;
    int roundRobin_ = 0;
    float lastReleaseSeconds_ = 0.3f;
};

} // namespace synth::sampler
