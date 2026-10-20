#pragma once

// Random MidiNote generation constrained to a scale, for the piano roll's scale assist. A thin
// wrapper over Music/MusicalScale.h's pickPitches: this file owns the Timeline dependency
// (MidiNote's field layout) so Source/Music/ never includes Timeline. Callers stay responsible for
// how generated notes get inserted (through the normal mutation API + undo, same as any other note
// edit).

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <vector>

namespace synth {

// Generates one note per grid step, starting at beat 0, for as long as the step's start still
// falls (with a small epsilon) before clipLengthBeats. Pitch is chosen uniformly among the
// in-scale pitches inside [minPitch, maxPitch] (every pitch in range when scale is null or
// chromatic); if no candidate pitch exists in range at all, returns empty rather than falling
// back to an out-of-range or out-of-scale note. RNG is caller-owned and caller-seeded so this
// function is deterministic and independently testable.
inline std::vector<synth::MidiNote> generateRandomNotes(double clipLengthBeats, double gridBeats, int minPitch,
                                                        int maxPitch, const MusicalScale* scale, juce::Random& rng) {
    std::vector<synth::MidiNote> notes;
    if (gridBeats <= 0.0 || clipLengthBeats <= 0.0)
        return notes;

    constexpr double eps = 1e-9;
    int numSteps = 0;
    for (double start = 0.0; start + eps < clipLengthBeats; start += gridBeats)
        ++numSteps;

    const std::vector<int> pitches = pickPitches(numSteps, minPitch, maxPitch, scale, rng);
    if (pitches.empty())
        return notes;

    size_t index = 0;
    for (double start = 0.0; start + eps < clipLengthBeats; start += gridBeats) {
        double remaining = clipLengthBeats - start;
        double length = std::min(gridBeats, remaining);

        synth::MidiNote note;
        note.startBeat = start;
        note.lengthBeats = length;
        note.pitch = pitches[index++];
        note.velocity = 100;
        note.channel = 1;
        note.muted = false;
        notes.push_back(note);
    }
    return notes;
}

} // namespace synth
