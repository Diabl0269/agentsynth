// SoundPacksSettingsTabTests.cpp -- the Settings "Sound packs" tab: one row per pack with its state and buttons, the
// credits section, the Remove question, the offline line, and the screen-reader names and tooltips. The installer
// runs inline on a fake network (SoundPackTestKit.h).
#include "../../SoundBrowser/SoundBrowserTestKit.h"
#include "../../SoundPacks/SoundPackTestKit.h"
#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "UI/Settings/SoundPacks/SoundPacksSettingsTab.h"

using namespace sound_pack_test;

namespace {

struct TabRig : Rig {
    SoundPacksSettingsTab tab{installer};
    TabRig() {
        tab.setSize(500, 400);
        tab.resized();
    }
    synth::ui::SoundPackRow& row() { return tab.getRowForTest(0); }
};

synth::soundpacks::SoundPackInstaller::Row packWithInstruments(int count) {
    synth::soundpacks::SoundPackInstaller::Row row;
    row.id = "big";
    row.name = "Big pack";
    row.downloadBytes = 40 * 1024 * 1024;
    for (int i = 1; i <= count; ++i) {
        synth::soundpacks::IndexInstrument instrument;
        instrument.id = "i" + juce::String(i);
        instrument.name = "Instrument" + juce::String(i);
        row.instruments.push_back(instrument);
    }
    return row;
}

juce::String fullListWithSize(int count, const juce::String& size) {
    juce::StringArray names;
    for (int i = 1; i <= count; ++i)
        names.add("Instrument" + juce::String(i));
    return names.joinIntoString(", ") + " - " + size;
}

} // namespace

TEST(SoundPacksSettingsTab, ShowsOneRowPerPackWithItsStateOnceTheListLoads) {
    TabRig rig;
    EXPECT_EQ(rig.tab.getRowCountForTest(), 0);
    rig.publish("core", "1.0.0");
    ASSERT_EQ(rig.tab.getRowCountForTest(), 1);
    auto& row = rig.row();
    EXPECT_EQ(row.getNameText(), "Pack core");
    EXPECT_EQ(row.getStatusText(), "Not installed");
    EXPECT_TRUE(row.getDetailText().contains("Grand"));
    EXPECT_TRUE(row.isButtonShown(row.getDownloadButton()));
    EXPECT_FALSE(row.isButtonShown(row.getRemoveButton()));
    EXPECT_FALSE(row.isButtonShown(row.getCancelButton()));
    EXPECT_EQ(rig.tab.getStatusTextForTest(), "");
    EXPECT_FALSE(rig.tab.isRetryShownForTest());
}

TEST(SoundPacksSettingsTab, DownloadInstallsThenOffersRemoveAndTheSourcesLink) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    auto& credit = rig.tab.getCreditForTest(0);
    EXPECT_FALSE(credit.isSourcesLinkShown()) << "no sources until the pack is installed";

    rig.row().getDownloadButton().onClick();

    auto& row = rig.row();
    EXPECT_EQ(row.getStatusText(), "Installed, version 1.0.0");
    EXPECT_TRUE(row.isButtonShown(row.getRemoveButton()));
    EXPECT_FALSE(row.isButtonShown(row.getDownloadButton()));
    EXPECT_TRUE(credit.isSourcesLinkShown());
    EXPECT_TRUE(rig.dir.path().getChildFile("core/pack.json").existsAsFile());
}

TEST(SoundPacksSettingsTab, AnUpdateIsOfferedNextToRemove) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    rig.row().getDownloadButton().onClick();
    rig.publish("core", "1.1.0");
    auto& row = rig.row();
    EXPECT_EQ(row.getStatusText(), "Update available: version 1.1.0");
    EXPECT_TRUE(row.isButtonShown(row.getUpdateButton()));
    EXPECT_TRUE(row.isButtonShown(row.getRemoveButton()));
    row.getUpdateButton().onClick();
    EXPECT_EQ(rig.row().getStatusText(), "Installed, version 1.1.0");
}

TEST(SoundPacksSettingsTab, ADownloadInProgressShowsPercentAndACancelButton) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    juce::String status;
    bool cancelShown = false;
    bool downloadShown = true;
    float bar = -1.0f;
    rig.server.onChunk = [&] {
        auto& row = rig.row();
        status = row.getStatusText();
        cancelShown = row.isButtonShown(row.getCancelButton());
        downloadShown = row.isButtonShown(row.getDownloadButton());
        bar = row.getProgress().getFraction();
    };
    rig.row().getDownloadButton().onClick();
    EXPECT_TRUE(status.startsWith("Downloading ")) << status;
    EXPECT_TRUE(status.endsWith("%"));
    EXPECT_TRUE(cancelShown);
    EXPECT_FALSE(downloadShown);
    EXPECT_GT(bar, 0.0f);
}

