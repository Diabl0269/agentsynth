// KnobStyleSettingsIntegrationTests.cpp -- the live-apply wire for the knob style, mirroring
// MeterColourStopsSettingsIntegrationTests.cpp: a write to the settings file reaches the shared
// AppLookAndFeel through MainComponent's settings-changed branch.
#include "MainComponentTestFixture.h"
#include "UI/Theme/KnobStyle.h"
#include "UserSettings.h"
#include <optional>

namespace {

// The test writes the knob keys into the same real settings file every MainComponent reads.
class PersistedKnobKeysGuard {
public:
    PersistedKnobKeysGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            if (s->containsKey(synth::theme::knobStyleKey()))
                style_ = s->getValue(synth::theme::knobStyleKey());
            if (s->containsKey(synth::theme::faderStyleKey()))
                fader_ = s->getValue(synth::theme::faderStyleKey());
            if (s->containsKey(synth::theme::knobColourByFamilyKey()))
                family_ = s->getBoolValue(synth::theme::knobColourByFamilyKey());
        }
    }
    ~PersistedKnobKeysGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            style_ ? s->setValue(synth::theme::knobStyleKey(), *style_) : s->removeValue(synth::theme::knobStyleKey());
            fader_ ? s->setValue(synth::theme::faderStyleKey(), *fader_)
                   : s->removeValue(synth::theme::faderStyleKey());
            family_ ? s->setValue(synth::theme::knobColourByFamilyKey(), *family_)
                    : s->removeValue(synth::theme::knobColourByFamilyKey());
            s->saveIfNeeded();
        }
    }

private:
    std::optional<juce::String> style_;
    std::optional<juce::String> fader_;
    std::optional<bool> family_;
};

} // namespace

TEST_F(MainComponentTest, WritingTheKnobKeysPushesTheAppearanceIntoTheSharedLookAndFeel) {
    PersistedKnobKeysGuard guard;
    MainComponent mc(std::make_unique<MockProvider>());

    synth::theme::writeKnobAppearance(*mc.getAppPropertiesForTest().getUserSettings(),
                                      {synth::theme::KnobStyle::Ring, false});
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(mc.getLookAndFeelForTest().getKnobAppearance().style, synth::theme::KnobStyle::Ring);
    EXPECT_FALSE(mc.getLookAndFeelForTest().getKnobAppearance().colourByFamily);
}

TEST_F(MainComponentTest, WritingOnlyTheFaderStyleReachesTheSharedLookAndFeel) {
    PersistedKnobKeysGuard guard;
    MainComponent mc(std::make_unique<MockProvider>());

    auto* settings = mc.getAppPropertiesForTest().getUserSettings();
    settings->setValue(synth::theme::knobStyleKey(), "classic");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    settings->setValue(synth::theme::faderStyleKey(), "neon"); // only the fader key changes
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(mc.getLookAndFeelForTest().getKnobAppearance().style, synth::theme::KnobStyle::Classic);
    EXPECT_EQ(mc.getLookAndFeelForTest().getKnobAppearance().faderStyle, synth::theme::KnobStyle::Neon);
}
