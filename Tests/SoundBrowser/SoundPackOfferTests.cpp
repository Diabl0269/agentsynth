// The Sounds view lists the sound packs this device lacks ("Get more", and the end of matching searches), greyed and
// with their size, and starts the shared installer's own download from a row. The installer runs inline on a fake
// network (SoundPackTestKit.h), so a test can read both views mid-download.
#include "../SoundPacks/SoundPackTestKit.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Settings/SoundPacks/SoundPacksSettingsTab.h"

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
            if (rows[i].kind == Kind::Hint && rows[i].text == "Get more")
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

TEST(SoundPackOffer, SearchEndsWithThePacksWhoseNameOrInstrumentsMatch) {
    OfferRig rig;
    rig.view->setQuery("grand");
    const auto& rows = rig.view->getRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].kind, Kind::Sound) << "the sounds come first";
    EXPECT_EQ(rows[1].kind, Kind::GetPack);
    EXPECT_TRUE(rows[1].viaInstrument) << "Grand is one of the pack's instruments, not its name";

    rig.view->setQuery("pack core");
    ASSERT_EQ(rig.view->getRows().size(), 1u);
    EXPECT_FALSE(rig.view->getRows()[0].viaInstrument);

    rig.view->setQuery("zzzz");
    EXPECT_EQ(rig.countOf(Kind::GetPack), 0);
    EXPECT_EQ(rig.view->getRows()[0].kind, Kind::Hint);
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
    pump();
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
