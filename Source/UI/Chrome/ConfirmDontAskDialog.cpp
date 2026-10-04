// Concern: the one confirm window with a "Don't ask again" box, shared by every question the app lets a person
// switch off (removing an LFO's last destination, deleting a track).
#include "UI/Chrome/ConfirmDontAskDialog.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

// Return confirms and Escape cancels. The box carries no component name: AlertWindow draws a custom component's name
// as a heading above it, which would repeat the box's own label.
// The box is read before the window goes, because the window owns it.
void showConfirmDontAsk(const ConfirmDontAskSpec& spec, std::function<void(bool, bool)> done) {
    auto* window = new juce::AlertWindow(spec.title, spec.message, juce::MessageBoxIconType::WarningIcon);
    auto* dontAsk = new juce::ToggleButton();
    dontAsk->setButtonText("Don't ask again");
    dontAsk->setComponentID(spec.dontAskId);
    dontAsk->setTitle("Don't ask again");
    dontAsk->setTooltip(spec.dontAskTooltip);
    dontAsk->setSize(220, 24);
    window->addCustomComponent(dontAsk);
    window->addButton(spec.confirmLabel, 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    synth::ui::PopupMotion::attach(*window);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([window, dontAsk, done = std::move(done)](int result) {
                                const bool tick = dontAsk->getToggleState();
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (done)
                                    done(result == 1, tick);
                            }),
                            false);
}

} // namespace synth::ui
