#include "AccountRow.h"
#include "SignInDialog.h"
#include "UI/Layout/PopupMotion.h"

namespace synth {

AccountRow::AccountRow() {
    addChildComponent(signInButton);
    signInButton.setButtonText("Sign in");
    signInButton.setTooltip("Sign in to your account");
    signInButton.onClick = [this] {
        if (accountService == nullptr)
            return;
        accountService->beginSignIn();
        launchSignInDialog();
    };

    addChildComponent(signOutButton);
    signOutButton.setButtonText("Sign out");
    signOutButton.setTooltip("Sign out");
    signOutButton.onClick = [this] {
        if (accountService != nullptr)
            accountService->signOut();
    };

    addChildComponent(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setMinimumHorizontalScale(1.0f);

    // Invisible/zero-height until setAccountService() attaches a real service — this is the
    // default state for every existing caller that doesn't know about accounts yet.
    setVisible(false);
}

AccountRow::~AccountRow() = default;

void AccountRow::setAccountService(AccountService* service) {
    accountService = service;

    if (accountService == nullptr) {
        rowFade_.setShown(false);
        return;
    }

    // Synchronous, not routed through onStateChanged — this row does not own that callback slot
    // (AIChatComponent does), and the caller needs an immediately-truthful reflection of the
    // snapshot right after attaching, not one that depends on a later dispatch-loop pump. The
    // controls take their state while the row is still hidden, so only the row itself fades in.
    updateFromSnapshot(accountService->getSnapshot());
    rowFade_.setShown(true);
}

void AccountRow::refresh() {
    if (accountService == nullptr)
        return;

    const auto snapshot = accountService->getSnapshot();
    updateFromSnapshot(snapshot);

    if (auto* dialog = openDialog.getComponent())
        dialog->refresh();
}

void AccountRow::updateFromSnapshot(const AccountSnapshot& snapshot) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour normalText = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour mutedText = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;

    switch (snapshot.state) {
    case AccountState::SignedOut:
        signInFade_.setShown(true);
        signOutFade_.setShown(false);
        statusFade_.setShown(false);
        break;

    case AccountState::SigningIn:
        statusLabel.setColour(juce::Label::textColourId, mutedText);
        statusLabel.setText("Signing in...", juce::dontSendNotification);
        signInFade_.setShown(false);
        signOutFade_.setShown(false);
        statusFade_.setShown(true);
        break;

    case AccountState::SignedIn:
        statusLabel.setColour(juce::Label::textColourId, normalText);
        statusLabel.setText(snapshot.email.isNotEmpty() ? snapshot.email : juce::String("Signed in"),
                            juce::dontSendNotification);
        signInFade_.setShown(false);
        signOutFade_.setShown(true);
        statusFade_.setShown(true);
        break;
    }

    resized();
}

void AccountRow::launchSignInDialog() {
    if (accountService == nullptr)
        return;

    auto* dialogContent = new SignInDialog(*accountService);
    dialogContent->setSize(360, 220);
    openDialog = dialogContent;

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialogContent);
    options.dialogTitle = "Sign in";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    synth::ui::PopupMotion::launchDialog(options);
}

void AccountRow::paint(juce::Graphics&) {
    // No background of its own — sits directly on AIChatComponent's already-painted panel.
}

void AccountRow::resized() {
    auto b = getLocalBounds();

    // Right-aligned to match signOutButton below and AIChatComponent's upsellButton in the row
    // beneath this one — every bottom-chrome action button hugs the same edge so the stack reads
    // as one coherent column instead of alternating sides.
    if (signInFade_.isShown()) {
        signInButton.setBounds(b.removeFromRight(90));
        return;
    }

    if (signOutFade_.isShown())
        signOutButton.setBounds(b.removeFromRight(90).reduced(2, 2));

    statusLabel.setBounds(b);
}

} // namespace synth
