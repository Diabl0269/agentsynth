#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <limits>
#include <vector>

namespace synth::ui {

// The retired "sections" format, read only by the project-load migration to amount lanes
// (docs/timeline/automation.md#migration-from-sections). A project saved before amount lanes kept the
// blocks of the song where an LFO modulator was on as an ordinary lane on the LFO's own `level`
// parameter: Hold points, 1 inside a block and 0 outside. Nothing writes that shape any more.

/** The LFO parameter a sections lane drove. */
inline const juce::String kSectionsParamId = "level";

/** One stretch where the modulator was on, in beats; `end` is infinity for a lane left on to the end. */
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

/**
 * The amount lane that plays `blocks` at `amount`: Hold points at the same edges, `amount` inside a
 * block and 0 outside. A single 0 at beat 0 for no blocks.
 */
std::vector<synth::AutomationLane::Breakpoint> amountPointsFromSections(const SectionBlocks& blocks, double amount);

/** The `level` lane of `lfoUuid`, wherever it sits, or nullptr. */
const synth::AutomationLane* sectionsLaneFor(const synth::TimelineDoc& doc, const juce::String& lfoUuid);

} // namespace synth::ui
