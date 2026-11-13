#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the "Duplicate points" combo (Timeline; docs/timeline/automation-points.md#duplicating) -- where Cmd+D puts
// the copies of selected points in the automation lanes and the curve editor. The choice is persisted under
// synth::ui::kDuplicatePlacementKey and applied process-wide through synth::ui::setDuplicatePlacement at once;
// MainComponent applies the saved value at launch (applyTimelineTransportPreferences).

namespace {
constexpr int kAfterLastComboId = 1;
constexpr int kSpacingComboId = 2;
constexpr int kGridComboId = 3;

int comboIdFor(synth::ui::DuplicatePlacement placement) {
    switch (placement) {
    case synth::ui::DuplicatePlacement::onePointSpacing:
        return kSpacingComboId;
    case synth::ui::DuplicatePlacement::nextGridLine:
        return kGridComboId;
    case synth::ui::DuplicatePlacement::afterLast:
        break;
    }
    return kAfterLastComboId;
}

synth::ui::DuplicatePlacement placementForComboId(int id) {
    switch (id) {
    case kSpacingComboId:
        return synth::ui::DuplicatePlacement::onePointSpacing;
    case kGridComboId:
        return synth::ui::DuplicatePlacement::nextGridLine;
    default:
        return synth::ui::DuplicatePlacement::afterLast;
    }
}
} // namespace

synth::ui::DuplicatePlacement PreferencesSettingsTab::getDuplicatePlacement() const {
    return placementForComboId(duplicatePointsCombo.getSelectedId());
}

void PreferencesSettingsTab::setDuplicatePlacement(synth::ui::DuplicatePlacement placement) {
    duplicatePointsCombo.setSelectedId(comboIdFor(placement), juce::dontSendNotification);
    persistDuplicatePlacement(placement);
}

void PreferencesSettingsTab::persistDuplicatePlacement(synth::ui::DuplicatePlacement placement) {
    appProperties.getUserSettings()->setValue(synth::ui::kDuplicatePlacementKey,
                                              synth::ui::duplicatePlacementToString(placement));
    appProperties.getUserSettings()->saveIfNeeded();
    synth::ui::setDuplicatePlacement(placement);
}

void PreferencesSettingsTab::setupDuplicatePointsControls() {
    contentHost.addAndMakeVisible(duplicatePointsLabel);
    duplicatePointsLabel.setText("Duplicate points:", juce::dontSendNotification);
    duplicatePointsLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(duplicatePointsCombo);
    duplicatePointsCombo.setTitle("Duplicate points");
    duplicatePointsCombo.setTooltip(
        "Where Cmd+D puts the copies of selected automation or curve points. Right after the last point keeps "
        "their spacing and starts one tick past the last one. One point-spacing after starts one gap past it, so "
        "repeated duplicates run evenly. At the next grid line after the selection moves the copies by whole grid "
        "steps (the same as the first choice with snap off). One selected point is copied one grid step on.");
    duplicatePointsCombo.addItem("Right after the last point", kAfterLastComboId);
    duplicatePointsCombo.addItem("One point-spacing after", kSpacingComboId);
    duplicatePointsCombo.addItem("At the next grid line after the selection", kGridComboId);
    // Show the saved value without re-applying it: MainComponent already applied it at launch, and a tab opened in a
    // test must not change the process-wide choice just by being built.
    duplicatePointsCombo.setSelectedId(
        comboIdFor(synth::ui::duplicatePlacementFromString(
            appProperties.getUserSettings()->getValue(synth::ui::kDuplicatePlacementKey, "afterLast"))),
        juce::dontSendNotification);
    duplicatePointsCombo.onChange = [this] { persistDuplicatePlacement(getDuplicatePlacement()); };
}
