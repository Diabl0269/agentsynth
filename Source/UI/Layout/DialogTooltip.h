#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace juce {
class ApplicationProperties;
}

namespace synth::ui {
class AppTooltipWindow;
}

// DialogTooltip.h (docs/layout/animation.md#tooltips): every dialog PopupMotion::launchDialog opens is a native window
// of its own, so the main window's tooltip window draws its tips under it. launchDialog gives each dialog its own
// AppTooltipWindow through attach(); the window is owned by the dialog and deleted with it.
namespace synth::ui::dialog_tooltip {

/** Where the dialogs' tooltip windows read "Show info tooltips" from. Set once at startup by the main component and
 *  cleared when it goes; null (the default) leaves info tooltips on. Tests that set it reset it in teardown. */
void setAppProperties(juce::ApplicationProperties* properties) noexcept;
juce::ApplicationProperties* appProperties() noexcept;

/** Gives `dialog` its tooltip window. Called by PopupMotion::launchDialog; owns itself, deleted with the window. */
void attach(juce::DialogWindow& dialog);

/** The tooltip window `dialog` hosts, or null. For tests. */
AppTooltipWindow* tooltipOf(const juce::Component& dialog);

/** How many AppTooltipWindows `dialog` hosts (one for a launchDialog dialog). For tests. */
int countIn(const juce::Component& dialog);

} // namespace synth::ui::dialog_tooltip
