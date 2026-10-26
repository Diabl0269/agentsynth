// The Match key switch in the Sounds view's footer: its default, wording, accessibility, persistence, what it does to a
// loop's target and the key line under a selected loop.
#include "../../../SoundBrowser/SoundBrowserTestKit.h"
#include "SoundBrowser/LoopStretch.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

using synth::soundbrowser::SoundEntry;
using synth::soundbrowser::SoundKind;
using synth::ui::SoundBrowserComponent;

namespace {

SoundEntry loopEntry(const juce::String& key, double tempo = 120.0) {
    SoundEntry e;
    e.name = "dusty";
    e.kind = SoundKind::Loop;
    e.tempo = tempo;
    e.key = key;
    return e;
}

} // namespace

TEST(SoundPreviewFooterMatchKeyTest, IsOffByDefaultAndSaysWhatItDoes) {
    synth::ui::SoundPreviewFooter footer;
    EXPECT_FALSE(footer.isMatchKey());
    auto& toggle = footer.getMatchKey();
    EXPECT_EQ(toggle.getTooltip(), "Play loops in the song's key");
    EXPECT_EQ(toggle.getTitle(), "Match key");
    EXPECT_EQ(toggle.getButtonText(), "Match key");
    EXPECT_TRUE(toggle.getWantsKeyboardFocus()) << "a keyboard stop like Auto-play";
    EXPECT_EQ(toggle.getWantsKeyboardFocus(), footer.getAutoPlay().getWantsKeyboardFocus());
    EXPECT_NE(footer.getAutoPlay().getTitle(), toggle.getTitle());
}

TEST(SoundPreviewFooterMatchKeyTest, SitsBesideAutoPlayAndTellsTheOwnerWhenFlipped) {
    synth::ui::SoundPreviewFooter footer;
    footer.setSize(280, synth::ui::SoundPreviewFooter::kHeight);
    EXPECT_TRUE(footer.getMatchKey().isVisible());
    EXPECT_EQ(footer.getMatchKey().getBounds().getY(), footer.getAutoPlay().getBounds().getY());
    EXPECT_LE(footer.getMatchKey().getRight(), footer.getAutoPlay().getX());
    EXPECT_GT(footer.getMatchKey().getWidth(), 40);
    int calls = 0;
    bool last = false;
    footer.onMatchKeyChanged = [&](bool on) {
        ++calls;
        last = on;
    };
    footer.getMatchKey().setToggleState(true, juce::sendNotificationSync);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(last);
    EXPECT_TRUE(footer.isMatchKey());
    footer.setMatchKey(false);
    EXPECT_EQ(calls, 1) << "setting it from outside is silent";
}

// Regression test for FRO996: at the narrowest sidebar (200 px) both labels were cut to "Match..." and "Auto-...".
TEST(SoundPreviewFooterMatchKeyTest, AtTheNarrowestSidebarBothSwitchesKeepRoomForTheirLabels) {
    for (const int width : {200, 236, 280}) {
        synth::ui::SoundPreviewFooter footer;
        footer.setSize(width, synth::ui::SoundPreviewFooter::kHeight);
        auto& match = footer.getMatchKey();
        auto& autoPlay = footer.getAutoPlay();
        EXPECT_GE(match.getWidth(), 80) << width;
        EXPECT_GE(autoPlay.getWidth(), 80) << width;
        EXPECT_FALSE(match.getBounds().intersects(autoPlay.getBounds())) << width;
        EXPECT_FALSE(match.getBounds().intersects(footer.getStopButton().getBounds())) << width;
        EXPECT_FALSE(autoPlay.getBounds().intersects(footer.getStopButton().getBounds())) << width;
        EXPECT_FALSE(autoPlay.getBounds().intersects(footer.getVolume().getBounds())) << width;
        EXPECT_FALSE(match.getBounds().intersects(footer.getVolume().getBounds())) << width;
        EXPECT_TRUE(footer.getLocalBounds().contains(autoPlay.getBounds())) << width;
    }
}

TEST(SoundPreviewFooterMatchKeyTest, PersistsAcrossViews) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "sound-matchkey-settings-test-" + juce::Uuid().toDashedString();
    opts.filenameSuffix = "settings";
    opts.folderName = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName();
    juce::PropertiesFile settings(opts);
    ModuleLibraryComponent library;
    {
        synth::ui::LibrarySidebarViews sidebar(library, &settings);
        sidebar.setSize(300, 800);
        EXPECT_FALSE(sidebar.getSounds().isMatchKey());
        sidebar.getSounds().getPreviewFooter().getMatchKey().setToggleState(true, juce::sendNotificationSync);
    }
    EXPECT_TRUE(settings.getBoolValue(synth::kSoundBrowserMatchKeySettingKey, false));
    ModuleLibraryComponent library2;
    synth::ui::LibrarySidebarViews again(library2, &settings);
    EXPECT_TRUE(again.getSounds().isMatchKey());
}

TEST(SoundPreviewFooterMatchKeyTest, ALoopIsFittedToTheSongTempoAndToItsKeyOnlyWithMatchKeyOn) {
    sound_browser_test::TempTree tree;
    tree.fill();
    auto index = sound_browser_test::makeIndex(tree);
    synth::soundbrowser::SoundLists lists;
    SoundBrowserComponent view(*index, lists);
    view.songTempoProvider = [] { return 96.0; };
    view.setSongKey("D minor");

    auto target = view.loopTargetFor(loopEntry("Am"));
    EXPECT_DOUBLE_EQ(target.fileBpm, 120.0);
    EXPECT_DOUBLE_EQ(target.songBpm, 96.0);
    EXPECT_EQ(target.semitones, 0) << "Match key is off";

    view.setPreviewSettings(true, -6.0f, true);
    EXPECT_EQ(view.loopTargetFor(loopEntry("Am")).semitones, 5);
    EXPECT_EQ(view.loopTargetFor(loopEntry("")).semitones, 0) << "a loop with no key stays put";
    EXPECT_DOUBLE_EQ(view.loopTargetFor(loopEntry("Am", 0.0)).fileBpm, 0.0) << "unknown tempo: no time-stretch";
    view.setSongKey("");
    EXPECT_EQ(view.loopTargetFor(loopEntry("Am")).semitones, 0) << "no song key";
}

TEST(SoundPreviewFooterMatchKeyTest, TheFooterLineShowsTheLoopsKeyNextToTheSongs) {
    const auto dot = juce::String::fromUTF8("\xc2\xb7");
    EXPECT_EQ(synth::ui::soundKeyMetaText(loopEntry("A minor"), "D minor"), "Key Am " + dot + " song Dm");
    EXPECT_EQ(synth::ui::soundKeyMetaText(loopEntry("C"), "Eb major"), "Key C " + dot + " song D#");
    EXPECT_EQ(synth::ui::soundKeyMetaText(loopEntry("Am"), ""), "Key Am") << "only the parts that are known";
    EXPECT_EQ(synth::ui::soundKeyMetaText(loopEntry(""), "Dm"), "") << "no key, nothing shown";
    auto oneShot = loopEntry("Am");
    oneShot.kind = SoundKind::OneShot;
    EXPECT_EQ(synth::ui::soundKeyMetaText(oneShot, "Dm"), "");
}
