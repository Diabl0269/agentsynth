#include "PlanBadge.h"

namespace synth {

PlanBadge::PlanBadge() {
    addChildComponent(textLabel);
    textLabel.setJustificationType(juce::Justification::centredLeft);
    textLabel.setMinimumHorizontalScale(1.0f);
    textLabel.setFont(juce::Font(12.0f));

    // Invisible/zero-height until setAccountService() attaches a real service with a known
    // entitlement — mirrors AccountRow's default state for every existing caller/test.
    setVisible(false);
}

PlanBadge::~PlanBadge() = default;

void PlanBadge::setAccountService(AccountService* service) {
    accountService = service;

    if (accountService == nullptr) {
        hasContent = false;
        setVisible(false);
        return;
    }

    // Synchronous, not routed through onStateChanged — this badge does not own that callback slot
    // (AIChatComponent does, same as AccountRow) — see the class comment.
    updateFromSnapshot(accountService->getSnapshot());
}

void PlanBadge::refresh() {
    if (accountService == nullptr)
        return;

    updateFromSnapshot(accountService->getSnapshot());
}

int PlanBadge::getPreferredHeight() const { return hasContent ? 18 : 0; }

void PlanBadge::updateFromSnapshot(const AccountSnapshot& snapshot) {
    hasContent = snapshot.state == AccountState::SignedIn && snapshot.entitlementKnown;
    setVisible(hasContent);
    textLabel.setVisible(hasContent);

    if (!hasContent) {
        if (auto* parent = getParentComponent())
            parent->resized();
        return;
    }

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const bool isPro = isProPlan(snapshot);
    const juce::Colour textColour = lf != nullptr
                                        ? (isPro ? lf->getTheme().colors.accent : lf->getTheme().colors.textMuted)
                                        : (isPro ? juce::Colours::lightblue : juce::Colours::grey);
    textLabel.setColour(juce::Label::textColourId, textColour);

    const juce::String planLabel = isPro ? "Pro" : "Free";
    textLabel.setText(planLabel + " - " + juce::String(snapshot.requestsUsed) + " / " +
                          juce::String(snapshot.monthlyRequestLimit) + " this month",
                      juce::dontSendNotification);

    resized();
    if (auto* parent = getParentComponent())
        parent->resized();
}

void PlanBadge::paint(juce::Graphics&) {
    // No background of its own — sits directly on AIChatComponent's already-painted panel, same
    // as AccountRow.
}

void PlanBadge::resized() { textLabel.setBounds(getLocalBounds()); }

} // namespace synth
