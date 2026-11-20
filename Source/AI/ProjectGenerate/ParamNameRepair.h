#pragma once

// A small local model often writes a real parameter under an invented name ("filterType_id", "attackDiv_id",
// "cutoff_1"). The patch validator refuses an unknown parameter name, so the whole plan fails over a spelling the
// intent is clear from. This renames such a key to the one real parameter of that module it is a spelling of.

#include <juce_core/juce_core.h>

namespace synth::projectgenerate {

/** In `root` (an edit plan, changed in place) renames each unknown parameter key of a patch node, a new track's
 *  instrument and its inserts to the real parameter id of that module when the key is that id with underscores,
 *  case, a trailing "id" or trailing digits added, and exactly one real parameter matches and is not already set.
 *  Appends one line per rename to `notes`. A key it cannot place is left for the validator to name. */
void repairInventedParamNames(juce::var& root, juce::StringArray& notes);

/** Two more slips of a small model in `addInstrumentTrack` operations of `root` (changed in place): "poly" written
 *  inside "instrumentParams" moves to the op's own "poly" (a boolean), and a second new track given the name of an
 *  earlier new track of the same answer gets a number after it ("Bass 2"), so a later "track" or song pattern naming
 *  the first one still finds it. One line per change in `notes`. */
void repairTrackOpSlips(juce::var& root, juce::StringArray& notes);

} // namespace synth::projectgenerate
