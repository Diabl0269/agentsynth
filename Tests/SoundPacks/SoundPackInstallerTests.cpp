// SoundPackInstallerTests.cpp -- downloading, verifying, unpacking and removing packs, fully offline: a fake
// fetcher serves small zips and the installer runs its background steps inline.
#include "SoundPackTestKit.h"
#include <gtest/gtest.h>

using namespace sound_pack_test;
using State = SoundPackInstaller::State;
using synth::sampler::SoundPackLibrary;

namespace {

struct Recorder : SoundPackInstaller::Listener {
    explicit Recorder(SoundPackInstaller& i)
        : installer(i) {
        installer.addListener(this);
    }
    ~Recorder() override { installer.removeListener(this); }
    void soundPacksChanged() override {
        ++calls;
        if (const auto row = installer.row("core"))
            if (row->state == State::Downloading && row->progress >= 0.0f) // -1 = size not known yet
                downloadFractions.push_back(row->progress);
    }
    SoundPackInstaller& installer;
    int calls = 0;
    std::vector<float> downloadFractions;
};

State stateOf(Rig& rig, const juce::String& id = "core") {
    const auto row = rig.installer.row(id);
    return row ? row->state : State::NotInstalled;
}

juce::String errorOf(Rig& rig) { return rig.installer.row("core").value().error; }

} // namespace

TEST(SoundPackInstallerTest, TheHostedListLoadsAndShowsEachPackAsNotInstalled) {
    Rig rig;
    EXPECT_EQ(rig.installer.indexStatus(), SoundPackInstaller::IndexStatus::NotLoaded);
    rig.publish("core", "1.0.0");
    EXPECT_EQ(rig.installer.indexStatus(), SoundPackInstaller::IndexStatus::Loaded);
    const auto rows = rig.installer.rows();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].name, "Pack core");
    EXPECT_EQ(rows[0].state, State::NotInstalled);
    EXPECT_EQ(rows[0].credit, "Made by someone.");
    EXPECT_TRUE(rows[0].inIndex);
    EXPECT_TRUE(rig.rootEntries().isEmpty());
}

TEST(SoundPackInstallerTest, InstallingUnpacksTheVerifiedPackAndTheLibraryFindsItsInstruments) {
    Rig rig;
    rig.publish("core", "1.0.0");
    ASSERT_TRUE(SoundPackLibrary::listInstruments().empty());
    Recorder recorder(rig.installer);

    ASSERT_TRUE(rig.installer.install("core"));

    EXPECT_EQ(stateOf(rig), State::Installed);
    const auto row = rig.installer.row("core").value();
    EXPECT_EQ(row.installedVersion, "1.0.0");
    EXPECT_TRUE(row.removable);
    EXPECT_TRUE(row.sources.existsAsFile());
    EXPECT_TRUE(row.error.isEmpty());
    EXPECT_EQ(rig.rootEntries(), juce::StringArray{"core"}) << "no staging or zip left behind";
    const auto instruments = SoundPackLibrary::listInstruments();
    ASSERT_EQ(instruments.size(), 1u);
    EXPECT_EQ(instruments[0].packId, "core");
    EXPECT_GT(recorder.calls, 0);
}

TEST(SoundPackInstallerTest, ProgressIsReportedWhileDownloadingAndEndsInstalled) {
    Rig rig;
    rig.publish("core", "1.0.0");
    Recorder recorder(rig.installer);
    rig.installer.install("core");
    ASSERT_GE(recorder.downloadFractions.size(), 3u);
    EXPECT_TRUE(std::is_sorted(recorder.downloadFractions.begin(), recorder.downloadFractions.end()));
    EXPECT_GT(recorder.downloadFractions.front(), 0.0f);
    EXPECT_NEAR(recorder.downloadFractions.back(), 1.0f, 0.001f);
    EXPECT_EQ(stateOf(rig), State::Installed);
}

TEST(SoundPackInstallerTest, AChecksumMismatchInstallsNothing) {
    Rig rig;
    rig.publish("core", "1.0.0", {}, juce::String::repeatedString("0", 64));
    rig.installer.install("core");
    EXPECT_EQ(stateOf(rig), State::NotInstalled);
    EXPECT_TRUE(errorOf(rig).contains("checksum")) << errorOf(rig);
    EXPECT_TRUE(rig.rootEntries().isEmpty());
    EXPECT_TRUE(SoundPackLibrary::listInstruments().empty());
}

TEST(SoundPackInstallerTest, ABadZipIsRejectedWithNothingInstalled) {
    Rig rig;
    const juce::String junk = "this is not a zip file at all";
    juce::MemoryBlock block(junk.toRawUTF8(), junk.getNumBytesAsUTF8());
    rig.publish("core", "1.0.0", block);
    rig.installer.install("core");
    EXPECT_EQ(stateOf(rig), State::NotInstalled);
    EXPECT_TRUE(errorOf(rig).contains("not a valid sound pack")) << errorOf(rig);
    EXPECT_TRUE(rig.rootEntries().isEmpty());
}

