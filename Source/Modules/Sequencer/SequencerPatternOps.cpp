#include "SequencerPatternOps.h"
#include <algorithm>

namespace synth::sequencer_ops {

namespace {

int activeSteps(int n) { return std::clamp(n, 1, kMaxSequencerSteps); }

SequencerPattern rotated(const SequencerPattern& pattern, int n, int placesLeft) {
    const int count = activeSteps(n);
    SequencerPattern out = pattern;
    const int shift = ((placesLeft % count) + count) % count;
    for (int i = 0; i < count; ++i)
        out.steps[(size_t)i] = pattern.steps[(size_t)((i + shift) % count)];
    return out;
}

} // namespace

SequencerPattern shiftLeft(const SequencerPattern& pattern, int n, int places) { return rotated(pattern, n, places); }

SequencerPattern shiftRight(const SequencerPattern& pattern, int n, int places) { return rotated(pattern, n, -places); }

SequencerPattern reversed(const SequencerPattern& pattern, int n) {
    const int count = activeSteps(n);
    SequencerPattern out = pattern;
    for (int i = 0; i < count; ++i)
        out.steps[(size_t)i] = pattern.steps[(size_t)(count - 1 - i)];
    return out;
}

bool canDouble(int n) { return activeSteps(n) * 2 <= kMaxSequencerSteps; }

SequencerPattern doubled(const SequencerPattern& pattern, int n) {
    const int count = activeSteps(n);
    if (!canDouble(count))
        return pattern;
    SequencerPattern out = pattern;
    for (int i = 0; i < count; ++i)
        out.steps[(size_t)(count + i)] = pattern.steps[(size_t)i];
    return out;
}

SequencerPattern movedByScaleStep(const SequencerPattern& pattern, int n, const MusicalScale& scale, int direction) {
    const int count = activeSteps(n);
    const int delta = direction >= 0 ? 1 : -1;
    SequencerPattern out = pattern;
    for (int i = 0; i < count; ++i) {
        auto& step = out.steps[(size_t)i];
        step.pitch = (std::uint8_t)std::clamp(scale.stepBy(step.pitch, delta), 0, 127);
    }
    return out;
}

SequencerPattern fitToScale(const SequencerPattern& pattern, int n, const MusicalScale& scale) {
    const int count = activeSteps(n);
    SequencerPattern out = pattern;
    for (int i = 0; i < count; ++i) {
        auto& step = out.steps[(size_t)i];
        step.pitch = (std::uint8_t)std::clamp(scale.snapPitch(step.pitch), 0, 127);
    }
    return out;
}

bool fitToScaleWouldChange(const SequencerPattern& pattern, int n, const MusicalScale& scale) {
    if (scale.isChromatic())
        return false;
    const int count = activeSteps(n);
    for (int i = 0; i < count; ++i) {
        const int pitch = pattern.steps[(size_t)i].pitch;
        if (scale.snapPitch(pitch) != pitch)
            return true;
    }
    return false;
}

SequencerPattern randomized(const SequencerPattern& pattern, int n, const MusicalScale& scale,
                            const RandomizeSettings& settings, juce::Random& rng) {
    const int count = activeSteps(n);
    SequencerPattern out = pattern;

    const auto pitches = pickPitches(count, settings.minPitch, settings.maxPitch, &scale, rng);
    if (!pitches.empty())
        for (int i = 0; i < count; ++i)
            out.steps[(size_t)i].pitch = (std::uint8_t)pitches[(size_t)i];

    if (settings.keepRhythm)
        return out;

    const float density = std::clamp(settings.density, 0.0f, 1.0f);
    bool anyOn = false;
    for (int i = 0; i < count; ++i) {
        const bool on = rng.nextFloat() < density;
        out.steps[(size_t)i].on = on;
        anyOn = anyOn || on;
    }
    if (!anyOn)
        out.steps[(size_t)rng.nextInt(count)].on = true;
    return out;
}

} // namespace synth::sequencer_ops
