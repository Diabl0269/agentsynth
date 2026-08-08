#pragma once

#include "../AI/AccountService.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

class SignInDialog;

/**
 * @class AccountRow
 * @brief Slim account-status row shown in the AI panel: "Sign in" when signed out, a greyed
 *        "Signing in..." label while a device-code flow is running, or the signed-in email (or
 *        "Signed in" if the /me fetch failed) plus "Sign out" once authenticated.
 *
 * Driven entirely by setAccountService(). With none attached — the default for every panel/test
 * that hasn't opted into accounts — the row stays invisible (zero preferred height) and does
 * nothing, so it can be added unconditionally to AIChatComponent's layout with no behavior
 * change for callers that never call setAccountService().
 *
 * Ownership note: this class deliberately does NOT set AccountService::onStateChanged /
 * onAccessTokenChanged itself. Those are single-slot std::function callbacks (not a multicast
 * listener list), and AIChatComponent already claims both slots — see
 * AIChatComponent::setAccountService()'s comment. refresh() is the one entry point a caller uses
 * to tell this row (and any currently-open SignInDialog launched from it) that the snapshot
 * changed.
 */
class AccountRow : public juce::Component {
public:
    AccountRow();
    ~AccountRow() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Non-owning, nullable. Pass nullptr to detach — the row goes invisible again.
    void setAccountService(AccountService* service);

    // Re-reads accountService->getSnapshot() and updates this row's UI, then forwards the same
    // notification to a currently-open SignInDialog (if this row launched one and it's still
    // open). Called by AIChatComponent from the single AccountService::onStateChanged callback
    // it owns.
    void refresh();

    // 0 when no AccountService is attached (row invisible, contributes nothing to layout);
    // a small fixed height otherwise. AIChatComponent::resized() uses this to reserve space
    // above the model-picker row only when the account UI is actually active.
    int getPreferredHeight() const { return accountService != nullptr ? 28 : 0; }

private:
    void updateFromSnapshot(const AccountSnapshot& snapshot);
    void launchSignInDialog();

    AccountService* accountService = nullptr;

    juce::TextButton signInButton;
    juce::TextButton signOutButton;
    juce::Label statusLabel; // email / "Signed in" / "Signing in..."

    // Non-owning; auto-nulls when the dialog (owned by its DialogWindow) is destroyed, whether
    // that happens via its own Cancel button, an auto-close on success, or the native title bar
    // — none of which route back through this class, so a raw pointer would dangle.
    juce::Component::SafePointer<SignInDialog> openDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccountRow)
};

} // namespace synth
