// CustomFilterVoiceCursors.cpp -- note spotting, shape choice and glide for the per-note modes.
#include "CustomFilterVoiceCursors.h"
#include <cmath>

namespace synth {

namespace {
constexpr float kStableSemitones = 0.1f;  // a block whose pitch moves less than this is a held note
constexpr float kNewNoteSemitones = 0.5f; // a held pitch this far from the note's is a new note

float semitonesBetween(float a, float b) noexcept { return 12.0f * std::log2(a / b); }
} // namespace

void CustomFilterVoiceCursors::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

/** The random sequence restarts too, so two renders of the same notes pick the same shapes. */
void CustomFilterVoiceCursors::reset() noexcept {
    voices_ = {};
    random_.setSeed(0x5eed);
}

int CustomFilterVoiceCursors::noteFromHz(float hz) noexcept {
    return juce::jlimit(0, CustomFilterPerNote::kNumKeys - 1, (int)std::lround(69.0f + 12.0f * std::log2(hz / 440.0f)));
}

/** Equal bands of kByPitchLowNote..kByPitchHighNote, lowest band on the leftmost shape. */
int CustomFilterVoiceCursors::shapeByPitch(int note, int count, const int* orderByX) noexcept {
    if (count <= 0)
        return -1;
    constexpr int lo = CustomFilterPerNote::kByPitchLowNote, hi = CustomFilterPerNote::kByPitchHighNote;
    const int clamped = juce::jlimit(lo, hi, note);
    const int band = juce::jmin(count - 1, (clamped - lo) * count / (hi - lo + 1));
    return orderByX != nullptr ? orderByX[band] : band;
}

/** The Filter sees no gate and no MIDI, only each voice's Pitch (Hz) and audio. A note starts when:
    - the pitch settles (moves less than kStableSemitones across the block) more than kNewNoteSemitones away
      from the note's pitch, or
    - the audio comes back after a silent block; this waits for the pitch to settle when there is one, so a
      Poly MIDI note (whose pitch glides for 5 ms while its sound starts) counts once, with its final pitch.
    A repeat of the same note on the same voice into a filter whose input never falls silent is not seen. */
bool CustomFilterVoiceCursors::detectNote(Voice& v, const VoiceInput& in, int& note) noexcept {
    if (in.audible && !v.wasAudible)
        v.pendingOnset = true;
    v.wasAudible = in.audible;

    const bool hasPitch = in.pitchStartHz > 0.0f && in.pitchEndHz > 0.0f;
    const bool stable = hasPitch && std::abs(semitonesBetween(in.pitchEndHz, in.pitchStartHz)) < kStableSemitones;
    const bool moved =
        stable && (v.noteHz <= 0.0f || std::abs(semitonesBetween(in.pitchEndHz, v.noteHz)) > kNewNoteSemitones);
    const bool fire = moved || (v.pendingOnset && (stable || !hasPitch));
    if (!fire)
        return false;
    v.pendingOnset = false;
    if (stable)
        v.noteHz = in.pitchEndHz;
    note = stable ? noteFromHz(in.pitchEndHz) : -1;
    return true;
}

/** Random never repeats the voice's previous shape when there is another to go to, so every new note is heard
    to move. */
int CustomFilterVoiceCursors::pickShape(const Voice& v, int note, const Settings& settings,
                                        const ShapeMap& map) noexcept {
    using Mode = CustomFilterPerNote::Mode;
    switch (settings.mode) {
    case Mode::ByHand: {
        if (note < 0 || settings.keys == nullptr)
            return -1;
        const int shape = settings.keys[note];
        return shape >= 0 && shape < map.count ? shape : -1;
    }
    case Mode::ByPitch:
        return note < 0 ? -1 : shapeByPitch(note, map.count, map.orderByX);
    case Mode::Random: {
        if (map.count <= 1)
            return map.count - 1;
        if (v.shape < 0 || v.shape >= map.count)
            return random_.nextInt(map.count);
        const int pick = random_.nextInt(map.count - 1);
        return pick >= v.shape ? pick + 1 : pick;
    }
    case Mode::Off:
    default:
        return -1;
    }
}

/** A note glides linearly in time from where the voice's cursor was to its shape: progress = time since the note
    started / (smoothness x kMaxGlideSeconds), counted at the block's end so smoothness 0 lands in the note's first
    block. The target is read every block, so dragging the shape (or Morph X/Y, for a note with no shape) moves a
    held note with it. A shape index that no longer exists (the shape was deleted) aims at the shared cursor. */
void CustomFilterVoiceCursors::update(const VoiceInput* in, int numVoices, int numSamples, const Settings& settings,
                                      const ShapeMap& map, float sharedX, float sharedY) noexcept {
    const double glideSamples =
        (double)juce::jlimit(0.0f, 1.0f, settings.smoothness) * CustomFilterPerNote::kMaxGlideSeconds * sampleRate_;
    for (int i = 0; i < juce::jmin(numVoices, kMaxVoices); ++i) {
        Voice& v = voices_[(size_t)i];
        v.noteStarted = false;
        int note = -1;
        if (detectNote(v, in[i], note)) {
            if (!v.engaged) {
                v.x = sharedX;
                v.y = sharedY;
            }
            v.shape = pickShape(v, note, settings, map);
            v.startX = v.x;
            v.startY = v.y;
            v.elapsed = 0.0;
            v.engaged = true;
            v.noteStarted = true;
        }
        if (!v.engaged) {
            v.x = sharedX;
            v.y = sharedY;
            continue;
        }
        v.elapsed += numSamples;
        const bool onShape = v.shape >= 0 && v.shape < map.count;
        const float tx = onShape ? map.x[v.shape] : sharedX;
        const float ty = onShape ? map.y[v.shape] : sharedY;
        const float t = glideSamples <= 0.0 ? 1.0f : (float)juce::jmin(1.0, v.elapsed / glideSamples);
        v.x = v.startX + (tx - v.startX) * t;
        v.y = v.startY + (ty - v.startY) * t;
    }
}

} // namespace synth
