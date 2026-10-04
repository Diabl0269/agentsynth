#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** What a "confirm, with a Don't ask again box" dialog says and which button Return presses. */
struct ConfirmDontAskSpec {
    juce::String title;
    juce::String message;
    juce::String confirmLabel;
    juce::String dontAskId;       // component id of the box, for tests and tooling
    juce::String dontAskTooltip;  // where the question can be turned back on
    bool cancelIsDefault = false; // Return presses Cancel (for a destructive confirm); otherwise it presses confirm
};

/** Opens the dialog, animated like every popup. `done(confirmed, dontAskAgain)` runs once after it closes; the caller
 *  applies the action and writes the preference. */
void showConfirmDontAsk(const ConfirmDontAskSpec& spec, std::function<void(bool confirmed, bool dontAskAgain)> done);

} // namespace synth::ui
