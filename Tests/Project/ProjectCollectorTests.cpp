// ProjectCollectorTests.cpp
//
// synth::ProjectCollector — Collect copies every external module file into the bundle and repoints
// the module (never moving, deleting or overwriting a source), and Archive zips the bundle so an
// unzipped copy opens standalone with no reference to the original paths.

#include "MacroSet.h"
#include "MidiRemote/RemoteModel.h"
#include "Modules/SamplerModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "PatchDocument.h"
#include "Project/ProjectCollector.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

using synth::ProjectBundle;
using synth::ProjectCollector;

namespace {

bool writeWav(const juce::File& file, int numFrames, float frequency = 0.05f) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    juce::AudioBuffer<float> buffer(1, numFrames);
    for (int i = 0; i < numFrames; ++i)
        buffer.setSample(0, i, 0.5f * std::sin((float)i * frequency));
    return writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
}

class ProjectCollectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("agentsynth-pc", "");
        root.createDirectory();
        bundle = root.getChildFile("Song.agsproj");
        bundle.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    template <typename Module>
    Module* add(juce::AudioProcessorGraph& graph) {
        auto node = graph.addNode(std::make_unique<Module>());
        return dynamic_cast<Module*>(node->getProcessor());
    }

    synth::ProjectLoadResult save(juce::AudioProcessorGraph& graph, const juce::File& dir) {
        synth::TimelineDoc timeline;
        synth::PatchDocument doc;
        return ProjectBundle::save(dir, graph, timeline, doc, synth::MacroSet(), synth::MidiRemoteProjectDoc());
    }

    synth::ProjectLoadResult load(juce::AudioProcessorGraph& graph, const juce::File& dir) {
        synth::TimelineDoc timeline;
        synth::PatchDocument doc;
        synth::MacroSet macros;
        synth::MidiRemoteProjectDoc remote;
        return ProjectBundle::load(dir, graph, timeline, doc, macros, remote);
    }

    template <typename Module>
    static Module* find(juce::AudioProcessorGraph& graph) {
        for (auto* node : graph.getNodes())
            if (auto* module = dynamic_cast<Module*>(node->getProcessor()))
                return module;
        return nullptr;
    }

    juce::File root;
    juce::File bundle;
};

} // namespace

TEST_F(ProjectCollectorTest, ExternalSampleIsCopiedIntoSamplesAndTheModuleRepointed) {
    const auto original = root.getChildFile("Downloads/kick.wav");
    ASSERT_TRUE(writeWav(original, 4410));
    juce::AudioProcessorGraph graph;
    auto* sampler = add<SamplerModule>(graph);
    ASSERT_TRUE(sampler->loadSampleFile(original));

    const auto result = ProjectCollector::collect(graph, bundle);

    const auto copy = bundle.getChildFile(ProjectBundle::kSamplesSubdirName).getChildFile("kick.wav");
    EXPECT_EQ(result.filesCopied, 1);
    EXPECT_TRUE(copy.existsAsFile());
    EXPECT_TRUE(original.existsAsFile()) << "Collect never moves or deletes the source";
    EXPECT_EQ(sampler->getSampleFilePath(), copy.getFullPathName());
    EXPECT_TRUE(synth::AssetManager::collectUnusedAssets(synth::TimelineDoc(), bundle).isEmpty())
        << "collected samples live outside Audio/, where clean-unused could sweep them";
}

TEST_F(ProjectCollectorTest, CollectIsIdempotentAndSharedSamplesAreCopiedOnce) {
    const auto original = root.getChildFile("Downloads/pad.wav");
    ASSERT_TRUE(writeWav(original, 4410));
    juce::AudioProcessorGraph graph;
    auto* first = add<SamplerModule>(graph);
    auto* second = add<SamplerModule>(graph);
    ASSERT_TRUE(first->loadSampleFile(original));
    ASSERT_TRUE(second->loadSampleFile(original));

    EXPECT_EQ(ProjectCollector::collect(graph, bundle).filesCopied, 1);
    EXPECT_EQ(first->getSampleFilePath(), second->getSampleFilePath());

    const auto rerun = ProjectCollector::collect(graph, bundle);
    EXPECT_EQ(rerun.filesCopied, 0) << "files already inside the bundle are left alone";
    EXPECT_EQ(bundle.getChildFile(ProjectBundle::kSamplesSubdirName).getNumberOfChildFiles(juce::File::findFiles), 1);
}

TEST_F(ProjectCollectorTest, MissingFileIsReportedAndLeftUntouched) {
    const auto original = root.getChildFile("Downloads/gone.wav");
    ASSERT_TRUE(writeWav(original, 4410));
    juce::AudioProcessorGraph graph;
    auto* sampler = add<SamplerModule>(graph);
    ASSERT_TRUE(sampler->loadSampleFile(original));
    ASSERT_TRUE(original.deleteFile());

    const auto result = ProjectCollector::collect(graph, bundle);

    EXPECT_EQ(result.filesCopied, 0);
    EXPECT_TRUE(result.missing.contains(original.getFullPathName()));
    EXPECT_EQ(sampler->getSampleFilePath(), original.getFullPathName()) << "a missing file's ref is never cleared";
}

