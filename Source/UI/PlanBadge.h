#pragma once

#include "../AI/AccountService.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/**
 * @class PlanBadge
 * @brief Slim usage indicator shown near the model picker: "Free · 240 / 1000 this month" or
 *        "Pro · 1,203 / 10,000 this month", sourced from AccountService's entitlement fields
 *        (P4-4).
 *
 * Driven entirely by setAccountService()/refresh() — same contract as AccountRow (see its class
 * comment): with no AccountService attached, or one attached but not yet SignedIn with a known
 * entitlement, this stays invisible with zero preferred height, so it can be added unconditionally
 * to AIChatComponent's layout with no behavior change for callers that never opt in.
 *
 * Like AccountRow, this class does NOT set AccountService::onStateChanged itself — that slot is
 * single-owner and AIChatComponent already claims it (see its setAccountService() comment).
 * refresh() is the one entry point a caller uses to tell this badge the snapshot changed.
 */
class PlanBadge : public juce::Component {
public:
    PlanBadge();
    ~PlanBadge() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Non-owning, nullable. Pass nullptr to detach — the badge goes invisible again.
    void setAccountService(AccountService* service);

    // Re-reads accountService->getSnapshot() and updates this badge's text/visibility. Called by
    // AIChatComponent from the single AccountService::onStateChanged callback it owns.
    void refresh();

    // 0 when no AccountService is attached, or entitlement isn't known yet (not signed in, or the
    // entitlement fetch hasn't completed/failed) — badge invisible, contributes nothing to
    // layout. AIChatComponent::resized() uses this to reserve space only when there is something
    // to show.
    int getPreferredHeight() const;

private:
    void updateFromSnapshot(const AccountSnapshot& snapshot);

    AccountService* accountService = nullptr;
    bool hasContent = false;

    juce::Label textLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlanBadge)
};

} // namespace synth
