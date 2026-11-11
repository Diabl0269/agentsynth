// The Sounds view lists the sound packs this device lacks ("Get more", and the end of matching searches), greyed and
// with their size, and starts the shared installer's own download from a row. The installer runs inline on a fake
// network (SoundPackTestKit.h), so a test can read both views mid-download.
#include "../SoundPacks/SoundPackTestKit.h"
#include "../TestWait.h"
#include "../UI/Layout/FadeVisibilityTestGuard.h"
#include "SoundBrowserTestKit.h"
#include "UI/Layout/ChevronTurn.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Settings/SoundPacks/SoundPacksSettingsTab.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UserSettings.h"

using namespace sound_browser_test;
using namespace sound_pack_test;
using synth::ui::SoundBrowserComponent;
using Kind = SoundBrowserComponent::RowKind;
using Installer = synth::soundpacks::SoundPackInstaller;

namespace {

juce::KeyPress key(int code) {
    return juce::KeyPress(code, juce::ModifierKeys(), code == juce::KeyPress::spaceKey ? ' ' : 0);
}

juce::MouseEvent mouseAt(SoundBrowserComponent& v, juce::Point<int> at, int mods = 0) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys(mods),
                            0, 0, 0, 0, 0, &v, &v, juce::Time::getCurrentTime(), at.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

// An installer on a fake network offering `ids`, a Sounds view over an index that reads the installer's folder, and a
// user folder holding one sound named "grand_hit".
struct OfferRig : Rig {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;

    explicit OfferRig(const juce::StringArray& ids = {"core"}) {
        writeWav(tree.user.getChildFile("grand_hit.wav"), 0.3);
        index = std::make_unique<synth::soundbrowser::SoundIndex>(
            juce::File(), [this] { return std::vector<juce::File>{dir.path()}; });
        index->setUserFolders({tree.user});
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
        view->setPackInstaller(&installer);
        offer(ids);
        scanAndWait(*index);
        pump();
    }
    void offer(const juce::StringArray& ids) {
        juce::StringArray entries;
        for (const auto& id : ids) {
            const auto zip = goodPackZip(id, "1.0.0");
            entries.add(packEntryJson(id, "1.0.0", zip));
            server.bodies["https://example.test/" + id + "-1.0.0.zip"] = zip;
        }
        const auto json = indexJsonOf(entries);
        server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
        installer.refreshIndex();
    }
    int firstOf(Kind kind) const {
        for (size_t i = 0; i < view->getRows().size(); ++i)
            if (view->getRows()[i].kind == kind)
                return (int)i;
        return -1;
    }
    int countOf(Kind kind) const {
        int n = 0;
        for (const auto& r : view->getRows())
            n += r.kind == kind ? 1 : 0;
        return n;
    }
};

} // namespace

TEST(SoundPackOffer, NotInstalledPacksAreListedUnderGetMoreWithTheirSize) {
    OfferRig rig({"core", "animals"});
    const auto& rows = rig.view->getRows();
    const int heading = [&] {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].kind == Kind::PackHeading && rows[i].text == "Get more")
                return (int)i;
        return -1;
    }();
    ASSERT_GE(heading, 0);
    ASSERT_EQ(rig.countOf(Kind::GetPack), 2);
    EXPECT_EQ(rows[(size_t)heading + 1].kind, Kind::GetPack);
    EXPECT_EQ(rows[(size_t)heading + 1].text, "Pack core");
    const auto info = rig.installer.row("core");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(rows[(size_t)heading + 1].detail, synth::soundpacks::formatPackSize(info->downloadBytes));
    EXPECT_TRUE(rows[(size_t)heading + 1].navigable()) << "a greyed row is still a list row for Up and Down";
}

