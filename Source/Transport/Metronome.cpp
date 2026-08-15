#include "Metronome.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {

// Epsilon-tolerant integer-beat scan: a beat position derived from an accumulated sample count can
// land a hair off an exact integer (see BlockTimeInfo's own beatFromSample conversions), so both the
// "is this the start of the scan" and "is this a downbeat" checks tolerate a tiny slop rather than
// silently skipping (or double-firing) a beat that was meant to be exact.
constexpr double kBeatEpsilon = 1.0e-7;

// A beat is always a quarter note regardless of the time signature's notated denominator — same
// convention TimelineTransportBar.cpp / TransportService::getPosition() use, duplicated rather than
// shared for the same reason noted there (a one-line formula, audio-thread-facing here vs. message-
// thread-facing there).
double beatsPerBarFrom(int numerator, int denominator) noexcept {
    const double v = (double)numerator * 4.0 / (double)std::max(1, denominator);
    return v > 0.0 ? v : 4.0;
}

// The smallest integer >= start, tolerant of start already being (within epsilon of) an integer —
// so a range beginning exactly on a beat includes that beat rather than skipping to the next one.
double firstIntegerBeatAtOrAfter(double start) noexcept { return std::ceil(start - kBeatEpsilon); }

bool isDownbeat(double beat, double beatsPerBar) noexcept {
    if (!(beatsPerBar > 0.0))
        return false;
    double remainder = std::fmod(beat, beatsPerBar);
    if (remainder < 0.0)
        remainder += beatsPerBar;
    return remainder < kBeatEpsilon || (beatsPerBar - remainder) < kBeatEpsilon;
}

// Beat -> block-relative sample offset, clamped into [baseOffset, lastSample]. Mirrors
// TimelineMidiSourceModule::beatToOffset exactly (same clamp, same reasoning: a locate plus a tempo
// change can make the raw beat distance astronomically large before llround ever sees it).
int beatToOffset(double beat, double rangeStart, double beatsPerSample, int baseOffset, int lastSample) noexcept {
    const double rel = juce::jlimit(-1.0e9, 1.0e9, (beat - rangeStart) / beatsPerSample);
    const std::int64_t offset = (std::int64_t)baseOffset + std::llround(rel);
    return (int)juce::jlimit<std::int64_t>(baseOffset, juce::jmax(baseOffset, lastSample), offset);
}

} // namespace

void Metronome::renderVoiceInto(juce::AudioBuffer<float>& buffer, Voice& voice, int fromSample,
                                int numSamplesInBlock) noexcept {
    if (voice.samplesRemaining <= 0 || fromSample >= numSamplesInBlock)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numToRender = std::min(voice.samplesRemaining, numSamplesInBlock - fromSample);

    for (int i = 0; i < numToRender; ++i) {
        const float sample = (float)(voice.amp * std::sin(voice.phase));
        const int index = fromSample + i;
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.getWritePointer(channel)[index] += sample;

        voice.phase += voice.phaseInc;
        if (voice.phase > juce::MathConstants<double>::twoPi)
            voice.phase -= juce::MathConstants<double>::twoPi;
        voice.amp *= voice.decayMul;
        --voice.samplesRemaining;
    }
}

Metronome::Voice& Metronome::allocateVoice() noexcept {
    int bestIndex = 0;
    int bestRemaining = voices_[0].samplesRemaining;
    for (int i = 0; i < kNumVoices; ++i) {
        if (voices_[i].samplesRemaining <= 0)
            return voices_[i]; // a free slot beats stealing anything
        if (voices_[i].samplesRemaining < bestRemaining) {
            bestRemaining = voices_[i].samplesRemaining;
            bestIndex = i;
        }
    }
    return voices_[bestIndex];
}

