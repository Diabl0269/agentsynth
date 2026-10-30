// Regression test for FRO1235: routed key pickers must be audible.
// Real renders (the engine's own device callback on a fake device, so the graph, the module and the output node all
// run): a Sequencer card's Key field cabled to a Mallet, and the instrument-track route the piano roll's Key picker and
// the Song key's chosen track share (Track In -> Pluck). Each says what is heard while a note is on and that silence
// comes back after the off. Skipped on purpose: the hover delay of the picker (KeyAuditionSoundTests covers it) and the
// 140 ms step timer, which the test seam advances.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../FakeAudioIODevice.h"
#include "../Graph/CardBody/CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/SongKeyField.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

namespace {

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr int kBlock = synth::test::kFakeDeviceBlockSize;

// A started fake device: block() renders one block through the engine's real device callback.
struct Render {
    synth::test::FakeAudioIODevice fake{0, 2};
    AudioEngine& engine;
    explicit Render(AudioEngine& e)
        : engine(e) {
        engine.audioDeviceAboutToStart(&fake);
    }
    ~Render() { engine.audioDeviceStopped(); }
    float block() {
        std::vector<float> left((size_t)kBlock), right((size_t)kBlock);
        float* outs[] = {left.data(), right.data()};
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, kBlock, {});
        float peak = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            peak = std::max({peak, std::abs(left[(size_t)i]), std::abs(right[(size_t)i])});
        return peak;
    }
    /** The loudest of `blocks` blocks. */
    float peak(int blocks) {
        float p = 0.0f;
        for (int i = 0; i < blocks; ++i)
            p = std::max(p, block());
        return p;
    }
    /** Blocks rendered until a block is below `floor` (a bounded loop, not a wait); -1 if it never gets there. */
    int blocksUntilQuiet(float floor, int maxBlocks) {
        for (int i = 0; i < maxBlocks; ++i)
            if (block() < floor)
                return i;
        return -1;
    }
};

constexpr float kQuiet = 1.0e-4f;
const synth::MusicalScale kMajor = synth::makeScale(0, synth::scalePresetIndex("Major"));

} // namespace

TEST(KeyAuditionRender, ASequencerCardsKeyFieldIsHeardThroughTheMalletItIsCabledTo) {
    cardbody_test::CardCanvas canvas;
    auto& graph = canvas.engine.getGraph();
    graph.setPlayConfigDetails(0, 2, synth::test::kFakeDeviceSampleRate, kBlock);
    const auto seqId = canvas.add(synth::AIStateMapper::createModule("Sequencer"), 40, 40);
    const auto malletId = canvas.add(synth::AIStateMapper::createModule("Mallet"), 400, 40);
    const auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{seqId, juce::AudioProcessorGraph::midiChannelIndex},
                                     {malletId, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(graph.addConnection({{malletId, 0}, {out->nodeID, 0}}));
    canvas.engine.refreshNormalling();
    canvas.editor.updateComponents();
    auto* card = canvas.card(seqId);
    ASSERT_NE(card, nullptr);
    auto* key = card->getCardBody() != nullptr
                    ? dynamic_cast<synth::ui::SequencerKeyField*>(card->getCardBody()->findWidget("scaleRoot"))
                    : nullptr;
    ASSERT_NE(key, nullptr);
    card->timerCallback(); // the card's tick wires the field's auditioner (wireSequencerKeyAudition)

    Render render(canvas.engine);
    EXPECT_LT(render.peak(20), kQuiet) << "silent before anything is hovered";

    key->getAuditioner().start(kMajor);
    ASSERT_TRUE(key->getAuditioner().isAuditioning());
    EXPECT_FALSE(key->getAuditioner().isUsingDefaultSound()) << "the cabled Mallet is the target, not the fallback";
    const float heard = render.peak(30);
    RecordProperty("sequencer_route_peak", std::to_string(heard));
    EXPECT_GT(heard, 0.001f) << "the note reaches the output through the Mallet";

    key->getAuditioner().stop();
    const int quietAfter = render.blocksUntilQuiet(kQuiet, 1500);
    EXPECT_GE(quietAfter, 0) << "the Mallet's ring dies away after the note-off";
    EXPECT_LT(render.peak(20), kQuiet) << "and stays silent";
}

TEST_F(MainComponentTest, ASongKeyTracksAndAPianoRollClipsScaleAreHeardThroughTheTracksPluck) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& panel = mc.getTimelinePanel();
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluckMenuId);
    const auto& tracks = mc.getTimelineDoc().getTracks();
    ASSERT_FALSE(tracks.empty());
    const auto track = tracks.back().id;
    ASSERT_TRUE(tracks.back().bindingUuid.isNotEmpty());
    const auto clip = mc.getTimelineDoc().addClip(track, 0.0, 4.0, "Riff");

    Render render(mc.getAudioEngine());
    EXPECT_LT(render.peak(20), kQuiet) << "silent before anything is hovered";

    // Song key: the picker's Preview sound chose this track.
    auto& songKey = panel.getTransportBar().getSongKeyField();
    songKey.setPreviewTrack(track);
    songKey.startAudition(kMajor);
    ASSERT_TRUE(songKey.isAuditioning());
    EXPECT_FALSE(songKey.getAuditioner().isUsingDefaultSound()) << "the chosen track, not the default sound";
    const float songPeak = render.peak(30);
    RecordProperty("song_key_track_peak", std::to_string(songPeak));
    EXPECT_GT(songPeak, 0.001f);
    songKey.stopAudition();
    EXPECT_GE(render.blocksUntilQuiet(kQuiet, 1500), 0) << "silent again after the note-off";
    EXPECT_LT(render.peak(20), kQuiet);

    // Piano roll: its Key picker plays through the open clip's track.
    panel.openPianoRoll(clip);
    ASSERT_TRUE(panel.getPianoRoll().isOpen());
    auto& rollAuditioner = panel.getPianoRoll().getScaleAssistPanel().getAuditioner();
    rollAuditioner.start(kMajor);
    ASSERT_TRUE(rollAuditioner.isAuditioning());
    EXPECT_FALSE(rollAuditioner.isUsingDefaultSound());
    const float rollPeak = render.peak(30);
    RecordProperty("piano_roll_track_peak", std::to_string(rollPeak));
    EXPECT_GT(rollPeak, 0.001f);
    rollAuditioner.stop();
    EXPECT_GE(render.blocksUntilQuiet(kQuiet, 1500), 0) << "silent again after the note-off";
    EXPECT_LT(render.peak(20), kQuiet);
}