TEST(SoundPackInstallerTest, EntriesThatEscapeThePackFolderAreRejected) {
    for (const char* evil : {"core/../../escaped.txt", "../escaped.txt", "/abs/escaped.txt", "other/escaped.txt",
                             "core\\..\\escaped.txt", "core/a/../../../escaped.txt"}) {
        Rig rig;
        rig.publish("core", "1.0.0", makeZip({{"core/pack.json", manifestJson("core", "1.0.0")}, {evil, "boom"}}));
        rig.installer.install("core");
        EXPECT_EQ(stateOf(rig), State::NotInstalled) << evil;
        EXPECT_TRUE(rig.rootEntries().isEmpty()) << evil;
        EXPECT_FALSE(rig.dir.path().getParentDirectory().getChildFile("escaped.txt").exists()) << evil;
        EXPECT_FALSE(rig.dir.path().getChildFile("escaped.txt").exists()) << evil;
    }
}

TEST(SoundPackInstallerTest, AZipWithoutAMatchingPackJsonIsRejected) {
    {
        Rig rig;
        rig.publish("core", "1.0.0", makeZip({{"core/other.txt", "x"}}));
        rig.installer.install("core");
        EXPECT_EQ(stateOf(rig), State::NotInstalled);
        EXPECT_TRUE(rig.rootEntries().isEmpty());
    }
    {
        Rig rig;
        rig.publish("core", "1.0.0", makeZip({{"core/pack.json", manifestJson("someone-else", "1.0.0")}}));
        rig.installer.install("core");
        EXPECT_EQ(stateOf(rig), State::NotInstalled);
        EXPECT_TRUE(rig.rootEntries().isEmpty());
    }
}

TEST(SoundPackInstallerTest, AZipThatUnpacksFarBeyondTheIndexedSizeIsRejected) {
    Rig rig;
    const juce::String big = juce::String::repeatedString("x", 9 * 1024 * 1024);
    rig.publish("core", "1.0.0", makeZip({{"core/pack.json", manifestJson("core", "1.0.0")}, {"core/big.bin", big}}));
    rig.installer.install("core"); // the index says 1000 installed bytes
    EXPECT_EQ(stateOf(rig), State::NotInstalled);
    EXPECT_TRUE(rig.rootEntries().isEmpty());
}

TEST(SoundPackInstallerTest, AFailedDownloadSaysWhyAndLeavesNothing) {
    Rig rig;
    rig.publish("core", "1.0.0");
    rig.server.bodies.erase("https://example.test/core-1.0.0.zip");
    rig.installer.install("core");
    EXPECT_TRUE(errorOf(rig).contains("404")) << errorOf(rig);
    EXPECT_TRUE(rig.rootEntries().isEmpty());

    rig.server.offline = true;
    rig.installer.install("core");
    EXPECT_TRUE(errorOf(rig).contains("internet")) << errorOf(rig);
    EXPECT_TRUE(rig.rootEntries().isEmpty());
}

TEST(SoundPackInstallerTest, CancellingMidDownloadCleansUpTheTempFiles) {
    Rig rig;
    rig.publish("core", "1.0.0");
    int chunks = 0;
    rig.server.onChunk = [&] {
        if (++chunks == 1)
            rig.installer.cancel("core");
    };
    rig.installer.install("core");
    EXPECT_EQ(stateOf(rig), State::NotInstalled);
    EXPECT_TRUE(errorOf(rig).isEmpty()) << "a cancel is not an error";
    EXPECT_TRUE(rig.rootEntries().isEmpty()) << "no staging folder or partial zip";
    EXPECT_EQ(chunks, 1);
    // And a retry afterwards works.
    rig.server.onChunk = nullptr;
    rig.installer.install("core");
    EXPECT_EQ(stateOf(rig), State::Installed);
}

TEST(SoundPackInstallerTest, AnUpdateReplacesTheOlderVersionWhole) {
    Rig rig;
    rig.publish("core", "1.0.0");
    rig.installer.install("core");
    ASSERT_TRUE(rig.dir.path().getChildFile("core/marker-1.0.0.txt").existsAsFile());

    rig.publish("core", "1.1.0");
    EXPECT_EQ(stateOf(rig), State::UpdateAvailable);
    EXPECT_EQ(rig.installer.row("core")->latestVersion, "1.1.0");
    rig.installer.install("core");

    EXPECT_EQ(stateOf(rig), State::Installed);
    EXPECT_EQ(rig.installer.row("core")->installedVersion, "1.1.0");
    EXPECT_TRUE(rig.dir.path().getChildFile("core/marker-1.1.0.txt").existsAsFile());
    EXPECT_FALSE(rig.dir.path().getChildFile("core/marker-1.0.0.txt").exists());
    EXPECT_EQ(rig.rootEntries(), juce::StringArray{"core"});
}

