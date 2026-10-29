// DrumKitModulePadAudio.cpp -- the Drum Kit's pads on the audio thread: taking up the published pad snapshot, the
// group parameters' automation reaching the pads, striking a pad (built-in drum, recorded kit or file), the hat choke
// and the chunk mix. No allocation and no locks beyond try-locks.
#include "DrumKitModule.h"
#include "Modules/Sampler/SamplerInstrumentPlayer.h"

namespace synth::drums {

namespace {
constexpr std::array<int, 6> kTomSounds = {41, 43, 45, 47, 48, 50};
// Low floor tom up to high tom, in Hz.
constexpr std::array<float, 6> kTomHz = {82.0f, 98.0f, 117.0f, 140.0f, 165.0f, 196.0f};
constexpr int kNoRank = 99;
} // namespace

int tomIndexForSound(int sound) noexcept {
    for (std::size_t i = 0; i < kTomSounds.size(); ++i)
        if (kTomSounds[i] == sound)
            return static_cast<int>(i);
    return -1;
}

float tomHzForIndex(int index) noexcept { return kTomHz[(std::size_t)std::clamp(index, 0, 5)]; }

int tickRankForSound(int sound) noexcept {
    switch (DrumKitModule::drumForNote(sound)) {
    case DrumKitModule::Drum::Kick:
        return 0;
    case DrumKitModule::Drum::Snare:
        return 1;
    case DrumKitModule::Drum::Rim:
        return 2;
    case DrumKitModule::Drum::Clap:
        return 3;
    case DrumKitModule::Drum::ClosedHat:
        return 4;
    case DrumKitModule::Drum::OpenHat:
        return 5;
    case DrumKitModule::Drum::Crash:
        return 6;
    case DrumKitModule::Drum::Ride:
        return 7;
    case DrumKitModule::Drum::Cowbell:
        return 8;
    case DrumKitModule::Drum::Tom:
        return 9 + tomIndexForSound(sound);
    case DrumKitModule::Drum::None:
        break;
    }
    return kNoRank;
}

// Decay is how long the file holds before a 50 ms fade, so a long recording can be cut short like a drum.
void SampleVoice::trigger(const sampler::SampleData::Ptr& sample, float start, float ratio, float g, float holdSeconds,
                          double sr) noexcept {
    data = sample;
    const int frames = data->audio.getNumSamples();
    pos = (double)std::clamp(start, 0.0f, 1.0f) * (double)juce::jmax(0, frames - 1);
    step = (double)ratio * data->sourceSampleRate / sr;
    gain = g;
    holdLeft = (int)(holdSeconds * sr);
    fade.level = 1.0f;
    fade.setDecay(0.05f, sr);
    active = frames > 0;
}

void SampleVoice::choke(double sr) noexcept {
    holdLeft = 0;
    fade.setDecay(0.003f, sr);
}

void SampleVoice::render(float* left, float* right, int count) noexcept {
    if (!active || data == nullptr)
        return;
    const int frames = data->audio.getNumSamples();
    const float* l = data->audio.getReadPointer(0);
    const float* r = data->audio.getNumChannels() > 1 ? data->audio.getReadPointer(1) : l;
    for (int i = 0; i < count; ++i) {
        if (pos >= (double)frames || !fade.active()) {
            active = false;
            return;
        }
        float amp = gain;
        if (holdLeft > 0)
            --holdLeft;
        else
            amp *= fade.tick();
        left[i] += sampler::interpolateCatmullRom(l, frames, pos) * amp;
        right[i] += sampler::interpolateCatmullRom(r, frames, pos) * amp;
        pos += step;
    }
}

} // namespace synth::drums

namespace {

using synth::drums::kMaxPads;
using synth::drums::PadKind;
using synth::sampler::VoiceParams;

constexpr float kChokeSeconds = 0.012f;
constexpr float kPedalHatGain = 0.8f;
// How much velocity shapes a recording's level on top of the layer it picks (the Sampler's Feel, fixed).
constexpr float kVelocityFeel = 0.5f;
constexpr int kMaxNote = 127;

// Fresh each call: a drum is one-shot, so only the envelope's attack matters and nothing is ever released.
VoiceParams recordedVoiceParams() {
    VoiceParams p;
    p.maxVoices = synth::sampler::VoicePool::kMaxVoices;
    return p;
}

int kitNoteOf(const synth::drums::PadSnapshot::Entry& e) {
    return e.sound != synth::drums::kNoSound ? e.sound : e.note;
}

} // namespace

