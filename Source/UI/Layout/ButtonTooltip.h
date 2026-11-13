#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ButtonTooltip.h (docs/development/accessibility.md#the-rule): the one way a button gets its tooltip. A button that
// shows its words gets only the explanation (the words are already on it); an icon-only button gets its name first.
namespace synth::ui {

/** True when the button draws its own text: a juce::TextButton or juce::ToggleButton with non-empty text. */
bool buttonShowsText(const juce::Button& button);

/** True when `tip` opens with `label` as a heading: the label (any case), then ':', ' -', ' (' or nothing. */
bool tooltipEchoesLabel(const juce::String& label, const juce::String& tip);

/** The tooltip `button` gets for `explanation`; the button's title (else its text) names an icon-only one. */
juce::String buttonTooltipFor(const juce::Button& button, const juce::String& explanation);

/** Sets buttonTooltipFor(button, explanation); set the button's title (an icon-only one's name) first. */
void setButtonTooltip(juce::Button& button, const juce::String& explanation);

} // namespace synth::ui
