#include "Modules/Sampler/SamplerVoicePool.h"
#include <algorithm>
#include <cmath>

namespace synth::sampler {

namespace {

constexpr float kSilenceEnv = 1.0e-4f;
constexpr int kMinReleaseSamples = 64;
constexpr int kEndFadeFrames = 64;
constexpr float kRetriggerReleaseSeconds = 0.03f;
constexpr float kStealFadeSeconds = 0.005f;
constexpr float kMinAttackSeconds = 0.001f;
constexpr double kQuietWindowSeconds = 0.5;
constexpr float kFilterBypassHz = 18000.0f;
constexpr float kMinCutoffHz = 200.0f;
constexpr float kMaxCutoffHz = 20000.0f;
constexpr int kGlideChunkFrames = 32; // the pitch of a gliding voice is recomputed this often

bool isHeldKeyVoice(const auto& v) { return v.active && v.keyDown && !v.releaseVoice && v.held; }

float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

// Stand-in used to steal among active voices only: pickVoice prefers free voices, so inactive ones are made
// to look like the newest held voices.
struct StealView {
    bool active;
    bool held;
    std::uint32_t serial;
};

} // namespace

struct VoicePool::BlockContext {
    const Instrument* inst;
    double sampleRate;
    double pitchSemis;
    float tone;
    float feel;
};

void VoicePool::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    stopAll();
}

void VoicePool::setInstrument(const Instrument* inst) {
    if (inst == instrument_)
        return;
    stopAll();
    instrument_ = inst;
}

void VoicePool::stopAll() {
    for (auto& v : voices_)
        v = Voice{};
    noteDown_.reset();
    pedalDown_ = false;
    monoCount_ = 0;
    lastNote_ = -1;
}

int VoicePool::activeVoiceCount() const {
    return (int)std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; });
}

std::bitset<128> VoicePool::soundingNotes() const {
    std::bitset<128> notes;
    for (const auto& v : voices_)
        if (v.active)
            notes.set((size_t)v.note);
    return notes;
}

// maxVoices limits the voices that are not already releasing. At the limit the oldest of them is given a short
// fade (a dying tail that keeps rendering) and the new note takes a free physical slot, so stealing never
// clicks; only when all physical slots are busy is a slot reused outright (oldest released voice first).
// Returns null when `allowSteal` is false and the limit is reached.
VoicePool::Voice* VoicePool::allocateVoice(const VoiceParams& p, bool allowSteal) {
    const int limit = juce::jlimit(1, kMaxVoices, p.maxVoices);
    const int sounding = (int)std::count_if(voices_.begin(), voices_.end(),
                                            [](const Voice& v) { return v.active && v.stage != Stage::Release; });
    if (sounding >= limit) {
        if (!allowSteal)
            return nullptr;
        std::array<StealView, kMaxVoices> view;
        for (size_t i = 0; i < voices_.size(); ++i) {
            const Voice& v = voices_[i];
            const bool candidate = v.active && v.stage != Stage::Release;
            view[i] = candidate ? StealView{true, true, v.serial} : StealView{true, true, UINT32_MAX};
        }
        beginRelease(voices_[synth::voices::pickVoice(view)], kStealFadeSeconds);
    }
    return &voices_[synth::voices::pickVoice(voices_)];
}

