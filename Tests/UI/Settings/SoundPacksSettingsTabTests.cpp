// SoundPacksSettingsTabTests.cpp -- the Settings "Sound packs" tab: one row per pack with its state and buttons, the
// details each row opens to (credit, licence, every instrument), the Remove question, the offline line, and the
// screen-reader names and tooltips. The installer runs inline on a fake network (SoundPackTestKit.h).
#include "../../SoundBrowser/SoundBrowserTestKit.h"
#include "../../SoundPacks/SoundPackTestKit.h"
#include "../../TestWait.h"
#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "UI/Settings/SettingsWindow.h"
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

TEST(SoundPacksSettingsTab, EveryRowCarriesItsCreditAndLicenceLinkAndTheLinksStillFire) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    rig.row().setExpanded(true);
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

// ---- each row opens to its credit, licence and every instrument ----

namespace {

// No visible child overlaps another or leaves `parent`; descends into `descend` (the open details).
void expectNoOverlap(juce::Component& parent, juce::Component* descend = nullptr) {
    std::vector<juce::Component*> kids;
    for (auto* child : parent.getChildren())
        if (child->isVisible())
            kids.push_back(child);
    for (size_t i = 0; i < kids.size(); ++i) {
        EXPECT_TRUE(parent.getLocalBounds().contains(kids[i]->getBounds()))
            << kids[i]->getComponentID() << " " << kids[i]->getBounds().toString() << " leaves "
            << parent.getLocalBounds().toString();
        for (size_t j = i + 1; j < kids.size(); ++j)
            EXPECT_FALSE(kids[i]->getBounds().intersects(kids[j]->getBounds()))
                << kids[i]->getBounds().toString() << " overlaps " << kids[j]->getBounds().toString();
    }
    if (descend != nullptr)
        expectNoOverlap(*descend);
}

synth::soundpacks::SoundPackInstaller::Row richPack(int instruments) {
    auto row = packWithInstruments(instruments);
    row.credit = juce::String::repeatedString("Recorded in a hall by someone patient. ", 6);
    row.licence = "CC BY 4.0";
    row.licenceUrl = "https://example.test/licence";
    row.sources = juce::File::getSpecialLocation(juce::File::tempDirectory);
    return row;
}

struct TestProps {
    juce::ApplicationProperties props;
    explicit TestProps(const juce::File& folder) {
        juce::PropertiesFile::Options options;
        options.applicationName = "SoundPacksTabTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.folderName = folder.getFullPathName();
        props.setStorageParameters(options);
        props.getUserSettings()->setFallbackPropertySet(nullptr);
    }
};

} // namespace

TEST(SoundPackRowDetails, AClosedRowIsTheOldHeightAndAnOpenRowAddsTheDetails) {
    synth::ui::SoundPackRow row("big", {});
    row.update(richPack(9));
    EXPECT_FALSE(row.isExpanded());
    EXPECT_EQ(row.heightForWidth(466), synth::ui::SoundPackRow::kHeight);
    EXPECT_FALSE(row.isCreditShown());
    row.setExpanded(true);
    EXPECT_TRUE(row.isExpanded());
    EXPECT_TRUE(row.isCreditShown());
    EXPECT_GT(row.heightForWidth(466), synth::ui::SoundPackRow::kHeight + 60);
    EXPECT_GT(row.heightForWidth(300), row.heightForWidth(466)) << "a narrower row wraps to more lines";
    row.setExpanded(false);
    EXPECT_EQ(row.heightForWidth(300), synth::ui::SoundPackRow::kHeight);
    EXPECT_FALSE(row.isCreditShown());
}

