// PreferencesSettingsTabAnimationModeTests.cpp (docs/layout/animation.md#reduced-motion): the "Animations" combo --
// default Follow system, changing it persists "animationMode" and applies the mode app-wide at once, and a fresh tab
// shows the saved choice.
#include "PreferencesSettingsTabTestFixture.h"
#include "UI/Layout/ReducedMotion.h"

namespace {
using synth::ui::AnimationMode;

// The mode is process-wide: put it back after every test here.
struct ModeGuard {
    ~ModeGuard() { synth::ui::setAnimationMode(AnimationMode::followSystem); }
};
} // namespace

TEST_F(PreferencesSettingsTabTest, AnimationsDefaultsToFollowSystem) {
    ModeGuard guard;
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getAnimationMode(), AnimationMode::followSystem);
}

TEST_F(PreferencesSettingsTabTest, ChoosingAnAnimationsItemPersistsAndAppliesTheMode) {
    ModeGuard guard;
    PreferencesSettingsTab tab(appProperties);
    juce::ComboBox* combo = nullptr;
    for (auto* child : descendantsOf(tab))
        if (auto* c = dynamic_cast<juce::ComboBox*>(child); c != nullptr && c->getTitle() == "Animations")
            combo = c;
    ASSERT_NE(combo, nullptr);

    combo->setSelectedId(4, juce::sendNotificationSync); // Off
    EXPECT_EQ(appProperties.getUserSettings()->getValue("animationMode"), "off");
    EXPECT_EQ(synth::ui::animationMode(), AnimationMode::off);

    combo->setSelectedId(3, juce::sendNotificationSync); // Reduced
    EXPECT_EQ(appProperties.getUserSettings()->getValue("animationMode"), "reduced");
    EXPECT_EQ(synth::ui::animationMode(), AnimationMode::reduced);

    combo->setSelectedId(2, juce::sendNotificationSync); // Full
    EXPECT_EQ(appProperties.getUserSettings()->getValue("animationMode"), "full");
    EXPECT_EQ(synth::ui::animationMode(), AnimationMode::full);

    combo->setSelectedId(1, juce::sendNotificationSync); // Follow system
    EXPECT_EQ(appProperties.getUserSettings()->getValue("animationMode"), "follow");
    EXPECT_EQ(synth::ui::animationMode(), AnimationMode::followSystem);
}

TEST_F(PreferencesSettingsTabTest, AnimationsChoiceSurvivesAFreshTabAndUnknownValuesFollowTheSystem) {
    ModeGuard guard;
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setAnimationMode(AnimationMode::reduced);
    }
    PreferencesSettingsTab tab2(appProperties);
    EXPECT_EQ(tab2.getAnimationMode(), AnimationMode::reduced);

    appProperties.getUserSettings()->setValue("animationMode", "notARealValue");
    PreferencesSettingsTab tab3(appProperties);
    EXPECT_EQ(tab3.getAnimationMode(), AnimationMode::followSystem);
}
