#include "PlanBadge.h"

namespace synth {

PlanBadge::PlanBadge() {
    addAndMakeVisible(textLabel); // the badge itself is what shows and hides
    textLabel.setJustificationType(juce::Justification::centredLeft);
    textLabel.setMinimumHorizontalScale(1.0f);
    textLabel.setFont(juce::Font(12.0f));

    // Invisible/zero-height until setAccountService() attaches a real service with a known
    // entitlement — mirrors AccountRow's default state for every existing caller/test.
    setVisible(false);
    fade_.onFrame = [this] {
        if (auto* parent = getParentComponent())
            parent->resized();
    };
}

PlanBadge::~PlanBadge() = default;

void PlanBadge::setAccountService(AccountService* service) {
    accountService = service;

    if (accountService == nullptr) {
        hasContent = false;
        fade_.setShown(false);
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

int PlanBadge::getPreferredHeight() const {
    // Visible covers the fade-out too, so the strip closes over the fade instead of snapping shut.
    if (!isVisible())
        return 0;
    return juce::jmax(1, juce::roundToInt(18.0f * fade_.progress()));
}

void PlanBadge::updateFromSnapshot(const AccountSnapshot& snapshot) {
    hasContent = snapshot.state == AccountState::SignedIn && snapshot.entitlementKnown;
    if (!hasContent) {
        fade_.setShown(false);
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
    textLabel.setText(planLabel + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(snapshot.requestsUsed) + " / " +
                          juce::String(snapshot.monthlyRequestLimit) + " this month",
                      juce::dontSendNotification);

    fade_.setShown(true);
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
