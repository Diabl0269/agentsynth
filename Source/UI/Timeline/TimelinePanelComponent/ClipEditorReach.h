// ClipEditorReach.h -- the keyboard's way back into the clip editor that is open on the Timeline panel.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class TimelinePanelComponent;

/** The grid of the clip editor that is open on `panel` (the home of its Tab walk): the piano roll, else the drum
    machine; null with none open. */
juce::Component* openClipEditorGrid(TimelinePanelComponent& panel) noexcept;

/** Gives keyboard focus to the open editor's grid. False, doing nothing, with no editor open. */
bool focusOpenClipEditor(TimelinePanelComponent& panel);

} // namespace synth::ui
