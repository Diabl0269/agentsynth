// The Sounds view's own folders: the "Add folder..." row and its chooser, folders dropped from Finder, the folder menu,
// renaming in place, Remove, and a folder whose drive is gone. The native pieces (the chooser, Finder, the pop-up menu)
// are replaced by the view's test seams, so nothing here opens a window.
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include <atomic>

using namespace sound_browser_test;
using synth::ui::SoundBrowserComponent;
using Kind = SoundBrowserComponent::RowKind;
using Action = SoundBrowserComponent::FolderAction;

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

juce::KeyPress key(int code) {
    return juce::KeyPress(code, juce::ModifierKeys(), code == juce::KeyPress::spaceKey ? ' ' : 0);
}

juce::MouseEvent mouseAt(SoundBrowserComponent& v, juce::Point<int> at, int mods = 0) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys(mods),
                            0, 0, 0, 0, 0, &v, &v, juce::Time::getCurrentTime(), at.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

struct FolderRig {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<synth::soundbrowser::SoundFolderBook> book;
    std::unique_ptr<SoundBrowserComponent> view;
    juce::File other = tree.dir.getChildFile("other");
    juce::File revealed;

    FolderRig() {
        tree.fill();
        writeWav(other.getChildFile("kick_one.wav"), 0.2);
        index = makeIndex(tree);
        book = std::make_unique<synth::soundbrowser::SoundFolderBook>(*index);
        book->goLive();
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
        view->setFolderBook(book.get());
        view->setRevealForTest([this](const juce::File& f) { revealed = f; });
        scanAndWait(*index);
        pump();
    }
    void settle() {
        ASSERT_TRUE(index->waitUntilIdleForTests(20000));
        pump();
    }
    int rowOf(Kind kind, const juce::String& text = {}) const {
        for (size_t i = 0; i < view->getRows().size(); ++i)
            if (view->getRows()[i].kind == kind && (text.isEmpty() || view->getRows()[i].text == text))
                return (int)i;
        return -1;
    }
};

} // namespace

TEST(SoundFolderView, AddFolderEndsThePlacesAndOnlyExistsWithABook) {
    FolderRig rig;
    const auto& rows = rig.view->getRows();
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows.back().kind, Kind::AddFolder);
    EXPECT_EQ(rows.back().text, "Add folder...");
    EXPECT_EQ(rows[rows.size() - 2].text, "Your folders") << "the heading shows even before the first folder";

    rig.view->setFolderBook(nullptr);
    EXPECT_EQ(rig.rowOf(Kind::AddFolder), -1);
}

TEST(SoundFolderView, TheChooserAddsAFolderScansItAndSavesIt) {
    FolderRig rig;
    int asked = 0;
    rig.view->setFolderPickerForTest([&](std::function<void(const juce::File&)> picked) {
        ++asked;
        picked(rig.other);
    });
    rig.view->selectRow(rig.rowOf(Kind::AddFolder));
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::returnKey)));
    EXPECT_EQ(asked, 1);
    rig.settle();

    ASSERT_EQ(rig.book->folders().size(), 1u);
    EXPECT_EQ(rig.book->folders()[0].path, rig.other);
    EXPECT_TRUE(rig.book->pathsSetting().contains(rig.other.getFullPathName())) << "saved for the next launch";
    const int row = rig.rowOf(Kind::Place, "other");
    ASSERT_GE(row, 0);
    EXPECT_TRUE(rig.view->getRows()[(size_t)row].userFolder);
    EXPECT_EQ(rig.view->getRows()[(size_t)row].soundCount, 1);
    EXPECT_EQ(rig.view->getSelectedIndex(), row) << "the new folder is selected";
    EXPECT_EQ(rig.index->search("kick_one").size(), 1u);
}

TEST(SoundFolderView, SpaceAndAClickOnTheRowOpenTheChooserToo) {
    FolderRig rig;
    int asked = 0;
    rig.view->setFolderPickerForTest([&](std::function<void(const juce::File&)>) { ++asked; });
    const int row = rig.rowOf(Kind::AddFolder);
    rig.view->selectRow(row);
    EXPECT_TRUE(rig.view->keyPressed(key(juce::KeyPress::spaceKey)));
    rig.view->mouseDown(mouseAt(*rig.view, rig.view->getRowBounds(row).getCentre()));
    EXPECT_EQ(asked, 2);
}

