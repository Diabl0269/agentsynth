#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <limits>
#include <vector>

namespace synth::ui {

// "Sections": the blocks of the song where an LFO modulator is on. They are stored as an ordinary
// automation lane on the LFO's own `level` parameter, so the engine needs no new concept -- the
// automation applier plays the lane and the LFO's output is silent wherever the level is 0. This file
// is the whole algebra between the two shapes: a lane's Hold breakpoints and a sorted list of blocks.
// No GUI in here, so the merge/split rules are testable without a component.
//
// The lane is a pure function of its blocks: (beat 0, 0, Hold), then (start, 1, Hold) and (end, 0, Hold)
// per block. A modulator with no such lane is on everywhere, which is what it was before sections.

/** The LFO parameter the sections lane drives. */
inline const juce::String kSectionsParamId = "level";

/** One stretch where the modulator is on, in beats; `end` is infinity for a lane left on to the end. */
struct SectionBlock {
    double start = 0.0;
    double end = 0.0;
    bool operator==(const SectionBlock& other) const noexcept { return start == other.start && end == other.end; }
};
using SectionBlocks = std::vector<SectionBlock>;

inline constexpr double kOpenEnd = std::numeric_limits<double>::infinity();

/** Sorted, with empty blocks dropped and overlapping or touching ones merged into one. */
SectionBlocks normalisedSections(SectionBlocks blocks);

/** Reads a sections lane back: a point at or above half level opens a block, one below closes it. */
SectionBlocks sectionsFromPoints(const std::vector<synth::AutomationLane::Breakpoint>& points);

/** The breakpoints that play `blocks` (normalised first). Empty for no blocks. */
std::vector<synth::AutomationLane::Breakpoint> pointsFromSections(const SectionBlocks& blocks);

/** `blocks` with [lo, hi) turned on / off. */
SectionBlocks paintedSpan(const SectionBlocks& blocks, double lo, double hi);
SectionBlocks erasedSpan(const SectionBlocks& blocks, double lo, double hi);

/** Block `index` with one edge dragged to `beat`; the edge never crosses the other one (`minLength` apart). */
SectionBlocks resizedBlock(const SectionBlocks& blocks, int index, bool startEdge, double beat, double minLength);
/** Block `index` moved so it starts at `newStart` (not before beat 0), keeping its length. */
SectionBlocks movedBlock(const SectionBlocks& blocks, int index, double newStart);
/** `blocks` without block `index`. */
SectionBlocks withoutBlock(const SectionBlocks& blocks, int index);

/** The index of the block containing `beat`, or -1. */
int blockIndexAt(const SectionBlocks& blocks, double beat);
/** The index of the block starting at `start` (within a hair), or -1. */
int blockIndexStartingAt(const SectionBlocks& blocks, double start);

/** "bars 9-16, 25-32" for the screen reader and the tooltip; "on everywhere" for no sections lane. */
juce::String describeSections(const SectionBlocks& blocks, bool hasLane, double beatsPerBar);

// ---- The document side ----

/** The lane that holds `lfoUuid`'s sections, wherever it sits, or nullptr. */
const synth::AutomationLane* sectionsLaneFor(const synth::TimelineDoc& doc, const juce::String& lfoUuid);

/**
 * Makes `blocks` the LFO's sections: creates the lane on `track` when there is none, rewrites it as one
 * mutation, and removes it when `blocks` is empty (so erasing the last block puts the modulator back to
 * "on everywhere"). The caller wraps this in a single undo step. False when nothing changed.
 */
bool applySections(synth::TimelineDoc& doc, synth::TrackId track, const juce::String& lfoUuid,
                   const SectionBlocks& blocks);

} // namespace synth::ui
