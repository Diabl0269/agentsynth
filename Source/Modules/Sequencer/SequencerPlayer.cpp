#include "SequencerPlayer.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {

constexpr int kChannel = 1;
constexpr int kFilterCc = 74;
constexpr int kMaxStepsPerSegment = 4096;
constexpr double kBoundaryTolerance = 1.0e-3;
// A legato note may ring this many steps past its own slot, waiting for the next played note.
constexpr double kLegatoGraceSteps = 0.5;

bool stepPlays(const SequencerStep& step, std::int64_t k, int steps, const SequencerPattern& pattern,
               const SequencerSettings& settings) noexcept {
    if (!step.on)
        return false;
    const std::int64_t loop = k / steps;
    if ((loop % std::max<int>(1, step.condOf)) != (step.condHit - 1))
        return false;
    const double threshold = (double)step.chance * (double)settings.chanceAmount;
    const auto roll = (double)(sequencerHash(pattern.seed, (std::uint64_t)k) % 100);
    return roll < threshold;
}

int velocityOf(const SequencerStep& step, const SequencerSettings& settings) noexcept {
    return std::clamp((int)std::lround((double)step.velocity * (double)settings.velocityAmount), 1, 127);
}

} // namespace

void SequencerPlayer::reset() noexcept {
    pendingCount_ = 0;
    hitCount_ = 0;
    lastHitKey_ = -1;
    activeStep_ = 0;
}

void SequencerPlayer::flush(juce::MidiBuffer& midi) {
    for (int i = 0; i < pendingCount_; ++i)
        midi.addEvent(juce::MidiMessage::noteOff(kChannel, held_[(size_t)i].note), 0);
    pendingCount_ = 0;
    lastHitKey_ = -1;
}

void SequencerPlayer::removeHeld(int index) noexcept {
    for (int i = index; i + 1 < pendingCount_; ++i)
        held_[(size_t)i] = held_[(size_t)i + 1];
    --pendingCount_;
}

void SequencerPlayer::hold(int note, int remaining, bool legato, int offset, juce::MidiBuffer& midi) {
    if (pendingCount_ >= kMaxHeld) {
        // Full: the oldest note ends now rather than the list growing.
        midi.addEvent(juce::MidiMessage::noteOff(kChannel, held_[0].note), offset);
        removeHeld(0);
    }
    held_[(size_t)pendingCount_++] = {note, remaining, legato};
}

void SequencerPlayer::emitDue(int upToOffset, juce::MidiBuffer& midi) {
    for (int i = 0; i < pendingCount_;) {
        if (held_[(size_t)i].remaining <= upToOffset) {
            midi.addEvent(juce::MidiMessage::noteOff(kChannel, held_[(size_t)i].note),
                          std::max(0, held_[(size_t)i].remaining));
            removeHeld(i);
        } else {
            ++i;
        }
    }
}

void SequencerPlayer::endBlock(int numSamples, juce::MidiBuffer& midi) {
    emitDue(numSamples - 1, midi);
    for (int i = 0; i < pendingCount_; ++i)
        held_[(size_t)i].remaining -= numSamples;
}

void SequencerPlayer::collectHits(const SequencerSegment& segment, const SequencerPattern& pattern, int steps,
                                  int numSamples) {
    if (segment.discontinuity)
        lastHitKey_ = -1;

    const auto first = std::max<std::int64_t>(0, (std::int64_t)std::ceil(segment.uStart - 1.5));
    const auto last = std::min<std::int64_t>((std::int64_t)std::floor(segment.uEnd + 0.5), first + kMaxStepsPerSegment);
    for (std::int64_t k = first; k <= last; ++k) {
        const auto& step = pattern.steps[(size_t)(k % steps)];
        const int repeats = std::clamp((int)step.repeat, 1, 8);
        for (int j = 0; j < repeats; ++j) {
            const double unit = (double)k + (double)step.nudge + (double)j / (double)repeats;
            // The first sample at or after the hit, within a hair so float error in a tempo or rate cannot push an
            // exact boundary a sample late. Each hit belongs to exactly one block: the one whose samples hold it.
            int local = (int)std::ceil((unit - segment.uStart) * segment.samplesPerUnit - kBoundaryTolerance);
            // An early nudge on a step that sits at or after the start of a restarted clock (the first loop, a
            // locate, a loop wrap) would fall before the start and never play: it plays at the start instead.
            if (local < 0 && segment.discontinuity && (double)k >= segment.uStart - kBoundaryTolerance)
                local = 0;
            if (local < 0 || local >= segment.sampleCount)
                continue;
            const std::int64_t key = k * 8 + j;
            if (key == lastHitKey_ || hitCount_ >= kMaxHits)
                continue;
            hits_[(size_t)hitCount_++] = {std::clamp(segment.sampleOffset + local, 0, numSamples - 1), key, k, j};
            lastHitKey_ = key;
        }
    }
}

