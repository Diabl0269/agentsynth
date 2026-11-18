#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "Telemetry/UsageStatsChoice.h"
#include "UI/Chrome/UsageStatsCardSlot.h"
#include "UI/Chrome/UsageStatsPromptComponent.h"
#include "UserSettings.h"

// Concern: the usage statistics question pinned above the category picker (the same UsageStatsPromptComponent the
// Welcome screen shows, in the same UsageStatsCardSlot, so both ask the same way and animate the same way). It is
// open while the question is unanswered and the owner allows it (the standalone app; a plugin records nothing, so
// it never asks there). An answer goes through the one writer (PreferencesSettingsTab::persistShareUsageStats ->
// applyShareUsageStatsChoice), which also flips the toggle below; an answer given elsewhere reaches this tab as a
// settings change (syncUsageStatsFromSettings). docs/development/usage-statistics.md. Chained from
// setupUsageStatsControls().

bool PreferencesSettingsTab::isUsageStatsCardShown() const { return usageStatsSlot_->isShown(); }

synth::ui::UsageStatsPromptComponent& PreferencesSettingsTab::getUsageStatsCardForTest() {
    return usageStatsSlot_->prompt();
}

int PreferencesSettingsTab::getUsageStatsCardSlotHeightForTest() const { return usageStatsSlot_->slotHeight(); }

void PreferencesSettingsTab::setupUsageStatsCard() {
    usageStatsSlot_ = std::make_unique<synth::ui::UsageStatsCardSlot>(*this, [this] {
        resized();
        repaint();
    });
    addChildComponent(usageStatsSlot_->prompt());
    usageStatsSlot_->prompt().setComponentID("preferencesUsageStatsCard"); // not the Welcome screen's copy
    usageStatsSlot_->prompt().toBack(); // first in the Tab order: the question sits above everything else
    usageStatsSlot_->onChoice = [this](bool share) {
        shareUsageStatsToggle.setToggleState(share, juce::dontSendNotification);
        persistShareUsageStats(share);
    };
    usageStatsSlot_->onLearnMore = [this] { urlOpener(juce::URL(synth::telemetry::kUsageStatsPrivacyUrl)); };
    // Keyboard focus lands on the toggle the answer just changed, not nowhere.
    usageStatsSlot_->onFocusRescue = [this] { shareUsageStatsToggle.grabKeyboardFocus(); };
}

void PreferencesSettingsTab::setUsageStatsCardAllowed(bool allowed) {
    usageStatsCardAllowed_ = allowed;
    if (usageStatsSlot_ == nullptr)
        return;
    usageStatsSlot_->setShown(allowed && synth::telemetry::shouldAskAboutUsageStats(appProperties.getUserSettings()));
}

void PreferencesSettingsTab::syncUsageStatsFromSettings() {
    auto* settings = appProperties.getUserSettings();
    if (settings == nullptr)
        return;
    // Answered in the Welcome screen: the toggle follows the stored choice and the card goes away.
    const bool on = settings->getBoolValue(synth::kShareUsageStatsSettingKey, false);
    if (on != shareUsageStatsToggle.getToggleState()) {
        shareUsageStatsToggle.setToggleState(on, juce::dontSendNotification);
        refreshUsageStatsIdRow();
        resized();
    }
    if (usageStatsSlot_ != nullptr && !synth::telemetry::shouldAskAboutUsageStats(settings))
        usageStatsSlot_->setShown(false);
}
