// ModuleFileRefsTests.cpp
//
// synth::ModuleFileRefs — the bundle-relative convention for module file state, on its own and
// through ProjectBundle::save/load: a Sampler sample inside the bundle is written as a relative ref
// and follows the bundle when it moves, an outside sample stays absolute, an old absolute-path
// project loads unchanged, and a relative ref that escapes the bundle is dropped, never resolved.

#include "MacroSet.h"
#include "MidiRemote/RemoteModel.h"
#include "Modules/SamplerModule.h"
#include "PatchDocument.h"
#include "Project/ModuleFileRefs.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

using synth::ModuleFileRefs;
using synth::ProjectBundle;

namespace {

bool writeSineWav(const juce::File& file, int numFrames = 4410) {
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
        buffer.setSample(0, i, 0.5f * std::sin((float)i * 0.05f));
    return writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
}

juce::var patchWithSampleFile(const juce::String& value) {
    auto* state = new juce::DynamicObject();
    state->setProperty("sampleFile", value);
    auto* node = new juce::DynamicObject();
    node->setProperty("type", "Sampler");
    node->setProperty("state", juce::var(state));
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node));
    auto* root = new juce::DynamicObject();
    root->setProperty("nodes", nodes);
    return juce::var(root);
}

juce::String sampleFileOf(const juce::var& patch) {
    return patch["nodes"][0]["state"].getProperty("sampleFile", juce::var()).toString();
}

class ModuleFileRefsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("agentsynth-mfr", "");
        root.createDirectory();
        bundle = root.getChildFile("Song.agsproj");
    }
    void TearDown() override { root.deleteRecursively(); }

    SamplerModule* addSampler(juce::AudioProcessorGraph& graph, const juce::File& sample) {
        auto node = graph.addNode(std::make_unique<SamplerModule>());
        auto* sampler = dynamic_cast<SamplerModule*>(node->getProcessor());
        EXPECT_TRUE(sampler->loadSampleFile(sample));
        return sampler;
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

    static SamplerModule* findSampler(juce::AudioProcessorGraph& graph) {
        for (auto* node : graph.getNodes())
            if (auto* sampler = dynamic_cast<SamplerModule*>(node->getProcessor()))
                return sampler;
        return nullptr;
    }

    juce::File root;
    juce::File bundle;
};

} // namespace

TEST_F(ModuleFileRefsTest, BundleRefIsRelativeWithForwardSlashesOnlyInsideTheBundle) {
    EXPECT_EQ(ModuleFileRefs::toBundleRef(bundle.getChildFile("Samples/kick.wav"), bundle), "Samples/kick.wav");
    EXPECT_TRUE(ModuleFileRefs::toBundleRef(root.getChildFile("kick.wav"), bundle).isEmpty());
    EXPECT_TRUE(ModuleFileRefs::toBundleRef(bundle.getChildFile("x.wav"), juce::File()).isEmpty());
}

TEST_F(ModuleFileRefsTest, ResolveRefusesEscapingAndAbsoluteRefs) {
    EXPECT_EQ(ModuleFileRefs::resolveBundleRef("Samples/kick.wav", bundle), bundle.getChildFile("Samples/kick.wav"));
    EXPECT_EQ(ModuleFileRefs::resolveBundleRef("../outside.wav", bundle), juce::File());
    EXPECT_EQ(ModuleFileRefs::resolveBundleRef("Samples/../../outside.wav", bundle), juce::File());
    EXPECT_EQ(ModuleFileRefs::resolveBundleRef("/etc/passwd", bundle), juce::File());
    EXPECT_EQ(ModuleFileRefs::resolveBundleRef("C:/Windows/win.ini", bundle), juce::File());
}

TEST_F(ModuleFileRefsTest, ResolveOnLoadDropsARelativeRefThatEscapesTheBundle) {
    auto patch = patchWithSampleFile("../../secret.wav");
    ModuleFileRefs::resolveOnLoad(patch, bundle);
    EXPECT_TRUE(sampleFileOf(patch).isEmpty()) << "an escaping ref must be removed, not resolved";

    auto absolute = patchWithSampleFile(root.getChildFile("elsewhere.wav").getFullPathName());
    ModuleFileRefs::resolveOnLoad(absolute, bundle);
    EXPECT_EQ(sampleFileOf(absolute), root.getChildFile("elsewhere.wav").getFullPathName())
        << "an absolute path is an old project or an outside file and passes through";
}

TEST_F(ModuleFileRefsTest, OnlyRegisteredModuleTypesAreTranslated) {
    auto patch = patchWithSampleFile(bundle.getChildFile("Samples/kick.wav").getFullPathName());
    patch["nodes"][0].getDynamicObject()->setProperty("type", "Oscillator");
    ModuleFileRefs::relativizeForSave(patch, bundle);
    EXPECT_TRUE(juce::File::isAbsolutePath(sampleFileOf(patch)));
}

TEST_F(ModuleFileRefsTest, SampleInsideTheBundleIsSavedRelativeAndFollowsAMovedBundle) {
    const auto sample = bundle.getChildFile("Samples/kick.wav");
    ASSERT_TRUE(writeSineWav(sample));
    juce::AudioProcessorGraph graph;
    addSampler(graph, sample);
    ASSERT_TRUE(save(graph, bundle).ok);

    const auto json = bundle.getChildFile(ProjectBundle::kProjectFileName).loadFileAsString();
    EXPECT_TRUE(json.contains("\"Samples/kick.wav\""));
    EXPECT_FALSE(json.contains(bundle.getFullPathName())) << "no absolute path to the bundle may be saved";

    const auto moved = root.getChildFile("Moved/Renamed.agsproj");
    moved.getParentDirectory().createDirectory();
    ASSERT_TRUE(bundle.moveFileTo(moved));

    juce::AudioProcessorGraph reopened;
    ASSERT_TRUE(load(reopened, moved).ok);
    auto* sampler = findSampler(reopened);
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->getSampleFilePath(), moved.getChildFile("Samples/kick.wav").getFullPathName());
    EXPECT_NE(sampler->getSample(), nullptr) << "the sample must actually be loaded from the moved bundle";
}

TEST_F(ModuleFileRefsTest, SampleOutsideTheBundleStaysAbsoluteAndOldProjectsLoadUnchanged) {
    const auto outside = root.getChildFile("Downloads/snare.wav");
    ASSERT_TRUE(writeSineWav(outside));
    juce::AudioProcessorGraph graph;
    addSampler(graph, outside);
    ASSERT_TRUE(save(graph, bundle).ok);
    EXPECT_TRUE(bundle.getChildFile(ProjectBundle::kProjectFileName)
                    .loadFileAsString()
                    .contains(juce::JSON::toString(outside.getFullPathName())));

    juce::AudioProcessorGraph reopened;
    ASSERT_TRUE(load(reopened, bundle).ok);
    auto* sampler = findSampler(reopened);
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->getSampleFilePath(), outside.getFullPathName());
}
