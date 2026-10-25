#pragma once

// Which knob and fader style a module card paints with: the node's own "controlStyle", else the style set for
// every module of its type (user setting "controlStyleByType"), else the app-wide Settings choice.
// A card publishes the first two as the component properties kKnobStyleProperty / kFaderStyleProperty that
// AppLookAndFeel reads up the parent chain. docs/layout/theming.md#per-module-control-style.

#include "UI/Theme/ControlStyleOverride.h"
#include <juce_gui_basics/juce_gui_basics.h>

class GraphEditor;
class ModuleComponent;

namespace synth {

/** The user settings file the editor persists to; null for a bare editor. */
juce::PropertiesFile* userSettingsOf(GraphEditor& editor);

/** The style `card` takes from its node and its module type, each kind unset when neither chooses one. */
theme::ControlStyleOverride effectiveControlStyle(GraphEditor& editor, const ModuleComponent& card);

/** Publishes `effectiveControlStyle` on the card (removing a property that is no longer set) and repaints the
 *  card when it changed. Message thread only. */
void refreshControlStyle(GraphEditor& editor, ModuleComponent& card);

/** refreshControlStyle on every card of the editor. */
void refreshAllControlStyles(GraphEditor& editor);

} // namespace synth
