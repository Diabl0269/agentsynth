// Concern: the one confirm window with a tick box ("Don't ask again", or "Remember my choice" when it stores an
// answer), shared by every question the app lets a person switch off (removing an LFO's last destination, deleting
// a track).
#include "UI/Chrome/ConfirmDontAskDialog.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/AppLookAndFeel/AlertIconBounds.h"
#include "UI/Theme/AppLookAndFeel/TickToggleMetrics.h"

namespace synth::ui {

void showConfirmDontAsk(const ConfirmDontAskSpec& spec, std::function<void(bool, bool)> done) {
    showConfirmChoice(spec, [done = std::move(done)](ConfirmChoice choice, bool tick) {
        if (done)
            done(choice == ConfirmChoice::confirm, tick);
    });
}

namespace {
constexpr int kConfirmBoxHeight = 24;
constexpr int kConfirmBoxRightMargin = 20; // clear space between the box's label and the window's right edge
} // namespace

// The box carries no component name: AlertWindow draws a custom component's name as a heading above it, which would
// repeat the box's own label. JUCE re-lays the window out from scratch on every addButton/addCustomComponent and
// centres a custom component, so the icon's room is made and the box placed only after the last button is in.
ConfirmChoiceWindow buildConfirmChoiceWindow(const ConfirmDontAskSpec& spec) {
    constexpr auto kIcon = juce::MessageBoxIconType::WarningIcon;
    ConfirmChoiceWindow built;
    built.window = std::make_unique<juce::AlertWindow>(spec.title, spec.message, kIcon);
    auto* window = built.window.get();
    auto* dontAsk = new juce::ToggleButton();
    dontAsk->setButtonText(spec.checkboxLabel);
    dontAsk->setComponentID(spec.dontAskId);
    dontAsk->setTitle(spec.checkboxLabel);
    dontAsk->setTooltip(spec.dontAskTooltip);
    dontAsk->setSize(220, kConfirmBoxHeight);
    window->addCustomComponent(dontAsk);
    window->addButton(spec.confirmLabel, 1, juce::KeyPress(juce::KeyPress::returnKey));
    if (spec.alternateLabel.isNotEmpty())
        window->addButton(spec.alternateLabel, 2);
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    makeRoomForAlertIcon(*window, kIcon);
    const int left = kAlertTextLeft - kTickToggleBoxInset;
    dontAsk->setBounds(left, dontAsk->getY(),
                       window->getWidth() - kAlertTextLeft - kConfirmBoxRightMargin + kTickToggleBoxInset,
                       kConfirmBoxHeight);
    built.box = dontAsk;
    return built;
}

// Return confirms and Escape cancels. The box is read before the window goes, because the window owns it.
void showConfirmChoice(const ConfirmDontAskSpec& spec, std::function<void(ConfirmChoice, bool)> done) {
    auto built = buildConfirmChoiceWindow(spec);
    auto* window = built.window.release();
    auto* dontAsk = built.box;
    synth::ui::PopupMotion::attach(*window);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([window, dontAsk, done = std::move(done)](int result) {
                                const bool tick = dontAsk->getToggleState();
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (done)
                                    done(result == 1   ? ConfirmChoice::confirm
                                         : result == 2 ? ConfirmChoice::alternate
                                                       : ConfirmChoice::cancel,
                                         tick);
                            }),
                            false);
}

} // namespace synth::ui