TEST(SoundPackOffer, GetMoreWaitsForThePackListToLoad) {
    Rig rig;
    synth::soundbrowser::SoundLists lists;
    synth::soundbrowser::SoundIndex index({}, [&] { return std::vector<juce::File>{rig.dir.path()}; });
    SoundBrowserComponent view(index, lists);
    view.setSize(300, 700);
    view.setPackInstaller(&rig.installer);
    for (const auto& row : view.getRows())
        EXPECT_NE(row.kind, Kind::GetPack);
    rig.publish("core", "1.0.0");
    int packs = 0;
    for (const auto& row : view.getRows())
        packs += row.kind == Kind::GetPack ? 1 : 0;
    EXPECT_EQ(packs, 1) << "the view follows the installer once the list arrives";
}

TEST(SoundPackOffer, SearchStartsWithThePacksWhoseNameOrInstrumentsMatch) {
    OfferRig rig;
    rig.view->setQuery("grand");
    const auto& rows = rig.view->getRows();
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].kind, Kind::Hint);
    EXPECT_EQ(rows[0].text, "Get more");
    EXPECT_FALSE(rows[0].navigable()) << "the heading is not a stop";
    EXPECT_EQ(rows[1].kind, Kind::GetPack) << "the packs come first";
    EXPECT_TRUE(rows[1].navigable()) << "so Down from the search box lands on the pack";
    EXPECT_TRUE(rows[1].viaInstrument) << "Grand is one of the pack's instruments, not its name";
    EXPECT_EQ(rows[2].kind, Kind::Sound);

    rig.view->setQuery("pack core");
    const auto& packOnly = rig.view->getRows();
    ASSERT_EQ(packOnly.size(), 3u);
    EXPECT_EQ(packOnly[0].text, "Get more");
    EXPECT_FALSE(packOnly[1].viaInstrument);
    EXPECT_EQ(packOnly[2].text, "No matching sounds") << "a search that finds only packs still says no sound matched";

    rig.view->setQuery("zzzz");
    EXPECT_EQ(rig.countOf(Kind::GetPack), 0);
    ASSERT_EQ(rig.view->getRows().size(), 1u);
    EXPECT_EQ(rig.view->getRows()[0].kind, Kind::Hint);
    EXPECT_EQ(rig.view->getRows()[0].text, "No matching sounds");
}

TEST(SoundPackOffer, SearchKeepsThePacksWhileAFilterChipIsOn) {
    OfferRig rig;
    rig.view->getFilters().setKind(synth::soundbrowser::SoundKind::OneShot, true);
    rig.view->setQuery("grand");
    const auto& rows = rig.view->getRows();
    ASSERT_GE(rows.size(), 3u);
    EXPECT_EQ(rows[0].text, "Get more");
    EXPECT_EQ(rows[1].kind, Kind::GetPack);
    EXPECT_EQ(rows[2].kind, Kind::Sound) << "the sound still shows, below the pack";

    rig.view->getFilters().setKind(synth::soundbrowser::SoundKind::OneShot, false);
    rig.view->getFilters().setKind(synth::soundbrowser::SoundKind::Loop, true);
    rig.view->setQuery("");
    rig.view->setQuery("grand");
    EXPECT_EQ(rig.view->getRows()[1].kind, Kind::GetPack) << "no sound passes Loops, the pack is still listed";
    EXPECT_EQ(rig.countOf(Kind::Sound), 0);
}

TEST(SoundPackOffer, SearchFindsAPackByTheCategoriesOfItsInstruments) {
    OfferRig rig;
    const auto entry = [&](const juce::String& id, const juce::String& instrumentName, const juce::String& category) {
        const auto zip = goodPackZip(id, "1.0.0");
        rig.server.bodies["https://example.test/" + id + "-1.0.0.zip"] = zip;
        auto json = packEntryJson(id, "1.0.0", zip);
        return json.replace(R"("name":"Grand","category":"Keys")",
                            R"("name":")" + instrumentName + R"(","category":")" + category + "\"");
    };
    const auto json = indexJsonOf({entry("core", "Kick", "Drums"), entry("animals", "Raven", "Crow calls")});
    rig.server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
    rig.installer.refreshIndex();
    pump();
    const auto packsFor = [&](const juce::String& query) {
        rig.view->setQuery(query);
        juce::StringArray names;
        for (const auto& row : rig.view->getRows())
            if (row.kind == Kind::GetPack)
                names.add(row.packId);
        return names.joinIntoString(",");
    };
    EXPECT_EQ(packsFor("drums"), "core") << "Drums is a category in the Core pack, not a name";
    EXPECT_EQ(packsFor("crow"), "animals");
    EXPECT_EQ(packsFor("rock"), "");
}

