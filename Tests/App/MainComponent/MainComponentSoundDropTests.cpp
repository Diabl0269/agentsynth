// A Sound loaded from the Sounds view on a real MainComponent: a file dropped under the last track makes an audio
// track with its clip in ONE undo step, and Return on a Sound goes to the selected Sampler, else the selected audio
// track, else a new Sampler.
#include "../../Modules/Sampler/SamplerTestWavs.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

using namespace sampler_test;

class SoundDropMainTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        wav = tempRoot.getChildFile("kick.wav");
        writeWav(wav, 22050, 1, 44100.0, [](int, int) { return 0.3f; });
        mc->getUndoManager().clearUndoHistory();
    }
    void TearDown() override {
        mc.reset();
        MainComponentTest::TearDown();
    }

    void activate(const juce::File& file) {
        synth::soundbrowser::SoundEntry entry;
        entry.path = file.getFullPathName();
        entry.name = file.getFileNameWithoutExtension();
        mc->getLibrarySidebarForTest().getSounds().onSoundActivated(entry);
    }
    std::vector<SamplerModule*> samplers() {
        std::vector<SamplerModule*> out;
        for (auto* node : mc->getAudioEngine().getGraph().getNodes())
            if (auto* s = dynamic_cast<SamplerModule*>(node->getProcessor()))
                out.push_back(s);
        return out;
    }
    synth::TrackId addAudioTrack() {
        mc->getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
        return mc->getTimelineDoc().getTracks().back().id;
    }

    std::unique_ptr<MainComponent> mc;
    juce::File wav;
};

TEST_F(SoundDropMainTest, ADropUnderTheLastTrackMakesOneAudioTrackWithTheClipInOneUndoStep) {
    auto& doc = mc->getTimelineDoc();
    const auto before = doc.getTracks().size();
    const auto nodesBefore = mc->getAudioEngine().getGraph().getNumNodes();
    mc->getUndoManager().clearUndoHistory();

    ASSERT_TRUE(static_cast<bool>(mc->getTimelinePanel().getClipLaneArea().onAudioFileDroppedBelowTracks));
    mc->getTimelinePanel().getClipLaneArea().onAudioFileDroppedBelowTracks(4.0, wav);

    ASSERT_EQ(doc.getTracks().size(), before + 1);
    const auto& track = doc.getTracks().back();
    EXPECT_EQ(track.kind, synth::TrackKind::Audio);
    ASSERT_EQ(track.clips.size(), 1u);
    EXPECT_DOUBLE_EQ(track.clips[0].startBeat, 4.0);
    EXPECT_TRUE(track.clips[0].assetRef.isNotEmpty());
    EXPECT_GT(mc->getAudioEngine().getGraph().getNumNodes(), nodesBefore) << "the track got its mixer channel";

    ASSERT_TRUE(mc->getUndoManager().canUndo());
    mc->getUndoManager().undo();
    EXPECT_EQ(doc.getTracks().size(), before) << "one undo removes the track and its clip";
    EXPECT_EQ(mc->getAudioEngine().getGraph().getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc->getUndoManager().canUndo());
}

TEST_F(SoundDropMainTest, AnUnreadableFileMakesNoTrack) {
    auto& doc = mc->getTimelineDoc();
    const auto before = doc.getTracks().size();
    mc->getTimelinePanel().getClipLaneArea().onAudioFileDroppedBelowTracks(0.0, tempRoot.getChildFile("missing.wav"));
    EXPECT_EQ(doc.getTracks().size(), before);
    EXPECT_FALSE(mc->getUndoManager().canUndo());
}

TEST_F(SoundDropMainTest, ReturnLoadsTheSelectedSamplerInOneUndoStepAndBeatsTheSelectedTrack) {
    auto& editor = mc->getGraphEditor();
    editor.addModuleAtCanvasPosition("Sampler", {200, 200}, {});
    editor.updateComponents();
    ASSERT_EQ(samplers().size(), 1u);
    juce::AudioProcessorGraph::NodeID samplerId;
    for (auto* node : mc->getAudioEngine().getGraph().getNodes())
        if (dynamic_cast<SamplerModule*>(node->getProcessor()) != nullptr)
            samplerId = node->nodeID;
    const auto track = addAudioTrack();
    mc->getTimelinePanel().selectTracks({track}, track);
    const auto earlier = tempRoot.getChildFile("earlier.wav");
    writeWav(earlier, 22050, 1, 44100.0, [](int, int) { return 0.1f; });
    samplers()[0]->loadSampleFile(earlier);
    editor.setSelectedNodes({samplerId});
    ASSERT_EQ(editor.getSelectedNodes().size(), 1u);
    ASSERT_EQ(editor.getSelectedNodes()[0], samplerId);
    ASSERT_EQ(samplers().size(), 1u);
    mc->getUndoManager().clearUndoHistory();

    activate(wav);
    EXPECT_EQ(samplers()[0]->getSampleFilePath(), wav.getFullPathName());
    EXPECT_TRUE(mc->getTimelineDoc().getTrack(track)->clips.empty()) << "the Sampler took it, not the track";
    mc->getUndoManager().undo();
    EXPECT_EQ(samplers()[0]->getSampleFilePath(), earlier.getFullPathName());
    EXPECT_FALSE(mc->getUndoManager().canUndo());
}

TEST_F(SoundDropMainTest, ReturnAddsAClipToTheSelectedAudioTrack) {
    const auto track = addAudioTrack();
    mc->getTimelinePanel().selectTracks({track}, track);
    mc->getUndoManager().clearUndoHistory();
    activate(wav);
    const auto* t = mc->getTimelineDoc().getTrack(track);
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->clips.size(), 1u);
    EXPECT_GE(t->clips[0].startBeat, 0.0);
    EXPECT_TRUE(samplers().empty());
}

TEST_F(SoundDropMainTest, ReturnWithNothingSelectedAddsASamplerHoldingTheSound) {
    activate(wav);
    ASSERT_EQ(samplers().size(), 1u);
    EXPECT_EQ(samplers()[0]->getSampleFilePath(), wav.getFullPathName());
    mc->getUndoManager().undo();
    EXPECT_TRUE(samplers().empty());
}

TEST_F(SoundDropMainTest, AnSfzSkipsTheSelectedAudioTrack) {
    const auto track = addAudioTrack();
    mc->getTimelinePanel().selectTracks({track}, track);
    const auto sfz = tempRoot.getChildFile("keys.sfz");
    sfz.replaceWithText("<region> sample=kick.wav key=60\n");
    activate(sfz);
    EXPECT_TRUE(mc->getTimelineDoc().getTrack(track)->clips.empty());
    EXPECT_EQ(samplers().size(), 1u);
}
