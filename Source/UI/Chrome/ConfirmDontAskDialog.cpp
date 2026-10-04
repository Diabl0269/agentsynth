// Concern: the one confirm window with a "Don't ask again" box, shared by every question the app lets a person
// switch off (removing an LFO's last destination, deleting a track).
#include "UI/Chrome/ConfirmDontAskDialog.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

// Escape always cancels: it is the button's own key when Return is not, and AlertWindow's built-in answer otherwise.
// The box is read before the window goes, because the window owns it.
void showConfirmDontAsk(const ConfirmDontAskSpec& spec, std::function<void(bool, bool)> done) {
    auto* window = new juce::AlertWindow(spec.title, spec.message, juce::MessageBoxIconType::WarningIcon);
    auto* dontAsk = new juce::ToggleButton("Don't ask again");
    dontAsk->setComponentID(spec.dontAskId);
    dontAsk->setTitle("Don't ask again");
    dontAsk->setTooltip(spec.dontAskTooltip);
    dontAsk->setSize(220, 24);
    window->addCustomComponent(dontAsk);
    const juce::KeyPress returnKey(juce::KeyPress::returnKey);
    const juce::KeyPress escapeKey(juce::KeyPress::escapeKey);
    window->addButton(spec.confirmLabel, 1, spec.cancelIsDefault ? juce::KeyPress() : returnKey);
    window->addButton("Cancel", 0, spec.cancelIsDefault ? returnKey : escapeKey);
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