void VoicePool::startVoices(int note, float velocity01, int velocity127, bool releaseTrigger, const VoiceParams& p,
                            float gain, double extraSemis) {
    std::array<RegionHit, kMaxHitsPerNote> hits;
    const int count = selectRegions(*instrument_, note, velocity127, releaseTrigger, roundRobin_++, hits);
    for (int h = 0; h < count; ++h) {
        Voice* v = allocateVoice(p, !releaseTrigger);
        if (v == nullptr)
            return;
        const Region& region = instrument_->regions[(size_t)hits[(size_t)h].regionIndex];
        *v = Voice{};
        v->active = true;
        v->serial = nextSerial_++;
        v->note = note;
        v->regionIndex = hits[(size_t)h].regionIndex;
        v->releaseVoice = releaseTrigger;
        v->keyDown = !releaseTrigger;
        v->semis = (double)(note - region.rootKey) + (double)region.tuneCents / 100.0 + extraSemis;
        if (!releaseTrigger && p.glideSeconds > 0.0f && lastNote_ >= 0 && lastNote_ != note) {
            v->glideFrames = std::max(1, (int)(p.glideSeconds * sampleRate_));
            v->glideOffset = (double)(lastNote_ - note);
            v->glideStep = v->glideOffset / (double)v->glideFrames;
        }
        v->rateRatio = region.sample->sourceSampleRate / sampleRate_;
        v->velocity01 = velocity01;
        const float feel = juce::jlimit(0.0f, 1.0f, p.feel);
        v->gain = gain * hits[(size_t)h].gain * dbToGain(region.volumeDb) *
                  ((1.0f - feel) + feel * std::pow(velocity01, 1.6f));
        v->attackStep = 1.0f / (float)std::max(1.0, std::max(p.attackSeconds, kMinAttackSeconds) * sampleRate_);
        v->watch.reset((int)(kQuietWindowSeconds * sampleRate_));
    }
}

void VoicePool::noteOn(int note, float velocity01, const VoiceParams& p, float gain, double extraSemis) {
    if (instrument_ == nullptr || note < 0 || note > 127)
        return;
    lastReleaseSeconds_ = p.releaseSeconds;
    const float vel = juce::jlimit(0.0f, 1.0f, velocity01);
    const int velocity127 = juce::jlimit(1, 127, (int)std::lround(vel * 127.0f));
    noteDown_.set((size_t)note);
    pressedVelocity_[(size_t)note] = velocity127;
    if (p.mono) {
        pushMonoKey(note);
        if (retargetHeldVoices(note, p)) {
            lastNote_ = note;
            return;
        }
        for (auto& v : voices_)
            if (isHeldKeyVoice(v)) {
                v.keyDown = false;
                beginRelease(v, kRetriggerReleaseSeconds);
            }
    } else {
        monoCount_ = 0;
    }
    for (auto& v : voices_)
        if (v.active && v.note == note && v.held && !v.releaseVoice)
            beginRelease(v, kRetriggerReleaseSeconds);
    startVoices(note, (float)velocity127 / 127.0f, velocity127, false, p, gain, extraSemis);
    lastNote_ = note;
}

void VoicePool::chokeNote(int note, float seconds) {
    for (auto& v : voices_)
        if (v.active && v.note == note && v.stage != Stage::Release)
            beginRelease(v, seconds);
}

void VoicePool::pushMonoKey(int note) {
    removeMonoKey(note);
    if (monoCount_ == kMonoStack) {
        std::copy(monoStack_.begin() + 1, monoStack_.end(), monoStack_.begin());
        --monoCount_;
    }
    monoStack_[(size_t)monoCount_++] = note;
}

void VoicePool::removeMonoKey(int note) {
    const auto end = monoStack_.begin() + monoCount_;
    monoCount_ = (int)(std::remove(monoStack_.begin(), end, note) - monoStack_.begin());
}

// Every held voice slides to `note` without a new attack, unless `note` lies outside the keys of one of their
// recordings (then a fresh note is struck instead, so a leap never stretches a sample across the keyboard).
bool VoicePool::retargetHeldVoices(int note, const VoiceParams& p) {
    bool any = false;
    for (const auto& v : voices_) {
        if (!isHeldKeyVoice(v))
            continue;
        const Region& region = instrument_->regions[(size_t)v.regionIndex];
        if (note < region.loKey || note > region.hiKey)
            return false;
        any = true;
    }
    if (!any)
        return false;
    for (auto& v : voices_)
        if (isHeldKeyVoice(v))
            retarget(v, note, p);
    return true;
}

void VoicePool::retarget(Voice& v, int note, const VoiceParams& p) {
    const Region& region = instrument_->regions[(size_t)v.regionIndex];
    const double soundingSemis = v.semis + v.glideOffset;
    v.note = note;
    v.semis = (double)(note - region.rootKey) + (double)region.tuneCents / 100.0;
    v.glideFrames = 0;
    v.glideOffset = 0.0;
    if (p.glideSeconds > 0.0f) {
        v.glideFrames = std::max(1, (int)(p.glideSeconds * sampleRate_));
        v.glideOffset = soundingSemis - v.semis;
        v.glideStep = v.glideOffset / (double)v.glideFrames;
    }
}

