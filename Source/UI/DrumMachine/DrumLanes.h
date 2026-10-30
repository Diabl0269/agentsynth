#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_graphics/juce_graphics.h>
#include <set>
#include <vector>

class DrumKitModule;

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
/** The rows a Drum Kit's pads give a clip that plays it: one per shown pad within the pad count, in pad order. A
 *  hidden pad has no row, held notes or not (it still plays them). */
struct KitRows {
    std::vector<DrumLane> lanes;
    std::set<int> hidden; ///< the notes of pads that are switched off and have no shown pad on the same note
    int total = 0;        ///< the pads within the count
    int hiddenCount = 0;  ///< how many of them are switched off

    bool operator==(const KitRows& other) const;
    bool operator!=(const KitRows& other) const { return !(*this == other); }
};
/** `kit`'s rows. A pad plays under its own colour: the drum it plays, or its note when it plays none. */
KitRows kitRowsFor(const DrumKitModule& kit);

/** The rows to show for `clip` (null for none). Without `kit`: the default set, plus the extra set when `showAll`,
 *  plus a row for every other pitch the clip holds a note on, so no note is ever invisible. With `kit` (the clip
 *  plays a Drum Kit): the kit's rows, plus a row for every other pitch the clip holds a note on unless a hidden pad
 *  owns it; `showAll` is ignored. */
std::vector<DrumLane> visibleDrumLanes(const synth::Clip* clip, bool showAll, const KitRows* kit = nullptr);

} // namespace synth::ui
