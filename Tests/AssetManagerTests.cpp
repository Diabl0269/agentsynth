// AssetManagerTests.cpp
//
// TL6-6: synth::AssetManager — import-into-bundle + dedupe, the relink flow (MainComponent's half),
// collect/clean of unused Audio/ + Peaks/ files, and the Recordings/-convention adoption pass that
// runs immediately before ProjectBundle::save. The missing-asset placeholder half of TL6-6 lives in
// Tests/TimelineClipLaneTests.cpp (group 6) alongside the rest of TimelineClipLaneArea's paint
// tests, since it's a lane-area concern rather than an AssetManager one.
//
// Groups:
//   1. ImportCopiesAndDedupes — collision-free naming, identical-content reuse, non-audio rejection.
//   2. RelinkRewritesAllSharingClips — through MainComponent::relinkClipAssetForTest.
//   3. CollectFindsExactlyUnused (+ clean deletes exactly that).
//   4. Recordings/ adoption on save — file+ref rewrite, project.json, stability, source-start
//      preservation, and that the original Recordings/ file is never touched.

#include "../Source/AI/AIProvider.h"
#include "../Source/PatchDocument.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/AssetManager.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "MainComponent.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::AssetManager;
using synth::ClipId;
using synth::PatchDocument;
using synth::ProjectBundle;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

// Writes a 32-bit float WAV of `numFrames` frames — the same minimal fixture-writer
// AudioClipPlaybackTests.cpp uses, duplicated here rather than shared (no common test-helper
// header exists in this codebase; every Tests/*.cpp file keeps its own).
bool writeWav(const juce::File& file, juce::int64 numFrames, int numChannels, double sampleRate,
              const std::function<float(juce::int64, int)>& generator) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int)numChannels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();

    juce::AudioBuffer<float> chunk(numChannels, (int)numFrames);
    for (int channel = 0; channel < numChannels; ++channel)
        for (juce::int64 i = 0; i < numFrames; ++i)
            chunk.getWritePointer(channel)[i] = generator(i, channel);
    const bool ok = writer->writeFromAudioSampleBuffer(chunk, 0, (int)numFrames);
    writer.reset(); // finalises the header
    return ok;
}

} // namespace

// ============================================================================
// 1. importAudioFile: collision-free naming, identical-content reuse, rejection
// ============================================================================

class AssetManagerImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-assetmanager-import");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root;
};

TEST_F(AssetManagerImportTest, ImportCopiesAndDedupes) {
    const auto bundleRoot = root.getChildFile("Bundle");

    // Same source, imported TWICE: identical content must reuse the same destination name rather
    // than writing a second copy.
    const auto sourceA = root.getChildFile("sources_a").getChildFile("clip.wav");
    ASSERT_TRUE(writeWav(sourceA, 100, 1, 44100.0, [](juce::int64 i, int) { return (float)i / 1000.0f; }));

    juce::String error1;
    const auto ref1 = AssetManager::importAudioFile(sourceA, bundleRoot, &error1);
    EXPECT_EQ(ref1, juce::String("Audio/clip.wav"));
    EXPECT_TRUE(error1.isEmpty());

    juce::String error2;
    const auto ref2 = AssetManager::importAudioFile(sourceA, bundleRoot, &error2);
    EXPECT_EQ(ref2, ref1) << "identical content under the same name must be REUSED, not duplicated";

    const auto audioDir = bundleRoot.getChildFile(ProjectBundle::kAudioSubdirName);
    EXPECT_EQ(audioDir.findChildFiles(juce::File::findFiles, false).size(), 1)
        << "exactly one file on disk after two imports of the identical source";

    // A DIFFERENT file, same base name ("clip.wav") and same length (so the size-only pre-filter
    // alone can't explain the split), different content: must get a "-2" suffix, never overwrite.
    const auto sourceB = root.getChildFile("sources_b").getChildFile("clip.wav");
    ASSERT_TRUE(writeWav(sourceB, 100, 1, 44100.0, [](juce::int64 i, int) { return (float)-i / 1000.0f; }));
    ASSERT_EQ(sourceA.getSize(), sourceB.getSize()) << "same size on purpose — this pins the HASH check, not just size";

    juce::String error3;
    const auto ref3 = AssetManager::importAudioFile(sourceB, bundleRoot, &error3);
    EXPECT_EQ(ref3, juce::String("Audio/clip-2.wav"));

    // A THIRD file, same base name again, different length this time (a plain size mismatch):
    // must land on the next free suffix.
    const auto sourceC = root.getChildFile("sources_c").getChildFile("clip.wav");
    ASSERT_TRUE(writeWav(sourceC, 150, 1, 44100.0, [](juce::int64 i, int) { return (float)i / 500.0f; }));

    juce::String error4;
    const auto ref4 = AssetManager::importAudioFile(sourceC, bundleRoot, &error4);
    EXPECT_EQ(ref4, juce::String("Audio/clip-3.wav"));

    EXPECT_EQ(audioDir.findChildFiles(juce::File::findFiles, false).size(), 3)
        << "clip.wav, clip-2.wav, clip-3.wav — no more, no less";

    // The source files are never moved or deleted.
    EXPECT_TRUE(sourceA.existsAsFile());
    EXPECT_TRUE(sourceB.existsAsFile());
    EXPECT_TRUE(sourceC.existsAsFile());

    // Non-audio garbage is rejected outright, with a non-empty error, and nothing is copied.
    const auto garbage = root.getChildFile("garbage.wav");
    garbage.replaceWithText("this is not a WAV file");

    juce::String garbageError;
    const auto garbageRef = AssetManager::importAudioFile(garbage, bundleRoot, &garbageError);
    EXPECT_TRUE(garbageRef.isEmpty());
    EXPECT_TRUE(garbageError.isNotEmpty());
    EXPECT_EQ(audioDir.findChildFiles(juce::File::findFiles, false).size(), 3) << "the garbage file must not appear";
}

