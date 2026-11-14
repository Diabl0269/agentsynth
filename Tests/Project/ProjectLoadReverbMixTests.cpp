// Concern: an old project's Reverb (saved with Wet and Dry) loads as the one Mix control plus Level,
// through the real project-load path, and a saved Wet lane or controller binding follows the Mix.
#include "Modules/LegacyParamIds.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

juce::String reverbProject(const juce::String& reverbParams) {
    return R"JSON({
  "schemaVersion": 1,
  "nodes": [
    { "id": 1, "type": "Reverb", "uuid": "rev", "params": { )JSON" +
           reverbParams + R"JSON( } }
  ],
  "connections": []
})JSON";
}

float param(juce::AudioProcessorGraph& graph, const juce::String& id) {
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == "rev")
            for (auto* p : node->getProcessor()->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                    if (r->getParameterID() == id)
                        return r->getNormalisableRange().convertFrom0to1(r->getValue());
    ADD_FAILURE() << "no parameter " << id;
    return -1.0f;
}

} // namespace

class ProjectLoadReverbMixTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = synth::userSettingsRootDirectory().getChildFile("agentsynth-reverbmix-tests");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    void load(const juce::String& reverbParams, juce::AudioProcessorGraph& graph) {
        auto dir = root.getChildFile(juce::String("p") + synth::ProjectBundle::kBundleExtension);
        dir.createDirectory();
        dir.getChildFile(synth::ProjectBundle::kAudioSubdirName).createDirectory();
        dir.getChildFile(synth::ProjectBundle::kPeaksSubdirName).createDirectory();
        dir.getChildFile(synth::ProjectBundle::kProjectFileName).replaceWithText(reverbProject(reverbParams));
        synth::TimelineDoc timeline;
        synth::PatchDocument patchDocument;
        synth::MacroSet macros;
        synth::MidiRemoteProjectDoc midiRemote;
        auto result = synth::ProjectBundle::load(dir, graph, timeline, patchDocument, macros, midiRemote);
        ASSERT_TRUE(result.ok) << result.message;
    }

    juce::File root;
};

TEST_F(ProjectLoadReverbMixTest, OldWetAndDryLoadAsMixAndLevel) {
    juce::AudioProcessorGraph graph;
    load(R"("wet": 0.33, "dry": 0.4, "outputLevel": 1.0)", graph);
    // a = 2*0.4 = 0.8, b = 3*0.33 = 0.99: mix = b/(a+b), level = min(1, a+b) = 1
    EXPECT_NEAR(param(graph, "mix"), 0.99f / 1.79f, 1e-3f);
    EXPECT_NEAR(param(graph, "outputLevel"), 1.0f, 1e-4f);
}

TEST_F(ProjectLoadReverbMixTest, QuietOldBlendKeepsItsLoudnessInLevel) {
    juce::AudioProcessorGraph graph;
    load(R"("wet": 0.1, "dry": 0.1, "outputLevel": 1.0)", graph);
    // a = 0.2, b = 0.3: sum 0.5 is under unity, so Level carries the 0.5
    EXPECT_NEAR(param(graph, "mix"), 0.6f, 1e-3f);
    EXPECT_NEAR(param(graph, "outputLevel"), 0.5f, 1e-3f);
}

TEST_F(ProjectLoadReverbMixTest, ANewProjectsMixIsLeftAlone) {
    juce::AudioProcessorGraph graph;
    load(R"("mix": 0.7)", graph);
    EXPECT_NEAR(param(graph, "mix"), 0.7f, 1e-4f);
}

TEST(LegacyParamIds, AWetBindingFollowsTheMixAndOthersStay) {
    EXPECT_EQ(synth::currentParamId("wet"), "mix");
    EXPECT_EQ(synth::currentParamId("mix"), "mix");
    EXPECT_EQ(synth::currentParamId("dry"), "dry");
    EXPECT_EQ(synth::currentParamId("cutoff"), "cutoff");
}
