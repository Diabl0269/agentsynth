#include "AccountSettingsTab.h"
#include "AI/UpgradeUrl.h"
#include "AccountButtonStyle.h"
#include "Branding.h"
#include "UI/Assistant/PlanBadge.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {
constexpr int kTimerMs = 250;
constexpr int kRowH = 22;
constexpr int kButtonH = 30;

const juce::String kManageText = "Manage subscription";
const juce::String kUpgradeText = "Upgrade to Pro";
} // namespace

AccountSettingsTab::AccountSettingsTab(synth::AccountService* accountServiceIn)
    : accountService(accountServiceIn) {
    setTitle("Account");

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Account", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    addChildComponent(noticeLabel);
    addChildComponent(signedOutLabel);
    signedOutLabel.setText("Sign in to see your plan and manage your account", juce::dontSendNotification);

    addChildComponent(signInButton);
    signInButton.setTitle("Sign in");
    signInButton.setTooltip("Sign in to your account");
    signInButton.onClick = [this] {
        if (accountService == nullptr)
            return;
        accountDeleted = false;
        accountService->beginSignIn();
        openDialog = synth::launchSignInDialog(*accountService, *this);
        syncFromSnapshot();
    };

    for (auto* label : {&emailLabel, &planLabel, &periodLabel})
        addChildComponent(*label);

    addChildComponent(manageButton);
    manageButton.onClick = [this] {
        if (accountService == nullptr)
            return;
        if (synth::isProPlan(accountService->getSnapshot()))
            openFlow(synth::AccountFlowPanel::Flow::manage, manageButton);
        else
            urlOpener(synth::buildUpgradeUrl(accountService->getSnapshot().email));
    };

    addChildComponent(signOutButton);
    signOutButton.setTitle("Sign out");
    signOutButton.setTooltip("Sign out of this device");
    signOutButton.onClick = [this] {
        if (accountService != nullptr)
            accountService->signOut();
        syncFromSnapshot();
    };

    addChildComponent(deleteButton);
    deleteButton.setButtonText(juce::String::fromUTF8("Delete account\xe2\x80\xa6"));
    deleteButton.setTitle("Delete account");
    deleteButton.setTooltip("Permanently delete your account and its data. You are asked to confirm first.");
    deleteButton.onClick = [this] { openFlow(synth::AccountFlowPanel::Flow::deleteAccount, deleteButton); };

    applySnapshot(accountService != nullptr ? accountService->getSnapshot() : synth::AccountSnapshot{});
}

AccountSettingsTab::~AccountSettingsTab() { stopTimer(); }

void AccountSettingsTab::paint(juce::Graphics& g) { g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); }

void AccountSettingsTab::visibilityChanged() {
    if (isShowing()) {
        syncFromSnapshot();
        startTimer(kTimerMs);
        wasForeground = foregroundCheck();
        // The plan may have changed since the tab last showed (a purchase in the browser).
        refreshEntitlementIfSignedIn();
    } else {
        stopTimer();
    }
}

void AccountSettingsTab::refreshEntitlementIfSignedIn() {
    if (accountService != nullptr && accountService->getSnapshot().state == synth::AccountState::SignedIn)
        accountService->refreshEntitlement();
}

void AccountSettingsTab::refreshOnReturnToForeground() {
    const bool foreground = foregroundCheck();
    const bool returned = foreground && !wasForeground;
    wasForeground = foreground;
    if (returned)
        refreshEntitlementIfSignedIn();
}

void AccountSettingsTab::lookAndFeelChanged() {
    synth::applyDestructiveLook(deleteButton);
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour muted = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;
    periodLabel.setColour(juce::Label::textColourId, muted);
    signedOutLabel.setColour(juce::Label::textColourId, muted);
}

void AccountSettingsTab::syncFromSnapshot() {
    applySnapshot(accountService != nullptr ? accountService->getSnapshot() : synth::AccountSnapshot{});
}

