#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::ui {

/** One drum-machine row: exactly one General MIDI pitch. */
struct DrumLane {
    int pitch = 0;
    juce::String name;
    juce::Colour colour;
};

/** The rows shown by default, top to bottom. */
const std::vector<DrumLane>& defaultDrumLanes();
/** The further rows "Show all drums" adds, after the default ones. */
const std::vector<DrumLane>& extraDrumLanes();
/** The table row for `pitch`, or a row named by its note number ("Note 60") when the table has none. */
DrumLane drumLaneForPitch(int pitch);
/** The rows to show for `clip` (null for none): the default set, plus the extra set when `showAll`, plus a row for
 *  every other pitch the clip holds a note on, so no note is ever invisible. */
std::vector<DrumLane> visibleDrumLanes(const synth::Clip* clip, bool showAll);

} // namespace synth::ui
