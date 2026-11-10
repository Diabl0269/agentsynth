// The Sounds view's preview: Auto-play on plays on select, off stays silent until Ctrl+Space, Ctrl+Space stops, the
// settings persist, instruments and missing files never play, the Play/Stop button follows the player, and the waveform
// repaints only the playhead's strip.
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

using namespace sound_browser_test;
using synth::soundbrowser::SoundPreviewPlayer;
using synth::ui::SoundBrowserComponent;

namespace {

constexpr double kRate = 48000.0;
juce::KeyPress space() { return juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' '); }
juce::KeyPress ctrlSpace() { return juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, ' '); }

int rowIndex(const SoundBrowserComponent& view, const juce::String& text) {
    for (size_t i = 0; i < view.getRows().size(); ++i)
        if (view.getRows()[i].text == text)
            return (int)i;
    return -1;
}

float renderPeak(SoundPreviewPlayer& player, int blocks = 12) {
    synth::BlockTimeInfo info;
    info.sampleRate = kRate;
    info.bpm = 120.0;
    info.timeSigNumerator = 4;
    info.timeSigDenominator = 4;
    info.numSamples = 512;
    info.loopWrapSample = -1;
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        player.process(buffer, 2, info);
        peak = std::max(peak, buffer.getMagnitude(0, 512));
    }
    return peak;
}

// A left click on `button` through its own mouse handlers.
void clickButton(juce::Button& button) {
    const juce::Point<float> p((float)button.getWidth() * 0.5f, (float)button.getHeight() * 0.5f);
    const auto event = [&](juce::Time down) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, &button, &button, juce::Time::getCurrentTime(), p, down, 1, false);
    };
    juce::Component& component = button; // Component's mouse handlers are public; Button's overrides are not
    component.mouseDown(event(juce::Time::getCurrentTime()));
    component.mouseUp(event(juce::Time::getCurrentTime()));
}

struct Fixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    SoundPreviewPlayer player;
    std::unique_ptr<SoundBrowserComponent> view;
    Fixture() {
        tree.fill();
        index = makeIndex(tree);
        scanAndWait(*index);
        player.prepare(kRate, 512);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 900);
        view->setPreviewPlayer(&player);
        view->showLocation("core", "Drums", 0);
        view->completeListAnimationForTest();
    }
    void selectSnare() { view->selectRow(rowIndex(*view, "snare_tight")); }
};

} // namespace

TEST(SoundPreviewViewTest, SelectingASoundPlaysItWhenAutoPlayIsOn) {
    Fixture f;
    EXPECT_TRUE(f.view->isAutoPlay()) << "Auto-play is on by default";
    f.selectSnare();
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    EXPECT_GT(renderPeak(f.player), 0.01f);
    EXPECT_TRUE(f.player.isPlaying());
}

TEST(SoundPreviewViewTest, WithAutoPlayOffSelectingIsSilentAndCtrlSpacePlaysAndStops) {
    Fixture f;
    f.view->setPreviewSettings(false, -6.0f);
    f.selectSnare();
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    EXPECT_EQ(renderPeak(f.player), 0.0f) << "selecting alone makes no sound";
    EXPECT_FALSE(f.player.isPlaying());

    EXPECT_FALSE(f.view->keyPressed(space())) << "plain Space is the song's play key: it must bubble to the app";
    EXPECT_FALSE(f.player.isPlaying()) << "plain Space never plays the preview";

    EXPECT_TRUE(f.view->keyPressed(ctrlSpace()));
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    EXPECT_GT(renderPeak(f.player, 2), 0.01f);
    EXPECT_TRUE(f.player.isPlaying());

    EXPECT_FALSE(f.view->keyPressed(space())) << "nor does plain Space stop it";
    EXPECT_TRUE(f.player.isPlaying());
    EXPECT_TRUE(f.view->keyPressed(ctrlSpace())) << "Ctrl+Space on a playing preview stops it";
    renderPeak(f.player, 4);
    EXPECT_EQ(renderPeak(f.player, 4), 0.0f);
}

