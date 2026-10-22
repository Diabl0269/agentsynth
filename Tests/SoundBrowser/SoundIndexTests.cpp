// SoundIndexTests.cpp -- the background scan, header-only listing, cache, search and folder browsing, on temp trees.
#include "SoundBrowser/SoundSearch.h"
#include "SoundBrowserTestKit.h"
#include "UI/Layout/SearchMatch.h"
#include <gtest/gtest.h>

using namespace sound_browser_test;
using namespace synth::soundbrowser;

namespace {

const SoundEntry* findByName(const SoundSnapshot& s, const juce::String& name) {
    for (const auto& e : s.entries)
        if (e.name == name)
            return &e;
    return nullptr;
}

} // namespace

TEST(SoundIndexTest, ScansPacksAndUserFoldersAndGuessesFromNames) {
    TempTree tree;
    tree.fill();
    auto index = makeIndex(tree);
    index->setUserFolders({tree.user});
    ASSERT_TRUE(index->waitUntilIdleForTests(20000));

    const auto snap = index->snapshot();
    ASSERT_EQ(snap->entries.size(), 4u);
    ASSERT_EQ(snap->places.size(), 2u);
    EXPECT_EQ(snap->places[0].id, "core");
    EXPECT_EQ(snap->places[0].name, "Core instruments");
    EXPECT_EQ(snap->places[0].soundCount, 3);
    EXPECT_EQ(snap->places[1].id, "folder:" + tree.user.getFullPathName());
    EXPECT_FALSE(snap->truncated);

    const auto* kick = findByName(*snap, "kick-808");
    ASSERT_NE(kick, nullptr);
    EXPECT_EQ(kick->folder, "Drums/Kicks");
    EXPECT_EQ(kick->kind, SoundKind::OneShot);
    EXPECT_NEAR(kick->lengthSeconds, 1.2, 0.01);
    EXPECT_EQ(kick->sampleRate, 8000.0);
    EXPECT_GT(kick->size, 0);
    EXPECT_EQ(kick->tags, juce::StringArray{"Drums"});

    const auto* loop = findByName(*snap, "dusty_keys_120_Am_loop");
    ASSERT_NE(loop, nullptr);
    EXPECT_EQ(loop->kind, SoundKind::Loop);
    EXPECT_EQ(loop->tempo, 120.0);
    EXPECT_EQ(loop->key, "A minor");
    EXPECT_EQ(loop->folder, "Field");
    EXPECT_EQ(loop->placeName, tree.user.getFileName());

    const auto* sfz = findByName(*snap, "upright");
    ASSERT_NE(sfz, nullptr);
    EXPECT_EQ(sfz->kind, SoundKind::Instrument);
}

TEST(SoundIndexTest, AWavWithAValidHeaderAndNoAudioDataStillListsBecauseNothingIsDecoded) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    const auto file = tree.packs.getChildFile("p/broken.wav");
    writeWav(file, 1.0);
    {
        // Keep everything up to the end of the data chunk's header (which still declares one second), then put
        // 64 bytes of junk where the audio was.
        juce::MemoryBlock original;
        file.loadFileAsData(original);
        int dataAt = -1;
        for (size_t i = 0; i + 4 <= original.getSize(); ++i)
            if (std::memcmp(static_cast<const char*>(original.getData()) + i, "data", 4) == 0) {
                dataAt = (int)i;
                break;
            }
        ASSERT_GT(dataAt, 0);
        const size_t keep = (size_t)dataAt + 8;
        juce::MemoryBlock cut(keep + 64, true);
        cut.fillWith(0xA5);
        std::memcpy(cut.getData(), original.getData(), keep);
        file.replaceWithData(cut.getData(), cut.getSize());
    }
    auto index = makeIndex(tree);
    scanAndWait(*index);
    const auto snap = index->snapshot();
    ASSERT_EQ(snap->entries.size(), 1u);
    EXPECT_EQ(snap->entries[0].name, "broken");
    EXPECT_NEAR(snap->entries[0].lengthSeconds, 1.0, 0.01) << "the length comes from the header, not from the audio";
}

TEST(SoundIndexTest, AFileThatIsNotAudioIsLeftOut) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    tree.packs.getChildFile("p/notes.wav").replaceWithText("this is not a wav file at all");
    tree.packs.getChildFile("p/readme.txt").replaceWithText("hello");
    auto index = makeIndex(tree);
    scanAndWait(*index);
    EXPECT_TRUE(index->snapshot()->entries.empty());
}