void AccountSettingsTab::applySnapshot(const synth::AccountSnapshot& snapshot) {
    const bool signedIn = snapshot.state == synth::AccountState::SignedIn;
    const bool signingIn = snapshot.state == synth::AccountState::SigningIn;
    const bool pro = synth::isProPlan(snapshot);

    const juce::String signature = juce::String((int)snapshot.state) + "|" + snapshot.email + "|" + snapshot.plan +
                                   "|" + juce::String(snapshot.requestsUsed) + "|" +
                                   juce::String(snapshot.monthlyRequestLimit) + "|" +
                                   juce::String((int)snapshot.entitlementKnown) + "|" + snapshot.periodEndIso + "|" +
                                   juce::String((int)snapshot.cancelAtPeriodEnd) + "|" + snapshot.userCode + "|" +
                                   juce::String((int)accountDeleted);
    if (signature == lastSignature)
        return;
    lastSignature = signature;

    if (auto* dialog = openDialog.getComponent())
        dialog->refresh(); // this tab's Sign in opened it, and the AI chat's row does not know

    noticeLabel.setVisible(accountDeleted && !signedIn);
    noticeLabel.setText("Your account was deleted.", juce::dontSendNotification);

    signedOutLabel.setVisible(!signedIn);
    signedOutLabel.setText(signingIn ? "Signing in..." : "Sign in to see your plan and manage your account",
                           juce::dontSendNotification);
    signInButton.setVisible(!signedIn && !signingIn);

    emailLabel.setVisible(signedIn);
    emailLabel.setText(snapshot.email.isNotEmpty() ? snapshot.email : juce::String("Signed in"),
                       juce::dontSendNotification);
    emailLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));

    const juce::String planText = synth::PlanBadge::formatText(snapshot, true);
    planLabel.setVisible(signedIn);
    planLabel.setText(planText.isNotEmpty() ? planText : juce::String("Your plan isn't available right now."),
                      juce::dontSendNotification);

    const juce::String periodText = synth::PlanBadge::formatPeriodLine(snapshot);
    periodLabel.setVisible(signedIn && periodText.isNotEmpty());
    periodLabel.setText(periodText, juce::dontSendNotification);

    manageButton.setVisible(signedIn);
    manageButton.setButtonText(pro ? kManageText : kUpgradeText);
    manageButton.setTitle(pro ? kManageText : kUpgradeText);
    manageButton.setTooltip(pro ? "Opens the billing portal, where you can change payment details, see invoices or "
                                  "cancel"
                                : "Opens the checkout page in your browser");
    signOutButton.setVisible(signedIn);
    deleteButton.setVisible(signedIn);

    resized();
}

void AccountSettingsTab::resized() {
    auto area = getLocalBounds().reduced(10);
    titleLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);

    if (noticeLabel.isVisible()) {
        noticeLabel.setBounds(area.removeFromTop(kRowH));
        area.removeFromTop(8);
    }

    if (signedOutLabel.isVisible()) {
        signedOutLabel.setBounds(area.removeFromTop(kRowH));
        area.removeFromTop(10);
        signInButton.setBounds(area.removeFromTop(kButtonH).removeFromLeft(110));
        return;
    }

    emailLabel.setBounds(area.removeFromTop(kRowH));
    planLabel.setBounds(area.removeFromTop(kRowH));
    if (periodLabel.isVisible())
        periodLabel.setBounds(area.removeFromTop(kRowH));
    area.removeFromTop(14);

    auto actions = area.removeFromTop(kButtonH);
    manageButton.setBounds(actions.removeFromLeft(170));
    actions.removeFromLeft(8);
    signOutButton.setBounds(actions.removeFromLeft(100));

    area.removeFromTop(28);
    deleteButton.setBounds(area.removeFromTop(kButtonH).removeFromLeft(150));
}

std::unique_ptr<synth::AccountFlowPanel> AccountSettingsTab::buildFlowPanel(synth::AccountFlowPanel::Flow flow,
                                                                            juce::Component& opener) {
    synth::AccountFlowPanel::Services services;
    services.account = accountService;
    services.requests = &requests;
    services.openUrl = [this, safe = juce::Component::SafePointer<juce::Component>(this)](const juce::URL& url) {
        if (safe.getComponent() != nullptr)
            urlOpener(url);
        else
            url.launchInDefaultBrowser();
    };
    services.onAccountDeleted = [safe = juce::Component::SafePointer<AccountSettingsTab>(this)] {
        if (auto* tab = safe.getComponent()) {
            tab->accountDeleted = true;
            tab->syncFromSnapshot();
        }
    };
    services.opener = &opener;
    return std::make_unique<synth::AccountFlowPanel>(flow, std::move(services));
}

std::unique_ptr<synth::AccountFlowPanel> AccountSettingsTab::createFlowPanelForTest(synth::AccountFlowPanel::Flow flow,
                                                                                    juce::Component& opener) {
    return buildFlowPanel(flow, opener);
}

void AccountSettingsTab::openFlow(synth::AccountFlowPanel::Flow flow, juce::Component& opener) {
    if (accountService == nullptr)
        return;
    auto panel = buildFlowPanel(flow, opener);
    if (flowLauncher)
        flowLauncher(std::move(panel));
    else
        juce::CallOutBox::launchAsynchronously(std::move(panel), opener.getScreenBounds(), nullptr);
}