// ============================================================================
// 2. Relink — through MainComponent::relinkClipAssetForTest
// ============================================================================

namespace {
class MockProviderAM : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockAM"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }

private:
    juce::String model = "MockModel";
};
} // namespace

class AssetManagerRelinkTest : public ::testing::Test {
protected:
    // Same on-disk-settings hygiene every other MainComponent-instantiating test file uses
    // (RecordTapTests.cpp / AudioClipPlaybackTests.cpp / TimelinePanelTests.cpp).
    void resetKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0");
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetKeys();
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-assetmanager-relink");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override {
        resetKeys();
        root.deleteRecursively();
    }

    juce::File root;
};

TEST_F(AssetManagerRelinkTest, RelinkRewritesAllSharingClips) {
    MainComponent mc(std::make_unique<MockProviderAM>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");

    const auto sharerA = doc.addClip(trackId, 0.0, 4.0, "A");
    const auto sharerB = doc.addClip(trackId, 8.0, 4.0, "B");
    const auto other = doc.addClip(trackId, 16.0, 4.0, "C");
    ASSERT_TRUE(sharerA.isValid());
    ASSERT_TRUE(sharerB.isValid());
    ASSERT_TRUE(other.isValid());
    ASSERT_TRUE(doc.setClipAsset(sharerA, "Audio/old.wav", 1.5));
    ASSERT_TRUE(doc.setClipAsset(sharerB, "Audio/old.wav", 2.5));
    ASSERT_TRUE(doc.setClipAsset(other, "Audio/other.wav", 9.0));

    const auto bundleDir = root.getChildFile("Relink.agsproj");
    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(ProjectBundle::isBundle(bundleDir));

    // The OLD file, physically on disk (content doesn't matter — this pins that it survives, not
    // that anything reads it back).
    const auto oldFile = bundleDir.getChildFile("Audio").getChildFile("old.wav");
    oldFile.replaceWithText("old take content");
    ASSERT_TRUE(oldFile.existsAsFile());

    // The file being relinked TO — a real, sniffable WAV, from outside the bundle.
    const auto newSource = root.getChildFile("newTake.wav");
    ASSERT_TRUE(writeWav(newSource, 200, 1, 44100.0, [](juce::int64 i, int) { return (float)i / 2000.0f; }));

    mc.getUndoManager().clearUndoHistory();

    mc.relinkClipAssetForTest(sharerA, newSource);

    const auto* clipA = doc.getClip(sharerA);
    const auto* clipB = doc.getClip(sharerB);
    const auto* clipC = doc.getClip(other);
    ASSERT_NE(clipA, nullptr);
    ASSERT_NE(clipB, nullptr);
    ASSERT_NE(clipC, nullptr);

    EXPECT_EQ(clipA->assetRef, juce::String("Audio/newTake.wav"));
    EXPECT_EQ(clipB->assetRef, juce::String("Audio/newTake.wav"))
        << "every clip sharing the OLD ref must move together";
    EXPECT_DOUBLE_EQ(clipA->sourceStartSeconds, 1.5) << "each clip's OWN sourceStartSeconds is preserved";
    EXPECT_DOUBLE_EQ(clipB->sourceStartSeconds, 2.5);

    EXPECT_EQ(clipC->assetRef, juce::String("Audio/other.wav")) << "a clip naming a DIFFERENT ref is untouched";
    EXPECT_DOUBLE_EQ(clipC->sourceStartSeconds, 9.0);

    EXPECT_TRUE(bundleDir.getChildFile("Audio").getChildFile("newTake.wav").existsAsFile());
    EXPECT_TRUE(oldFile.existsAsFile()) << "the old file is never deleted by a relink";

    // ONE undo step for both sharers.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    mc.getUndoManager().undo();
    EXPECT_EQ(doc.getClip(sharerA)->assetRef, juce::String("Audio/old.wav"));
    EXPECT_EQ(doc.getClip(sharerB)->assetRef, juce::String("Audio/old.wav"));
    EXPECT_DOUBLE_EQ(doc.getClip(sharerA)->sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(doc.getClip(sharerB)->sourceStartSeconds, 2.5);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole relink was ONE undo step";
}

// ============================================================================
// 3. collectUnusedAssets / cleanUnusedAssets
// ============================================================================

class AssetManagerCollectTest : public ::testing::Test {
protected:
    void SetUp() override {
        root =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-assetmanager-collect");
        root.deleteRecursively();
        bundleRoot = root.getChildFile("Bundle.agsproj");
        bundleRoot.createDirectory();
        audioDir = bundleRoot.getChildFile(ProjectBundle::kAudioSubdirName);
        peaksDir = bundleRoot.getChildFile(ProjectBundle::kPeaksSubdirName);
        audioDir.createDirectory();
        peaksDir.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root, bundleRoot, audioDir, peaksDir;
};

TEST_F(AssetManagerCollectTest, CollectFindsExactlyUnused) {
    for (const juce::String& stem : {"a", "b", "c"}) {
        audioDir.getChildFile(stem + ".wav").replaceWithText("wav-" + stem);
        peaksDir.getChildFile(stem + ".agpk").replaceWithText("peaks-" + stem);
    }

    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipA = doc.addClip(trackId, 0.0, 4.0, "A");
    const auto clipB = doc.addClip(trackId, 4.0, 4.0, "B");
    const auto clipMissing = doc.addClip(trackId, 8.0, 4.0, "Missing");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());
    ASSERT_TRUE(clipMissing.isValid());
    ASSERT_TRUE(doc.setClipAsset(clipA, "Audio/a.wav", 0.0));
    ASSERT_TRUE(doc.setClipAsset(clipB, "Audio/b.wav", 0.0));
    // A ref naming a file that ISN'T actually there — must never cause a deletion anywhere, and
    // must never be misread as making "a.wav"/"b.wav" unused.
    ASSERT_TRUE(doc.setClipAsset(clipMissing, "Audio/ghost.wav", 0.0));

    const auto unused = AssetManager::collectUnusedAssets(doc, bundleRoot);
    ASSERT_EQ(unused.size(), 1);
    EXPECT_EQ(unused[0], juce::String("Audio/c.wav"));

    const int deleted = AssetManager::cleanUnusedAssets(doc, bundleRoot);
    EXPECT_EQ(deleted, 1);

    EXPECT_FALSE(audioDir.getChildFile("c.wav").existsAsFile()) << "the unused wav must be gone";
    EXPECT_FALSE(peaksDir.getChildFile("c.agpk").existsAsFile()) << "and its peaks sidecar with it";

    EXPECT_TRUE(audioDir.getChildFile("a.wav").existsAsFile()) << "referenced files must survive";
    EXPECT_TRUE(audioDir.getChildFile("b.wav").existsAsFile());
    EXPECT_TRUE(peaksDir.getChildFile("a.agpk").existsAsFile());
    EXPECT_TRUE(peaksDir.getChildFile("b.agpk").existsAsFile());
}

TEST_F(AssetManagerCollectTest, EmptyDocMeansEverythingIsUnused) {
    audioDir.getChildFile("orphan.wav").replaceWithText("orphan");
    TimelineDoc doc; // no tracks, no clips at all
    const auto unused = AssetManager::collectUnusedAssets(doc, bundleRoot);
    EXPECT_EQ(unused.size(), 1);
    EXPECT_EQ(AssetManager::cleanUnusedAssets(doc, bundleRoot), 1);
    EXPECT_FALSE(audioDir.getChildFile("orphan.wav").existsAsFile());
}

// ============================================================================
// 4. Recordings/ adoption on save
// ============================================================================

class AssetManagerAdoptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-assetmanager-adopt");
        root.deleteRecursively();
        root.createDirectory();
        appData = root.getChildFile("AppData");
        recordingsRoot = appData.getChildFile("Recordings");
        recordingsRoot.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root, appData, recordingsRoot;
};

TEST_F(AssetManagerAdoptionTest, RecordingsAdoptionOnSave) {
    const auto takeFile = recordingsRoot.getChildFile("take-1.wav");
    ASSERT_TRUE(writeWav(takeFile, 300, 1, 44100.0, [](juce::int64 i, int) { return (float)i / 3000.0f; }));
    const auto peaksFile = recordingsRoot.getChildFile("take-1.agpk");
    peaksFile.replaceWithText("fake-peaks-content");

    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = doc.addClip(trackId, 0.0, 4.0, "Take");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(doc.setClipAsset(clipId, "Recordings/take-1.wav", 0.0));

    const auto bundleDir = root.getChildFile("Adopt.agsproj");

    // Exactly what MainComponent::saveToFile does: adopt, THEN ProjectBundle::save.
    const int adopted = AssetManager::adoptRecordingsAssets(doc, recordingsRoot, bundleDir);
    EXPECT_EQ(adopted, 1);

    const auto* clip = doc.getClip(clipId);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->assetRef, juce::String("Audio/take-1.wav")) << "the reserved Recordings/ prefix must be gone";

    EXPECT_TRUE(bundleDir.getChildFile("Audio").getChildFile("take-1.wav").existsAsFile());
    EXPECT_TRUE(bundleDir.getChildFile("Peaks").getChildFile("take-1.agpk").existsAsFile());

    juce::AudioProcessorGraph graph;
    PatchDocument patchDocument;
    const auto saveResult = ProjectBundle::save(bundleDir, graph, doc, patchDocument);
    ASSERT_TRUE(saveResult.ok) << saveResult.message;

    const auto json = juce::JSON::parse(bundleDir.getChildFile(ProjectBundle::kProjectFileName));
    const auto timelineVar = json.getProperty("timeline", {});
    const auto* tracks = timelineVar.getProperty("tracks", {}).getArray();
    ASSERT_NE(tracks, nullptr);
    ASSERT_FALSE(tracks->isEmpty());
    const auto* clips = (*tracks)[0].getProperty("clips", {}).getArray();
    ASSERT_NE(clips, nullptr);
    ASSERT_FALSE(clips->isEmpty());
    EXPECT_EQ((*clips)[0].getProperty("assetRef", {}).toString(), juce::String("Audio/take-1.wav"))
        << "project.json must carry the post-adoption ref, never Recordings/...";

    // The original Recordings/ file is left in place — never deleted, adoption or not.
    EXPECT_TRUE(takeFile.existsAsFile());
    EXPECT_TRUE(peaksFile.existsAsFile());

    // Resaving is stable: nothing left to adopt, no second copy.
    const int adoptedAgain = AssetManager::adoptRecordingsAssets(doc, recordingsRoot, bundleDir);
    EXPECT_EQ(adoptedAgain, 0);
    EXPECT_EQ(doc.getClip(clipId)->assetRef, juce::String("Audio/take-1.wav"));
    EXPECT_EQ(bundleDir.getChildFile("Audio").findChildFiles(juce::File::findFiles, false).size(), 1)
        << "resaving must not create a second copy of the adopted take";

    const auto resaveResult = ProjectBundle::save(bundleDir, graph, doc, patchDocument);
    ASSERT_TRUE(resaveResult.ok) << resaveResult.message;
    EXPECT_EQ(bundleDir.getChildFile("Audio").findChildFiles(juce::File::findFiles, false).size(), 1);
}