TEST(SoundFolderView, OnlyFoldersAreAcceptedFromFinder) {
    FolderRig rig;
    const auto file = rig.tree.user.getChildFile("Field/dusty_keys_120_Am_loop.wav").getFullPathName();
    EXPECT_TRUE(rig.view->isInterestedInFileDrag({rig.other.getFullPathName()}));
    EXPECT_FALSE(rig.view->isInterestedInFileDrag({file})) << "a file is for the cards and tracks";
    EXPECT_FALSE(rig.view->isInterestedInFileDrag({rig.other.getFullPathName(), file}));
    EXPECT_FALSE(rig.view->isInterestedInFileDrag({}));

    rig.view->fileDragEnter({rig.other.getFullPathName()}, 10, 10);
    EXPECT_TRUE(rig.view->isFolderDropActive());
    rig.view->filesDropped({rig.other.getFullPathName()}, 10, 10);
    EXPECT_FALSE(rig.view->isFolderDropActive());
    rig.settle();
    ASSERT_EQ(rig.book->folders().size(), 1u);
    EXPECT_GE(rig.rowOf(Kind::Place, "other"), 0);

    rig.view->setFolderBook(nullptr);
    EXPECT_FALSE(rig.view->isInterestedInFileDrag({rig.other.getFullPathName()})) << "no book, no drop";
}

TEST(SoundFolderView, ARightClickOnAPackNeverOpensIt) {
    FolderRig rig;
    const int row = rig.rowOf(Kind::Place, "Core instruments");
    ASSERT_GE(row, 0);
    rig.view->mouseDown(
        mouseAt(*rig.view, rig.view->getRowBounds(row).getCentre(), juce::ModifierKeys::rightButtonModifier));
    EXPECT_EQ(rig.view->getPlaceId(), "") << "a right-click selects; it does not navigate";
    EXPECT_EQ(rig.view->getSelectedIndex(), row);
    EXPECT_FALSE(rig.view->showContextMenuForKeyboardFocus()) << "a pack has no folder menu";
}

TEST(SoundFolderView, TheFolderMenuOffersRenameFinderScanAgainAndRemove) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    const int row = rig.rowOf(Kind::Place, "other");
    ASSERT_GE(row, 0);
    juce::StringArray labels;
    auto menu = rig.view->buildFolderMenu(row);
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text.isNotEmpty())
            labels.add(it.getItem().text);
    EXPECT_EQ(labels.joinIntoString("|"), "Rename|Show in " + synth::ui::fileManagerName() + "|Scan again|Remove");
    EXPECT_EQ(rig.view->buildFolderMenu(rig.rowOf(Kind::Place, "Core instruments")).getNumItems(), 0);
}

TEST(SoundFolderView, ShowInFinderAndScanAgainAct) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    const int row = rig.rowOf(Kind::Place, "other");
    EXPECT_TRUE(rig.view->applyFolderAction(row, Action::ShowInFinder));
    EXPECT_EQ(rig.revealed, rig.other);
    const int before = rig.index->headersReadForTests();
    writeWav(rig.other.getChildFile("snare_new.wav"), 0.2);
    EXPECT_TRUE(rig.view->applyFolderAction(row, Action::ScanAgain));
    rig.settle();
    EXPECT_EQ(rig.index->search("snare_new").size(), 1u);
    EXPECT_EQ(rig.index->headersReadForTests(), before + 1) << "only the new file was opened";
}

TEST(SoundFolderView, RenamingChangesTheShownNameOnly) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    const int row = rig.rowOf(Kind::Place, "other");
    ASSERT_TRUE(rig.view->applyFolderAction(row, Action::Rename));
    auto& label = rig.view->getRenameLabel();
    EXPECT_TRUE(label.isVisible()) << "the editor sits over the row";
    EXPECT_EQ(label.getText(), "other");

    label.setText("Drum hits", juce::sendNotification);
    pump();
    EXPECT_GE(rig.rowOf(Kind::Place, "Drum hits"), 0);
    EXPECT_EQ(rig.rowOf(Kind::Place, "other"), -1);
    EXPECT_TRUE(rig.other.isDirectory()) << "the folder on disk is untouched";
    EXPECT_TRUE(rig.other.getChildFile("kick_one.wav").existsAsFile());
    EXPECT_EQ(rig.index->find(rig.other.getChildFile("kick_one.wav").getFullPathName())->placeName, "Drum hits");
    EXPECT_EQ(rig.book->folders()[0].path, rig.other);
}