TEST(SoundPacksSettingsTab, CancelStopsTheDownloadAndGoesBackToDownload) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    int chunks = 0;
    rig.server.onChunk = [&] {
        if (++chunks == 1)
            rig.row().getCancelButton().onClick();
    };
    rig.row().getDownloadButton().onClick();
    EXPECT_EQ(rig.row().getStatusText(), "Not installed");
    EXPECT_TRUE(rig.row().isButtonShown(rig.row().getDownloadButton()));
}

TEST(SoundPacksSettingsTab, AFailedDownloadShowsTheReasonInPlace) {
    TabRig rig;
    rig.publish("core", "1.0.0", {}, juce::String::repeatedString("0", 64));
    rig.row().getDownloadButton().onClick();
    EXPECT_TRUE(rig.row().getStatusText().contains("checksum")) << rig.row().getStatusText();
    EXPECT_TRUE(rig.row().isButtonShown(rig.row().getDownloadButton())) << "it can be tried again";
}

TEST(SoundPacksSettingsTab, RemoveAsksFirstAndOnlyAYesDeletesThePack) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    rig.row().getDownloadButton().onClick();
    juce::String question;
    std::function<void()> yes;
    rig.tab.setConfirmerForTesting([&](const juce::String& message, std::function<void()> onYes) {
        question = message;
        yes = std::move(onYes);
    });

    rig.row().getRemoveButton().onClick();
    EXPECT_TRUE(question.contains("Pack core"));
    EXPECT_TRUE(rig.dir.path().getChildFile("core").isDirectory()) << "nothing is deleted before the answer";

    yes();
    EXPECT_FALSE(rig.dir.path().getChildFile("core").exists());
    EXPECT_EQ(rig.row().getStatusText(), "Not installed");
}

TEST(SoundPacksSettingsTab, OfflineShowsTheLineAndRetryAndKeepsInstalledPacksListed) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    rig.row().getDownloadButton().onClick();

    rig.server.offline = true;
    rig.installer.refreshIndex();
    EXPECT_EQ(rig.tab.getStatusTextForTest(), "Couldn't reach the pack list.");
    EXPECT_TRUE(rig.tab.isRetryShownForTest());
    ASSERT_EQ(rig.tab.getRowCountForTest(), 1);
    EXPECT_EQ(rig.row().getStatusText(), "Installed, version 1.0.0");

    rig.server.offline = false;
    rig.tab.getRetryLinkForTest().onClick();
    EXPECT_EQ(rig.tab.getStatusTextForTest(), "");
    EXPECT_FALSE(rig.tab.isRetryShownForTest());
}

TEST(SoundPacksSettingsTab, CreditsListEveryPackWithItsLicenceLink) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    auto& credit = rig.tab.getCreditForTest(0);
    EXPECT_EQ(credit.getCreditText(), "Made by someone.");
    EXPECT_EQ(credit.getLicenceText(), "Licence: CC0 1.0");
    EXPECT_TRUE(credit.isLicenceLinkShown());

    juce::String opened;
    rig.tab.setUrlOpenerForTesting([&](const juce::URL& url) { opened = url.toString(false); });
    credit.getLicenceLink().onClick();
    EXPECT_EQ(opened, "https://example.test/licence");

    juce::File sources;
    rig.installer.setFileOpenerForTesting([&](const juce::File& f) { sources = f; });
    rig.row().getDownloadButton().onClick();
    credit.getSourcesLink().onClick();
    EXPECT_EQ(sources.getFileName(), "SOURCES.md");
}

TEST(SoundPacksSettingsTab, EveryControlHasAKeyboardStopANameAndATooltip) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    rig.row().getDownloadButton().onClick();
    rig.publish("core", "1.1.0");
    auto& row = rig.row();
    for (auto* button :
         {&row.getDownloadButton(), &row.getUpdateButton(), &row.getRemoveButton(), &row.getCancelButton()}) {
        EXPECT_TRUE(button->getWantsKeyboardFocus());
        EXPECT_TRUE(button->getTitle().contains("Pack core")) << button->getTitle();
        EXPECT_TRUE(button->getTooltip().contains("Pack core")) << button->getTooltip();
    }
    auto& credit = rig.tab.getCreditForTest(0);
    EXPECT_TRUE(credit.getLicenceLink().getTitle().contains("Pack core"));
    EXPECT_TRUE(credit.getSourcesLink().getTooltip().isNotEmpty());
    EXPECT_TRUE(credit.getLicenceLink().getWantsKeyboardFocus());
    EXPECT_TRUE(rig.tab.getRetryLinkForTest().getTitle().isNotEmpty());
    EXPECT_TRUE(rig.tab.getRetryLinkForTest().getTooltip().isNotEmpty());
    EXPECT_TRUE(row.getTitle().startsWith("Pack core, update available"));
    EXPECT_TRUE(row.getProgress().getTitle().contains("percent"));
}

TEST(SoundPacksSettingsTab, ARowNamesItsProgressForAScreenReader) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    juce::String title;
    rig.server.onChunk = [&] { title = rig.row().getTitle(); };
    rig.row().getDownloadButton().onClick();
    EXPECT_TRUE(title.startsWith("Pack core, downloading, ")) << title;
    EXPECT_TRUE(title.contains("percent"));
}

