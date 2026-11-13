#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the "Animations" combo (docs/layout/animation.md#reduced-motion; Panels & Windows) -- Follow system (macOS
// Reduce Motion decides, default) / Full / Reduced / Off. The choice is persisted under synth::ui::kAnimationModeKey
// and applied process-wide through synth::ui::setAnimationMode at once; MainComponent applies the saved value at
// launch. Chained between the panel-detach-mode row and the "Show info tooltips" row.

namespace {
constexpr int kAnimationFollowComboId = 1;
constexpr int kAnimationFullComboId = 2;
constexpr int kAnimationReducedComboId = 3;
constexpr int kAnimationOffComboId = 4;

int comboIdFor(synth::ui::AnimationMode mode) {
    switch (mode) {
    case synth::ui::AnimationMode::full:
        return kAnimationFullComboId;
    case synth::ui::AnimationMode::reduced:
        return kAnimationReducedComboId;
    case synth::ui::AnimationMode::off:
        return kAnimationOffComboId;
    case synth::ui::AnimationMode::followSystem:
        break;
    }
    return kAnimationFollowComboId;
}

synth::ui::AnimationMode modeForComboId(int id) {
    switch (id) {
    case kAnimationFullComboId:
        return synth::ui::AnimationMode::full;
    case kAnimationReducedComboId:
        return synth::ui::AnimationMode::reduced;
    case kAnimationOffComboId:
        return synth::ui::AnimationMode::off;
    default:
        return synth::ui::AnimationMode::followSystem;
    }
}
} // namespace

synth::ui::AnimationMode PreferencesSettingsTab::getAnimationMode() const {
    return modeForComboId(animationModeCombo.getSelectedId());
}

void PreferencesSettingsTab::setAnimationMode(synth::ui::AnimationMode mode) {
    animationModeCombo.setSelectedId(comboIdFor(mode), juce::dontSendNotification);
    persistAnimationMode(mode);
}

void PreferencesSettingsTab::persistAnimationMode(synth::ui::AnimationMode mode) {
    appProperties.getUserSettings()->setValue(synth::ui::kAnimationModeKey, synth::ui::animationModeToString(mode));
    appProperties.getUserSettings()->saveIfNeeded();
    synth::ui::setAnimationMode(mode);
}

void PreferencesSettingsTab::setupAnimationModeControls() {
    contentHost.addAndMakeVisible(animationModeLabel);
    animationModeLabel.setText("Animations:", juce::dontSendNotification);
    animationModeLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(animationModeCombo);
    animationModeCombo.setTitle("Animations");
    animationModeCombo.setTooltip(
        "How much the app animates. Reduced keeps short fades; Off makes everything instant.");
    animationModeCombo.addItem("Follow system (macOS Reduce Motion)", kAnimationFollowComboId);
    animationModeCombo.addItem("Full", kAnimationFullComboId);
    animationModeCombo.addItem("Reduced", kAnimationReducedComboId);
    animationModeCombo.addItem("Off", kAnimationOffComboId);
    // Show the saved value without re-applying it: MainComponent already applied it at launch, and a tab opened in a
    // test must not change the process-wide mode just by being built.
    animationModeCombo.setSelectedId(
        comboIdFor(synth::ui::animationModeFromString(
            appProperties.getUserSettings()->getValue(synth::ui::kAnimationModeKey, "follow"))),
        juce::dontSendNotification);
    animationModeCombo.onChange = [this] { persistAnimationMode(getAnimationMode()); };
    setupInfoTooltipsControls(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutAnimationModeGroup(
    int& y, int contentWidth, bool previousGroupWasVisible,
    const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
    const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible) {
    enterCategory(Category::Panels, y);
    const std::initializer_list<juce::Component*> comps = {&animationModeLabel, &animationModeCombo};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    const auto chainNext = [&] {
        layoutInfoTooltipsGroup(y, contentWidth, visible || previousGroupWasVisible, groupMatches, setGroupVisible);
    };
    if (!visible) {
        chainNext();
        return;
    }
    if (previousGroupWasVisible) {
        // Same divider math as layoutPanelDetachModeGroup().
        y += 10;
        dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
        y += 11;
    }
    juce::Rectangle<int> row(0, y, contentWidth, 24);
    animationModeLabel.setBounds(row.removeFromLeft(230));
    row.removeFromLeft(4);
    animationModeCombo.setBounds(row.removeFromLeft(240));
    y += 24;
    chainNext();
}