// Mono: letting go of the sounding key while an older one is still down returns to that older key.
bool VoicePool::returnToHeldKey(int note, const VoiceParams& p) {
    removeMonoKey(note);
    if (monoCount_ == 0)
        return false;
    const int back = monoStack_[(size_t)monoCount_ - 1];
    for (const auto& v : voices_)
        if (isHeldKeyVoice(v) && v.note == back)
            return true; // the sounding key is still down: nothing changes
    if (!retargetHeldVoices(back, p)) {
        monoCount_ = 0;
        return false;
    }
    return true;
}

float VoicePool::releaseSecondsFor(const Voice& v, const VoiceParams& p) const {
    const float own = instrument_->regions[(size_t)v.regionIndex].ampReleaseSeconds;
    return own >= 0.0f ? own : p.releaseSeconds;
}

// The envelope falls to kSilenceEnv over `seconds` (never fewer than kMinReleaseSamples, so it cannot click)
// and a release already in progress only ever gets faster.
void VoicePool::beginRelease(Voice& v, float seconds) {
    const double samples = std::max((double)kMinReleaseSamples, (double)seconds * sampleRate_);
    const float coef = (float)std::pow((double)kSilenceEnv, 1.0 / samples);
    v.releaseCoef = v.stage == Stage::Release ? std::min(v.releaseCoef, coef) : coef;
    v.stage = Stage::Release;
    v.held = false;
}

// A key or pedal release: one-shot regions ignore it and play to the end.
void VoicePool::releaseKey(Voice& v, const VoiceParams& p) {
    if (instrument_->regions[(size_t)v.regionIndex].loop == RegionSpec::Loop::OneShot || v.releaseVoice)
        return;
    beginRelease(v, releaseSecondsFor(v, p));
}

void VoicePool::noteOff(int note, const VoiceParams& p) {
    if (instrument_ == nullptr || note < 0 || note > 127)
        return;
    lastReleaseSeconds_ = p.releaseSeconds;
    const bool wasDown = noteDown_.test((size_t)note);
    noteDown_.reset((size_t)note);
    if (p.mono && wasDown && returnToHeldKey(note, p))
        return;
    for (auto& v : voices_) {
        if (!v.active || v.note != note || !v.keyDown)
            continue;
        v.keyDown = false;
        if (pedalDown_)
            v.pedalHeld = true;
        else
            releaseKey(v, p);
    }
    if (p.releaseSound && wasDown)
        startVoices(note, (float)pressedVelocity_[(size_t)note] / 127.0f, pressedVelocity_[(size_t)note], true, p);
}

void VoicePool::sustainPedal(bool down, const VoiceParams& p) {
    pedalDown_ = down;
    if (down || instrument_ == nullptr)
        return;
    lastReleaseSeconds_ = p.releaseSeconds;
    for (auto& v : voices_) {
        if (v.active && v.pedalHeld) {
            v.pedalHeld = false;
            releaseKey(v, p);
        }
    }
}

void VoicePool::allNotesOff() {
    if (instrument_ == nullptr)
        return;
    VoiceParams p;
    p.releaseSeconds = lastReleaseSeconds_;
    noteDown_.reset();
    pedalDown_ = false;
    monoCount_ = 0;
    for (auto& v : voices_) {
        if (!v.active)
            continue;
        v.keyDown = v.pedalHeld = false;
        releaseKey(v, p);
    }
}

void VoicePool::stepGlide(Voice& v, int frames) const {
    if (v.glideFrames <= 0 || frames <= 0)
        return;
    if (frames >= v.glideFrames) {
        v.glideFrames = 0;
        v.glideOffset = 0.0;
        return;
    }
    v.glideOffset -= v.glideStep * (double)frames;
    v.glideFrames -= frames;
}