TEST(SoundPreviewViewTest, FoldersAndInstrumentsNeverPlay) {
    Fixture f;
    f.view->showLocation("core", "", 0);
    f.view->completeListAnimationForTest();
    const int piano = rowIndex(*f.view, "Piano");
    ASSERT_GE(piano, 0);
    f.view->selectRow(piano);
    EXPECT_FALSE(f.view->playSelected());
    f.view->showLocation("core", "Piano", 0);
    f.view->completeListAnimationForTest();
    f.view->selectRow(rowIndex(*f.view, "upright"));
    EXPECT_FALSE(f.view->playSelected()) << "an instrument is not an audio file";
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    EXPECT_EQ(renderPeak(f.player), 0.0f);
}

TEST(SoundPreviewViewTest, AMissingFileNeverPlays) {
    Fixture f;
    f.selectSnare();
    f.player.stop();
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    renderPeak(f.player, 20);
    f.tree.packs.getChildFile("core/Drums/snare_tight.wav").deleteFile();
    EXPECT_FALSE(f.view->playSelected());
    EXPECT_EQ(renderPeak(f.player), 0.0f);
}

TEST(SoundPreviewViewTest, TheVolumeSliderIsThePreviewsOwnGain) {
    Fixture f;
    auto& volume = f.view->getPreviewFooter().getVolume();
    EXPECT_EQ(volume.getMinimum(), -60.0);
    EXPECT_EQ(volume.getMaximum(), 6.0);
    EXPECT_FLOAT_EQ(f.view->getPreviewGainDb(), -6.0f);
    EXPECT_EQ(volume.getTitle(), "Preview volume");
    EXPECT_EQ(volume.getTooltip(), "Preview volume, only for previews");
    EXPECT_EQ(volume.getTextFromValue(-6.0), "-6 dB");
    bool changed = false;
    f.view->onPreviewSettingsChanged = [&changed] { changed = true; };
    volume.setValue(-12.0, juce::sendNotificationSync);
    EXPECT_TRUE(changed);
    EXPECT_FLOAT_EQ(f.player.getGainDb(), -12.0f);
    EXPECT_EQ(f.view->getPreviewFooter().getAutoPlay().getTooltip(), "Play a sound when you select it");
}

TEST(SoundPreviewViewTest, AutoPlayAndVolumePersistAcrossViews) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "sound-preview-settings-test-" + juce::Uuid().toDashedString();
    opts.filenameSuffix = "settings";
    opts.folderName = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName();
    juce::PropertiesFile settings(opts);
    ModuleLibraryComponent library;
    {
        synth::ui::LibrarySidebarViews sidebar(library, &settings);
        sidebar.setSize(300, 800);
        EXPECT_TRUE(sidebar.getSounds().isAutoPlay());
        sidebar.getSounds().getPreviewFooter().getAutoPlay().setToggleState(false, juce::sendNotificationSync);
        sidebar.getSounds().getPreviewFooter().getVolume().setValue(-20.0, juce::sendNotificationSync);
    }
    EXPECT_FALSE(settings.getBoolValue(synth::kSoundBrowserAutoPlaySettingKey, true));
    EXPECT_NEAR(settings.getDoubleValue(synth::kSoundBrowserPreviewGainDbSettingKey, 0.0), -20.0, 0.01);
    ModuleLibraryComponent library2;
    synth::ui::LibrarySidebarViews again(library2, &settings);
    EXPECT_FALSE(again.getSounds().isAutoPlay());
    EXPECT_FLOAT_EQ(again.getSounds().getPreviewGainDb(), -20.0f);
}