// Regression test: the Sounds view restored at startup turned visible while its window was still hidden, skipped the
// fetch, and never got a second call, so Get more stayed empty until the view was switched away and back.
TEST(SoundPackOffer, AViewThatTurnsVisibleBeforeItsWindowShowsStillLoadsThePackList) {
    Rig rig;
    const auto json = indexJson("core", "1.0.0", goodPackZip("core", "1.0.0"), {});
    rig.server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
    synth::soundbrowser::SoundLists lists;
    synth::soundbrowser::SoundIndex index({}, [&] { return std::vector<juce::File>{rig.dir.path()}; });
    SoundBrowserComponent view(index, lists);
    view.setSize(300, 700);
    view.setPackInstaller(&rig.installer);
    ASSERT_EQ(rig.installer.indexStatus(), Installer::IndexStatus::NotLoaded) << "a hidden view fetches nothing";
    view.setVisible(true);
    ASSERT_FALSE(view.isShowing()) << "no window: the startup case";
    EXPECT_EQ(rig.installer.indexStatus(), Installer::IndexStatus::Loaded);
    int packs = 0;
    for (const auto& row : view.getRows())
        packs += row.kind == Kind::GetPack ? 1 : 0;
    EXPECT_EQ(packs, 1);
}

TEST(SoundPackOffer, AnInstallerGivenToAVisibleViewLoadsThePackList) {
    Rig rig;
    const auto json = indexJson("core", "1.0.0", goodPackZip("core", "1.0.0"), {});
    rig.server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
    synth::soundbrowser::SoundLists lists;
    synth::soundbrowser::SoundIndex index({}, [&] { return std::vector<juce::File>{rig.dir.path()}; });
    SoundBrowserComponent view(index, lists);
    view.setSize(300, 700);
    view.setVisible(true);
    view.setPackInstaller(&rig.installer);
    EXPECT_EQ(rig.installer.indexStatus(), Installer::IndexStatus::Loaded);
}

TEST(SoundPackOffer, AGreyedRowRefusesToBeDragged) {
    OfferRig rig;
    const int row = rig.firstOf(Kind::GetPack);
    ASSERT_GE(row, 0);
    EXPECT_FALSE(rig.view->beginSoundDrag(row));
    const auto at = rig.view->getRowBounds(row).getCentre().withX(20);
    rig.view->mouseDown(mouseAt(*rig.view, at));
    rig.view->mouseDrag(mouseAt(*rig.view, at.translated(60, 0)));
    EXPECT_FALSE(rig.view->isSoundDragging());
}

TEST(SoundPackOffer, ATooltipNamesThePackItsSizeAndWhatItIsFor) {
    OfferRig rig;
    const auto size = synth::soundpacks::formatPackSize(rig.installer.row("core")->downloadBytes);
    const int row = rig.firstOf(Kind::GetPack);
    rig.view->mouseMove(mouseAt(*rig.view, rig.view->getRowBounds(row).getCentre().withX(20)));
    EXPECT_EQ(rig.view->getTooltip(), "Download Pack core (" + size + ")");
    rig.view->setQuery("grand");
    const int found = rig.firstOf(Kind::GetPack);
    rig.view->mouseMove(mouseAt(*rig.view, rig.view->getRowBounds(found).getCentre().withX(21)));
    EXPECT_EQ(rig.view->getTooltip(), "Download Pack core (" + size + ") to use this sound");
}