TEST(SoundPackRowDetails, TheOpenPartHoldsTheCreditTheLicenceBothLinksAndEveryInstrument) {
    int licences = 0;
    juce::String sourcesFor;
    synth::ui::SoundPackRow row(
        "big",
        {{}, {}, {}, [&](const juce::String&) { ++licences; }, [&](const juce::String& id) { sourcesFor = id; }});
    row.update(richPack(9));
    row.setSize(466, row.heightForWidth(466));
    auto& credit = row.getCredit();
    EXPECT_TRUE(credit.getCreditText().startsWith("Recorded in a hall"));
    EXPECT_EQ(credit.getLicenceText(), "Licence: CC BY 4.0");
    EXPECT_TRUE(credit.isLicenceLinkShown());
    EXPECT_TRUE(credit.isSourcesLinkShown());
    for (int i = 1; i <= 9; ++i)
        EXPECT_TRUE(credit.getInstrumentsText().contains("Instrument" + juce::String(i)));
    EXPECT_TRUE(credit.getInstrumentsText().startsWith("Instruments: "));
    credit.getLicenceLink().onClick();
    credit.getSourcesLink().onClick();
    EXPECT_EQ(licences, 1);
    EXPECT_EQ(sourcesFor, "big");
}

TEST(SoundPackRowDetails, TheCreditLinksAreOnlyOnScreenWhileTheRowIsOpen) {
    synth::ui::SoundPackRow row("big", {});
    row.update(richPack(3));
    EXPECT_FALSE(row.getCredit().isVisible()) << "closed: not on screen, so not a Tab stop";
    row.setExpanded(true);
    EXPECT_TRUE(row.getCredit().isVisible());
    row.setExpanded(false);
    EXPECT_FALSE(row.getCredit().isVisible());
}

TEST(SoundPackRowDetails, TheDetailLineKeepsItsTooltipWhileClosedAndOpen) {
    synth::ui::SoundPackRow row("big", {});
    row.setSize(300, synth::ui::SoundPackRow::kHeight);
    row.update(packWithInstruments(9));
    const auto tip = row.getDetailTooltip();
    EXPECT_TRUE(tip.contains("Instrument9"));
    row.setExpanded(true);
    row.setSize(300, row.heightForWidth(300));
    EXPECT_EQ(row.getDetailTooltip(), tip);
}

TEST(SoundPackRowDetails, TheDisclosureNamesThePackAndItsStateAndTurnsTheChevron) {
    synth::ui::SoundPackRow row("big", {});
    row.update(packWithInstruments(2));
    auto& disclosure = row.getDisclosure();
    EXPECT_EQ(disclosure.getTitle(), "Details for Big pack, collapsed");
    EXPECT_EQ(disclosure.getTooltip(), "Show who made Big pack, its licence and every instrument");
    EXPECT_TRUE(disclosure.getWantsKeyboardFocus());
    EXPECT_EQ(disclosure.getChevronOpenness(), 0.0f);
    EXPECT_TRUE(row.getTitle().startsWith("Big pack, not installed")) << "the row's own name is unchanged";
    row.setExpanded(true);
    EXPECT_EQ(disclosure.getTitle(), "Details for Big pack, expanded");
    EXPECT_EQ(disclosure.getTooltip(), "Hide the details of Big pack");
    EXPECT_EQ(disclosure.getChevronOpenness(), 1.0f);
}

TEST(SoundPackRowDetails, ReturnOnTheDisclosureOpensAndClosesTheRowAndCallsTheOwner) {
    synth::ui::SoundPackRow row("big", {});
    row.update(packWithInstruments(2));
    int changes = 0;
    row.onExpandedChanged = [&] { ++changes; };
    EXPECT_TRUE(row.getDisclosure().pressKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::waitUntil([&] { return row.isExpanded(); }); // a key press clicks on the next message
    EXPECT_TRUE(row.isExpanded());
    row.getDisclosure().onClick(); // what Space and a mouse click end in
    EXPECT_FALSE(row.isExpanded());
    EXPECT_EQ(changes, 2);
}

TEST(SoundPackRowDetails, NoChildOfAnOpenRowOverlapsAnotherOrLeavesItAtTheNarrowestWidth) {
    for (const int width : {300, SettingsWindow::kMinWidth - 20 - 14}) {
        synth::ui::SoundPackRow row("big", {});
        row.update(richPack(12));
        row.setExpanded(true);
        row.setSize(width, row.heightForWidth(width));
        expectNoOverlap(row, &row.getCredit());
        EXPECT_GE(row.getCredit().getY(), synth::ui::SoundPackRow::kHeight - 8) << "the details sit under the band";
        EXPECT_LE(row.getCredit().getBottom(), row.getHeight());
        EXPECT_LE(row.getDetailBounds().getRight(), row.getUpdateButton().getX());
    }
}

TEST(SoundPacksSettingsTab, TheCreditsHeadingAndTheBottomListAreGone) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    for (auto* child : rig.tab.getContentForTest().getChildren()) {
        if (auto* label = dynamic_cast<juce::Label*>(child))
            EXPECT_NE(label->getText(), "Credits");
        EXPECT_EQ(dynamic_cast<synth::ui::SoundPackCredit*>(child), nullptr) << "a credit lives inside its row";
    }
    EXPECT_EQ(&rig.tab.getCreditForTest(0), &rig.row().getCredit());
}

