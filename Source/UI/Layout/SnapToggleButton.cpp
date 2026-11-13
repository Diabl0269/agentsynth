#include "SnapToggleButton.h"

namespace synth::ui {

SnapToggleButton::SnapToggleButton()
    : ToolbarButton("Snap") {
    setIcon(synth::theme::Icon::ToolbarSnap, ToolbarGroup::Edit);
}

} // namespace synth::ui
