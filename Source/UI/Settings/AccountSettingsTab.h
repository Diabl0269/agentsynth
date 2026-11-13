#pragma once

#include "AI/AccountService.h"
#include "AccountFlowPanel.h"
#include "AccountRequests.h"
#include "UI/Assistant/SignInDialog.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// The Settings "Account" tab: who is signed in, the plan and this month's usage, when the plan renews or
// ends, and the buttons that act on it (Sign in / Sign out, Manage subscription or Upgrade to Pro, Delete
// account). Manage subscription and Delete account open an AccountFlowPanel popover from their button.
//
// The tab does not own AccountService::onStateChanged (the AI chat does), so it watches the published snapshot
// with a light timer that runs only while the tab is showing and repaints only when something changed.
//
// NOTE: AccountSettingsTab.cpp, AccountFlowPanel.cpp, AccountRequests.cpp and LeavingSurveyPanel.cpp MUST be in
// BOTH the app target and the test target (cmake/AppUISources.cmake).
class AccountSettingsTab
    : public juce::Component
    , private juce::Timer {
public:
    // `accountService` is nullable: without one the tab shows the signed-out line and its buttons do nothing.
    explicit AccountSettingsTab(synth::AccountService* accountService);
    ~AccountSettingsTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;

    // Re-reads the snapshot and updates the tab if anything shown changed. The timer calls it.
    void syncFromSnapshot();

    // Testing hooks -----------------------------------------------------
    void setUrlOpenerForTesting(std::function<void(const juce::URL&)> opener) { urlOpener = std::move(opener); }
    void setHttpPerformerForTesting(synth::AuthClient::HttpPerformer performer) {
        requests.setHttpPerformerForTesting(std::move(performer));
    }
    void setRunRequestsInlineForTesting(bool inlineRun) { requests.setRunInlineForTesting(inlineRun); }
    // Replaces opening the popover (a CallOutBox needs a desktop): receives the panel the click would show.
    void setFlowLauncherForTesting(std::function<void(std::unique_ptr<synth::AccountFlowPanel>)> launcher) {
        flowLauncher = std::move(launcher);
    }
    // Replaces juce::Process::isForegroundProcess for the return-to-the-app refresh.
    void setForegroundCheckForTesting(std::function<bool()> check) { foregroundCheck = std::move(check); }
    // Runs one foreground check, as the timer does while the tab is showing.
    void pollForegroundForTest() { refreshOnReturnToForeground(); }
    std::unique_ptr<synth::AccountFlowPanel> createFlowPanelForTest(synth::AccountFlowPanel::Flow flow,
                                                                    juce::Component& opener);

    juce::TextButton& getSignInButtonForTest() { return signInButton; }
    juce::TextButton& getSignOutButtonForTest() { return signOutButton; }
    juce::TextButton& getManageButtonForTest() { return manageButton; }
    juce::TextButton& getDeleteButtonForTest() { return deleteButton; }
    juce::String getEmailTextForTest() const { return emailLabel.getText(); }
    juce::String getPlanTextForTest() const { return planLabel.getText(); }
    juce::String getPeriodTextForTest() const { return periodLabel.getText(); }
    juce::String getSignedOutTextForTest() const { return signedOutLabel.getText(); }
    juce::String getNoticeTextForTest() const { return noticeLabel.getText(); }

private:
    void timerCallback() override {
        syncFromSnapshot();
        refreshOnReturnToForeground();
    }
    // Re-fetches the plan when the app comes back to the foreground (a cancel or upgrade done in the browser).
    void refreshOnReturnToForeground();
    void refreshEntitlementIfSignedIn();
    void applySnapshot(const synth::AccountSnapshot& snapshot);
    void openFlow(synth::AccountFlowPanel::Flow flow, juce::Component& opener);
    std::unique_ptr<synth::AccountFlowPanel> buildFlowPanel(synth::AccountFlowPanel::Flow flow,
                                                            juce::Component& opener);

    synth::AccountService* accountService = nullptr;
    synth::AccountRequests requests;
    std::function<void(const juce::URL&)> urlOpener = [](const juce::URL& u) { u.launchInDefaultBrowser(); };

    std::function<void(std::unique_ptr<synth::AccountFlowPanel>)> flowLauncher;
    std::function<bool()> foregroundCheck = [] { return juce::Process::isForegroundProcess(); };
    bool wasForeground = true;

    juce::String lastSignature;
    bool accountDeleted = false;
    juce::Component::SafePointer<synth::SignInDialog> openDialog;

    juce::Label titleLabel;
    juce::Label noticeLabel;
    juce::Label signedOutLabel;
    juce::TextButton signInButton{"Sign in"};
    juce::Label emailLabel;
    juce::Label planLabel;
    juce::Label periodLabel;
    juce::TextButton manageButton{"Manage subscription"};
    juce::TextButton signOutButton{"Sign out"};
    juce::TextButton deleteButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccountSettingsTab)
};
