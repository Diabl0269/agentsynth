// MeterColourStopsSettingsIntegrationTests.cpp -- FRO147: proves the actual live-apply wire, not
// just its two halves in isolation. AppearanceSettingsTab has no direct pointer to the live
// meters (see its own FRO147 comment); instead MainComponent::changeListenerCallback's
// `source == appProperties.getUserSettings()` branch re-reads "meterColourStops" on EVERY settings
// write and pushes it into the shared AppLookAndFeel (MainComponentCallbacks.cpp). This drives that
// exact path end to end on a real MainComponent: write the key the same way
// AppearanceSettingsTab::applyMeterColourStopsChange() does, pump the message loop for the
// ChangeBroadcaster's async notification, and assert the SAME AppLookAndFeel instance every meter
// painter reads (getLookAndFeelForTest()) picked it up.
#include "MainComponentTestFixture.h"
#include "UI/Mixer/MeterColourStops.h"
#include "UI/Theme/BuiltInThemes.h"
#include <optional>

namespace {

// This test writes "meterColourStops" into the SAME real "Agent Synth" settings file every
// MainComponent in this process (and a real shipped build) reads -- same idiom
// DetachRedockStateTests.cpp/MixerDockMeterGatingTests.cpp use for their own persisted keys.
class PersistedMeterColourStopsKeyGuard {
public:
    PersistedMeterColourStopsKeyGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(options());
        if (auto* settings = props.getUserSettings()) {
            if (settings->containsKey(synth::ui::meterColourStopsKey()))
                saved_ = settings->getValue(synth::ui::meterColourStopsKey());
        }
    }

    ~PersistedMeterColourStopsKeyGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(options());
        if (auto* settings = props.getUserSettings()) {
            if (saved_.has_value())
                settings->setValue(synth::ui::meterColourStopsKey(), *saved_);
            else
                settings->removeValue(synth::ui::meterColourStopsKey());
            settings->saveIfNeeded();
        }
    }

private:
    static juce::PropertiesFile::Options options() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        return opts;
    }

    std::optional<juce::String> saved_;
};

} // namespace

TEST_F(MainComponentTest, WritingTheSettingsKeyPushesTheOverrideIntoTheSharedLookAndFeel) {
    PersistedMeterColourStopsKeyGuard guard;
    MainComponent mc(std::make_unique<MockProvider>());
    ASSERT_FALSE(mc.getLookAndFeelForTest().hasMeterColourStopsOverride())
        << "a fresh instance with no persisted key must start on theme defaults";

    const synth::ui::MeterColourStops custom({{synth::ui::kMeterMinDb, juce::Colour(0xff102030)}});
    synth::ui::saveMeterColourStopsOverride(*mc.getAppPropertiesForTest().getUserSettings(), custom);

    // The write's ChangeBroadcaster notification is async (juce::ChangeBroadcaster::
    // sendChangeMessage) -- pump the message loop for it to actually reach
    // MainComponent::changeListenerCallback.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_TRUE(mc.getLookAndFeelForTest().hasMeterColourStopsOverride());
    EXPECT_EQ(mc.getLookAndFeelForTest().getMeterColourStops().colourForDb(-30.0f), juce::Colour(0xff102030));
}

TEST_F(MainComponentTest, ClearingTheSettingsKeyRevertsTheSharedLookAndFeelToTheTheme) {
    PersistedMeterColourStopsKeyGuard guard;
    MainComponent mc(std::make_unique<MockProvider>());

    synth::ui::saveMeterColourStopsOverride(
        *mc.getAppPropertiesForTest().getUserSettings(),
        synth::ui::MeterColourStops({{synth::ui::kMeterMinDb, juce::Colour(0xff102030)}}));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc.getLookAndFeelForTest().hasMeterColourStopsOverride());

    synth::ui::clearMeterColourStopsOverride(*mc.getAppPropertiesForTest().getUserSettings());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_FALSE(mc.getLookAndFeelForTest().hasMeterColourStopsOverride());
    const auto expected = synth::ui::MeterColourStops::fromTheme(mc.getLookAndFeelForTest().getTheme().colors);
    EXPECT_EQ(mc.getLookAndFeelForTest().getMeterColourStops().colourForDb(-30.0f), expected.colourForDb(-30.0f));
}
