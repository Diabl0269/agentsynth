#pragma once

#include "DrumGrid.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

/** The lanes the drum machine can show under its grid, in chip order. */
enum class DrumLaneKind { Loudness = 0, Chance, Repeat, Nudge, Condition };
constexpr int kDrumLaneKindCount = 5;

/**
 * Everything that makes one lane differ from another: its words, its value range, its keys and how a value reads.
 * `DrumHitLane` draws and edits any lane from a spec, so the five lanes are one class. A hit's value is the model's
 * own number encoded as an int: loudness 1..127, chance 0..100, nudge in whole percent of a step (-50..50), repeat as
 * `repeat * 2 + flam` and condition as `of * 16 + hit`. A discrete lane (`options` not empty) snaps a drag or key to
 * an option, yet a stored value that is not an option still shows its own text. docs/timeline/drum-machine.md.
 */
struct DrumLaneSpec {
    struct Option {
        int value = 0;
        juce::String label;
    };

    DrumLaneKind kind = DrumLaneKind::Loudness;
    juce::String name;        ///< the chip and the lane's gutter: "Chance"
    juce::String word;        ///< what a screen reader says before the value: "chance"
    juce::String componentId; ///< "drumChanceLane"
    juce::String tooltip;
    int minValue = 0;
    int maxValue = 0;
    int scaleMin = 0;  ///< the value drawn as an empty bar (continuous lanes)
    int keyStep = 1;   ///< Up and Down: in value units, or in options for a discrete lane
    int shiftStep = 1; ///< the same with Shift
    bool bipolar = false;
    std::vector<Option> options;

    bool isDiscrete() const noexcept { return !options.empty(); }
    /** How a value reads ("50%", "Flam", "2/4"), also for a value that is not one of the options. */
    juce::String textOf(int value) const;
    /** 0..1 along the lane's height (an off-list value of a discrete lane sits at the nearest option). */
    double fractionOf(int value) const;
    /** The value at a 0..1 height: rounded and clamped, or snapped to an option. */
    int valueAtFraction(double fraction) const;
    /** One key press: `steps` units (options) up or down from `value`; an off-list value moves to the next option. */
    int stepped(int value, int steps) const;
};

const DrumLaneSpec& drumLaneSpec(DrumLaneKind kind);

/** The value the lane shows for a cell (the first note's settings). */
int drumLaneValueOf(DrumLaneKind kind, const drumgrid::CellHits& hits);

/** Writes `value` to every note of `ids`, through the TimelineDoc mutators (the caller wraps it in one undo step). */
void applyDrumLaneValue(synth::TimelineDoc& doc, DrumLaneKind kind, const std::vector<synth::NoteId>& ids, int value);

} // namespace synth::ui
