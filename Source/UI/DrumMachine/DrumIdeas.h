#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

// The drum machine's ideas, as plain functions over a clip's notes: a starter pattern per style, three variations of
// what is there, and a randomized pattern. No component, no document, no clock: the same seed gives the same result,
// so every rule is tested without a window. docs/timeline/drum-machine.md#ideas.
namespace synth::ui::drumideas {

enum class Style { House, Techno, HipHop, Trap, DrumAndBass, Breakbeat, Rock, Funk, Disco, Reggaeton, Afrobeat, LoFi };
constexpr int kStyleCount = 12;

juce::String styleName(Style style);
Style styleAt(int index);

/** The clip being worked on: its length, the bar length and the grid's step, all in beats. */
struct Grid {
    double lengthBeats = 4.0;
    double beatsPerBar = 4.0;
    double stepBeats = 0.25;
    int stepsPerBar() const noexcept;
};

/** The grid resolution a style needs: 16 steps per bar, 32 for Trap's hat rolls. */
int requiredStepsPerBar(Style style) noexcept;

/** The style's one-bar pattern repeated across the clip (a partial last bar is cut). Sorted by start. */
std::vector<synth::MidiNote> starter(Style style, const Grid& grid);

/** A cell that a result changed: a drum (pitch) and a step of the grid. */
using ChangedCell = std::pair<int, int>;

struct Variation {
    std::vector<synth::MidiNote> notes;
    std::vector<ChangedCell> changed;
};

/**
 * One variation of `notes`: ghost hits on the snare and hats move, appear or go, the kick gets pushes, a closed hat
 * swaps for an open one, and some velocities are humanised. `amount` (0 subtle, 1 wild) scales how many changes are
 * made. A kick on a bar's first beat and the snare backbeats stay put unless amount is Wild (0.95 and up).
 */
Variation vary(const std::vector<synth::MidiNote>& notes, const Grid& grid, double amount, int seed);

/**
 * A new pattern. `density` (0 sparse, 1 busy) scales how many hits there are. With `keepKickAndSnare` the existing
 * kick (35, 36) and snare (38, 40) notes are kept as they are and only the other drums are generated.
 */
Variation randomize(const std::vector<synth::MidiNote>& notes, const Grid& grid, double density, bool keepKickAndSnare,
                    int seed);

/** The cells whose hits differ between two note lists (a pitch and a step of `grid`), for the changed-cell outline. */
std::vector<ChangedCell> changedCells(const std::vector<synth::MidiNote>& before,
                                      const std::vector<synth::MidiNote>& after, double stepBeats);

bool isKick(int pitch) noexcept;
bool isSnare(int pitch) noexcept;

} // namespace synth::ui::drumideas