void Metronome::startClick(juce::AudioBuffer<float>& buffer, int offset, int numSamplesInBlock, bool downbeat,
                           double sampleRate) noexcept {
    if (!(sampleRate > 0.0))
        return;

    Voice& voice = allocateVoice();
    const double frequency = downbeat ? kDownbeatFrequencyHz : kOtherBeatFrequencyHz;
    const int decaySamples = juce::jmax(1, (int)std::lround(kDecaySeconds * sampleRate));

    voice.phase = 0.0;
    voice.phaseInc = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    voice.amp = downbeat ? kDownbeatAmplitude : kOtherBeatAmplitude;
    // amp * decayMul^decaySamples == amp * 10^(kDecayFloorDb/20) -> decayMul = 10^(kDecayFloorDb / 20 / decaySamples).
    voice.decayMul = (float)std::pow(10.0, kDecayFloorDb / 20.0 / (double)decaySamples);
    voice.samplesRemaining = decaySamples;

    renderVoiceInto(buffer, voice, offset, numSamplesInBlock);
}

void Metronome::scanRange(juce::AudioBuffer<float>& buffer, double rangeStart, double rangeEnd, int baseOffset,
                          double beatsPerSample, double beatsPerBar, int lastSample, int numSamplesInBlock,
                          double sampleRate) noexcept {
    if (!(rangeEnd > rangeStart))
        return;

    for (double beat = firstIntegerBeatAtOrAfter(rangeStart); beat < rangeEnd - kBeatEpsilon; beat += 1.0) {
        const int offset = beatToOffset(beat, rangeStart, beatsPerSample, baseOffset, lastSample);
        startClick(buffer, offset, numSamplesInBlock, isDownbeat(beat, beatsPerBar), sampleRate);
    }
}

void Metronome::renderClicks(juce::AudioBuffer<float>& buffer, const BlockTimeInfo& info) noexcept {
    const bool active = enabled_.load(std::memory_order_relaxed) || forcedOn_.load(std::memory_order_relaxed);
    if (!active) {
        // Fully disabled: touch nothing, including voice state, so a stale ringing voice never
        // resumes with a pop when the metronome is re-enabled later (see DisabledMetronomeIsSilentAndFree).
        for (auto& voice : voices_)
            voice.samplesRemaining = 0;
        return;
    }

    const int numSamples = info.numSamples;
    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    // Continue whatever was already ringing from the previous call, from sample 0 — this is what
    // lets a click span a block boundary with no discontinuity.
    for (auto& voice : voices_)
        renderVoiceInto(buffer, voice, 0, numSamples);

    // New crossings only while playing: a paused transport crosses no beats, but the voices above
    // still finish naturally regardless of the playing flag.
    if (!info.playing)
        return;

    const double beatsPerSample = info.beatsPerSample();
    if (!(beatsPerSample > 0.0))
        return;

    const double beatsPerBar = beatsPerBarFrom(info.timeSigNumerator, info.timeSigDenominator);
    const int lastSample = juce::jmax(0, numSamples - 1);
    const bool wraps = info.loopWrapSample >= 0;

    // Primary range, capped at the loop end when this block wraps — mirrors
    // TimelineMidiSourceModule::emitBlock exactly (info.endPpq is the UNWRAPPED virtual end and
    // would emit beats the transport never actually reaches).
    scanRange(buffer, info.startPpq, wraps ? info.loopEndPpq : info.endPpq, /*baseOffset=*/0, beatsPerSample,
              beatsPerBar, lastSample, numSamples, info.sampleRate);

    if (!wraps)
        return;

    const int wrapOffset = juce::jlimit(0, lastSample, info.loopWrapSample);
    // The wrapped range's end, derived the same way TimelineMidiSourceModule derives it (samples
    // remaining after the wrap, converted to beats) — BlockTimeInfo reports only the FIRST wrap in a
    // block, so a loop shorter than the remainder degrades to "some repeats missing", never a leak.
    const double wrappedRangeEnd = info.loopStartPpq + (double)(numSamples - wrapOffset) * beatsPerSample;
    scanRange(buffer, info.loopStartPpq, wrappedRangeEnd, wrapOffset, beatsPerSample, beatsPerBar, lastSample,
              numSamples, info.sampleRate);
}

} // namespace synth
