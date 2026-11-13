#pragma once

// The card context menu's "Control Style..." entry and the call-out it opens beside the card.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class ModuleComponent;

namespace synth {

/** Appends "Control Style..." when `card` shows a knob or a fader (returns true); nothing otherwise. */
bool appendControlStyleMenuItem(juce::PopupMenu& menu, ModuleComponent& card);

/** The card menu's layout entries, each followed by the next and a closing separator: "Edit Layout..." and
 *  "Control Style..." on a card drawn from a layout; on a hosted plugin card "Edit Layout...", "Control Style..."
 *  and "Add control from plugin window...". Nothing for a card with neither. */
void appendLayoutAndStyleMenuItems(juce::PopupMenu& menu, ModuleComponent& card);

/** Opens the control style panel beside `card`. */
void showControlStylePanel(ModuleComponent& card);

/** Test seam: while set, the panel is handed here instead of opening a call-out. Message thread only. */
void setControlStylePanelLauncherForTest(std::function<void(std::unique_ptr<juce::Component>)> launcher);

} // namespace synth