// Once per block, with a try-lock: a block that races a publish keeps the pads it had.
void DrumKitModule::acquirePads() noexcept {
    const juce::SpinLock::ScopedTryLockType lock(padLock_);
    if (lock.isLocked() && publishedPads_.get() != heldPads_.get())
        heldPads_ = publishedPads_;
}

// Automation writes a group parameter with setValue, which tells no listener: its new value reaches the pads here,
// the same way the message-thread listener does it.
void DrumKitModule::pollGroupLinks() noexcept {
    if (heldPads_ == nullptr)
        return;
    for (int l = 0; l < numLinks_; ++l) {
        auto& link = links_[(size_t)l];
        const float v = link.param->get();
        if (v == link.audioSeen.load(std::memory_order_relaxed))
            continue;
        link.audioSeen.store(v, std::memory_order_relaxed);
        for (int i = 0; i < kMaxPads; ++i)
            if (linkReaches(link, heldPads_->pads[(size_t)i].sound)) {
                auto* p = padParameter(i, link.field);
                static_cast<juce::AudioProcessorParameter*>(p)->setValue(p->convertTo0to1(v));
            }
    }
}

// Every pad on the note strikes, in pad order.
void DrumKitModule::triggerNote(int note, float velocity) noexcept {
    if (heldPads_ == nullptr)
        return;
    for (int i = 0; i < kMaxPads; ++i)
        if (heldPads_->pads[(size_t)i].note == note)
            triggerPad(i, velocity);
}

void DrumKitModule::triggerPad(int index, float velocity) noexcept {
    if (heldPads_ == nullptr)
        return;
    const auto& e = heldPads_->pads[(size_t)index];
    lastPlayedPad_.store(index, std::memory_order_relaxed);
    switch (e.kind) {
    case PadKind::Instrument:
        return;
    case PadKind::File: {
        if (e.sample == nullptr)
            return;
        if (synth::drums::chokesOpenHat(e.sound))
            chokeOpenHats();
        const auto& p = padParams_[(size_t)index];
        const float ratio = synth::drums::semitoneRatio(p.tune->get());
        voices_[(size_t)index].sample.trigger(e.sample, p.start->get(), ratio,
                                              p.level->get() * juce::jlimit(0.0f, 1.0f, velocity), p.decay->get(),
                                              sampleRate_);
        return;
    }
    case PadKind::Kit: {
        const int kitNote = juce::jlimit(0, kMaxNote, kitNoteOf(e));
        if (poolKit_ != nullptr && covered_.test((size_t)kitNote)) {
            triggerRecordedPad(index, kitNote, e.sound, velocity);
            return;
        }
        break;
    }
    case PadKind::BuiltIn:
        break;
    }
    triggerSynth(index, e.sound, velocity);
}