TEST_F(ProjectCollectorTest, CancelledCopyRewritesNothing) {
    const auto original = root.getChildFile("Downloads/kick.wav");
    ASSERT_TRUE(writeWav(original, 4410));
    juce::AudioProcessorGraph graph;
    auto* sampler = add<SamplerModule>(graph);
    ASSERT_TRUE(sampler->loadSampleFile(original));

    const auto plan = ProjectCollector::plan(graph, bundle);
    const auto result = ProjectCollector::copy(plan, [](double) { return false; });

    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(ProjectCollector::apply(graph, result), 0);
    EXPECT_EQ(sampler->getSampleFilePath(), original.getFullPathName());
}

TEST_F(ProjectCollectorTest, WavetableFolderIsCopiedWholeAndTheCursorStaysOnTheLoadedTable) {
    const auto folder = root.getChildFile("Tables/Pads");
    for (const auto* name : {"a.wav", "b.wav", "c.wav"})
        ASSERT_TRUE(writeWav(folder.getChildFile(name), 2048));
    ASSERT_TRUE(folder.getChildFile("notes.txt").replaceWithText("not a table"));

    juce::AudioProcessorGraph graph;
    auto* wavetable = add<WavetableOscillatorModule>(graph);
    wavetable->setWavetableFolder(folder);
    ASSERT_TRUE(wavetable->selectWavetableAt(1));

    const auto result = ProjectCollector::collect(graph, bundle);

    const auto copiedFolder = bundle.getChildFile(ProjectBundle::kWavetablesSubdirName).getChildFile("Pads");
    EXPECT_EQ(result.filesCopied, 3) << "every table in the folder, so < > browsing still works";
    EXPECT_FALSE(copiedFolder.getChildFile("notes.txt").exists());
    EXPECT_EQ(wavetable->getWavetableFolder(), copiedFolder);
    EXPECT_EQ(wavetable->getWavetableFile(), copiedFolder.getChildFile("b.wav"));
    EXPECT_EQ(wavetable->getFolderWavetableCount(), 3);
    EXPECT_EQ(wavetable->getFolderIndex(), 1);
}

TEST_F(ProjectCollectorTest, SameNamedFoldersGetSeparateCopies) {
    const auto first = root.getChildFile("A/Pads");
    const auto second = root.getChildFile("B/Pads");
    ASSERT_TRUE(writeWav(first.getChildFile("x.wav"), 2048, 0.05f));
    ASSERT_TRUE(writeWav(second.getChildFile("x.wav"), 2048, 0.2f));

    juce::AudioProcessorGraph graph;
    auto* one = add<WavetableOscillatorModule>(graph);
    auto* two = add<WavetableOscillatorModule>(graph);
    one->setWavetableFolder(first);
    two->setWavetableFolder(second);

    ProjectCollector::collect(graph, bundle);

    EXPECT_NE(one->getWavetableFolder(), two->getWavetableFolder());
    EXPECT_TRUE(one->getWavetableFolder().isAChildOf(bundle));
    EXPECT_TRUE(two->getWavetableFolder().isAChildOf(bundle));
}

TEST_F(ProjectCollectorTest, ArchiveUnzippedElsewhereOpensWithNoReferenceToTheOriginalPaths) {
    const auto original = root.getChildFile("Downloads/kick.wav");
    ASSERT_TRUE(writeWav(original, 4410));
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(add<SamplerModule>(graph)->loadSampleFile(original));
    ASSERT_TRUE(save(graph, bundle).ok);
    ProjectCollector::collect(graph, bundle);
    ASSERT_TRUE(save(graph, bundle).ok);
    ASSERT_TRUE(bundle.getChildFile(ProjectBundle::kAutosaveFileName).replaceWithText("{}"));
    ASSERT_TRUE(bundle.getChildFile("Exports/mix.wav").create().wasOk());

    const auto zip = root.getChildFile("Song.zip");
    ASSERT_TRUE(ProjectCollector::writeArchive(bundle, zip, {"Exports"}, nullptr).isEmpty());

    const auto elsewhere = root.getChildFile("Elsewhere");
    juce::ZipFile archive(zip);
    ASSERT_TRUE(archive.uncompressTo(elsewhere).wasOk());
    const auto unzipped = elsewhere.getChildFile("Song.agsproj");
    EXPECT_FALSE(unzipped.getChildFile(ProjectBundle::kAutosaveFileName).exists());
    EXPECT_FALSE(unzipped.getChildFile("Exports").exists());

    const auto json = unzipped.getChildFile(ProjectBundle::kProjectFileName).loadFileAsString();
    EXPECT_FALSE(json.contains(original.getFullPathName()));
    EXPECT_FALSE(json.contains(bundle.getFullPathName()));

    juce::AudioProcessorGraph reopened;
    ASSERT_TRUE(load(reopened, unzipped).ok);
    auto* sampler = find<SamplerModule>(reopened);
    ASSERT_NE(sampler, nullptr);
    EXPECT_TRUE(juce::File(sampler->getSampleFilePath()).isAChildOf(unzipped));
    EXPECT_NE(sampler->getSample(), nullptr);
}

TEST_F(ProjectCollectorTest, ArchiveOfAMissingBundleFailsWithoutWritingAZip) {
    const auto zip = root.getChildFile("Nothing.zip");
    EXPECT_FALSE(ProjectCollector::writeArchive(root.getChildFile("Nope.agsproj"), zip, {}, nullptr).isEmpty());
    EXPECT_FALSE(zip.exists());
}
