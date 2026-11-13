#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * Draws `toggle` as the small toggle pill of a card's footer row, or (`pill` false) as the tick box
 * again. Only the look changes: the toggle keeps its attachment, focus, title, tooltip and keys.
 * AppLookAndFeel paints it (paintTogglePill). docs/layout/module-card.md#the-footer-row.
 */
inline void setTogglePillStyle(juce::ToggleButton& toggle, bool pill) {
    toggle.getProperties().set(synth::theme::AppLookAndFeel::kTogglePillProperty, pill);
    toggle.repaint();
}

} // namespace synth::ui
