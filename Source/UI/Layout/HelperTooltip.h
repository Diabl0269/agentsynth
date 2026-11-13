#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// HelperTooltip.h (docs/layout/animation.md#tooltips): the marker that tells AppTooltipWindow a component's tooltip is
// a helper tip. An info tooltip explains a control ("Send level: drag to change") and is switched off by Preferences >
// "Show info tooltips". A helper tip reports something the screen does not show anywhere else (the sources badge's
// "Plays into this channel: Lead, Pad") and keeps showing whatever that preference says.
namespace synth::ui {

constexpr const char* kHelperTooltipProperty = "synthHelperTooltip";

/** Marks `c`'s tooltip (juce::TooltipClient::getTooltip) as a helper tip. */
inline void markHelperTooltip(juce::Component& c) { c.getProperties().set(kHelperTooltipProperty, true); }

inline bool isHelperTooltip(const juce::Component& c) { return (bool)c.getProperties()[kHelperTooltipProperty]; }

} // namespace synth::ui