TEST(SoundPackOffer, TheButtonStartsTheInstallerJobAndBothViewsShowItsProgress) {
    OfferRig rig;
    SoundPacksSettingsTab tab(rig.installer);
    tab.setSize(500, 400);
    tab.resized();
    const int row = rig.firstOf(Kind::GetPack);
    ASSERT_GE(row, 0);

    std::vector<std::pair<float, float>> seen; // the browser's fraction, the Settings bar's fraction
    bool downloadingInBoth = true;
    rig.server.onChunk = [&] {
        const auto& browserRow = rig.view->getRows()[(size_t)rig.firstOf(Kind::GetPack)];
        seen.emplace_back(browserRow.progress, tab.getRowForTest(0).getProgress().getFraction());
        downloadingInBoth = downloadingInBoth && browserRow.busy && browserRow.cancellable &&
                            rig.installer.row("core")->state == SoundPackInstaller::State::Downloading &&
                            tab.getRowForTest(0).getStatusText().startsWith("Downloading");
    };

    const auto button = rig.view->getPackButtonBounds(row);
    ASSERT_FALSE(button.isEmpty());
    rig.view->mouseDown(mouseAt(*rig.view, button.getCentre()));

    ASSERT_GE(seen.size(), 2u);
    for (const auto& [browser, settings] : seen) {
        EXPECT_FLOAT_EQ(browser, settings) << "one job, one progress";
        EXPECT_GE(browser, 0.0f);
        EXPECT_LE(browser, 1.0f);
    }
    EXPECT_LT(seen.front().first, seen.back().first);
    EXPECT_TRUE(downloadingInBoth);
    EXPECT_EQ(rig.installer.row("core")->state, SoundPackInstaller::State::Installed);
    EXPECT_TRUE(rig.dir.path().getChildFile("core/pack.json").existsAsFile());
}

TEST(SoundPackOffer, ADownloadStartedInSettingsShowsInTheBrowserRow) {
    OfferRig rig;
    SoundPacksSettingsTab tab(rig.installer);
    tab.setSize(500, 400);
    tab.resized();
    bool sawBusy = false;
    rig.server.onChunk = [&] { sawBusy = sawBusy || rig.view->getRows()[(size_t)rig.firstOf(Kind::GetPack)].busy; };
    tab.getRowForTest(0).getDownloadButton().onClick();
    EXPECT_TRUE(sawBusy);
}

TEST(SoundPackOffer, AnInstalledPackBecomesANormalPlace) {
    OfferRig rig;
    ASSERT_TRUE(rig.installer.install("core"));
    // Until the index has scanned the new folder the row stays, finishing, so the pack is never in neither list.
    ASSERT_GE(rig.firstOf(Kind::GetPack), 0);
    EXPECT_TRUE(rig.view->getRows()[(size_t)rig.firstOf(Kind::GetPack)].busy);

    scanAndWait(*rig.index);
    synth::test::waitUntil([&] { return rig.countOf(Kind::GetPack) == 0; });
    EXPECT_EQ(rig.countOf(Kind::GetPack), 0);
    bool place = false;
    for (const auto& row : rig.view->getRows())
        place = place || (row.kind == Kind::Place && row.placeId == "core");
    EXPECT_TRUE(place);
}

TEST(SoundPackOffer, ReturnAndSpaceOnAPackRowStartTheDownload) {
    OfferRig rig({"core", "animals"});
    rig.view->selectRow(rig.firstOf(Kind::GetPack));
    EXPECT_EQ(rig.installer.row("animals")->state, SoundPackInstaller::State::NotInstalled);
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::returnKey)));
    EXPECT_EQ(rig.installer.row("core")->state, SoundPackInstaller::State::Installed) << "the first row is core";

    int second = -1;
    for (size_t i = 0; i < rig.view->getRows().size(); ++i)
        if (rig.view->getRows()[i].packId == "animals")
            second = (int)i;
    ASSERT_GE(second, 0);
    rig.view->selectRow(second);
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::spaceKey)));
    EXPECT_EQ(rig.installer.row("animals")->state, SoundPackInstaller::State::Installed);
}

