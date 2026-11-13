#include "SignInDialog.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/PopupMotion.h"

namespace synth {

SignInDialog::SignInDialog(AccountService& service)
    : accountService(service) {
    addAndMakeVisible(codeLabel);
    codeLabel.setJustificationType(juce::Justification::centred);
    codeLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::bold));

    addAndMakeVisible(openBrowserButton);
    openBrowserButton.setButtonText("Open in Browser");
    openBrowserButton.setTooltip("Open the verification page in your default browser (Return)");
    openBrowserButton.onClick = [this] {
        const auto snapshot = accountService.getSnapshot();
        if (snapshot.verificationUriComplete.isNotEmpty())
            juce::URL(snapshot.verificationUriComplete).launchInDefaultBrowser();
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.setTooltip("Stop signing in and close this window (Esc)");
    cancelButton.onClick = [this] { cancelAndClose(); };

    updateFromSnapshot(accountService.getSnapshot());
}

SignInDialog::~SignInDialog() {
    // Covers every close path this dialog doesn't drive itself (native title bar, the owning
    // DialogWindow being torn down out from under it) so a device-code poll loop never keeps
    // running for a dialog nobody can see anymore. Harmless no-op if nothing is signing in —
    // already succeeded, already cancelled via the Cancel button above, or never started — per
    // AccountService::cancelSignIn()'s own doc comment.
    accountService.cancelSignIn();
}

void SignInDialog::refresh() { updateFromSnapshot(accountService.getSnapshot()); }

void SignInDialog::updateFromSnapshot(const AccountSnapshot& snapshot) {
    if (snapshot.state == AccountState::SignedIn) {
        // Simplest and sufficient per the approved plan: AccountRow already reflects the
        // signed-in state on its own, so the dialog has nothing left to say before closing.
        closeDialog();
        return;
    }

    if (snapshot.userCode.isNotEmpty()) {
        codeLabel.setText(snapshot.userCode, juce::dontSendNotification);

        if (!hasAutoOpened && snapshot.verificationUriComplete.isNotEmpty()) {
            hasAutoOpened = true;
            juce::URL(snapshot.verificationUriComplete).launchInDefaultBrowser();
        }
    } else {
        codeLabel.setText("Starting...", juce::dontSendNotification);
    }

    if (snapshot.state == AccountState::SigningIn) {
        statusLabel.setText("Waiting for approval...", juce::dontSendNotification);
    } else if (snapshot.lastError.isNotEmpty()) {
        // SigningIn -> SignedOut with a populated lastError is the terminal-failure path
        // (expired/denied code, transport error mid-poll) — see AccountService's flow docs.
        statusLabel.setText(snapshot.lastError, juce::dontSendNotification);
    } else {
        statusLabel.setText({}, juce::dontSendNotification);
    }
}

void SignInDialog::cancelAndClose() {
    accountService.cancelSignIn();
    closeDialog();
}

bool SignInDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        cancelAndClose();
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        if (openBrowserButton.onClick)
            openBrowserButton.onClick();
        return true;
    }
    return false;
}

void SignInDialog::closeDialog() { synth::ui::closeHostingWindow(*this); }

void SignInDialog::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(lf != nullptr ? lf->getTheme().colors.bg1 : juce::Colours::darkgrey.darker(0.5f));
}

void SignInDialog::resized() {
    auto b = getLocalBounds().reduced(20);
    codeLabel.setBounds(b.removeFromTop(50));
    b.removeFromTop(10);
    openBrowserButton.setBounds(b.removeFromTop(30).withSizeKeepingCentre(160, 30));
    b.removeFromTop(10);
    statusLabel.setBounds(b.removeFromTop(30));
    b.removeFromTop(10);
    cancelButton.setBounds(b.removeFromTop(30).withSizeKeepingCentre(100, 30));
}

juce::Component::SafePointer<SignInDialog> launchSignInDialog(AccountService& service, juce::Component& centreAround) {
    auto* dialogContent = new SignInDialog(service);
    dialogContent->setSize(360, 220);
    juce::Component::SafePointer<SignInDialog> opened(dialogContent);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialogContent);
    options.dialogTitle = "Sign in";
    options.componentToCentreAround = &centreAround;
    options.useNativeTitleBar = true;
    options.resizable = false;
    synth::ui::PopupMotion::launchDialog(options);
    return opened;
}

} // namespace synth
