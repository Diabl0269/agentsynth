#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <functional>
#include <memory>
#include <vector>

namespace synth::ui {

// What the "Add automation..." picker offers for one track, and the picker built from it. The picker
// itself is the Mod Matrix's searchable list (ModMatrixPicker), reused as it is: rows grouped under the
// module that owns them, a search field, Up/Down/Return/Escape. This file only turns the host's
// parameters into its items and its pick back into a parameter.

struct AddAutomationChoices {
    std::vector<TrackHeaderHost::AutomatableParameter> parameters;
    // One item per parameter, grouped by module in the order the modules first appear; item id =
    // index into `parameters` + 1 (the picker reserves 0 for "nothing").
    std::vector<ModMatrixPicker::Item> items;
};

/** What `host` offers `track`, minus every parameter that already has a lane in `doc`. The host leaves
 *  those out itself; filtering here as well keeps the rule true for any host. */
AddAutomationChoices collectAddAutomationChoices(TrackHeaderHost& host, const synth::TimelineDoc& doc,
                                                 synth::TrackId track);

/** The picker for `choices`; `onPick` gets the chosen parameter. `trackName` words its screen-reader
 *  names. The caller launches it (a CallOutBox in the app, a test hook in tests). */
std::unique_ptr<ModMatrixPicker>
buildAddAutomationPicker(const AddAutomationChoices& choices, const juce::String& trackName,
                         std::function<void(const TrackHeaderHost::AutomatableParameter&)> onPick);

} // namespace synth::ui
