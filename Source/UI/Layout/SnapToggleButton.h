#pragma once

#include "UI/Chrome/ToolbarButton/ToolbarButton.h"

namespace synth::ui {

// The one button that switches snapping on or off, on every surface that has the switch (the timeline's transport
// bar, the curve window, the shape editor's grid rows): the Snap icon on an Edit-group chip, named "Snap". It carries
// only that look. Each surface sets its own layout, caption, tooltip and toggle behaviour, and keeps its own state.
class SnapToggleButton : public ToolbarButton {
public:
    SnapToggleButton();

    // The start of every Snap tooltip: "Snap to grid".
    static juce::String tooltipLead() { return "Snap to grid"; }

    JUCE_DECLARE_NON_COPYABLE(SnapToggleButton)
};

} // namespace synth::ui
