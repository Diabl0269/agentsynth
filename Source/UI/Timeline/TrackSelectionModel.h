#pragma once

#include "Timeline/TimelineDoc/TimelineTypes.h"
#include "UI/Timeline/ListSelectionModel.h"

namespace synth::ui {

// Which timeline tracks are selected. TimelinePanelComponent owns the single instance; like focusedTrackIndex_ it is
// view state only.
using TrackSelectionModel = ListSelectionModel<synth::TrackId>;

} // namespace synth::ui
