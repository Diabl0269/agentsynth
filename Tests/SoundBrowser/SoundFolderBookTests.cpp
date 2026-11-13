// The user's own sound folders in Core: adding, scanning, renaming (a label only), removing (the files stay), a folder
// whose drive is unplugged, and results that reach the snapshot while a scan is still running.
#include "SoundBrowser/SoundFolderBook.h"
#include "SoundBrowserTestKit.h"
#include <atomic>

using namespace sound_browser_test;
using synth::soundbrowser::SoundFolderBook;

namespace {

struct BookRig {
    TempTree tree;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index = makeIndex(tree);
    SoundFolderBook book{*index};
    juce::File second = tree.dir.getChildFile("second");

    BookRig() {
        tree.fill();
        writeWav(second.getChildFile("one.wav"), 0.2);
        book.goLive();
    }
    void settle() { ASSERT_TRUE(index->waitUntilIdleForTests(20000)); }
    int soundsIn(const juce::String& placeId) {
        int n = 0;
        for (const auto& e : index->snapshot()->entries)
            n += e.placeId == placeId ? 1 : 0;
        return n;
    }
    const synth::soundbrowser::SoundPlace* place(const juce::File& folder) {
        for (const auto& p : index->snapshot()->places)
            if (p.id == "folder:" + folder.getFullPathName())
                return &p;
        return nullptr;
    }
};

} // namespace

TEST(SoundFolderBook, AddingAFolderListsItScansItAndPersists) {
    BookRig rig;
    EXPECT_TRUE(rig.book.add(rig.tree.user));
    EXPECT_FALSE(rig.book.add(rig.tree.user)) << "a folder is listed once";
    EXPECT_FALSE(rig.book.add(rig.tree.dir.getChildFile("nowhere"))) << "a path that is not a folder is refused";
    EXPECT_FALSE(rig.book.add(rig.tree.user.getChildFile("Field/dusty_keys_120_Am_loop.wav"))) << "so is a file";
    rig.settle();
    EXPECT_EQ(rig.soundsIn("folder:" + rig.tree.user.getFullPathName()), 1);

    SoundFolderBook restored(*rig.index);
    restored.load(rig.book.pathsSetting(), rig.book.namesSetting());
    ASSERT_EQ(restored.folders().size(), 1u);
    EXPECT_EQ(restored.folders()[0].path, rig.tree.user);
}

TEST(SoundFolderBook, ResultsReachTheSnapshotBeforeTheScanEnds) {
    BookRig rig;
    rig.index->setProgressIntervalForTests(0);
    std::atomic<bool> release{false};
    // The first folder holds one sound. The hold stops the scan on the second folder's file.
    rig.index->setScanHoldForTests([&](const juce::File& file) {
        if (file.isAChildOf(rig.second))
            while (!release.load())
                juce::Thread::sleep(2);
    });
    rig.book.add(rig.tree.user);
    rig.book.add(rig.second);

    bool early = false;
    for (int i = 0; i < 2000 && !early; ++i) {
        const auto snap = rig.index->snapshot();
        early = snap->scanning && rig.soundsIn("folder:" + rig.tree.user.getFullPathName()) == 1 &&
                rig.place(rig.second) != nullptr && !rig.place(rig.second)->scanned;
        if (!early)
            juce::Thread::sleep(5);
    }
    EXPECT_TRUE(early) << "the first folder's sound is searchable while the second is still being read";
    EXPECT_EQ(rig.index->search("dusty").size(), 1u);
    release = true;
    rig.settle();
    EXPECT_FALSE(rig.index->snapshot()->scanning);
    EXPECT_EQ(rig.soundsIn("folder:" + rig.second.getFullPathName()), 1);
}

TEST(SoundFolderBook, RenamingChangesTheLabelAndNothingElse) {
    BookRig rig;
    rig.book.add(rig.tree.user);
    rig.settle();
    const auto before = rig.book.pathsSetting();
    const auto file = rig.tree.user.getChildFile("Field/dusty_keys_120_Am_loop.wav");

    rig.book.rename(rig.tree.user, "My loops");
    EXPECT_EQ(rig.place(rig.tree.user)->name, "My loops");
    EXPECT_EQ(rig.index->find(file.getFullPathName())->placeName, "My loops");
    EXPECT_EQ(rig.place(rig.tree.user)->id, "folder:" + rig.tree.user.getFullPathName()) << "the id is still the path";
    EXPECT_EQ(rig.book.pathsSetting(), before);
    EXPECT_TRUE(rig.tree.user.isDirectory());
    EXPECT_EQ(rig.tree.user.getFileName(), "user") << "the folder on disk keeps its name";
    EXPECT_TRUE(rig.book.namesSetting().contains("My loops"));

    rig.settle();
    EXPECT_EQ(rig.place(rig.tree.user)->name, "My loops") << "a later scan keeps the name";

    rig.book.rename(rig.tree.user, "user");
    EXPECT_EQ(rig.book.namesSetting(), "") << "the folder's own name is the default, not a stored name";
    EXPECT_EQ(rig.place(rig.tree.user)->name, "user");
}

TEST(SoundFolderBook, RemovingStopsListingAndLeavesEveryFileAlone) {
    BookRig rig;
    rig.book.add(rig.tree.user);
    rig.settle();
    const auto file = rig.tree.user.getChildFile("Field/dusty_keys_120_Am_loop.wav");
    const auto size = file.getSize();

    rig.book.remove(rig.tree.user);
    rig.settle();
    EXPECT_EQ(rig.place(rig.tree.user), nullptr);
    EXPECT_EQ(rig.index->search("dusty").size(), 0u);
    EXPECT_TRUE(rig.book.folders().empty());
    EXPECT_EQ(rig.book.pathsSetting(), "");
    EXPECT_TRUE(file.existsAsFile());
    EXPECT_EQ(file.getSize(), size);
}

TEST(SoundFolderBook, AMissingFolderStaysListedAndComesBackWhenThePathReappears) {
    BookRig rig;
    rig.book.add(rig.tree.user);
    rig.settle();
    EXPECT_FALSE(rig.index->rescanIfFoldersChanged()) << "nothing changed, so nothing is walked";
    rig.settle();
    ASSERT_FALSE(rig.place(rig.tree.user)->missing);

    const auto parked = rig.tree.dir.getChildFile("parked");
    ASSERT_TRUE(rig.tree.user.moveFileTo(parked));
    EXPECT_TRUE(rig.index->rescanIfFoldersChanged());
    rig.settle();
    ASSERT_NE(rig.place(rig.tree.user), nullptr) << "it stays in the list";
    EXPECT_TRUE(rig.place(rig.tree.user)->missing);
    EXPECT_EQ(rig.soundsIn("folder:" + rig.tree.user.getFullPathName()), 0);
    EXPECT_EQ(rig.book.folders().size(), 1u) << "and in the saved list";
    EXPECT_FALSE(rig.index->rescanIfFoldersChanged()) << "still gone: no new walk";

    ASSERT_TRUE(parked.moveFileTo(rig.tree.user));
    EXPECT_TRUE(rig.index->rescanIfFoldersChanged());
    rig.settle();
    EXPECT_FALSE(rig.place(rig.tree.user)->missing);
    EXPECT_EQ(rig.soundsIn("folder:" + rig.tree.user.getFullPathName()), 1);
}