TEST(SoundPackOffer, DeleteCancelsARunningDownload) {
    OfferRig rig;
    rig.view->selectRow(rig.firstOf(Kind::GetPack));
    rig.server.onChunk = [&] { EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::deleteKey))); };
    rig.view->keyPressed(key(juce::KeyPress::returnKey));
    EXPECT_EQ(rig.installer.row("core")->state, SoundPackInstaller::State::NotInstalled);
    EXPECT_FALSE(rig.dir.path().getChildFile("core").isDirectory());
}

TEST(SoundPackOffer, TheScreenReaderHearsThatARowIsNotDownloaded) {
    OfferRig rig;
    rig.view->selectRow(rig.firstOf(Kind::GetPack));
    const auto spoken = rig.view->getAccessibilityValueText();
    EXPECT_TRUE(spoken.startsWith("Pack core, not downloaded, ")) << spoken;
    EXPECT_TRUE(spoken.contains("press Return to download")) << spoken;
}

// The "Get more" heading of the Places screen folds the packs you can download: a click, Return, Left and Right
// toggle it, the choice is saved, a download in flight keeps it open, the rows fade over 190 ms and the screen reader
// hears the state.
namespace {

struct GetMoreRig : Rig {
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;

    GetMoreRig() {
        index = std::make_unique<synth::soundbrowser::SoundIndex>(
            juce::File(), [this] { return std::vector<juce::File>{dir.path()}; });
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
        view->setAnimateOffscreenForTest(true);
        view->setPackInstaller(&installer);
        juce::StringArray entries;
        for (const char* id : {"core", "animals"}) {
            const auto zip = goodPackZip(id, "1.0.0");
            entries.add(packEntryJson(id, "1.0.0", zip));
            server.bodies[juce::String("https://example.test/") + id + "-1.0.0.zip"] = zip;
        }
        const auto json = indexJsonOf(entries);
        server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
        installer.refreshIndex();
        index->rescan();
        index->waitUntilIdleForTests(20000);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    }
    int count(Kind kind) const {
        int n = 0;
        for (const auto& r : view->getRows())
            n += r.kind == kind ? 1 : 0;
        return n;
    }
    int heading() const {
        for (size_t i = 0; i < view->getRows().size(); ++i)
            if (view->getRows()[i].kind == Kind::PackHeading)
                return (int)i;
        return -1;
    }
    void settle() { view->completeListAnimationForTest(); }
};

} // namespace

TEST(SoundBrowserGetMore, TheHeadingIsANavigableRowThatCountsThePacks) {
    GetMoreRig rig;
    const int heading = rig.heading();
    ASSERT_GE(heading, 0);
    const auto& row = rig.view->getRows()[(size_t)heading];
    EXPECT_TRUE(row.navigable()) << "Up and Down reach it";
    EXPECT_EQ(row.detail, "2 packs");
    EXPECT_EQ(rig.count(Kind::GetPack), 2);
    EXPECT_EQ(rig.view->getRows()[(size_t)heading + 1].kind, Kind::GetPack);
}

TEST(SoundBrowserGetMore, AClickOnTheRowFoldsAndOpensIt) {
    GetMoreRig rig;
    const auto at = rig.view->getRowBounds(rig.heading()).getCentre().withX(60);
    rig.view->mouseDown(mouseAt(*rig.view, at));
    rig.view->mouseUp(mouseAt(*rig.view, at));
    rig.settle();
    EXPECT_EQ(rig.count(Kind::GetPack), 0);
    ASSERT_GE(rig.heading(), 0) << "the heading stays so it can be opened";
    EXPECT_EQ(rig.view->getSelectedIndex(), rig.heading());
    rig.view->mouseDown(mouseAt(*rig.view, rig.view->getRowBounds(rig.heading()).getCentre().withX(60)));
    rig.view->mouseUp(mouseAt(*rig.view, rig.view->getRowBounds(rig.heading()).getCentre().withX(60)));
    rig.settle();
    EXPECT_EQ(rig.count(Kind::GetPack), 2);
}