void DrumKitModule::triggerSynth(int index, int sound, float velocity) noexcept {
    using synth::drums::MetallicKind;
    using synth::drums::VoiceType;
    const auto& p = padParams_[(size_t)index];
    auto& v = voices_[(size_t)index];
    synth::drums::Hit hit;
    hit.velocity = juce::jlimit(0.0f, 1.0f, velocity);
    hit.gain = p.level->get();
    hit.tune = synth::drums::semitoneRatio(p.tune->get());
    hit.decay = p.decay->get();
    switch (drumForNote(sound)) {
    case Drum::Kick:
        v.type = VoiceType::Kick;
        v.kick.trigger(hit, sampleRate_);
        break;
    case Drum::Snare:
    case Drum::Rim:
        v.type = VoiceType::Snare;
        v.snare.trigger(hit, drumForNote(sound) == Drum::Rim, sampleRate_);
        break;
    case Drum::Clap:
        v.type = VoiceType::Clap;
        v.clap.trigger(hit, sampleRate_);
        break;
    case Drum::ClosedHat: {
        // The pedal hat (44) is a little shorter and softer than the closed hat (42).
        const bool pedal = sound == 44;
        hit.decay *= pedal ? 0.8f : 1.0f;
        if (pedal)
            hit.gain *= kPedalHatGain;
        chokeOpenHats();
        v.type = VoiceType::Metal;
        v.metal.trigger(hit, MetallicKind::ClosedHat, sampleRate_);
        break;
    }
    case Drum::OpenHat:
        v.type = VoiceType::Metal;
        v.metal.trigger(hit, MetallicKind::OpenHat, sampleRate_);
        break;
    case Drum::Tom:
        v.type = VoiceType::Tom;
        v.tom.trigger(hit, synth::drums::tomHzForIndex(synth::drums::tomIndexForSound(sound)), sampleRate_);
        break;
    case Drum::Crash:
        v.type = VoiceType::Metal;
        v.metal.trigger(hit, MetallicKind::Crash, sampleRate_);
        break;
    case Drum::Ride:
        hit.decay *= 0.8f;
        v.type = VoiceType::Metal;
        v.metal.trigger(hit, MetallicKind::Ride, sampleRate_);
        break;
    case Drum::Cowbell:
        v.type = VoiceType::Cowbell;
        v.cowbell.trigger(hit, sampleRate_);
        break;
    case Drum::None:
        break;
    }
}

void DrumKitModule::triggerRecordedPad(int index, int kitNote, int sound, float velocity) noexcept {
    if (synth::drums::chokesOpenHat(sound))
        chokeOpenHats();
    const auto& p = padParams_[(size_t)index];
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    float gain = p.level->get() * ((1.0f - kVelocityFeel) + kVelocityFeel * std::pow(vel, 1.6f));
    if (sound == 44)
        gain *= kPedalHatGain;
    recordedPool_.noteOn(kitNote, vel, recordedVoiceParams(), gain, (double)p.tune->get());
}

// A closed or pedal hat shuts every open hat pad, whichever kind of voice it is playing.
void DrumKitModule::chokeOpenHats() noexcept {
    for (int i = 0; i < kMaxPads; ++i) {
        const auto& e = heldPads_->pads[(size_t)i];
        if (!synth::drums::isOpenHat(e.sound))
            continue;
        auto& v = voices_[(size_t)i];
        if (v.type == synth::drums::VoiceType::Metal)
            v.metal.choke(sampleRate_);
        if (v.sample.active)
            v.sample.choke(sampleRate_);
        recordedPool_.chokeNote(juce::jlimit(0, kMaxNote, kitNoteOf(e)), kChokeSeconds);
    }
}

bool DrumKitModule::samplesActive() const noexcept {
    for (const auto& v : voices_)
        if (v.sample.active)
            return true;
    return false;
}

void DrumKitModule::renderSamples(float* left, float* right, int count) noexcept {
    for (auto& v : voices_)
        v.sample.render(left, right, count);
}

// Mixes `count` samples: the synth drums (mono, onto both sides) plus the recorded and file voices (stereo), then
// the module's Level and the soft limit. Recordings get no extra headroom: unlike the Sampler's chords only a few
// hits overlap, and a -9 dB trim made a recorded kit play about 14 dB below the synth kit at the same settings.
void DrumKitModule::renderChunk(float* left, float* right, int count) noexcept {
    std::array<float, kRenderChunk> recL{}, recR{};
    const bool kit = poolKit_ != nullptr;
    if (kit)
        recordedPool_.render(recL.data(), recR.data(), 0, count, recordedVoiceParams());
    const bool files = samplesActive();
    if (files)
        renderSamples(recL.data(), recR.data(), count);
    const bool stereo = kit || files;
    for (int i = 0; i < count; ++i) {
        const float synthSum = synthSample();
        const float level = smoothedLevel_.getNextValue();
        if (!stereo) {
            left[i] = limited(synthSum * level);
            if (right != nullptr)
                right[i] = left[i];
            continue;
        }
        left[i] = limited((synthSum + recL[(size_t)i]) * level);
        if (right != nullptr)
            right[i] = limited((synthSum + recR[(size_t)i]) * level);
    }
    int sounding = recordedPool_.activeVoiceCount();
    for (const auto& v : voices_)
        sounding += v.sample.active ? 1 : 0;
    recordedVoices_.store(sounding, std::memory_order_relaxed);
}