TEST(SoundFolderView, RemoveForgetsTheFolderAndKeepsItsFiles) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    const auto file = rig.other.getChildFile("kick_one.wav");
    const int row = rig.rowOf(Kind::Place, "other");
    EXPECT_TRUE(rig.view->applyFolderAction(row, Action::Remove));
    EXPECT_EQ(rig.rowOf(Kind::Place, "other"), -1) << "gone from the list at once";
    EXPECT_TRUE(rig.book->folders().empty());
    rig.settle();
    EXPECT_EQ(rig.index->search("kick_one").size(), 0u);
    EXPECT_TRUE(file.existsAsFile()) << "nothing on disk changes";
    EXPECT_TRUE(rig.other.isDirectory());
}

TEST(SoundFolderView, ARemovedFolderThatWasOpenGoesBackToPlaces) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    rig.view->showLocation("folder:" + rig.other.getFullPathName(), {});
    rig.view->completeListAnimationForTest();
    rig.book->remove(rig.other);
    EXPECT_EQ(rig.view->getPlaceId(), "");
}

TEST(SoundFolderView, AFolderThatIsGoneShowsNotFoundAndComesBack) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    const auto parked = rig.tree.dir.getChildFile("parked");
    ASSERT_TRUE(rig.other.moveFileTo(parked));
    rig.view->checkFoldersOnReturn();
    rig.settle();
    int row = rig.rowOf(Kind::Place, "other");
    ASSERT_GE(row, 0) << "it stays listed";
    EXPECT_TRUE(rig.view->getRows()[(size_t)row].missing);
    EXPECT_EQ(rig.view->getRows()[(size_t)row].detail, "Not found");
    rig.view->selectRow(row);
    EXPECT_TRUE(rig.view->getAccessibilityValueText().contains("not found"));
    auto menu = rig.view->buildFolderMenu(row);
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Show in " + synth::ui::fileManagerName())
            EXPECT_FALSE(it.getItem().isEnabled) << "there is nothing to show";

    ASSERT_TRUE(parked.moveFileTo(rig.other));
    rig.view->checkFoldersOnReturn();
    rig.settle();
    row = rig.rowOf(Kind::Place, "other");
    EXPECT_FALSE(rig.view->getRows()[(size_t)row].missing);
    EXPECT_EQ(rig.view->getRows()[(size_t)row].detail, "1");
}

TEST(SoundFolderView, ComingBackToTheAppChecksTheFoldersOnce) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    bool foreground = false;
    rig.view->setForegroundCheckForTest([&] { return foreground; });
    rig.view->noteAppFocusForTest(); // away
    const auto parked = rig.tree.dir.getChildFile("parked");
    ASSERT_TRUE(rig.other.moveFileTo(parked));
    foreground = true;
    rig.view->noteAppFocusForTest(); // back
    rig.settle();
    EXPECT_TRUE(rig.view->getRows()[(size_t)rig.rowOf(Kind::Place, "other")].missing);
}

TEST(SoundFolderView, WhileAFolderIsBeingReadItsRowSaysScanning) {
    FolderRig rig;
    rig.book->add(rig.other);
    rig.settle();
    std::atomic<bool> release{false};
    rig.index->setScanHoldForTests([&](const juce::File& f) {
        if (f.isAChildOf(rig.other))
            while (!release.load())
                juce::Thread::sleep(2);
    });
    writeWav(rig.other.getChildFile("new_one.wav"), 0.2);
    rig.view->applyFolderAction(rig.rowOf(Kind::Place, "other"), Action::ScanAgain);
    bool saw = false;
    for (int i = 0; i < 400 && !saw; ++i) {
        pump();
        const int row = rig.rowOf(Kind::Place, "other");
        saw = row >= 0 && rig.view->getRows()[(size_t)row].detail == "Scanning";
    }
    EXPECT_TRUE(saw);
    release = true;
    rig.settle();
    EXPECT_EQ(rig.view->getRows()[(size_t)rig.rowOf(Kind::Place, "other")].detail, "2");
}