TEST(SoundBrowserGetMore, ReturnToggleAndLeftFoldsRightOpens) {
    GetMoreRig rig;
    rig.view->selectRow(rig.heading());
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::returnKey)));
    rig.settle();
    EXPECT_EQ(rig.count(Kind::GetPack), 0);
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::returnKey)));
    rig.settle();
    EXPECT_EQ(rig.count(Kind::GetPack), 2);
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::leftKey)));
    rig.settle();
    EXPECT_EQ(rig.count(Kind::GetPack), 0);
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::leftKey))) << "Left on a folded heading is taken, not goUp";
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::rightKey)));
    rig.settle();
    EXPECT_EQ(rig.count(Kind::GetPack), 2);
    EXPECT_EQ(rig.view->getSelectedIndex(), rig.heading());
}

TEST(SoundBrowserGetMore, TheScreenReaderHearsTheCountAndTheState) {
    GetMoreRig rig;
    rig.view->selectRow(rig.heading());
    EXPECT_TRUE(rig.view->getAccessibilityValueText().startsWith("Get more, 2 packs, expanded"));
    rig.view->setGetMoreOpen(false);
    rig.settle();
    EXPECT_TRUE(rig.view->getAccessibilityValueText().startsWith("Get more, 2 packs, collapsed"));
}

namespace {

juce::PropertiesFile::Options settingsOptions() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "sound-browser-get-more-test-" + juce::Uuid().toDashedString();
    opts.filenameSuffix = "settings";
    opts.folderName = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName();
    return opts;
}

// Turns the animation seams on for one test and always off again.
struct MotionSeams {
    MotionSeams() { synth::ui::FadeVisibility::setAnimateOffScreenForTest(true); }
    ~MotionSeams() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

} // namespace

TEST(SoundBrowserGetMore, TheChoiceIsSavedAndRestoredByANewViewBeforeItsWindowShows) {
    juce::PropertiesFile settings(settingsOptions());
    Rig rig;
    {
        ModuleLibraryComponent library;
        synth::ui::LibrarySidebarViews sidebar(library, &settings);
        EXPECT_TRUE(sidebar.getSounds().isGetMoreOpen()) << "open by default";
        sidebar.getSounds().setGetMoreOpen(false);
        EXPECT_FALSE(settings.getBoolValue(synth::kSoundBrowserGetMoreOpenSettingKey, true));
    }
    ModuleLibraryComponent library;
    synth::ui::LibrarySidebarViews again(library, &settings);
    auto& sounds = again.getSounds();
    EXPECT_FALSE(sounds.isGetMoreOpen());
    sounds.setSize(300, 700);
    sounds.setVisible(true);
    ASSERT_FALSE(sounds.isShowing()) << "no window: the startup case";
    sounds.setPackInstaller(&rig.installer);
    rig.publish("core", "1.0.0");
    int headings = 0;
    int packs = 0;
    for (const auto& row : sounds.getRows()) {
        headings += row.kind == Kind::PackHeading ? 1 : 0;
        packs += row.kind == Kind::GetPack ? 1 : 0;
    }
    EXPECT_EQ(headings, 1);
    EXPECT_EQ(packs, 0) << "the restored choice folds the section without the view being touched";
    EXPECT_EQ(sounds.getGetMoreChevronOpenness(), 0.0f);
}

TEST(SoundBrowserGetMore, ADownloadKeepsTheSectionOpenWithoutChangingTheChoice) {
    GetMoreRig rig;
    const int animals = [&] {
        for (size_t i = 0; i < rig.view->getRows().size(); ++i)
            if (rig.view->getRows()[i].packId == "animals")
                return (int)i;
        return -1;
    }();
    ASSERT_GE(animals, 0);
    bool openThroughout = true;
    bool sawChunk = false;
    rig.server.onChunk = [&] {
        if (!sawChunk) {
            sawChunk = true;
            rig.view->setGetMoreOpen(false, false); // the user folds it mid-download
        }
        openThroughout = openThroughout && rig.view->isGetMoreShowing() && rig.count(Kind::GetPack) >= 1;
    };
    ASSERT_TRUE(rig.view->toggleDownload(animals));
    ASSERT_TRUE(sawChunk);
    EXPECT_TRUE(openThroughout) << "a pack that downloads is never hidden";
    EXPECT_FALSE(rig.view->isGetMoreOpen()) << "the saved choice is the user's";
}

