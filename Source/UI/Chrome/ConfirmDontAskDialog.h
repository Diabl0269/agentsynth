#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** What a "confirm, with a Don't ask again box" dialog says and what it offers. */
struct ConfirmDontAskSpec {
    juce::String title;
    juce::String message;
    juce::String confirmLabel;
    juce::String dontAskId;        // component id of the box, for tests and tooling
    juce::String dontAskTooltip;   // where the question can be turned back on
    juce::String alternateLabel{}; // a second way to go on ("Keep module"); empty = the window has no such button
    juce::String checkboxLabel{"Don't ask again"}; // the box's words ("Remember my choice" when it stores a choice)
};

/** What the person pressed in a window that can offer a second way to go on. */
enum class ConfirmChoice { cancel, confirm, alternate };

/** Opens the dialog, animated like every popup. `done(confirmed, dontAskAgain)` runs once after it closes; the caller
 *  applies the action and writes the preference. */
void showConfirmDontAsk(const ConfirmDontAskSpec& spec, std::function<void(bool confirmed, bool dontAskAgain)> done);

/** The same window with the second button (`spec.alternateLabel`, when set) reported apart from the confirm.
 *  `done(choice, boxTicked)` runs once after it closes. Return picks the confirm, Escape cancels. */
void showConfirmChoice(const ConfirmDontAskSpec& spec, std::function<void(ConfirmChoice choice, bool boxTicked)> done);

} // namespace synth::ui
