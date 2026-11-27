#include "Timeline/Melody/MelodyQuantize.h"

#include "Music/SongKey.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace synth::melody {

namespace {

int nearestPitch(float midi) { return std::clamp((int)std::lround((double)midi), 0, 127); }

// Loudness -45..-12 dBFS onto velocity 60..120.
int velocityFor(float rmsDb) {
    const double t = std::clamp(((double)rmsDb + 45.0) / 33.0, 0.0, 1.0);
    return (int)std::lround(60.0 + t * 60.0);
}

double snapToGrid(double beat, double grid) { return std::round(beat / grid) * grid; }

std::vector<MelodyNote> asSung(const std::vector<CutNote>& notes, const MelodyOptions& options) {
    std::vector<MelodyNote> result;
    for (const auto& note : notes) {
        const double start = std::max(0.0, note.startSec / options.secondsPerBeat);
        const double end = std::min(options.lengthBeats, note.endSec / options.secondsPerBeat);
        if (end - start <= 1.0e-6)
            continue;
        result.push_back({start, end - start, nearestPitch(note.midi), velocityFor(note.rmsDb)});
    }
    return result;
}

// Starts and ends on the grid, at least one step long, one voice: a note that runs into the next one's start is
// trimmed to it, and one left with no length (two starts snapped together) is dropped.
std::vector<MelodyNote> snapped(const std::vector<CutNote>& notes, const MelodyOptions& options,
                                const MusicalScale& key) {
    const double grid = options.gridBeats > 0.0 ? options.gridBeats : 0.25;
    std::vector<MelodyNote> result;
    for (const auto& note : notes) {
        const double start = snapToGrid(note.startSec / options.secondsPerBeat, grid);
        double end = snapToGrid(note.endSec / options.secondsPerBeat, grid);
        end = std::max(end, start + grid);
        result.push_back({std::max(0.0, start), end - std::max(0.0, start), key.snapPitch(nearestPitch(note.midi)),
                          velocityFor(note.rmsDb)});
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const MelodyNote& a, const MelodyNote& b) { return a.startBeat < b.startBeat; });
    std::vector<MelodyNote> voice;
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto note = result[i];
        if (i + 1 < result.size())
            note.lengthBeats = std::min(note.lengthBeats, result[i + 1].startBeat - note.startBeat);
        note.lengthBeats = std::min(note.lengthBeats, options.lengthBeats - note.startBeat);
        if (note.lengthBeats > 1.0e-6)
            voice.push_back(note);
    }
    return voice;
}

} // namespace

MusicalScale detectHumKey(const std::vector<CutNote>& notes) {
    std::array<double, 12> weights{};
    for (const auto& note : notes)
        weights[(size_t)(nearestPitch(note.midi) % 12)] += std::max(0.0, note.endSec - note.startSec);
    return detectKey(weights).value_or(MusicalScale{});
}

std::vector<MelodyNote> notesToMelody(const std::vector<CutNote>& notes, const MelodyOptions& options,
                                      MusicalScale* usedKey) {
    if (options.mode == MelodyMode::AsSung)
        return asSung(notes, options);
    const MusicalScale key = options.key.has_value() ? *options.key : detectHumKey(notes);
    if (usedKey != nullptr)
        *usedKey = key;
    return snapped(notes, options, key);
}

} // namespace synth::melody