class SoundPacksSettingsWindowTest : public AccessibilitySettingsTest {};

TEST_F(SoundPacksSettingsWindowTest, TheTabIsOnlyThereWhenTheWindowIsGivenTheInstaller) {
    appProperties.getUserSettings()->clear();
    SettingsWindow without(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    for (int i = 0; i < without.getNumTabs(); ++i)
        EXPECT_NE(without.getTabName(i), "Sound packs");

    SoundPackInstaller installer;
    SettingsWindow with(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr,
                        nullptr, true, {}, {}, &installer);
    EXPECT_EQ(with.getTabName(with.getNumTabs() - 1), "Sound packs");
    EXPECT_EQ(with.getNumTabs(), without.getNumTabs() + 1);
}

// Regression test for FRO1034: the instrument line was cut to six names and "and N more", so the rest were unreadable.
TEST(SoundPackRowDetail, ANarrowRowKeepsEveryInstrumentAndOffersTheFullListAsATooltip) {
    synth::ui::SoundPackRow row("big", {});
    row.setSize(300, synth::ui::SoundPackRow::kHeight);
    row.update(packWithInstruments(9));
    const auto full = fullListWithSize(9, row.getDetailText().fromLastOccurrenceOf(" - ", false, false));
    EXPECT_EQ(row.getDetailText(), full);
    EXPECT_FALSE(row.getDetailText().contains("more"));
    EXPECT_EQ(row.getDetailTooltip(), full);
    EXPECT_TRUE(row.getDetailTooltip().contains("Instrument9"));
    EXPECT_TRUE(row.getDetailTooltip().contains(" MB"));
    EXPECT_TRUE(row.getTitle().startsWith("Big pack, not installed"));
}

TEST(SoundPackRowDetail, AWideRowShowsTheWholeLineWithNoTooltip) {
    synth::ui::SoundPackRow row("big", {});
    row.setSize(2400, synth::ui::SoundPackRow::kHeight);
    row.update(packWithInstruments(9));
    EXPECT_TRUE(row.getDetailText().contains("Instrument9"));
    EXPECT_TRUE(row.getDetailTooltip().isEmpty());
}

TEST(SoundPackRowDetail, AShortListIsTheSameTextWithNoTooltipAndWideningDropsTheTooltip) {
    synth::ui::SoundPackRow row("big", {});
    row.setSize(500, synth::ui::SoundPackRow::kHeight);
    row.update(packWithInstruments(2));
    EXPECT_EQ(row.getDetailText(), fullListWithSize(2, row.getDetailText().fromLastOccurrenceOf(" - ", false, false)));
    EXPECT_TRUE(row.getDetailTooltip().isEmpty());

    row.update(packWithInstruments(9));
    EXPECT_TRUE(row.getDetailTooltip().isNotEmpty());
    row.setSize(2400, synth::ui::SoundPackRow::kHeight);
    EXPECT_TRUE(row.getDetailTooltip().isEmpty()) << "a resize re-decides the tooltip";
}

TEST(SoundPackRowDetail, TheLineNeverReachesTheButtonsAtTheNarrowestWidth) {
    synth::ui::SoundPackRow row("big", {});
    row.setSize(300, synth::ui::SoundPackRow::kHeight);
    row.update(packWithInstruments(9));
    EXPECT_LE(row.getDetailBounds().getRight(), row.getUpdateButton().getX());
    EXPECT_GT(row.getDetailBounds().getWidth(), 0);
}

TEST(SoundPacksSettingsTab, YourFoldersListsTheSoundsViewFoldersAndRemoveKeepsTheirFiles) {
    Rig rig;
    sound_browser_test::TempTree tree;
    tree.fill();
    auto index = sound_browser_test::makeIndex(tree);
    synth::soundbrowser::SoundFolderBook book(*index);
    book.add(tree.user);
    SoundPacksSettingsTab tab(rig.installer, &book);
    tab.setSize(500, 500);
    tab.resized();
    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };

    ASSERT_EQ(tab.getFolderRowCountForTest(), 1);
    auto& row = tab.getFolderRowForTest(0);
    EXPECT_EQ(row.getNameText(), "user");
    EXPECT_EQ(row.getPathText(), tree.user.getFullPathName());
    EXPECT_EQ(row.getRemoveButton().getTitle(), "Remove user from your folders");
    EXPECT_TRUE(row.getRemoveButton().getTooltip().contains("The files stay where they are"));

    book.rename(tree.user, "Field loops");
    pump();
    EXPECT_EQ(tab.getFolderRowForTest(0).getNameText(), "Field loops") << "a rename in the Sounds view shows here";

    const auto file = tree.user.getChildFile("Field/dusty_keys_120_Am_loop.wav");
    tab.getFolderRowForTest(0).getRemoveButton().onClick();
    pump();
    EXPECT_EQ(tab.getFolderRowCountForTest(), 0);
    EXPECT_TRUE(book.folders().empty());
    EXPECT_TRUE(file.existsAsFile());
}