// Renders one voice's frames; false when the voice has ended.
bool VoicePool::renderVoice(Voice& v, const BlockContext& ctx, float* left, float* right, int start, int end) {
    const Region& region = ctx.inst->regions[(size_t)v.regionIndex];
    const auto& audio = region.sample->audio;
    const int numFrames = audio.getNumSamples();
    const float* srcL = audio.getReadPointer(0);
    const float* srcR = audio.getNumChannels() > 1 ? audio.getReadPointer(1) : srcL;

    const auto incrementNow = [&] {
        return std::exp2((v.semis + v.glideOffset + ctx.pitchSemis) / 12.0) * v.rateRatio;
    };
    double inc = incrementNow();
    int glided = 0;
    float cutoff = kMinCutoffHz * std::pow(kMaxCutoffHz / kMinCutoffHz, ctx.tone);
    cutoff *= std::exp2(ctx.feel * (v.velocity01 - 0.5f) * 2.0f);
    const bool filtered = cutoff < kFilterBypassHz;
    const float coef = 1.0f - std::exp(-juce::MathConstants<float>::twoPi *
                                       std::min(cutoff, (float)(ctx.sampleRate * 0.45)) / (float)ctx.sampleRate);

    double loopStart = 0.0, loopEnd = (double)(numFrames - 1);
    if (region.loopStart >= 0 && region.loopEnd > region.loopStart + 1 && region.loopEnd < numFrames) {
        loopStart = (double)region.loopStart;
        loopEnd = (double)region.loopEnd;
    }
    const bool loops = region.loop == RegionSpec::Loop::Continuous ||
                       (region.loop == RegionSpec::Loop::Sustain && (v.keyDown || v.pedalHeld));
    const double fadeSpan = (double)kEndFadeFrames * inc;

    for (int i = start; i < end; ++i) {
        if (v.glideFrames > 0 && i > start && (i - start) % kGlideChunkFrames == 0) {
            stepGlide(v, kGlideChunkFrames);
            glided += kGlideChunkFrames;
            inc = incrementNow();
        }
        if (v.stage == Stage::Attack) {
            v.env += v.attackStep;
            if (v.env >= 1.0f) {
                v.env = 1.0f;
                v.stage = Stage::Sustain;
            }
        } else if (v.stage == Stage::Release) {
            v.env *= v.releaseCoef;
            if (v.env <= kSilenceEnv)
                return false;
        }
        if (loops) {
            if (v.position >= loopEnd)
                v.position -= loopEnd - loopStart;
        } else if (v.position >= (double)(numFrames - 1)) {
            return false;
        }
        float l = interpolateCatmullRom(srcL, numFrames, v.position);
        float r = srcR == srcL ? l : interpolateCatmullRom(srcR, numFrames, v.position);
        if (!loops && (double)(numFrames - 1) - v.position < fadeSpan) {
            const float fade = (float)(((double)(numFrames - 1) - v.position) / fadeSpan);
            l *= fade;
            r *= fade;
        }
        if (filtered) {
            v.lowpassL += coef * (l - v.lowpassL);
            v.lowpassR += coef * (r - v.lowpassR);
            l = v.lowpassL;
            r = v.lowpassR;
        }
        const float g = v.env * v.gain;
        left[i] += l * g;
        right[i] += r * g;
        if (v.watch.quietAfter(std::max(std::fabs(l), std::fabs(r)) * g))
            return false;
        v.position += inc;
    }
    stepGlide(v, end - start - glided);
    return true;
}

void VoicePool::render(float* left, float* right, int start, int end, const VoiceParams& p) {
    if (instrument_ == nullptr)
        return;
    lastReleaseSeconds_ = p.releaseSeconds;
    const BlockContext ctx{instrument_, sampleRate_, p.pitchSemis, juce::jlimit(0.0f, 1.0f, p.tone),
                           juce::jlimit(0.0f, 1.0f, p.feel)};
    for (auto& v : voices_) {
        if (!v.active)
            continue;
        if (!renderVoice(v, ctx, left, right, start, end)) {
            v.active = false;
            v.pedalHeld = v.keyDown = false;
        }
    }
}

} // namespace synth::sampler
