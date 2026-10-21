#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

// The drum machine's cell arithmetic, free of any component: a cell is one step of one row, and a note belongs to the
// cell containing its start beat. docs/timeline/drum-machine.md.
namespace synth::ui::drumgrid {

constexpr int kNewHitVelocity = 100;
constexpr int kAccentVelocity = 127;
constexpr int kAccentThreshold = 120;
constexpr double kBeatEpsilon = 1.0e-6;

inline bool isAccent(int velocity) noexcept { return velocity >= kAccentThreshold; }

inline double stepBeats(int stepsPerBar, double beatsPerBar) noexcept {
    return beatsPerBar / static_cast<double>(std::max(1, stepsPerBar));
}

/** How many cells a clip of `clipLengthBeats` spans (a partial last step counts); at least one. */
inline int stepCount(double clipLengthBeats, double stepLengthBeats) noexcept {
    return std::max(1, static_cast<int>(std::ceil(clipLengthBeats / stepLengthBeats - kBeatEpsilon)));
}

/** The cell holding a note that starts at the clip-relative `startBeat` (floor). */
inline int cellOf(double startBeat, double stepLengthBeats) noexcept {
    return static_cast<int>(std::floor(startBeat / stepLengthBeats + kBeatEpsilon));
}

/** The notes of `pitch` that start in cell `step`. */
struct CellHits {
    std::vector<synth::NoteId> ids;
    int velocity = 0;   ///< the loudest of them; 0 when the cell is empty
    bool muted = false; ///< every one of them carries the note-level mute from the piano roll

    bool empty() const noexcept { return ids.empty(); }
    bool accent() const noexcept { return isAccent(velocity); }
};

inline void addNoteToHits(CellHits& hits, const synth::MidiNote& note) {
    hits.muted = hits.ids.empty() ? note.muted : hits.muted && note.muted;
    hits.ids.push_back(note.id);
    hits.velocity = std::max(hits.velocity, note.velocity);
}

inline CellHits hitsInCell(const synth::Clip& clip, int pitch, int step, double stepLengthBeats) {
    CellHits hits;
    for (const auto& note : clip.notes)
        if (note.pitch == pitch && cellOf(note.startBeat, stepLengthBeats) == step)
            addNoteToHits(hits, note);
    return hits;
}

using CellKey = long long;
inline CellKey cellKey(int pitch, int step) noexcept { return static_cast<CellKey>(pitch) * 1000000 + step; }

/** Every occupied cell of the clip at once, so a paint never rescans the notes per cell. */
inline std::unordered_map<CellKey, CellHits> buildCellIndex(const synth::Clip& clip, double stepLengthBeats) {
    std::unordered_map<CellKey, CellHits> index;
    for (const auto& note : clip.notes)
        addNoteToHits(index[cellKey(note.pitch, cellOf(note.startBeat, stepLengthBeats))], note);
    return index;
}

} // namespace synth::ui::drumgrid