TEST_F(AssetManagerAdoptionTest, AdoptionPreservesSourceStart) {
    const auto takeFile = recordingsRoot.getChildFile("take-7.wav");
    ASSERT_TRUE(writeWav(takeFile, 500, 1, 44100.0, [](juce::int64 i, int) { return (float)i / 5000.0f; }));

    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = doc.addClip(trackId, 2.0, 4.0, "Take");
    ASSERT_TRUE(clipId.isValid());
    constexpr double kSourceStart = 3.25;
    ASSERT_TRUE(doc.setClipAsset(clipId, "Recordings/take-7.wav", kSourceStart));

    const auto bundleDir = root.getChildFile("AdoptPreserve.agsproj");
    EXPECT_EQ(AssetManager::adoptRecordingsAssets(doc, recordingsRoot, bundleDir), 1);

    const auto* clip = doc.getClip(clipId);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->assetRef, juce::String("Audio/take-7.wav"));
    EXPECT_DOUBLE_EQ(clip->sourceStartSeconds, kSourceStart) << "adoption must not disturb the trim point";
    EXPECT_DOUBLE_EQ(clip->startBeat, 2.0) << "and must not touch the clip's timeline position either";
}

TEST_F(AssetManagerAdoptionTest, MissingSourceFileIsLeftUntouched) {
    // A Recordings/ ref whose source file has since vanished (deleted by hand, or never written):
    // adoption must skip it rather than fail the whole pass, and the ref stays exactly as it was.
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = doc.addClip(trackId, 0.0, 4.0, "Gone");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(doc.setClipAsset(clipId, "Recordings/never-existed.wav", 0.0));

    const auto bundleDir = root.getChildFile("AdoptMissing.agsproj");
    EXPECT_EQ(AssetManager::adoptRecordingsAssets(doc, recordingsRoot, bundleDir), 0);
    EXPECT_EQ(doc.getClip(clipId)->assetRef, juce::String("Recordings/never-existed.wav"));
}
