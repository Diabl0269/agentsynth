#pragma once

// The audio thread's side of the Drum Kit's pads: the immutable pad snapshot the message thread publishes, and one
// voice per pad slot (every synthesized drum kind plus an audio-file player), so two pads on one drum both sound.

#include "DrumKitPads.h"
#include "DrumVoices.h"
#include "Modules/Sampler/SamplerSampleData.h"
#include <cstdint>

namespace synth::drums {

/** What the audio thread needs of the pad table. Built and freed on the message thread only. */
struct PadSnapshot : public juce::ReferenceCountedObject {
    using Ptr = juce::ReferenceCountedObjectPtr<PadSnapshot>;
    struct Entry {
        PadKind kind = PadKind::BuiltIn;
        int sound = kNoSound;
        int note = 60;
        sampler::SampleData::Ptr sample; // PadKind::File, once decoded
    };
    std::array<Entry, kMaxPads> pads;
    std::array<std::int8_t, kMaxPads> tickOrder{}; // every pad, in the order the old one-voice-per-drum kit mixed
};

/** The order the original kit summed its voices in (kick, snare, rim, clap, closed hat, open hat, crash, ride,
    cowbell, then the toms low to high): the shared noise source is read in this order, so it is part of the sound. */
int tickRankForSound(int sound) noexcept;

/** Low floor tom (41) up to high tom (50): the tom index of `sound`, or -1. */
int tomIndexForSound(int sound) noexcept;
float tomHzForIndex(int index) noexcept;

/** Plays a decoded file from a start point at a pitch ratio; holds for `holdSeconds`, then fades out. */
struct SampleVoice {
    sampler::SampleData::Ptr data; // kept alive by the module's message-thread retain list
    double pos = 0.0, step = 1.0;
    float gain = 0.0f;
    int holdLeft = 0;
    DecayEnv fade;
    bool active = false;

    void trigger(const sampler::SampleData::Ptr& sample, float start, float ratio, float g, float holdSeconds,
                 double sr) noexcept;
    void choke(double sr) noexcept;
    void stop() noexcept { active = false; }
    /** Adds `count` samples into left/right (a mono file goes to both). */
    void render(float* left, float* right, int count) noexcept;
};

enum class VoiceType : std::uint8_t { None, Kick, Snare, Clap, Metal, Tom, Cowbell };

struct PadVoice {
    VoiceType type = VoiceType::None;
    KickVoice kick;
    SnareVoice snare;
    ClapVoice clap;
    MetallicVoice metal;
    TomVoice tom;
    CowbellVoice cowbell;
    SampleVoice sample;

    bool synthActive() const noexcept {
        switch (type) {
        case VoiceType::Kick:
            return kick.active();
        case VoiceType::Snare:
            return snare.active();
        case VoiceType::Clap:
            return clap.active();
        case VoiceType::Metal:
            return metal.active();
        case VoiceType::Tom:
            return tom.active();
        case VoiceType::Cowbell:
            return cowbell.active();
        case VoiceType::None:
            break;
        }
        return false;
    }

    /** Call only while synthActive(). */
    float tickSynth(double sr, NoiseSource& noise) noexcept {
        switch (type) {
        case VoiceType::Kick:
            return kick.tick(sr, noise);
        case VoiceType::Snare:
            return snare.tick(sr, noise);
        case VoiceType::Clap:
            return clap.tick(noise);
        case VoiceType::Metal:
            return metal.tick(sr, noise);
        case VoiceType::Tom:
            return tom.tick(sr);
        case VoiceType::Cowbell:
            return cowbell.tick(sr);
        case VoiceType::None:
            break;
        }
        return 0.0f;
    }

    void stop() noexcept {
        kick.stop();
        snare.stop();
        clap.stop();
        metal.stop();
        tom.stop();
        cowbell.stop();
        sample.stop();
    }
};

} // namespace synth::drums