TEST(SoundBrowserGetMore, FoldingSlidesThePacksOnTheChevronsClockAndTurnsTheChevron) {
    MotionSeams seams;
    GetMoreRig rig;
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 1.0f);
    rig.view->setGetMoreOpen(false);
    EXPECT_FALSE(rig.view->isListAnimating()) << "no cross-fade: the rows above the packs never dissolve";
    EXPECT_TRUE(rig.view->isGetMoreFolding());
    EXPECT_TRUE(rig.view->getMoreChevron().isTurning());
    synth::ui::ChevronTurn::stepAllForTest(0.5f);
    EXPECT_GT(rig.view->getMoreChevron().openness(), 0.0f);
    EXPECT_LT(rig.view->getMoreChevron().openness(), 1.0f) << "the arrow is between the two on the way";
    EXPECT_GT(rig.view->getMoreFoldShift(), 0);
    synth::ui::ChevronTurn::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.view->getMoreChevron().isTurning());
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 0.0f);
    EXPECT_EQ(rig.count(Kind::GetPack), 0);
}

namespace {

juce::Image paintWhole(SoundBrowserComponent& view) {
    juce::Image image(juce::Image::ARGB, view.getWidth(), view.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    view.paintEntireComponent(g, false);
    return image;
}

// True when every pixel in the band [top, bottom) is the same in both pictures.
bool bandsEqual(const juce::Image& a, const juce::Image& b, int top, int bottom) {
    for (int y = top; y < bottom; ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

} // namespace

// Regression test for FRO1542: the fold cross-faded the whole list, so every row above the packs dissolved too.
TEST(SoundBrowserGetMore, MidSlideTheRowsAboveTheHeadingPaintExactlyAsAtRestInADarkAndALightTheme) {
    MotionSeams seams;
    GetMoreRig rig;
    synth::theme::AppLookAndFeel laf;
    rig.view->setLookAndFeel(&laf);
    for (const auto& theme : {synth::theme::makeObsidian(), synth::theme::makeDaylight()}) {
        laf.applyTheme(theme);
        rig.view->setGetMoreOpen(true, false);
        synth::ui::ChevronTurn::stepAllForTest(1.0f);
        const int headingTop = rig.view->getRowBounds(rig.heading()).getY();
        const int listTop = rig.view->listArea().getY();
        ASSERT_GT(headingTop, listTop) << "Places has rows above the heading";
        const auto rest = paintWhole(*rig.view);
        for (const bool opening : {false, true}) {
            rig.view->setGetMoreOpen(opening);
            synth::ui::ChevronTurn::stepAllForTest(0.5f);
            const auto mid = paintWhole(*rig.view);
            EXPECT_TRUE(bandsEqual(rest, mid, listTop, headingTop)) << theme.id << (opening ? " opening" : " closing");
            const auto headingBottom = headingTop + SoundBrowserComponent::kRowHeight;
            EXPECT_FALSE(bandsEqual(rest, mid, headingBottom, rig.view->listArea().getBottom()))
                << theme.id << ": the rows below the heading are mid-slide";
            synth::ui::ChevronTurn::stepAllForTest(1.0f);
        }
    }
    rig.view->setLookAndFeel(nullptr);
}

TEST(SoundBrowserGetMore, OpeningMovesThePacksMonotonicallyFromUnderTheHeadingAndClosingMirrorsIt) {
    MotionSeams seams;
    GetMoreRig rig;
    rig.view->setGetMoreOpen(false, false);
    synth::ui::ChevronTurn::stepAllForTest(1.0f);
    const int block = 2 * SoundBrowserComponent::kRowHeight;

    rig.view->setGetMoreOpen(true);
    int previous = block + 1;
    std::vector<int> opening;
    for (const float t : {0.2f, 0.5f, 0.8f}) {
        synth::ui::ChevronTurn::stepAllForTest(t);
        const int shift = rig.view->getMoreFoldShift();
        opening.push_back(shift);
        EXPECT_GT(shift, 0);
        EXPECT_LT(shift, block) << "between the closed position (the whole block) and the open one (none)";
        EXPECT_LT(shift, previous) << "the packs only ever move down";
        previous = shift;
    }
    synth::ui::ChevronTurn::stepAllForTest(1.0f);
    EXPECT_EQ(rig.view->getMoreFoldShift(), 0);
    EXPECT_FALSE(rig.view->isGetMoreFolding()) << "landed";

    rig.view->setGetMoreOpen(false);
    EXPECT_EQ(rig.count(Kind::GetPack), 0) << "the list is already the folded one";
    previous = -1;
    for (const float t : {0.2f, 0.5f, 0.8f}) {
        synth::ui::ChevronTurn::stepAllForTest(t);
        const int shift = rig.view->getMoreFoldShift();
        EXPECT_GT(shift, 0);
        EXPECT_LT(shift, block);
        EXPECT_GT(shift, previous) << "the packs only ever move up, under the heading";
        previous = shift;
    }
    synth::ui::ChevronTurn::stepAllForTest(1.0f);
    EXPECT_EQ(rig.view->getMoreFoldShift(), 0);
    EXPECT_FALSE(rig.view->isGetMoreFolding());
}

TEST(SoundBrowserGetMore, AnimationsOffLandsTheFoldAtOnce) {
    FadeAnimateGuard off(synth::ui::AnimationMode::off);
    GetMoreRig rig;
    rig.view->setGetMoreOpen(false);
    EXPECT_FALSE(rig.view->isGetMoreFolding());
    EXPECT_FALSE(rig.view->isListAnimating());
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 0.0f);
    rig.view->setGetMoreOpen(true);
    EXPECT_FALSE(rig.view->isGetMoreFolding());
    EXPECT_EQ(rig.count(Kind::GetPack), 2);
}

TEST(SoundBrowserGetMore, UnderReduceMotionOnlyThePacksFadeAndNothingMoves) {
    MotionSeams seams;
    synth::ui::setReducedMotionForTest(true);
    GetMoreRig rig;
    synth::theme::AppLookAndFeel laf;
    rig.view->setLookAndFeel(&laf);
    laf.applyTheme(synth::theme::makeObsidian());
    const int headingTop = rig.view->getRowBounds(rig.heading()).getY();
    const int listTop = rig.view->listArea().getY();
    const auto rest = paintWhole(*rig.view);
    rig.view->setGetMoreOpen(false);
    ASSERT_TRUE(rig.view->isListAnimating()) << "a short fade, not a turn";
    rig.view->stepListAnimationForTest(40.0);
    EXPECT_EQ(rig.view->getMoreFoldShift(), 0);
    const auto mid = paintWhole(*rig.view);
    EXPECT_TRUE(bandsEqual(rest, mid, listTop, headingTop));
    EXPECT_FALSE(
        bandsEqual(rest, mid, headingTop + SoundBrowserComponent::kRowHeight, rig.view->listArea().getBottom()))
        << "the packs are partly faded";
    rig.settle();
    EXPECT_FALSE(rig.view->isGetMoreFolding());
    rig.view->setLookAndFeel(nullptr);
}

TEST(SoundBrowserGetMore, UnderReduceMotionTheChevronFlipsAtOnce) {
    MotionSeams seams;
    synth::ui::setReducedMotionForTest(true);
    GetMoreRig rig;
    rig.view->setGetMoreOpen(false);
    EXPECT_FALSE(rig.view->getMoreChevron().isTurning());
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 0.0f);
    rig.view->setGetMoreOpen(true);
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 1.0f);
}
