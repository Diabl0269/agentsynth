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
            if (s->containsKey(synth::theme::knobColourByFamilyKey()))
                family_ = s->getBoolValue(synth::theme::knobColourByFamilyKey());
        }
    }
    ~PersistedKnobKeysGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            style_ ? s->setValue(synth::theme::knobStyleKey(), *style_) : s->removeValue(synth::theme::knobStyleKey());
            family_ ? s->setValue(synth::theme::knobColourByFamilyKey(), *family_)
                    : s->removeValue(synth::theme::knobColourByFamilyKey());
            s->saveIfNeeded();
        }
    }

private:
    std::optional<juce::String> style_;
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