TEST(SoundPreviewViewTest, ThePlayheadRepaintsOnlyItsOwnStripAndStopsWhenThePreviewEnds) {
    Fixture f;
    auto& wave = f.view->getPreviewFooter().getWaveform();
    wave.setBounds(0, 0, 280, 60);
    std::vector<juce::Rectangle<int>> strips;
    wave.onStripRepaintForTest = [&strips](juce::Rectangle<int> r) { strips.push_back(r); };
    f.selectSnare();
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    renderPeak(f.player, 4);
    for (int i = 0; i < 4; ++i) {
        renderPeak(f.player, 6);
        wave.tickForTest();
    }
    ASSERT_FALSE(strips.empty());
    for (const auto& r : strips)
        EXPECT_LT(r.getWidth(), wave.getWidth() / 3) << "a playhead strip, never the whole waveform";
    f.player.stop();
    renderPeak(f.player, 4);
    wave.tickForTest();
    const auto count = strips.size();
    wave.tickForTest();
    wave.tickForTest();
    EXPECT_EQ(strips.size(), count) << "no repaint once nothing plays";
}

// Regression test for FRO1382: the button said Stop while nothing played. It reads Play, Stop while a preview sounds,
// and a click toggles.
TEST(SoundPreviewViewTest, ThePlayButtonReadsPlayWhenIdleAndStopWhileSoundingAndAClickToggles) {
    Fixture f;
    auto& footer = f.view->getPreviewFooter();
    auto& button = footer.getPlayButton();
    EXPECT_EQ(button.getButtonText(), "Play") << "nothing plays yet";
    EXPECT_EQ(button.getTitle(), "Play preview");
    EXPECT_EQ(button.getTooltip(),
              "Play or stop the preview (" + ShortcutManager::keyPressToDisplayString(ctrlSpace()) + ")");
    EXPECT_FALSE(button.getTooltip().contains("(Space)")) << "plain Space is the song's";

    f.selectSnare(); // Auto-play is on
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    renderPeak(f.player, 2);
    footer.syncPlayState();
    EXPECT_EQ(button.getButtonText(), "Stop");
    EXPECT_EQ(button.getTitle(), "Stop preview");

    clickButton(button);
    renderPeak(f.player, 4);
    footer.syncPlayState();
    EXPECT_FALSE(f.player.isPlaying());
    EXPECT_EQ(button.getButtonText(), "Play") << "the click stopped it";
    EXPECT_EQ(button.getTitle(), "Play preview");

    clickButton(button);
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    renderPeak(f.player, 2);
    footer.syncPlayState();
    EXPECT_TRUE(f.player.isPlaying()) << "the second click plays the selected sound";
    EXPECT_LT(f.player.playheadFraction(), 0.5f) << "from the start";
    EXPECT_EQ(button.getButtonText(), "Stop");
}

TEST(SoundPreviewViewTest, ThePlayButtonFlipsToPlayWhenTheSoundEndsOrCtrlSpaceStopsIt) {
    Fixture f;
    auto& footer = f.view->getPreviewFooter();
    auto& button = footer.getPlayButton();
    f.selectSnare();
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    renderPeak(f.player, 2);
    footer.syncPlayState();
    ASSERT_EQ(button.getButtonText(), "Stop");
    renderPeak(f.player, 120); // the 0.3 s snare ends by itself
    footer.syncPlayState();
    EXPECT_FALSE(f.player.isPlaying());
    EXPECT_EQ(button.getButtonText(), "Play");

    EXPECT_TRUE(f.view->keyPressed(ctrlSpace()));
    ASSERT_TRUE(f.player.waitForDecodeForTests(10000));
    renderPeak(f.player, 2);
    footer.syncPlayState();
    EXPECT_EQ(button.getButtonText(), "Stop");
    EXPECT_TRUE(f.view->keyPressed(ctrlSpace()));
    renderPeak(f.player, 4);
    footer.syncPlayState();
    EXPECT_EQ(button.getButtonText(), "Play") << "Ctrl+Space stopped it";
}

TEST(SoundPreviewViewTest, ThePlayStateTimerRunsOnlyWhileTheFooterShows) {
    Fixture f;
    auto& footer = f.view->getPreviewFooter();
    f.view->setVisible(true);
    EXPECT_TRUE(footer.isPlayStateTimerRunningForTest());
    footer.setVisible(false);
    EXPECT_FALSE(footer.isPlayStateTimerRunningForTest());
    footer.setVisible(true);
    EXPECT_TRUE(footer.isPlayStateTimerRunningForTest());
}