TEST(SoundPackInstallerTest, AFailedUpdateKeepsTheOlderVersionPlayable) {
    Rig rig;
    rig.publish("core", "1.0.0");
    rig.installer.install("core");
    rig.publish("core", "1.1.0", {}, juce::String::repeatedString("0", 64));
    rig.installer.install("core");
    EXPECT_EQ(stateOf(rig), State::UpdateAvailable);
    EXPECT_TRUE(rig.dir.path().getChildFile("core/marker-1.0.0.txt").existsAsFile());
    EXPECT_EQ(SoundPackLibrary::listInstruments().size(), 1u);
}

TEST(SoundPackInstallerTest, RemovingDeletesThePackFolder) {
    Rig rig;
    rig.publish("core", "1.0.0");
    rig.installer.install("core");
    Recorder recorder(rig.installer);
    ASSERT_TRUE(rig.installer.remove("core"));
    EXPECT_EQ(stateOf(rig), State::NotInstalled);
    EXPECT_TRUE(rig.rootEntries().isEmpty());
    EXPECT_TRUE(SoundPackLibrary::listInstruments().empty());
    EXPECT_GT(recorder.calls, 0);
    EXPECT_FALSE(rig.installer.remove("core")) << "nothing left to remove";
}

TEST(SoundPackInstallerTest, APackOutsideTheUserFolderCannotBeRemoved) {
    TempDir bundled;
    Rig rig;
    SoundPackLibrary::setRootsForTests(std::vector<juce::File>{bundled.path(), rig.dir.path()});
    const auto pack = bundled.path().getChildFile("builtin");
    pack.createDirectory();
    pack.getChildFile("pack.json").replaceWithText(manifestJson("builtin", "3"));

    const auto row = rig.installer.row("builtin");
    ASSERT_TRUE(row.has_value());
    EXPECT_FALSE(row->removable);
    EXPECT_FALSE(rig.installer.remove("builtin"));
    EXPECT_TRUE(pack.isDirectory());
    EXPECT_TRUE(rig.installer.row("builtin")->error.isNotEmpty());
}

TEST(SoundPackInstallerTest, OfflineKeepsInstalledPacksListedAndSaysTheListCouldNotBeReached) {
    Rig rig;
    rig.publish("core", "1.0.0");
    rig.installer.install("core");

    rig.server.offline = true;
    rig.installer.refreshIndex();

    EXPECT_EQ(rig.installer.indexStatus(), SoundPackInstaller::IndexStatus::Failed);
    EXPECT_EQ(rig.installer.indexError(), "Couldn't reach the pack list.");
    EXPECT_EQ(stateOf(rig), State::Installed);

    // A fresh installer that never got the list still shows what is on disk.
    SoundPackInstaller fresh;
    fresh.setInstallRootForTesting(rig.dir.path());
    fresh.setIndexUrlForTesting(kIndexUrl);
    fresh.setFetcherForTesting(rig.server.fetcher());
    fresh.setRunInlineForTesting(true);
    fresh.refreshIndex();
    const auto rows = fresh.rows();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].state, State::Installed);
    EXPECT_FALSE(rows[0].inIndex);
    EXPECT_EQ(rows[0].credit, "Someone") << "an installed pack credits itself from its own pack.json";

    rig.server.offline = false;
    rig.installer.refreshIndex();
    EXPECT_EQ(rig.installer.indexStatus(), SoundPackInstaller::IndexStatus::Loaded);
}

TEST(SoundPackInstallerTest, AnUnreadableListReportsItAndAnUnknownPackCannotBeInstalled) {
    Rig rig;
    rig.server.bodies[kIndexUrl] = juce::MemoryBlock("{\"format\":9}", 12);
    rig.installer.refreshIndex();
    EXPECT_EQ(rig.installer.indexStatus(), SoundPackInstaller::IndexStatus::Failed);
    EXPECT_TRUE(rig.installer.indexError().contains("different version"));
    EXPECT_FALSE(rig.installer.install("core"));
}

TEST(SoundPackInstallerTest, ShowSourcesOpensTheInstalledSourcesFile) {
    Rig rig;
    rig.publish("core", "1.0.0");
    juce::File opened;
    rig.installer.setFileOpenerForTesting([&](const juce::File& f) { opened = f; });
    rig.installer.showSources("core");
    EXPECT_EQ(opened, juce::File()) << "nothing installed yet, nothing to open";
    rig.installer.install("core");
    rig.installer.showSources("core");
    EXPECT_EQ(opened, rig.dir.path().getChildFile("core/LICENSES/SOURCES.md"));
}
