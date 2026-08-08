#pragma once

#include "../AI/AccountService.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/**
 * @class SignInDialog
 * @brief Modal content component for the device-code sign-in flow.
 *
 * This is the *content* handed to juce::DialogWindow::LaunchOptions::content.setOwned(), not a
 * DialogWindow subclass itself — same shape as SettingsWindow (see Source/UI/SettingsWindow.h).
 * The owning DialogWindow supplies the native title bar / close button.
 *
 * AccountRow calls accountService.beginSignIn() immediately before constructing this dialog, so
 * the constructor may find a snapshot with a userCode already populated (shown right away) or
 * still empty (briefly shows "Starting..." until the device-code request round-trips and
 * AccountRow::refresh() forwards the next state change here via refresh()).
 *
 * Does NOT set AccountService::onStateChanged/onAccessTokenChanged itself: those single-slot
 * callbacks are owned by AIChatComponent (see its setAccountService() comment). AccountRow
 * forwards notifications to whichever SignInDialog it most recently launched by calling
 * refresh() on it.
 */
class SignInDialog : public juce::Component {
public:
    explicit SignInDialog(AccountService& service);
    ~SignInDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Called by AccountRow whenever AccountService::onStateChanged fires, so the dialog reflects
    // the live snapshot (code arrives, polling succeeds/fails, sign-in completes) without owning
    // the callback slot itself.
    void refresh();

private:
    void updateFromSnapshot(const AccountSnapshot& snapshot);
    void closeDialog();

    AccountService& accountService;

    // Guards the one-time auto-open of the verification URL — set the first time
    // verificationUriComplete is seen non-empty, so a later refresh() (e.g. a slow_down /
    // authorization_pending poll tick republishing the same snapshot) never reopens the browser.
    bool hasAutoOpened = false;

    juce::Label codeLabel;
    juce::TextButton openBrowserButton;
    juce::Label statusLabel;
    juce::TextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignInDialog)
};

} // namespace synth