TEST(SoundPacksSettingsTab, TheListGrowsWhenARowOpensAndShrinksWhenItCloses) {
    TabRig rig;
    rig.publish("core", "1.0.0");
    const int closed = rig.tab.getContentHeightForTest();
    EXPECT_EQ(rig.row().getHeight(), synth::ui::SoundPackRow::kHeight);
    rig.row().getDisclosure().onClick();
    const int open = rig.tab.getContentHeightForTest();
    EXPECT_GT(open, closed);
    EXPECT_EQ(rig.row().getHeight(), rig.row().heightForWidth(rig.row().getWidth()));
    rig.row().getDisclosure().onClick();
    EXPECT_EQ(rig.tab.getContentHeightForTest(), closed);
}

TEST(SoundPacksSettingsTab, WhichPacksAreOpenIsRememberedAcrossTabsAndRowRebuilds) {
    TabRig rig;
    TestProps holder(rig.dir.path());
    auto& props = holder.props;
    {
        SoundPacksSettingsTab tab(rig.installer, nullptr, &props);
        tab.setSize(500, 400);
        tab.resized();
        rig.publish("core", "1.0.0");
        EXPECT_FALSE(tab.getRowForTest(0).isExpanded()) << "all closed by default";
        tab.getRowForTest(0).getDisclosure().onClick();
        EXPECT_EQ(props.getUserSettings()->getValue("soundPacksOpen"), "core");

        rig.publish("other", "1.0.0"); // the row set changes, then comes back
        rig.publish("core", "1.0.0");
        EXPECT_TRUE(tab.getRowForTest(0).isExpanded()) << "a rebuild keeps the open state";
    }
    SoundPacksSettingsTab again(rig.installer, nullptr, &props);
    again.setSize(500, 400);
    again.resized();
    ASSERT_EQ(again.getRowCountForTest(), 1);
    EXPECT_TRUE(again.getRowForTest(0).isExpanded());
    EXPECT_TRUE(again.getRowForTest(0).isCreditShown());
    again.getRowForTest(0).getDisclosure().onClick();
    EXPECT_TRUE(props.getUserSettings()->getValue("soundPacksOpen").isEmpty());
}

TEST(SoundPacksSettingsTab, TheIntroPointsAtTheRowsInsteadOfAListBelow) {
    TabRig rig;
    EXPECT_EQ(rig.tab.getIntroTextForTest(), "Optional instruments for the Sampler. They download to this device when "
                                             "you ask for them. Open a pack to see who made it, its licence and every "
                                             "instrument.");
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
    synth::test::waitUntil([&] { return (tab.getFolderRowForTest(0).getNameText()) == ("Field loops"); });
    EXPECT_EQ(tab.getFolderRowForTest(0).getNameText(), "Field loops") << "a rename in the Sounds view shows here";

    const auto file = tree.user.getChildFile("Field/dusty_keys_120_Am_loop.wav");
    tab.getFolderRowForTest(0).getRemoveButton().onClick();
    pump();
    EXPECT_EQ(tab.getFolderRowCountForTest(), 0);
    EXPECT_TRUE(book.folders().empty());
    EXPECT_TRUE(file.existsAsFile());
}
