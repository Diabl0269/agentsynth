#pragma once

// The Drum Kit as it played before pads: one voice per drum, struck by note through the group parameters. A copy of
// the old module's trigger and mix, kept only so the migration tests can prove a project from before pads renders
// the same samples through the pad engine.

#include "Modules/DrumKit/DrumVoices.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <string>
#include <vector>

namespace legacy_drums {

using namespace synth::drums;

struct NoteHit {
    int note;
    float velocity;
    int atSample;
};

/** Group values by the old parameter id (kickLevel ... cowbellDecay, level). Missing ids take the old defaults. */
using GroupValues = std::map<std::string, float>;

inline GroupValues defaultGroupValues() {
    return {{"kickLevel", 0.9f},   {"kickTune", 0.0f},      {"kickDecay", 0.35f},  {"snareLevel", 0.8f},
            {"snareTune", 0.0f},   {"snareDecay", 0.18f},   {"clapLevel", 0.8f},   {"clapTune", 0.0f},
            {"clapDecay", 0.2f},   {"hatLevel", 0.6f},      {"hatTune", 0.0f},     {"closedDecay", 0.05f},
            {"openDecay", 0.45f},  {"tomLevel", 0.8f},      {"tomTune", 0.0f},     {"tomDecay", 0.3f},
            {"cymbalLevel", 0.5f}, {"cymbalTune", 0.0f},    {"cymbalDecay", 1.5f}, {"cowbellLevel", 0.8f},
            {"cowbellTune", 0.0f}, {"cowbellDecay", 0.25f}, {"level", 0.7f}};
}

class Kit {
public:
    Kit(GroupValues values, double sampleRate)
        : v_(std::move(values))
        , sr_(sampleRate) {
        for (const auto& [id, value] : defaultGroupValues())
            v_.emplace(id, value);
        level_.reset(sr_, 0.01);
        level_.setCurrentAndTargetValue(v_["level"]);
    }

    /** Left channel (the old kit wrote the same signal to both) of `samples` samples. */
    std::vector<float> render(const std::vector<NoteHit>& hits, int samples) {
        std::vector<float> out((size_t)samples, 0.0f);
        for (int i = 0; i < samples; ++i) {
            for (const auto& h : hits)
                if (h.atSample == i)
                    trigger(h.note, h.velocity);
            out[(size_t)i] = limited(synthSample() * level_.getNextValue());
        }
        return out;
    }

private:
    Hit hit(const char* level, const char* tune, float decay, float velocity) {
        Hit h;
        h.velocity = std::clamp(velocity, 0.0f, 1.0f);
        h.gain = v_[level];
        h.tune = semitoneRatio(v_[tune]);
        h.decay = decay;
        return h;
    }

    static int tomIndex(int note) {
        const int notes[] = {41, 43, 45, 47, 48, 50};
        for (int i = 0; i < 6; ++i)
            if (notes[i] == note)
                return i;
        return -1;
    }

    void trigger(int note, float velocity) {
        static constexpr float kTomHz[] = {82.0f, 98.0f, 117.0f, 140.0f, 165.0f, 196.0f};
        switch (note) {
        case 35:
        case 36:
            kick_.trigger(hit("kickLevel", "kickTune", v_["kickDecay"], velocity), sr_);
            return;
        case 38:
        case 40:
            snare_.trigger(hit("snareLevel", "snareTune", v_["snareDecay"], velocity), false, sr_);
            return;
        case 37:
            rim_.trigger(hit("snareLevel", "snareTune", v_["snareDecay"], velocity), true, sr_);
            return;
        case 39:
            clap_.trigger(hit("clapLevel", "clapTune", v_["clapDecay"], velocity), sr_);
            return;
        case 42:
        case 44: {
            const bool pedal = note == 44;
            auto h = hit("hatLevel", "hatTune", v_["closedDecay"] * (pedal ? 0.8f : 1.0f), velocity);
            if (pedal)
                h.gain *= 0.8f;
            openHat_.choke(sr_);
            closedHat_.trigger(h, MetallicKind::ClosedHat, sr_);
            return;
        }
        case 46:
            openHat_.trigger(hit("hatLevel", "hatTune", v_["openDecay"], velocity), MetallicKind::OpenHat, sr_);
            return;
        case 49:
        case 57:
            crash_.trigger(hit("cymbalLevel", "cymbalTune", v_["cymbalDecay"], velocity), MetallicKind::Crash, sr_);
            return;
        case 51:
        case 59:
            ride_.trigger(hit("cymbalLevel", "cymbalTune", v_["cymbalDecay"] * 0.8f, velocity), MetallicKind::Ride,
                          sr_);
            return;
        case 56:
            cowbell_.trigger(hit("cowbellLevel", "cowbellTune", v_["cowbellDecay"], velocity), sr_);
            return;
        default:
            if (const int t = tomIndex(note); t >= 0)
                toms_[(size_t)t].trigger(hit("tomLevel", "tomTune", v_["tomDecay"], velocity), kTomHz[t], sr_);
        }
    }

    float synthSample() {
        float sum = 0.0f;
        if (kick_.active())
            sum += kick_.tick(sr_, noise_);
        if (snare_.active())
            sum += snare_.tick(sr_, noise_);
        if (rim_.active())
            sum += rim_.tick(sr_, noise_);
        if (clap_.active())
            sum += clap_.tick(noise_);
        if (closedHat_.active())
            sum += closedHat_.tick(sr_, noise_);
        if (openHat_.active())
            sum += openHat_.tick(sr_, noise_);
        if (crash_.active())
            sum += crash_.tick(sr_, noise_);
        if (ride_.active())
            sum += ride_.tick(sr_, noise_);
        if (cowbell_.active())
            sum += cowbell_.tick(sr_);
        for (auto& tom : toms_)
            if (tom.active())
                sum += tom.tick(sr_);
        return sum;
    }

    static float limited(float sum) {
        const float mag = std::fabs(sum);
        if (mag > 0.9f)
            sum = std::copysign(0.9f + 0.08f * std::tanh((mag - 0.9f) * 12.5f), sum);
        return sum;
    }

    GroupValues v_;
    double sr_;
    NoiseSource noise_;
    KickVoice kick_;
    SnareVoice snare_, rim_;
    ClapVoice clap_;
    CowbellVoice cowbell_;
    MetallicVoice closedHat_, openHat_, crash_, ride_;
    std::array<TomVoice, 6> toms_;
    juce::SmoothedValue<float> level_;
};

} // namespace legacy_drums