TEST(SoundIndexTest, AnAcidChunkGivesTempoRootNoteAndTheOneShotFlag) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    juce::StringPairArray loopMeta;
    loopMeta.set(juce::WavAudioFormat::acidOneShot, "0");
    loopMeta.set(juce::WavAudioFormat::acidRootSet, "1");
    loopMeta.set(juce::WavAudioFormat::acidRootNote, "57");
    loopMeta.set(juce::WavAudioFormat::acidTempo, "96.0");
    writeWav(tree.packs.getChildFile("p/plain_name.wav"), 1.0, loopMeta);
    juce::StringPairArray shotMeta;
    shotMeta.set(juce::WavAudioFormat::acidOneShot, "1");
    shotMeta.set(juce::WavAudioFormat::acidTempo, "100.0");
    writeWav(tree.packs.getChildFile("p/hit_loop_name.wav"), 1.0, shotMeta);

    auto index = makeIndex(tree);
    scanAndWait(*index);
    const auto snap = index->snapshot();
    const auto* loop = findByName(*snap, "plain_name");
    ASSERT_NE(loop, nullptr);
    EXPECT_EQ(loop->kind, SoundKind::Loop);
    EXPECT_NEAR(loop->tempo, 96.0, 0.01);
    EXPECT_EQ(loop->key, "A");
    const auto* shot = findByName(*snap, "hit_loop_name");
    ASSERT_NE(shot, nullptr);
    EXPECT_EQ(shot->kind, SoundKind::OneShot) << "the file's own one-shot flag beats the word loop in its name";
    EXPECT_EQ(shot->tempo, 0.0);
}

TEST(SoundIndexTest, TheCacheRestoresTheListAndARescanOpensOnlyChangedFiles) {
    TempTree tree;
    tree.fill();
    {
        auto first = makeIndex(tree, true);
        first->setUserFolders({tree.user});
        ASSERT_TRUE(first->waitUntilIdleForTests(20000));
        EXPECT_EQ(first->headersReadForTests(), 3) << "three audio files; the .sfz needs no header";
    }
    ASSERT_TRUE(tree.cache().existsAsFile());

    auto second = makeIndex(tree, true);
    second->setUserFolders({tree.user});
    ASSERT_TRUE(second->waitUntilIdleForTests(20000));
    EXPECT_EQ(second->snapshot()->entries.size(), 4u);
    EXPECT_EQ(second->headersReadForTests(), 0) << "an unchanged tree is not opened again";

    const auto changed = tree.packs.getChildFile("core/Drums/snare_tight.wav");
    ASSERT_TRUE(changed.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::hours(1)));
    scanAndWait(*second);
    EXPECT_EQ(second->headersReadForTests(), 1) << "only the file whose mtime moved is read again";

    // A new file is picked up; a deleted one disappears.
    writeWav(tree.packs.getChildFile("core/Drums/new_clap.wav"), 0.2);
    ASSERT_TRUE(tree.packs.getChildFile("core/Drums/Kicks/kick-808.wav").deleteFile());
    scanAndWait(*second);
    const auto snap = second->snapshot();
    EXPECT_NE(findByName(*snap, "new_clap"), nullptr);
    EXPECT_EQ(findByName(*snap, "kick-808"), nullptr);
}

TEST(SoundIndexTest, AColdIndexShowsTheCachedListBeforeItsOwnScanFinishes) {
    TempTree tree;
    tree.fill();
    {
        auto first = makeIndex(tree, true);
        first->setUserFolders({tree.user});
        ASSERT_TRUE(first->waitUntilIdleForTests(20000));
    }
    auto second = makeIndex(tree, true);
    second->setUserFolders({tree.user});
    ASSERT_TRUE(second->waitUntilIdleForTests(20000));
    // The scan ended with the same entries the cache held, so the cache round-tripped every field.
    const auto* loop = findByName(*second->snapshot(), "dusty_keys_120_Am_loop");
    ASSERT_NE(loop, nullptr);
    EXPECT_EQ(loop->tempo, 120.0);
    EXPECT_EQ(loop->key, "A minor");
    EXPECT_EQ(loop->tags, juce::StringArray{"Keys"});
    EXPECT_EQ(loop->kind, SoundKind::Loop);
}

TEST(SoundIndexTest, TheEntryCapStopsTheScanAndFlagsTruncation) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    for (int i = 0; i < 12; ++i)
        writeWav(tree.packs.getChildFile("p/s" + juce::String(i) + ".wav"), 0.05);
    auto index = makeIndex(tree);
    index->setEntryCapForTests(5);
    scanAndWait(*index);
    const auto snap = index->snapshot();
    EXPECT_EQ(snap->entries.size(), 5u);
    EXPECT_TRUE(snap->truncated);
}

TEST(SoundIndexTest, SearchRanksNameMatchesBeforeFolderMatches) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    writeWav(tree.packs.getChildFile("p/Snares/rim.wav"), 0.1);       // folder only
    writeWav(tree.packs.getChildFile("p/Hits/my_snare.wav"), 0.1);    // inside a word boundary
    writeWav(tree.packs.getChildFile("p/Hits/snare_tight.wav"), 0.1); // starts with
    writeWav(tree.packs.getChildFile("p/Hits/crash.wav"), 0.1);       // no match
    auto index = makeIndex(tree);
    scanAndWait(*index);

    const auto hits = index->search("snare");
    ASSERT_EQ(hits.size(), 3u);
    EXPECT_EQ(hits[0].name, "snare_tight");
    EXPECT_EQ(hits[1].name, "my_snare");
    EXPECT_EQ(hits[2].name, "rim");

    SoundFilter drums;
    drums.tag = "Drums";
    EXPECT_EQ(index->search("snare", drums).size(), 3u) << "the folder tag Drums is not set by 'Hits'; snare names are";
    SoundFilter loops;
    loops.kind = SoundKind::Loop;
    EXPECT_TRUE(index->search("snare", loops).empty());
    SoundFilter elsewhere;
    elsewhere.placeId = "nothing";
    EXPECT_TRUE(index->search("", elsewhere).empty());
    EXPECT_EQ(index->search("").size(), 4u);
}

