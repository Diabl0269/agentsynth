#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "Telemetry/TelemetryIdStore.h"
#include "Telemetry/UsageStatsChoice.h"
#include "UserSettings.h"

// Concern: the Privacy group -- the opt-in "Share anonymous usage statistics" toggle, the "What we collect" link
// and the id row shown only while opted in (docs/development/usage-statistics.md). The toggle is the writer of
// synth::kShareUsageStatsSettingKey; it also creates or deletes the id and queue files itself so the row and the
// opt-out promise hold even where no TelemetryService is running (a plugin build). Chained after the patch save
// location row in the setup chain and after the MIDI Remote group in the layout chain.

bool PreferencesSettingsTab::isShareUsageStatsEnabled() const { return shareUsageStatsToggle.getToggleState(); }

void PreferencesSettingsTab::setShareUsageStatsEnabled(bool enabled) {
    shareUsageStatsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistShareUsageStats(enabled);
}

/** The shared choice (also made by the Welcome screen's card): the setting and the answered flag are written, the
    id file appears at once on (only if there is none) so the row can show it, and off deletes the id and the unsent
    queue before this returns; the running service empties its memory when the settings change reaches it. */
void PreferencesSettingsTab::persistShareUsageStats(bool enabled) {
    synth::telemetry::applyShareUsageStatsChoice(*appProperties.getUserSettings(), enabled);
    refreshUsageStatsIdRow();
    resized();
}

void PreferencesSettingsTab::refreshUsageStatsIdRow() {
    const auto id =
        shareUsageStatsToggle.getToggleState() ? synth::telemetry::TelemetryIdStore().load() : juce::String();
    usageStatsIdLabel.setText(id.isEmpty() ? juce::String() : "Your usage statistics ID: " + id,
                              juce::dontSendNotification);
    usageStatsIdLabel.setTitle(usageStatsIdLabel.getText());
}

void PreferencesSettingsTab::setupUsageStatsControls() {
    contentHost.addAndMakeVisible(shareUsageStatsToggle);
    shareUsageStatsToggle.setTooltip("Sends a daily summary of which features you use, with no audio, projects, "
                                     "prompts, names or files. Off unless you turn it on.");
    shareUsageStatsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(synth::kShareUsageStatsSettingKey, false),
        juce::dontSendNotification);
    shareUsageStatsToggle.onClick = [this] { persistShareUsageStats(shareUsageStatsToggle.getToggleState()); };

    contentHost.addAndMakeVisible(usageStatsLearnMoreButton);
    usageStatsLearnMoreButton.setTitle("What we collect");
    usageStatsLearnMoreButton.setTooltip("Opens the privacy page that lists exactly what a usage summary holds.");
    usageStatsLearnMoreButton.onClick = [this] { urlOpener(juce::URL(synth::telemetry::kUsageStatsPrivacyUrl)); };

    contentHost.addAndMakeVisible(usageStatsIdLabel);
    usageStatsIdLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    usageStatsIdLabel.setTooltip("The random id usage summaries are filed under. It is not linked to your account "
                                 "or this computer, and it is deleted when you turn sharing off.");

    contentHost.addAndMakeVisible(usageStatsCopyIdButton);
    usageStatsCopyIdButton.setTitle("Copy usage statistics ID");
    usageStatsCopyIdButton.setTooltip("Copies your usage statistics ID, so you can quote it when asking for your "
                                      "data to be deleted.");
    usageStatsCopyIdButton.onClick = [this] { clipboardWriter(synth::telemetry::TelemetryIdStore().load()); };

    refreshUsageStatsIdRow();
    setupCategorySelector(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutUsageStatsGroup(int& y, int contentWidth, bool previousGroupWasVisible,
                                                   const GroupMatchFn& groupMatches,
                                                   const SetVisibleFn& setGroupVisible) {
    enterCategory(Category::Privacy, y);
    const std::initializer_list<juce::Component*> comps = {&shareUsageStatsToggle, &usageStatsLearnMoreButton};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    // The id row is part of the group but follows the toggle, not the search: no id exists while sharing is off.
    const bool showId = visible && shareUsageStatsToggle.getToggleState() && usageStatsIdLabel.getText().isNotEmpty();
    usageStatsIdLabel.setVisible(showId);
    usageStatsCopyIdButton.setVisible(showId);
    if (!visible)
        return;
    if (previousGroupWasVisible) {
        y += 10;
        dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
        y += 11;
    }
    shareUsageStatsToggle.setBounds(0, y, contentWidth, 24);
    y += 28;
    usageStatsLearnMoreButton.setBounds(0, y, 120, 24);
    y += 24;
    if (!showId)
        return;
    y += 12;
    usageStatsIdLabel.setBounds(0, y, contentWidth, 24);
    y += 28;
    usageStatsCopyIdButton.setBounds(0, y, 80, 24);
    y += 24;
}
