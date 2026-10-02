#include "AppLookAndFeel.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::theme {

// Concern: handing every popup window JUCE builds to synth::ui::PopupMotion. JUCE has no
// "window created" hook of its own, so each window kind is caught at the earliest look-and-feel
// call that receives it, before it is first shown:
//   - menus, submenus and ComboBox dropdowns: preparePopupMenuWindow, at the end of the menu
//     window's constructor;
//   - alert boxes (AlertWindow::showAsync and friends): createAlertWindow;
//   - call-out boxes: getCallOutBoxBorderSize, which the call-out's constructor asks for before
//     it puts itself on the desktop. PopupMotion::attach ignores repeats.
// Dialogs have no such call and are attached where they are launched (PopupMotion::launchDialog).

void AppLookAndFeel::preparePopupMenuWindow(juce::Component& window) {
    juce::LookAndFeel_V4::preparePopupMenuWindow(window);
    synth::ui::PopupMotion::attach(window);
}

juce::AlertWindow* AppLookAndFeel::createAlertWindow(const juce::String& title, const juce::String& message,
                                                     const juce::String& button1, const juce::String& button2,
                                                     const juce::String& button3, juce::MessageBoxIconType iconType,
                                                     int numButtons, juce::Component* associatedComponent) {
    auto* alert = juce::LookAndFeel_V4::createAlertWindow(title, message, button1, button2, button3, iconType,
                                                          numButtons, associatedComponent);
    if (alert != nullptr)
        synth::ui::PopupMotion::attach(*alert);
    return alert;
}

int AppLookAndFeel::getCallOutBoxBorderSize(const juce::CallOutBox& box) {
    // A call-out inside a parent component has no window of its own; attach() leaves it alone.
    synth::ui::PopupMotion::attach(const_cast<juce::CallOutBox&>(box));
    return juce::LookAndFeel_V4::getCallOutBoxBorderSize(box);
}

} // namespace synth::theme