TEST(SoundIndexTest, SearchUsesTheSameRulesAsTheUiMatcher) {
    const char* texts[] = {"acoustic-snare-center", "Snare Tight", "kick", "x_snare"};
    const char* queries[] = {"snare", "SN", "tight snare", "", "zzz", "kick  "};
    for (const auto* t : texts)
        for (const auto* q : queries)
            EXPECT_EQ(soundSearchScore(t, q), synth::ui::searchScore(t, q)) << t << " / " << q;
}

TEST(SoundIndexTest, ChildrenOfListsSubfoldersWithCountsAndTheSoundsAtThatLevel) {
    TempTree tree;
    tree.fill();
    auto index = makeIndex(tree);
    scanAndWait(*index);

    const auto root = index->childrenOf("core", "");
    ASSERT_EQ(root.folders.size(), 2u);
    EXPECT_EQ(root.folders[0].name, "Drums");
    EXPECT_EQ(root.folders[0].soundCount, 2);
    EXPECT_EQ(root.folders[1].name, "Piano");
    EXPECT_TRUE(root.entries.empty());

    const auto drums = index->childrenOf("core", "Drums");
    ASSERT_EQ(drums.folders.size(), 1u);
    EXPECT_EQ(drums.folders[0].name, "Kicks");
    EXPECT_EQ(drums.folders[0].soundCount, 1);
    ASSERT_EQ(drums.entries.size(), 1u);
    EXPECT_EQ(drums.entries[0].name, "snare_tight");

    EXPECT_TRUE(index->childrenOf("missing", "").folders.empty());
    EXPECT_TRUE(index->find(drums.entries[0].path).has_value());
    EXPECT_FALSE(index->find("/nope.wav").has_value());
}

namespace {

struct ThreadRecorder : SoundIndex::Listener {
    void soundIndexChanged() override {
        ++calls;
        allOnMessageThread = allOnMessageThread && juce::MessageManager::getInstance()->isThisTheMessageThread();
        sawFinished = !index->snapshot()->scanning && !index->snapshot()->entries.empty();
    }
    SoundIndex* index = nullptr;
    int calls = 0;
    bool allOnMessageThread = true;
    bool sawFinished = false;
};

} // namespace

TEST(SoundIndexTest, TheListenerHearsAboutResultsOnTheMessageThread) {
    TempTree tree;
    tree.fill();
    auto index = makeIndex(tree);
    ThreadRecorder recorder;
    recorder.index = index.get();
    index->addListener(&recorder);
    index->setUserFolders({tree.user});

    const auto deadline = juce::Time::getMillisecondCounter() + 20000;
    while (!recorder.sawFinished && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    index->removeListener(&recorder);

    EXPECT_GT(recorder.calls, 0);
    EXPECT_TRUE(recorder.allOnMessageThread);
    EXPECT_TRUE(recorder.sawFinished);
    EXPECT_EQ(index->snapshot()->entries.size(), 4u);
}

TEST(SoundIndexTest, TwoPacksWithTheSameIdListOnlyTheFirst) {
    TempTree tree;
    writePack(tree.packs, "dup", "First");
    writeWav(tree.packs.getChildFile("dup/a.wav"), 0.1);
    const auto second = tree.dir.getChildFile("packs2");
    writePack(second, "dup", "Second");
    writeWav(second.getChildFile("dup/b.wav"), 0.1);
    synth::soundbrowser::SoundIndex index({}, [&] { return std::vector<juce::File>{tree.packs, second}; });
    index.rescan();
    ASSERT_TRUE(index.waitUntilIdleForTests(20000));
    ASSERT_EQ(index.snapshot()->entries.size(), 1u);
    EXPECT_EQ(index.snapshot()->entries[0].name, "a");
}

TEST(SoundIndexTest, DestroyingTheIndexMidScanStopsTheScanPromptlyWithAListenerAttached) {
    TempTree tree;
    writePack(tree.packs, "p", "Pack");
    for (int i = 0; i < 1500; ++i)
        writeWav(tree.packs.getChildFile("p/d" + juce::String(i / 100) + "/s" + juce::String(i) + ".wav"), 0.01);
    ThreadRecorder recorder;
    auto index = makeIndex(tree);
    recorder.index = index.get();
    index->addListener(&recorder);
    index->rescan();
    while (!index->isScanning())
        juce::Thread::yield();

    const auto start = juce::Time::getMillisecondCounter();
    index->removeListener(&recorder);
    index.reset();
    EXPECT_LT(juce::Time::getMillisecondCounter() - start, 2000u);

    // A change notice the scan queued before it stopped must not reach anything once the index is gone.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}
