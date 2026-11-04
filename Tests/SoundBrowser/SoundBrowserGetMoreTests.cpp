// The "Get more" heading of the Places screen folds the packs you can download: a click, Return, Left and Right
// toggle it, the choice is saved, a download in flight keeps it open, the rows fade over 190 ms and the screen reader
// hears the state.
#include "../SoundPacks/SoundPackTestKit.h"
#include "SoundBrowserTestKit.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UserSettings.h"

using namespace sound_browser_test;
using namespace sound_pack_test;
using synth::ui::SoundBrowserComponent;
using Kind = SoundBrowserComponent::RowKind;

namespace {

juce::KeyPress key(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }

juce::MouseEvent mouseAt(SoundBrowserComponent& v, juce::Point<int> at) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys(), 0, 0,
                            0, 0, 0, &v, &v, juce::Time::getCurrentTime(), at.toFloat(), juce::Time::getCurrentTime(),
                            1, false);
}

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

TEST(SoundBrowserGetMore, FoldingFadesTheListOver190MsAndTurnsTheChevron) {
    MotionSeams seams;
    GetMoreRig rig;
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 1.0f);
    rig.view->setGetMoreOpen(false);
    EXPECT_TRUE(rig.view->isListAnimating()) << "the rows fade rather than vanish";
    EXPECT_TRUE(rig.view->getMoreChevron().isTurning());
    synth::ui::ChevronTurn::stepAllForTest(0.5f);
    EXPECT_GT(rig.view->getMoreChevron().openness(), 0.0f);
    EXPECT_LT(rig.view->getMoreChevron().openness(), 1.0f) << "the arrow is between the two on the way";
    rig.settle();
    synth::ui::ChevronTurn::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.view->isListAnimating());
    EXPECT_FLOAT_EQ(rig.view->getMoreChevron().openness(), 0.0f);
    EXPECT_EQ(rig.count(Kind::GetPack), 0);
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