void SequencerPlayer::playHit(const Hit& hit, const SequencerPattern& pattern, const SequencerScaleSnap& snap,
                              const SequencerSettings& settings, double samplesPerUnit, juce::MidiBuffer& midi) {
    const auto& step = pattern.steps[(size_t)(hit.step % settings.steps)];
    if (hit.index == 0)
        activeStep_ = (int)(hit.step % settings.steps);
    if (!stepPlays(step, hit.step, settings.steps, pattern, settings))
        return;

    const int offset = hit.offset;
    const int repeats = std::clamp((int)step.repeat, 1, 8);
    const int root = snap.snap(std::clamp((int)step.pitch + settings.transpose, 0, 127));
    SequencerChordNotes chord;
    if (settings.poly)
        chord = sequencerChordNotes(step.chord, root, snap.scale(), pattern.seed, hit.step);
    else {
        chord.notes[0] = root;
        chord.count = 1;
    }

    // The previous notes end where this one starts, except legato ones, which end just after it.
    for (int i = 0; i < pendingCount_;) {
        if (held_[(size_t)i].legato) {
            ++i;
            continue;
        }
        midi.addEvent(juce::MidiMessage::noteOff(kChannel, held_[(size_t)i].note), offset);
        removeHeld(i);
    }

    if (!settings.poly)
        midi.addEvent(juce::MidiMessage::controllerEvent(kChannel, kFilterCc,
                                                         std::clamp((int)std::lround(step.filter * 127.0f), 0, 127)),
                      offset);
    const auto velocity = (juce::uint8)velocityOf(step, settings);
    for (int i = 0; i < chord.count; ++i)
        midi.addEvent(juce::MidiMessage::noteOn(kChannel, chord.notes[(size_t)i], velocity), offset);

    for (int i = 0; i < pendingCount_;) {
        bool retriggered = false;
        for (int n = 0; n < chord.count; ++n)
            retriggered = retriggered || chord.notes[(size_t)n] == held_[(size_t)i].note;
        if (!retriggered)
            midi.addEvent(juce::MidiMessage::noteOff(kChannel, held_[(size_t)i].note), offset);
        removeHeld(i);
    }

    const double slot = samplesPerUnit / (double)repeats;
    const int length = std::max(1, (int)std::lround(slot * (double)step.gate * (double)settings.gateAmount));
    const bool legato = step.slide && !settings.poly && hit.index == repeats - 1;
    const int legatoLength = (int)std::lround(slot + kLegatoGraceSteps * samplesPerUnit);
    for (int i = 0; i < chord.count; ++i)
        hold(chord.notes[(size_t)i], offset + (legato ? legatoLength : length), legato, offset, midi);
}

void SequencerPlayer::renderBlock(const SequencerClockBlock& block, const SequencerPattern& pattern,
                                  const SequencerScaleSnap& snap, const SequencerSettings& settings,
                                  juce::MidiBuffer& midi, int numSamples) {
    hitCount_ = 0;
    const int steps = std::clamp(settings.steps, 1, kMaxSequencerSteps);
    SequencerSettings safe = settings;
    safe.steps = steps;
    for (int i = 0; i < block.count; ++i)
        collectHits(block.segments[(size_t)i], pattern, steps, numSamples);

    std::sort(hits_.begin(), hits_.begin() + hitCount_,
              [](const Hit& a, const Hit& b) { return a.offset != b.offset ? a.offset < b.offset : a.key < b.key; });

    const double samplesPerUnit = block.count > 0 ? block.segments[0].samplesPerUnit : 1.0;
    for (int i = 0; i < hitCount_; ++i) {
        emitDue(hits_[(size_t)i].offset, midi);
        playHit(hits_[(size_t)i], pattern, snap, safe, samplesPerUnit, midi);
    }
    endBlock(numSamples, midi);
}

} // namespace synth
