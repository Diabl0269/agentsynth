// Concern: FRO13 (P9-7, docs/mixer/track-presets.md#saving-and-setting-a-default) -- the per-type default track preset consulted
// by "+ Track -> Audio Track" (MainComponentTrackCreation.cpp's addAudioTrack) BEFORE the factory
// Track Audio -> EQ -> Compressor -> Strip -> Master chain is built. Unlike TrackPresetTests.cpp /
// TrackPresetCaptureTests.cpp (pure TrackPresetManager, no MainComponent needed), this exercises
// the real settings key (mixerDefaultTrackPresetAudio) AND the real on-disk preset directory
// (TrackPresetManager::getDefaultTrackPresetsDirectory(), hardcoded at the addAudioTrack call
// site -- there is no injectable override), so this file follows AutosaveTests.cpp's own posture
// toward that shared, real, process-global state: a distinctive-enough name to not collide with a
// real saved preset, and a reset in SetUp AND TearDown so no other test file sees what's left.

#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "TrackPresetTestFixture.h"

#include <gtest/gtest.h>

namespace {

// Same shared "Agent Synth" settings file every other MainComponent test resets around itself;
// mixerDefaultTrackPresetAudio/Instrument are FRO13's own two keys
// (PreferencesSettingsTabInternal.h / MainComponentTrackCreation.cpp -- both copies must agree).
constexpr const char* kMixerDefaultTrackPresetAudioKey = "mixerDefaultTrackPresetAudio";
// A preset name no real user or other test is plausibly using, so a stray leftover file can never
// be confused with real saved data.
constexpr const char* kTestPresetName = "__FRO13_Test_Default_Audio_Preset__";

} // namespace

class TrackPresetDefaultsTest : public MainComponentTest {
protected:
    void resetDefaultKey() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->removeValue(kMixerDefaultTrackPresetAudioKey);
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        MainComponentTest::SetUp();
        resetDefaultKey();
        synth::TrackPresetManager::deleteTrackPreset(synth::TrackPresetManager::getDefaultTrackPresetsDirectory(),
                                                     kTestPresetName);
    }
    void TearDown() override {
        synth::TrackPresetManager::deleteTrackPreset(synth::TrackPresetManager::getDefaultTrackPresetsDirectory(),
                                                     kTestPresetName);
        resetDefaultKey();
        MainComponentTest::TearDown();
    }
};

TEST_F(TrackPresetDefaultsTest, PerTypeDefaultUsedByPlusTrackAudio) {
    // Build and save a distinctive preset (it carries a bare Oscillator -- the factory "+ Track ->
    // Audio Track" chain, Track Audio -> EQ -> Compressor -> Strip, never contains one) to the REAL
    // default track-presets directory, entirely independent of any MainComponent.
    {
        HostedPatchCFT patch;
        GraphEditor editor(patch.engine);
        const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
        ASSERT_NE(rig.macro, nullptr);
        auto preset = synth::TrackPresetManager::extractTrackPreset(
            patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Audio, kTestPresetName);
        ASSERT_TRUE(preset.isObject());
        ASSERT_TRUE(synth::TrackPresetManager::saveTrackPreset(
            synth::TrackPresetManager::getDefaultTrackPresetsDirectory(), kTestPresetName, preset));
    }

    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    mc->setSize(1600, 900);
    mc->getAudioEngine().suspendDeviceCallback();
    mc->getAppPropertiesForTest().getUserSettings()->setValue(kMixerDefaultTrackPresetAudioKey, kTestPresetName);

    const auto tracksBefore = mc->getTimelineDoc().getTracks().size();
    // AudioEngine::initialise() falls back to a real starter patch (PresetManager::
    // loadDefaultPreset, or createDefaultPatch()) that already contains its own Oscillator, so a
    // bare "does an Oscillator exist" check would pass even if the default track preset were never
    // consulted at all. Count before/after and require exactly one MORE, so this test is actually
    // falsifiable against the factory Track Audio -> EQ -> Compressor chain (which adds none).
    const auto oscillatorsBefore = countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::Oscillator);

    // Same headless "+ Track -> Audio Track" seam ChannelFlowTest::addAudioTrack uses internally
    // (that helper is protected on ChannelFlowTest, not reachable from this unrelated fixture).
    mc->getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);

    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), tracksBefore + 1);
    EXPECT_EQ(countNodesOfTypeCFT(mc->getAudioEngine().getGraph(), ModuleType::Oscillator), oscillatorsBefore + 1)
        << "the default preset's Oscillator must be what + Track -> Audio Track actually inserted, "
           "not the factory Track Audio -> EQ -> Compressor chain (which never has one)";
}
